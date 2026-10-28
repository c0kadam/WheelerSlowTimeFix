#include "OwnershipDiagnostics.h"

#if WSTF_BOUNDED_DIAGNOSTICS
#include "DiagnosticsSettings.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace OwnershipDiagnostics
{
	namespace
	{
		const char* Name(Kind kind)
		{
			constexpr std::array names{ "owner-acquired", "owner-released", "restore-blocked", "restore-deferred-menu",
				"restore-completed", "souls-activated", "souls-restored", "effect-start",
				"effect-finish-begin", "effect-finish-end", "restore-deferred-effect",
				"restore-transferred", "effect-redirect", "fallback-completed", "fallback-skipped",
				"missing-history", "unexpected-state", "owner-not-acquired", "wheel-apply-completed" };
			static_assert(names.size() == static_cast<std::size_t>(Kind::Count));
			return names[static_cast<std::size_t>(kind)];
		}
		const char* Name(Reason reason)
		{
			constexpr std::array names{ "normal-current", "observed-normal-target", "baseline-matched", "normal-request",
				"unrelated-writer", "no-owner-preserve-external", "active-Souls-menu", "Souls-menu-exit",
				"native-SlowTimeEffect", "For-Honor-SlowTimeEffect", "before-native-finish", "after-native-finish",
				"another-effect-active", "expected-baseline-match", "newer-multiplier-preserved",
				"unknown-previous-target", "non-finite-multiplier", "missing-multiplier-pointers",
				"pending-restore-without-owner", "menu-exit-without-observed-entry", "current-target-synchronized", "finish-without-recorded-start", "external-or-unknown-baseline" };
			static_assert(names.size() == static_cast<std::size_t>(Reason::Count));
			return names[static_cast<std::size_t>(reason)];
		}
		std::string ModuleName(std::uintptr_t module)
		{
			if (!module) { return "none"; }
			std::array<char, MAX_PATH> path{};
			const auto length = ::GetModuleFileNameA(reinterpret_cast<HMODULE>(module), path.data(), static_cast<DWORD>(path.size()));
			if (!length || length >= path.size()) { return "unknown"; }
			const std::string_view full(path.data(), length);
			return std::string(full.substr(full.find_last_of("/\\") + 1));
		}

		void WriteEvent(const std::shared_ptr<spdlog::logger>& logger, const Event& event,
			std::uint64_t& number, LONGLONG origin, LONGLONG frequency)
		{
			const double milliseconds = (static_cast<double>(event.ticks) - static_cast<double>(origin)) * 1000.0 / static_cast<double>(frequency);
			const auto state = [&](unsigned bit) { return !(event.flagsValid & bit) ? "?" : (event.flags & bit) ? "1" : "0"; };
			logger->info("[{}] #{} t={:.3f}ms tid={} {} reason={} writer={} module=0x{:X} prev={:.6f} current={:.6f} requested={:.6f} current_after={:.6f} target_after={:.6f} baseline={:.6f} expected={:.6f} effect_baseline={:.6f} owner={} pending={} souls={} native={} effect=0x{:X} type={} form={:08X} local={:06X}",
				kBuildID, ++number, milliseconds, event.threadID, Name(event.kind), Name(event.reason), ModuleName(event.module), event.module,
				event.previous, event.current, event.requested, event.currentAfter, event.targetAfter, event.baseline, event.expected, event.effectBaseline,
				state(1), state(2), state(4), state(8), event.effect, event.effectType == EffectType::ForHonor ? "ForHonor" : event.effectType == EffectType::Native ? "Native" : "see-effect-start",
				event.formID, event.localID);
		}

		void Run(std::shared_ptr<spdlog::logger> logger, LONGLONG origin, LONGLONG frequency) noexcept
		{
			try {
				std::uint64_t number = 0, lastDropped = 0, lastCapped = 0;
				unsigned lossNotices = 0;
				bool firstBatch = true;
				auto lastLossNotice = std::chrono::steady_clock::now() - std::chrono::seconds(5);
				for (;;) {
					std::this_thread::sleep_for(std::chrono::milliseconds(250));
					Event event;
					bool wrote = false;
					for (unsigned count = 0; count < 32 && queue.TryPop(event); ++count) {
						WriteEvent(logger, event, number, origin, frequency);
						wrote = true;
					}
					const auto lost = dropped.load(std::memory_order_relaxed);
					const auto limit = capped.load(std::memory_order_relaxed);
					const auto now = std::chrono::steady_clock::now();
					const bool firstCapNotice = limit != 0 && lastCapped == 0;
					if (firstCapNotice || (lossNotices < 16 && lost != lastDropped && now - lastLossNotice >= std::chrono::seconds(5))) {
						logger->warn("[{}] diagnostic loss: queue/contention-dropped={} session-capped={} repeated-decisions-suppressed={}; missing events must not be treated as proof of no transition", kBuildID, lost, limit, suppressed.load(std::memory_order_relaxed));
						lastDropped = lost; lastCapped = limit; lastLossNotice = now;
						if (!firstCapNotice) { ++lossNotices; }
						wrote = true;
					}
					if (wrote || firstBatch) { logger->flush(); }
					firstBatch = false;
				}
			} catch (...) {
				enabled.store(false, std::memory_order_release);
				try { logger->error("[{}] diagnostics stopped after a consumer failure; correction remains active", kBuildID); } catch (...) {}
			}
		}
	}

	void Start() noexcept
	{
		try {
			// Match the plugin's process lifetime; never join a worker under the DLL loader lock.
			static const bool started = [] {
				auto logger = spdlog::default_logger();
				logger->flush_on(spdlog::level::warn);
				LARGE_INTEGER frequency{}, origin{};
				if (!::QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
					logger->warn("[{}] diagnostics unavailable: performance counter frequency missing", kBuildID);
					return false;
				}
				::QueryPerformanceCounter(&origin);
				logger->info("[{}] bounded diagnostic x64 Release; legacy verbose=OFF; queue={}; session-budget={}; drain<=32/250ms; loss notices<=16 plus one cap notice; state '?' means not captured by that event", kBuildID, kCapacity, kSessionLimit);
				logger->info("[{}] same correction as quiet test DLL SHA256 5600138CDD2F59DD7450F1C7021342BFB2FFE006E426DBFF2C783BC4CC468132; nan means unavailable; event times use capture QPC, not flush time", kBuildID);
				logger->flush();
				enabled.store(true, std::memory_order_release);
				std::thread(Run, logger, origin.QuadPart, frequency.QuadPart).detach();
				return true;
			}();
			(void)started;
		} catch (...) {
			enabled.store(false, std::memory_order_release);
			try { spdlog::warn("[{}] diagnostics unavailable; correction remains active", kBuildID); } catch (...) {}
		}
	}
	void Initialize() noexcept
	{
		static const bool initialized = []() noexcept {
			try {
				std::array<wchar_t, 32768> executable{};
				const auto length = ::GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
				Settings::Status status = Settings::Status::PathUnavailable;
				if (length != 0 && length < executable.size()) {
					const auto ini = std::filesystem::path(executable.data()).parent_path() / L"Data/SKSE/Plugins/WheelerSlowTimeFix.ini";
					status = Settings::Read(ini.c_str());
				}
				const bool selected = status == Settings::Status::Enabled;
				spdlog::info("[SlowTimeFix] Bounded diagnostics: {} ({}); startup-only; legacy verbose=OFF",
					selected ? "ON" : "OFF", Settings::Description(status));
				if (selected) { Start(); }
			} catch (...) {
				enabled.store(false, std::memory_order_release);
				try { spdlog::warn("[SlowTimeFix] Bounded diagnostics: OFF (configuration unavailable); legacy verbose=OFF"); } catch (...) {}
			}
			return true;
		}();
		(void)initialized;
	}

}
#endif
