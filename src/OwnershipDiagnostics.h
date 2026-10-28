#pragma once

#ifndef WSTF_BOUNDED_DIAGNOSTICS
#define WSTF_BOUNDED_DIAGNOSTICS 0
#endif

#if WSTF_BOUNDED_DIAGNOSTICS
#if WSTF_VERBOSE_LOGGING
#error Bounded diagnostics require legacy verbose logging to be disabled.
#endif

#include <Windows.h>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace OwnershipDiagnostics
{
	inline constexpr std::size_t kCapacity = 1024;
	inline constexpr std::uint32_t kSessionLimit = 16384;
	inline constexpr float kUnknown = std::numeric_limits<float>::quiet_NaN();
	inline constexpr char kBuildID[] = "WSTF-OWNERSHIP-DIAG-01";

	enum class Kind : std::uint8_t
	{
		OwnerAcquired, OwnerReleased, RestoreBlocked, RestoreDeferredMenu,
		RestoreCompleted, SoulsActivated, SoulsRestored, EffectStart,
		EffectFinishBegin, EffectFinishEnd, RestoreDeferredEffect,
		RestoreTransferred, EffectRedirect, FallbackCompleted, FallbackSkipped,
		MissingHistory, Unexpected, OwnerNotAcquired, WheelApplyCompleted, Count
	};
	enum class Reason : std::uint8_t
	{
		NormalCurrent, ObservedNormalTarget, BaselineMatched, NormalRequest,
		UnrelatedWriter, NoOwnerExternal, ActiveMenu, MenuExit,
		NativeEffect, ForHonorEffect, EffectEnding, NativeFinishReturned,
		OverlappingEffect, ExpectedBaseline, NewerMultiplier,
		UnknownPreviousTarget, NonFinite, MissingPointers,
		PendingWithoutOwner, UnobservedMenuEntry, SyncCompleted, UntrackedFinish, ExternalBaseline, Count
	};
	enum class EffectType : std::uint8_t { None, Native, ForHonor };

	struct Event
	{
		std::uint64_t ticks{ 0 };
		std::uintptr_t module{ 0 };
		std::uintptr_t effect{ 0 };
		float previous{ kUnknown };
		float current{ kUnknown };
		float requested{ kUnknown };
		float currentAfter{ kUnknown };
		float targetAfter{ kUnknown };
		float baseline{ kUnknown };
		float expected{ kUnknown };
		float effectBaseline{ kUnknown };
		std::uint32_t threadID{ 0 };
		std::uint32_t formID{ 0 };
		std::uint32_t localID{ 0 };
		std::uint16_t flags{ 0 };  // owner=1, pending wheel restore=2, Souls active=4, native active=8
		std::uint16_t flagsValid{ 0 };
		Kind kind{ Kind::Unexpected };
		Reason reason{ Reason::NonFinite };
		EffectType effectType{ EffectType::None };
	};
	static_assert(std::is_trivially_copyable_v<Event>);
	static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
	static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
	static_assert((kCapacity & (kCapacity - 1)) == 0);

	// Bounded MPSC queue. A preempted producer never makes another producer wait.
	class Queue
	{
		struct Cell { std::atomic<std::uint64_t> sequence{ 0 }; Event event{}; };
		std::array<Cell, kCapacity> cells_{};
		alignas(64) std::atomic<std::uint64_t> write_{ 0 };
		alignas(64) std::uint64_t read_{ 0 };  // single consumer only

	public:
		Queue() noexcept
		{
			for (std::size_t i = 0; i < cells_.size(); ++i) {
				cells_[i].sequence.store(i, std::memory_order_relaxed);
			}
		}
		bool TryPush(const Event& event) noexcept
		{
			auto position = write_.load(std::memory_order_relaxed);
			for (unsigned attempt = 0; attempt < 4; ++attempt) {
				auto& cell = cells_[position & (kCapacity - 1)];
				const auto sequence = cell.sequence.load(std::memory_order_acquire);
				if (sequence == position) {
					if (write_.compare_exchange_weak(position, position + 1, std::memory_order_relaxed)) {
						cell.event = event;
						cell.sequence.store(position + 1, std::memory_order_release);
						return true;
					}
				} else if (sequence < position) {
					return false;
				} else {
					position = write_.load(std::memory_order_relaxed);
				}
			}
			return false;
		}
		bool TryPop(Event& event) noexcept
		{
			auto& cell = cells_[read_ & (kCapacity - 1)];
			if (cell.sequence.load(std::memory_order_acquire) != read_ + 1) {
				return false;
			}
			event = cell.event;
			cell.sequence.store(read_ + kCapacity, std::memory_order_release);
			++read_;
			return true;
		}
	};

	inline Queue queue;
	inline std::atomic_bool enabled{ false };
	inline std::atomic<std::uint32_t> sessionReservations{ 0 };
	inline std::atomic<std::uint64_t> dropped{ 0 }, capped{ 0 }, suppressed{ 0 };
	inline std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Kind::Count)> lastDecision{};

	inline bool IsEnabled() noexcept
	{
		return enabled.load(std::memory_order_relaxed);
	}

	inline void Record(Event event) noexcept
	{
		if (!enabled.load(std::memory_order_relaxed)) {
			return;
		}
		if (event.kind == Kind::OwnerAcquired || event.kind == Kind::EffectStart ||
			event.kind == Kind::SoulsActivated || event.kind == Kind::SoulsRestored) {
			lastDecision[static_cast<std::size_t>(Kind::RestoreBlocked)].store(0, std::memory_order_relaxed);
			lastDecision[static_cast<std::size_t>(Kind::RestoreDeferredMenu)].store(0, std::memory_order_relaxed);
			lastDecision[static_cast<std::size_t>(Kind::MissingHistory)].store(0, std::memory_order_relaxed);
			lastDecision[static_cast<std::size_t>(Kind::OwnerNotAcquired)].store(0, std::memory_order_relaxed);
		}
		if (event.kind == Kind::RestoreBlocked || event.kind == Kind::RestoreDeferredMenu ||
			event.kind == Kind::MissingHistory || event.kind == Kind::Unexpected || event.kind == Kind::OwnerNotAcquired) {
			std::uint64_t key = 1469598103934665603ull;
			auto mix = [&](std::uint64_t value) { key = (key ^ value) * 1099511628211ull; };
			mix(event.module); mix(event.effect); mix(event.flags); mix(static_cast<unsigned>(event.reason));
			mix(std::bit_cast<std::uint32_t>(event.previous));
			mix(std::bit_cast<std::uint32_t>(event.current));
			mix(std::bit_cast<std::uint32_t>(event.requested));
			key = key ? key : 1;
			if (lastDecision[static_cast<std::size_t>(event.kind)].exchange(key, std::memory_order_relaxed) == key) {
				suppressed.fetch_add(1, std::memory_order_relaxed);
				return;
			}
		}
		auto reserved = sessionReservations.load(std::memory_order_relaxed);
		bool granted = false;
		for (unsigned attempt = 0; attempt < 4 && reserved < kSessionLimit; ++attempt) {
			if (sessionReservations.compare_exchange_weak(reserved, reserved + 1, std::memory_order_relaxed)) {
				granted = true;
				break;
			}
		}
		if (!granted) {
			(reserved >= kSessionLimit ? capped : dropped).fetch_add(1, std::memory_order_relaxed);
			return;
		}
		LARGE_INTEGER now{};
		::QueryPerformanceCounter(&now);
		event.ticks = static_cast<std::uint64_t>(now.QuadPart);
		event.threadID = ::GetCurrentThreadId();
		if (!queue.TryPush(event)) {
			dropped.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void Initialize() noexcept;
}
#endif
