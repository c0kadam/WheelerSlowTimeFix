#include "SlowTimeCompat.h"
#if WSTF_BOUNDED_DIAGNOSTICS
#include "OwnershipDiagnostics.h"
#endif

#include "TimeTargetWatch.h"
#include "ThreadDiscovery.h"

#include <limits>

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

	struct AdditionalModule
	{
		std::string name;
		std::atomic<HMODULE> module{ nullptr };
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
	std::array<AdditionalModule, 64> g_additionalModules{};
	std::atomic_size_t g_additionalModuleCount{ 0 };
	std::once_flag g_configurationLoaded;
	std::atomic_bool g_wheelerOwnsTimeScale{ false };
	std::atomic<float> g_preWheelerTimeScale{ 1.0f };
	// Last observed target, including our corrections; current may still be interpolating.
	std::atomic<float> g_previousTarget{ std::numeric_limits<float>::quiet_NaN() };
	std::atomic_bool g_skyrimSoulsSlowActive{ false };
	std::atomic_bool g_wheelerRestorePending{ false };

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

	bool DLLNamesEqual(std::string_view a_lhs, std::string_view a_rhs)
	{
		std::array<wchar_t, 256> lhs{}, rhs{};
		const int lhsLength = ::MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, a_lhs.data(), static_cast<int>(a_lhs.size()), lhs.data(), static_cast<int>(lhs.size()));
		const int rhsLength = ::MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, a_rhs.data(), static_cast<int>(a_rhs.size()), rhs.data(), static_cast<int>(rhs.size()));
		return lhsLength > 0 && rhsLength > 0 && ::CompareStringOrdinal(lhs.data(), lhsLength, rhs.data(), rhsLength, TRUE) == CSTR_EQUAL;
	}

	bool IsValidDLLName(const std::string& a_name)
	{
		if (a_name.size() <= 4 || a_name.size() > 255 || !DLLNamesEqual(std::string_view(a_name).substr(a_name.size() - 4), ".dll")) {
			return false;
		}
		for (const unsigned char ch : a_name) {
			if (ch < 32 || ch == 127 || std::string_view("\\/:*?\"<>|").find(ch) != std::string_view::npos) {
				return false;
			}
		}
		return DLLNamesEqual(a_name, a_name);
	}

	void ParseAdditionalDLLs(std::string_view a_value)
	{
		std::size_t count = 0;
		while (!a_value.empty()) {
			const auto comma = a_value.find(',');
			auto entry = a_value.substr(0, comma);
			a_value = comma == std::string_view::npos ? std::string_view{} : a_value.substr(comma + 1);
			const auto first = entry.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				continue;
			}
			entry = entry.substr(first, entry.find_last_not_of(" \t\r\n") - first + 1);
			const std::string name(entry);
			if (!IsValidDLLName(name)) {
				SKSE::log::warn("[SlowTimeFix] ignoring invalid AdditionalDLLs entry (DLL base name required)");
				continue;
			}
			bool duplicate = DLLNamesEqual(name, "wheeler.dll") ||
			                 DLLNamesEqual(name, "SkyrimSoulsRE.dll") ||
			                 DLLNamesEqual(name, "WheelerSlowTimeFix.dll");
			for (std::size_t i = 0; i < count && !duplicate; ++i) {
				duplicate = DLLNamesEqual(name, g_additionalModules[i].name);
			}
			if (duplicate) {
				continue;
			}
			if (count == g_additionalModules.size()) {
				SKSE::log::warn("[SlowTimeFix] AdditionalDLLs limit is {}; ignoring {}", count, name);
				continue;
			}
			g_additionalModules[count++].name = name;
		}
		g_additionalModuleCount.store(count, std::memory_order_release);
		SKSE::log::info("[SlowTimeFix] configured {} additional Wheeler-compatible module(s)", count);
	}

	void LoadAdditionalModuleConfiguration()
	{
		const auto executable = ModulePathFromHandle(::GetModuleHandleA(nullptr));
		if (executable.empty()) {
			SKSE::log::warn("[SlowTimeFix] game path unavailable; using built-in compatibility modules only");
			return;
		}
		const auto path = (std::filesystem::path(executable).parent_path() / "Data/SKSE/Plugins/WheelerSlowTimeFix.ini").string();
		if (::GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
			SKSE::log::info("[SlowTimeFix] optional INI unavailable; using built-in compatibility modules only");
			return;
		}
		std::array<char, 8192> value{};
		const DWORD length = ::GetPrivateProfileStringA("Compatibility", "AdditionalDLLs", "", value.data(), static_cast<DWORD>(value.size()), path.c_str());
		if (length >= value.size() - 1) {
			SKSE::log::warn("[SlowTimeFix] AdditionalDLLs value is too long; using built-in compatibility modules only");
			return;
		}
		ParseAdditionalDLLs(std::string_view(value.data(), length));
	}

	void RefreshAdditionalModuleHandles()
	{
		const auto count = g_additionalModuleCount.load(std::memory_order_acquire);
		for (std::size_t i = 0; i < count; ++i) {
			auto& configured = g_additionalModules[i];
			HMODULE module = configured.module.load(std::memory_order_relaxed);
			if (!module) {
				// Pin an already-loaded module so its cached handle cannot be recycled.
				if (::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN, configured.name.c_str(), &module)) {
					configured.module.store(module, std::memory_order_relaxed);
				}
			}
			SKSE::log::info("[SlowTimeFix] configured module {}: {} ({})", configured.name, module ? "recognized" : "unavailable", module ? ModulePathFromHandle(module) : "not loaded");
		}
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

	bool IsWheelerLikeModule(HMODULE a_module)
	{
		if (!a_module) {
			return false;
		}
		if (a_module == g_wheelerModule) {
			return true;
		}
		const auto count = g_additionalModuleCount.load(std::memory_order_acquire);
		for (std::size_t i = 0; i < count; ++i) {
			if (a_module == g_additionalModules[i].module.load(std::memory_order_relaxed)) {
				return true;
			}
		}
		return false;
	}

	bool ShouldSyncTargetWriteForWriter(const WriterInfo& a_writer)
	{
		if (!a_writer.module) {
			return false;
		}

		return IsWheelerLikeModule(a_writer.module) || a_writer.module == g_skyrimSoulsModule;
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


#if WSTF_BOUNDED_DIAGNOSTICS
	void RecordWatch(OwnershipDiagnostics::Kind kind, OwnershipDiagnostics::Reason reason,
		const WriterInfo& writer, float previous, float current, float requested) noexcept
	{
		if (!OwnershipDiagnostics::IsEnabled()) { return; }
		using namespace OwnershipDiagnostics;
		const auto* now = g_currentMultiplier.get();
		const auto* target = g_targetMultiplier.get();
		Event event{ .module = reinterpret_cast<std::uintptr_t>(writer.module),
			.previous = previous, .current = current, .requested = requested,
			.currentAfter = now ? *now : kUnknown, .targetAfter = target ? *target : kUnknown,
			.baseline = g_preWheelerTimeScale.load(),
			.flags = static_cast<std::uint16_t>((g_wheelerOwnsTimeScale.load() ? 1 : 0) |
				(g_wheelerRestorePending.load() ? 2 : 0) | (g_skyrimSoulsSlowActive.load() ? 4 : 0) |
				(SlowTimeCompat::HasActiveSlowTimeEffect() ? 8 : 0)),
			.flagsValid = 15, .kind = kind, .reason = reason };
		Record(event);
	}

#endif
	void ObserveTargetWrite(const WriterInfo& a_writer, float a_previousTarget)
	{
		const auto* target = g_targetMultiplier.get();
		if (!target || !std::isfinite(*target)) {
			return;
		}
		if (a_writer.module && a_writer.module == g_skyrimSoulsModule) {

#if WSTF_BOUNDED_DIAGNOSTICS
			const bool diagSoulsWasActive = OwnershipDiagnostics::IsEnabled() && g_skyrimSoulsSlowActive.load();
#endif
			// Souls multiplies on menu entry and divides on exit, writing both
			// multipliers before returning. Do not guess or hardcode its factor.
			if (std::isfinite(a_previousTarget) && *target < a_previousTarget) {
				g_skyrimSoulsSlowActive.store(true);
#if WSTF_BOUNDED_DIAGNOSTICS
				if (OwnershipDiagnostics::IsEnabled()) {
					if (!diagSoulsWasActive) {
						RecordWatch(OwnershipDiagnostics::Kind::SoulsActivated, OwnershipDiagnostics::Reason::ActiveMenu,
							a_writer, a_previousTarget, g_currentMultiplier.get() ? *g_currentMultiplier.get() : OwnershipDiagnostics::kUnknown, *target);
					}
				}
#endif

			} else if (std::isfinite(a_previousTarget) && *target > a_previousTarget) {
				g_skyrimSoulsSlowActive.store(false);
#if WSTF_BOUNDED_DIAGNOSTICS
				if (OwnershipDiagnostics::IsEnabled()) {
					RecordWatch(diagSoulsWasActive ? OwnershipDiagnostics::Kind::SoulsRestored : OwnershipDiagnostics::Kind::Unexpected,
						diagSoulsWasActive ? OwnershipDiagnostics::Reason::MenuExit : OwnershipDiagnostics::Reason::UnobservedMenuEntry,
						a_writer, a_previousTarget, g_currentMultiplier.get() ? *g_currentMultiplier.get() : OwnershipDiagnostics::kUnknown, *target);
				}
#endif

			}
		} else if (!a_writer.isSelf && !IsWheelerLikeModule(a_writer.module) &&
		           !SlowTimeCompat::HasActiveSlowTimeEffect()) {

#if WSTF_BOUNDED_DIAGNOSTICS
			const bool diagReleased = OwnershipDiagnostics::IsEnabled() && (g_wheelerOwnsTimeScale.load() || g_wheelerRestorePending.load());
#endif
			// An unrelated writer supersedes the wheel baseline. Fail closed;
			// a later wheel close must not cancel that writer's slowdown.
			g_wheelerRestorePending.store(false);
			g_wheelerOwnsTimeScale.store(false);
#if WSTF_BOUNDED_DIAGNOSTICS
			if (diagReleased) {
				RecordWatch(OwnershipDiagnostics::Kind::OwnerReleased, OwnershipDiagnostics::Reason::UnrelatedWriter,
					a_writer, a_previousTarget, g_currentMultiplier.get() ? *g_currentMultiplier.get() : OwnershipDiagnostics::kUnknown, *target);
			}
#endif

		}
	}

	bool HandleWheelerOwnershipOnTargetWrite(const WriterInfo& a_writer, float a_previousTarget)
	{
		if (a_writer.module && a_writer.module == g_skyrimSoulsModule &&
			g_wheelerRestorePending.load() && !g_skyrimSoulsSlowActive.load()) {
			auto* current = g_currentMultiplier.get();
			auto* target = g_targetMultiplier.get();
			const float baseline = g_preWheelerTimeScale.load();
#if WSTF_BOUNDED_DIAGNOSTICS
			const float diagMenuCurrent = OwnershipDiagnostics::IsEnabled() && current ? *current : OwnershipDiagnostics::kUnknown;
			const float diagMenuRequested = OwnershipDiagnostics::IsEnabled() && target ? *target : OwnershipDiagnostics::kUnknown;
#endif

			if (current && target && std::isfinite(*current) && std::isfinite(*target) && std::isfinite(baseline)) {
				float replacement = baseline;
				SlowTimeCompat::TryDeferWheelerRestore(*current, baseline, baseline, replacement);
				*target = replacement;
				ClearTargetInterpolation();
				g_wheelerRestorePending.store(false);
				g_wheelerOwnsTimeScale.store(false);
			}
			// Continue through the existing Souls current/target synchronization.
#if WSTF_BOUNDED_DIAGNOSTICS
			if (OwnershipDiagnostics::IsEnabled()) {
				if (!g_wheelerRestorePending.load()) {
					RecordWatch(OwnershipDiagnostics::Kind::OwnerReleased, OwnershipDiagnostics::Reason::MenuExit,
					a_writer, a_previousTarget, diagMenuCurrent, diagMenuRequested);
				}
			}
#endif

			return false;
		}
		if (!IsWheelerLikeModule(a_writer.module)) {
			return false;
		}

		auto* current = g_currentMultiplier.get();
		auto* target = g_targetMultiplier.get();

#if WSTF_BOUNDED_DIAGNOSTICS
		if (OwnershipDiagnostics::IsEnabled()) {
			if (!current || !target) {
				RecordWatch(OwnershipDiagnostics::Kind::Unexpected, OwnershipDiagnostics::Reason::MissingPointers,
					a_writer, a_previousTarget, OwnershipDiagnostics::kUnknown, OwnershipDiagnostics::kUnknown);
			}
		}
#endif
		if (!current || !target) {
			return false;
		}

		const float currentBefore = *current;
		const float targetAfter = *target;
		if (!std::isfinite(currentBefore) || !std::isfinite(targetAfter)) {
#if WSTF_BOUNDED_DIAGNOSTICS
			if (OwnershipDiagnostics::IsEnabled()) {
				RecordWatch(OwnershipDiagnostics::Kind::Unexpected, OwnershipDiagnostics::Reason::NonFinite,
					a_writer, a_previousTarget, currentBefore, targetAfter);
			}
#endif

			return false;
		}

		const bool currentIsNormal = currentBefore >= 0.99f && currentBefore <= 1.01f;
		const bool targetIsNormal = targetAfter >= 0.99f && targetAfter <= 1.01f;
		const bool targetIsSlow = targetAfter < 0.99f;
		const bool ownsTimeScale = g_wheelerOwnsTimeScale.load();

		// A witnessed normal target -> wheel slowdown also proves acquisition when
		// current lags behind target. Never infer this baseline from a low current.
		const bool previousTargetIsNormal = a_previousTarget >= 0.99f && a_previousTarget <= 1.01f;
		const bool witnessedNormalTarget = previousTargetIsNormal && !SlowTimeCompat::HasActiveSlowTimeEffect();
#if WSTF_BOUNDED_DIAGNOSTICS
		if (OwnershipDiagnostics::IsEnabled()) {
			if (!std::isfinite(a_previousTarget)) {
				RecordWatch(OwnershipDiagnostics::Kind::MissingHistory, OwnershipDiagnostics::Reason::UnknownPreviousTarget,
					a_writer, a_previousTarget, currentBefore, targetAfter);
			}
			if (!ownsTimeScale && targetIsSlow && !currentIsNormal && !witnessedNormalTarget) {
				RecordWatch(OwnershipDiagnostics::Kind::OwnerNotAcquired,
					SlowTimeCompat::HasActiveSlowTimeEffect() ? OwnershipDiagnostics::Reason::NativeEffect : OwnershipDiagnostics::Reason::ExternalBaseline,
					a_writer, a_previousTarget, currentBefore, targetAfter);
			}
			if (g_wheelerRestorePending.load() && !ownsTimeScale) {
				RecordWatch(OwnershipDiagnostics::Kind::Unexpected, OwnershipDiagnostics::Reason::PendingWithoutOwner,
					a_writer, a_previousTarget, currentBefore, targetAfter);
			}
		}
#endif

		if (!ownsTimeScale && targetIsSlow && (currentIsNormal || witnessedNormalTarget)) {
			g_preWheelerTimeScale.store(currentIsNormal ? currentBefore : a_previousTarget);
			g_wheelerOwnsTimeScale.store(true);
#if WSTF_BOUNDED_DIAGNOSTICS
			if (OwnershipDiagnostics::IsEnabled()) {
				RecordWatch(OwnershipDiagnostics::Kind::OwnerAcquired,
					currentIsNormal ? OwnershipDiagnostics::Reason::NormalCurrent : OwnershipDiagnostics::Reason::ObservedNormalTarget,
					a_writer, a_previousTarget, currentBefore, targetAfter);
			}
#endif


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


#if WSTF_BOUNDED_DIAGNOSTICS
			if (OwnershipDiagnostics::IsEnabled()) {
				RecordWatch(OwnershipDiagnostics::Kind::RestoreBlocked, OwnershipDiagnostics::Reason::NoOwnerExternal,
					a_writer, a_previousTarget, currentBefore, targetAfter);
			}
#endif
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
			if (targetIsNormal && g_skyrimSoulsSlowActive.load()) {
				// Leave the menu multiplier intact; finish the wheel restore on
				// the observed Souls exit write, not while its slowdown is active.
				*target = currentBefore;
				ClearTargetInterpolation();
				g_wheelerRestorePending.store(true);
#if WSTF_BOUNDED_DIAGNOSTICS
				if (OwnershipDiagnostics::IsEnabled()) {
					RecordWatch(OwnershipDiagnostics::Kind::RestoreDeferredMenu, OwnershipDiagnostics::Reason::ActiveMenu,
						a_writer, a_previousTarget, currentBefore, targetAfter);
				}
#endif

				return true;
			}
			const float cachedBaseline = g_preWheelerTimeScale.load();
			if (targetIsNormal) {
				float replacementTarget = targetAfter;
				if (SlowTimeCompat::TryDeferWheelerRestore(currentBefore, targetAfter, cachedBaseline, replacementTarget)) {
					*target = replacementTarget;
					ClearTargetInterpolation();
					g_wheelerOwnsTimeScale.store(false);
#if WSTF_BOUNDED_DIAGNOSTICS
					if (OwnershipDiagnostics::IsEnabled()) {
						RecordWatch(OwnershipDiagnostics::Kind::OwnerReleased, OwnershipDiagnostics::Reason::NativeEffect,
							a_writer, a_previousTarget, currentBefore, targetAfter);
					}
#endif


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
#if WSTF_BOUNDED_DIAGNOSTICS
				if (OwnershipDiagnostics::IsEnabled()) {
					RecordWatch(OwnershipDiagnostics::Kind::OwnerReleased,
						NearlyEqual(targetAfter, cachedBaseline) ? OwnershipDiagnostics::Reason::BaselineMatched : OwnershipDiagnostics::Reason::NormalRequest,
						a_writer, a_previousTarget, currentBefore, targetAfter);
				}
#endif


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

		const auto* targetBeforeHandling = g_targetMultiplier.get();
		const float previousTarget = targetBeforeHandling ?
			g_previousTarget.exchange(*targetBeforeHandling) : std::numeric_limits<float>::quiet_NaN();

		CacheModuleHandles();
		const WriterInfo writer = ResolveWriter(a_info->ExceptionRecord->ExceptionAddress);
		if (!writer.isSelf) {

#if WSTF_BOUNDED_DIAGNOSTICS
			const bool diagCapture = OwnershipDiagnostics::IsEnabled();
			const bool diagOwnedAtEntry = diagCapture && g_wheelerOwnsTimeScale.load();
			const bool diagCompletionEligible = diagOwnedAtEntry && (IsWheelerLikeModule(writer.module) ||
				(writer.module && writer.module == g_skyrimSoulsModule && g_wheelerRestorePending.load()));
			const float diagCurrentAtEntry = diagCompletionEligible && g_currentMultiplier.get() ? *g_currentMultiplier.get() : OwnershipDiagnostics::kUnknown;
			const float diagRequestedAtEntry = diagCompletionEligible && g_targetMultiplier.get() ? *g_targetMultiplier.get() : OwnershipDiagnostics::kUnknown;
#endif
			ObserveTargetWrite(writer, previousTarget);
#if WSTF_VERBOSE_LOGGING
			MaybeLogWriterDiscovery(writer);
#endif
			if (!HandleWheelerOwnershipOnTargetWrite(writer, previousTarget)) {
				if (auto* target = g_targetMultiplier.get()) {
					SlowTimeCompat::MaybeOverrideTrackedFinishRestore(*target);
				}
				if (ShouldSyncTargetWriteForWriter(writer)) {
					SyncCurrentToTarget("target-write", writer);
				}
#if WSTF_BOUNDED_DIAGNOSTICS
				if (diagCapture) {
					if (!diagOwnedAtEntry && g_wheelerOwnsTimeScale.load() && IsWheelerLikeModule(writer.module) &&
						g_currentMultiplier.get() && g_targetMultiplier.get() && NearlyEqual(*g_currentMultiplier.get(), *g_targetMultiplier.get())) {
						RecordWatch(OwnershipDiagnostics::Kind::WheelApplyCompleted, OwnershipDiagnostics::Reason::SyncCompleted,
							writer, previousTarget, *g_currentMultiplier.get(), *g_targetMultiplier.get());
					}
					if (diagCompletionEligible && !g_wheelerOwnsTimeScale.load() && !SlowTimeCompat::HasActiveSlowTimeEffect() &&
						g_currentMultiplier.get() && g_targetMultiplier.get() && NearlyEqual(*g_currentMultiplier.get(), *g_targetMultiplier.get())) {
						RecordWatch(OwnershipDiagnostics::Kind::RestoreCompleted, OwnershipDiagnostics::Reason::SyncCompleted,
							writer, previousTarget, diagCurrentAtEntry, diagRequestedAtEntry);
					}
				}
#endif

			}
		}

		if (const auto* target = g_targetMultiplier.get()) {
			g_previousTarget.store(*target);
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
#if WSTF_BOUNDED_DIAGNOSTICS
				if (OwnershipDiagnostics::IsEnabled()) {
					spdlog::default_logger()->flush();
				}
#endif

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
		std::call_once(g_configurationLoaded, []() {
			try {
				LoadAdditionalModuleConfiguration();
			} catch (const std::exception& error) {
				g_additionalModuleCount.store(0, std::memory_order_release);
				SKSE::log::warn("[SlowTimeFix] optional INI could not be read: {}; using built-in compatibility modules only", error.what());
			}
		});
		RefreshAdditionalModuleHandles();

		if (g_watchState.installLoopStarted.exchange(true)) {
			return;
		}

		std::thread([]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			InstallLoop();
		}).detach();
	}
}
