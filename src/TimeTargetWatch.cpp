#include "SlowTimeCompat.h"
#include "TimeTargetWatch.h"
#include "ThreadDiscovery.h"

namespace
{
	struct WriterInfo
	{
		HMODULE module{ nullptr };
#if WSTF_VERBOSE_LOGGING
		std::string path;
		std::string name;
#endif
		bool isSelf{ false };
	};

	struct WatchState
	{
		DWORD bootstrapThreadID{ 0 };
		std::atomic_bool installLoopStarted{ false };
		std::atomic_bool installed{ false };
		std::atomic_uint32_t logBudget{ 64 };
	};

#if WSTF_VERBOSE_LOGGING
	struct EventState
	{
		std::mutex lock;
		std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastLogByKey;
	};
#endif

	WatchState g_watchState{};

	REL::Relocation<float*> g_currentMultiplier{ RELOCATION_ID(511882, 388442) };
	REL::Relocation<float*> g_targetMultiplier{ RELOCATION_ID(511883, 388443) };

	PVOID g_exceptionHandler = nullptr;
	HMODULE g_selfModule = nullptr;
	HMODULE g_wheelerModule = nullptr;
	HMODULE g_skyrimSoulsModule = nullptr;
	std::atomic_bool g_wheelerOwnsTimeScale{ false };
	std::atomic<float> g_preWheelerTimeScale{ 1.0f };

#if WSTF_VERBOSE_LOGGING
	std::mutex g_seenWritersLock;
	std::unordered_set<std::string> g_seenWriters;
#endif

	std::mutex g_threadSlotsLock;
	std::unordered_map<DWORD, int> g_threadSlots;
	// Worker-owned pins prevent registered thread IDs being recycled before pruning.
	std::unordered_map<DWORD, ThreadDiscovery::ThreadPin> g_threadPins;
#if WSTF_VERBOSE_LOGGING
	EventState g_eventState{};
#endif

	constexpr float kEqualityEpsilon = 0.0005f;

	bool NearlyEqual(float a_lhs, float a_rhs)
	{
		return std::fabs(a_lhs - a_rhs) <= kEqualityEpsilon;
	}

#if WSTF_VERBOSE_LOGGING
	bool ConsumeLogBudget()
	{
		const auto remainingBudget = g_watchState.logBudget.fetch_sub(1);
		return remainingBudget > 0;
	}

	bool ShouldLogEvent(std::string a_key, std::chrono::milliseconds a_window)
	{
		const auto now = std::chrono::steady_clock::now();
		std::lock_guard lock(g_eventState.lock);

		const auto it = g_eventState.lastLogByKey.find(a_key);
		if (it != g_eventState.lastLogByKey.end() && (now - it->second) < a_window) {
			return false;
		}

		g_eventState.lastLogByKey[std::move(a_key)] = now;
		return true;
	}
#endif

	void CacheModuleHandles()
	{
		g_selfModule = ::GetModuleHandleA("WheelerSlowTimeFix.dll");
		g_wheelerModule = ::GetModuleHandleA("wheeler.dll");
		g_skyrimSoulsModule = ::GetModuleHandleA("SkyrimSoulsRE.dll");
	}

	std::string ModulePathFromHandle(HMODULE a_module)
	{
		if (!a_module) {
			return {};
		}

		std::array<char, MAX_PATH> buffer{};
		const DWORD length = ::GetModuleFileNameA(a_module, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size()) {
			return {};
		}

		return std::string(buffer.data(), length);
	}

	WriterInfo ResolveWriter(void* a_address)
	{
		WriterInfo writer{};

		HMODULE module = nullptr;
		if (!::GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				static_cast<LPCSTR>(a_address),
				&module)) {
			return writer;
		}

		writer.module = module;
#if WSTF_VERBOSE_LOGGING
		writer.path = ModulePathFromHandle(module);
		writer.name = writer.path.empty() ? "unknown" : std::filesystem::path(writer.path).filename().string();
#endif
		writer.isSelf = module == g_selfModule;
		return writer;
	}

#if WSTF_VERBOSE_LOGGING
	void MaybeLogWriterDiscovery(const WriterInfo& a_writer)
	{
		if (a_writer.name.empty()) {
			return;
		}

		{
			std::lock_guard lock(g_seenWritersLock);
			if (!g_seenWriters.insert(a_writer.name).second) {
				return;
			}
		}

		SKSE::log::info(
			"[SlowTimeFix] observed target write by {} ({})",
			a_writer.name,
			a_writer.path.empty() ? "path unavailable" : a_writer.path);
	}
#endif

	void ClearTargetInterpolation()
	{
		if (auto* timer = RE::BSTimer::GetSingleton()) {
			timer->useGlobalTimeMultiplierTarget = false;
		}
	}

	bool ShouldSyncTargetWriteForWriter(const WriterInfo& a_writer)
	{
		if (!a_writer.module) {
			return false;
		}

		return a_writer.module == g_wheelerModule || a_writer.module == g_skyrimSoulsModule;
	}

	void SyncCurrentToTarget(const char* a_reason, const WriterInfo& a_writer)
	{
		auto* current = g_currentMultiplier.get();
		auto* target = g_targetMultiplier.get();
		if (!current || !target) {
			return;
		}

		const float currentBefore = *current;
		const float targetBefore = *target;
		if (!std::isfinite(currentBefore) || !std::isfinite(targetBefore)) {
			return;
		}

		if (NearlyEqual(currentBefore, targetBefore)) {
			ClearTargetInterpolation();

#if WSTF_VERBOSE_LOGGING
			const std::string writerName = a_writer.name.empty() ? "unknown" : a_writer.name;
			const std::string noOpKey = std::format("already-equal:{}:{:.2f}", writerName, currentBefore);
			if (ShouldLogEvent(noOpKey, std::chrono::seconds(2)) && ConsumeLogBudget()) {
				SKSE::log::info(
					"[SlowTimeFix] target-write required no sync reason={} writer={} current={:.3f} target={:.3f}",
					a_reason ? a_reason : "unknown",
					writerName,
					currentBefore,
					targetBefore);
			}
#endif
			return;
		}

		*current = targetBefore;
		ClearTargetInterpolation();

#if WSTF_VERBOSE_LOGGING
		const std::string writerName = a_writer.name.empty() ? "unknown" : a_writer.name;
		const std::string syncKey = std::format("sync:{}:{:.2f}:{:.2f}", writerName, currentBefore, targetBefore);
		if (ShouldLogEvent(syncKey, std::chrono::milliseconds(500)) && ConsumeLogBudget()) {
			SKSE::log::info(
				"[SlowTimeFix] synced target-write reason={} writer={} current_before={:.3f} target_before={:.3f} current_after={:.3f}",
				a_reason ? a_reason : "unknown",
				writerName,
				currentBefore,
				targetBefore,
				*current);
		}
#endif
	}

	bool HandleWheelerOwnershipOnTargetWrite(const WriterInfo& a_writer)
	{
		if (!a_writer.module || a_writer.module != g_wheelerModule) {
			return false;
		}

		auto* current = g_currentMultiplier.get();
		auto* target = g_targetMultiplier.get();
		if (!current || !target) {
			return false;
		}

		const float currentBefore = *current;
		const float targetAfter = *target;
		if (!std::isfinite(currentBefore) || !std::isfinite(targetAfter)) {
			return false;
		}

		const bool currentIsNormal = currentBefore >= 0.99f && currentBefore <= 1.01f;
		const bool targetIsNormal = targetAfter >= 0.99f && targetAfter <= 1.01f;
		const bool targetIsSlow = targetAfter < 0.99f;
		const bool ownsTimeScale = g_wheelerOwnsTimeScale.load();

		// Wheeler is taking ownership from a normal baseline; allow its slow-time apply.
		if (!ownsTimeScale && currentIsNormal && targetIsSlow) {
			g_preWheelerTimeScale.store(currentBefore);
			g_wheelerOwnsTimeScale.store(true);

#if WSTF_VERBOSE_LOGGING
			const std::string key = std::format("owner-acquired:{:.2f}:{:.2f}", currentBefore, targetAfter);
			if (ShouldLogEvent(key, std::chrono::milliseconds(750)) && ConsumeLogBudget()) {
				SKSE::log::info(
					"[SlowTimeFix] Wheeler ownership acquired current_before={:.3f} target_after={:.3f}",
					currentBefore,
					targetAfter);
			}
#endif
			return false;
		}

		// Wheeler is trying to restore to normal without owning timescale; preserve the external effect.
		if (!ownsTimeScale && targetIsNormal && currentBefore < (targetAfter - kEqualityEpsilon)) {
			*target = currentBefore;
			ClearTargetInterpolation();

#if WSTF_VERBOSE_LOGGING
			const std::string key = std::format("blocked-restore:{:.2f}:{:.2f}", currentBefore, targetAfter);
			if (ShouldLogEvent(key, std::chrono::seconds(2)) && ConsumeLogBudget()) {
				SKSE::log::info(
					"[SlowTimeFix] blocked Wheeler restore over external slow effect current_before={:.3f} target_after={:.3f}",
					currentBefore,
					targetAfter);
			}
#endif
			return true;
		}

		if (ownsTimeScale) {
			const float cachedBaseline = g_preWheelerTimeScale.load();
			if (targetIsNormal) {
				float replacementTarget = targetAfter;
				if (SlowTimeCompat::TryDeferWheelerRestore(currentBefore, targetAfter, cachedBaseline, replacementTarget)) {
					*target = replacementTarget;
					ClearTargetInterpolation();
					g_wheelerOwnsTimeScale.store(false);

#if WSTF_VERBOSE_LOGGING
					const std::string key = std::format("deferred-restore:{:.2f}:{:.2f}", currentBefore, cachedBaseline);
					if (ShouldLogEvent(key, std::chrono::seconds(2)) && ConsumeLogBudget()) {
						SKSE::log::info(
							"[SlowTimeFix] deferred Wheeler restore current_before={:.3f} replacement_target={:.3f} restore_baseline={:.3f}",
							currentBefore,
							replacementTarget,
							cachedBaseline);
					}
#endif
					return true;
				}
			}

			if (NearlyEqual(targetAfter, cachedBaseline) || (cachedBaseline >= 0.99f && targetIsNormal)) {
				g_wheelerOwnsTimeScale.store(false);

#if WSTF_VERBOSE_LOGGING
				const std::string key = std::format("owner-released:{:.2f}:{:.2f}", cachedBaseline, targetAfter);
				if (ShouldLogEvent(key, std::chrono::milliseconds(750)) && ConsumeLogBudget()) {
					SKSE::log::info(
						"[SlowTimeFix] Wheeler ownership released cached={:.3f} target_after={:.3f}",
						cachedBaseline,
						targetAfter);
				}
#endif
			}
		}

		return false;
	}

	void ApplyWatchpointToSlot(CONTEXT& a_context, int a_slot, std::uintptr_t a_address)
	{
		switch (a_slot) {
		case 0:
			a_context.Dr0 = a_address;
			break;
		case 1:
			a_context.Dr1 = a_address;
			break;
		case 2:
			a_context.Dr2 = a_address;
			break;
		case 3:
			a_context.Dr3 = a_address;
			break;
		default:
			return;
		}

		const std::uint64_t enableBit = 1ull << (a_slot * 2);
		const std::uint64_t controlShift = 16ull + (a_slot * 4ull);
		a_context.Dr7 |= enableBit;
		a_context.Dr7 &= ~(0xFull << controlShift);
		a_context.Dr7 |= (0x1ull << controlShift);
		a_context.Dr7 |= (0x3ull << (controlShift + 2));
		a_context.Dr6 = 0;
	}

	int FindFreeDebugSlot(const CONTEXT& a_context)
	{
		for (int slot = 0; slot < 4; ++slot) {
			const std::uint64_t localBit = 1ull << (slot * 2);
			const std::uint64_t globalBit = 1ull << (slot * 2 + 1);
			if ((a_context.Dr7 & localBit) == 0 && (a_context.Dr7 & globalBit) == 0) {
				return slot;
			}
		}
		return -1;
	}

	bool HasWatchpointForThread(DWORD a_threadID)
	{
		std::lock_guard lock(g_threadSlotsLock);
		return g_threadSlots.contains(a_threadID);
	}

	void RememberWatchpointForThread(DWORD a_threadID, int a_slot)
	{
		std::lock_guard lock(g_threadSlotsLock);
		g_threadSlots[a_threadID] = a_slot;
	}

	int GetWatchpointSlotForCurrentThread()
	{
		const DWORD threadID = ::GetCurrentThreadId();
		std::lock_guard lock(g_threadSlotsLock);
		const auto it = g_threadSlots.find(threadID);
		return it != g_threadSlots.end() ? it->second : -1;
	}

	void PruneDeadThreadSlots(const std::unordered_set<DWORD>& a_aliveThreads)
	{
		{
			std::lock_guard lock(g_threadSlotsLock);
			std::erase_if(g_threadSlots, [&](const auto& a_pair) {
				return !a_aliveThreads.contains(a_pair.first);
			});
		}
		// Remove the slot before releasing the last pin that permits ID reuse.
		std::erase_if(g_threadPins, [&](const auto& a_pair) {
			return !a_aliveThreads.contains(a_pair.first);
		});
	}

	bool InstallWatchpointOnThread(DWORD a_threadID, ThreadDiscovery::ThreadPin& a_pin)
	{
		if (a_threadID == ::GetCurrentThreadId() || HasWatchpointForThread(a_threadID)) {
			return false;
		}

		HANDLE thread = ::OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, a_threadID);
		if (!thread) {
			return false;
		}

		bool success = false;
		if (::SuspendThread(thread) != static_cast<DWORD>(-1)) {
			CONTEXT context{};
			context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
			if (::GetThreadContext(thread, &context)) {
				const int slot = FindFreeDebugSlot(context);
				if (slot >= 0) {
					ApplyWatchpointToSlot(context, slot, reinterpret_cast<std::uintptr_t>(g_targetMultiplier.get()));
					if (::SetThreadContext(thread, &context)) {
						RememberWatchpointForThread(a_threadID, slot);
						success = true;
					}
				}
			}
			::ResumeThread(thread);
		}

		if (success) {
			a_pin.reset(thread);
		} else {
			::CloseHandle(thread);
		}
		return success;
	}

	std::size_t InstallWatchpointsOnProcessThreads()
	{
		const auto snapshot = ThreadDiscovery::Capture();
		if (!snapshot.complete) {
			return 0;
		}

		std::unordered_set<DWORD> aliveThreads;
		std::size_t installedCount = 0;

		for (const auto threadID : snapshot.ids) {
			aliveThreads.insert(threadID);
			if (threadID == ::GetCurrentThreadId() || HasWatchpointForThread(threadID)) {
				continue;
			}
			// Allocate bookkeeping before suspending a thread or modifying its registers.
			auto pin = g_threadPins.try_emplace(threadID).first;
			if (InstallWatchpointOnThread(threadID, pin->second)) {
				installedCount++;
			} else {
				g_threadPins.erase(pin);
			}
		}

		PruneDeadThreadSlots(aliveThreads);
		return installedCount;
	}

	bool WasTriggered(const CONTEXT* a_context, int a_slot)
	{
		if (!a_context || a_slot < 0) {
			return false;
		}

		const std::uint64_t slotBit = 1ull << a_slot;
		return (a_context->Dr6 & slotBit) != 0;
	}

	LONG CALLBACK ExceptionHandler(EXCEPTION_POINTERS* a_info)
	{
		if (!a_info || !a_info->ExceptionRecord || !a_info->ContextRecord) {
			return EXCEPTION_CONTINUE_SEARCH;
		}
		if (a_info->ExceptionRecord->ExceptionCode != STATUS_SINGLE_STEP) {
			return EXCEPTION_CONTINUE_SEARCH;
		}
		if (!g_watchState.installed.load()) {
			return EXCEPTION_CONTINUE_SEARCH;
		}

		const int slot = GetWatchpointSlotForCurrentThread();
		if (!WasTriggered(a_info->ContextRecord, slot)) {
			return EXCEPTION_CONTINUE_SEARCH;
		}

		a_info->ContextRecord->Dr6 = 0;

		CacheModuleHandles();
		const WriterInfo writer = ResolveWriter(a_info->ExceptionRecord->ExceptionAddress);
		if (!writer.isSelf) {
#if WSTF_VERBOSE_LOGGING
			MaybeLogWriterDiscovery(writer);
#endif
			if (!HandleWheelerOwnershipOnTargetWrite(writer)) {
				if (auto* target = g_targetMultiplier.get()) {
					SlowTimeCompat::MaybeOverrideTrackedFinishRestore(*target);
				}
				if (ShouldSyncTargetWriteForWriter(writer)) {
					SyncCurrentToTarget("target-write", writer);
				}
			}
		}

		return EXCEPTION_CONTINUE_EXECUTION;
	}

	void LogLoadedModuleState()
	{
		auto pathOrMissing = [](HMODULE a_module) {
			const auto path = ModulePathFromHandle(a_module);
			return path.empty() ? std::string("<not loaded>") : path;
		};

		SKSE::log::info("[SlowTimeFix] module wheeler={}", pathOrMissing(g_wheelerModule));
		SKSE::log::info("[SlowTimeFix] module SkyrimSoulsRE={}", pathOrMissing(g_skyrimSoulsModule));
	}

	void EnsureExceptionHandler()
	{
		if (g_exceptionHandler) {
			return;
		}

		g_exceptionHandler = ::AddVectoredExceptionHandler(1, ExceptionHandler);
		if (!g_exceptionHandler) {
			SKSE::log::critical("[SlowTimeFix] AddVectoredExceptionHandler failed: {}", ::GetLastError());
		}
	}

	std::size_t GetInstalledThreadCount()
	{
		std::lock_guard lock(g_threadSlotsLock);
		return g_threadSlots.size();
	}

	void InstallLoop()
	{
		EnsureExceptionHandler();
		if (!g_exceptionHandler) {
			return;
		}

		CacheModuleHandles();
		LogLoadedModuleState();

		for (;;) {
			const std::size_t newInstalls = InstallWatchpointsOnProcessThreads();
			const std::size_t totalInstalled = GetInstalledThreadCount();

			if (totalInstalled > 0 && !g_watchState.installed.exchange(true)) {
				SKSE::log::info(
					"[SlowTimeFix] installed process-wide target watchpoints on {} thread(s) for {:p}",
					totalInstalled,
					static_cast<const void*>(g_targetMultiplier.get()));
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
	}
}

namespace TimeTargetWatch
{
	void SetMainThreadID(DWORD a_threadID)
	{
		g_watchState.bootstrapThreadID = a_threadID;
	}

	void QueueInstall()
	{
		if (g_watchState.installLoopStarted.exchange(true)) {
			return;
		}

		std::thread([]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			InstallLoop();
		}).detach();
	}
}
