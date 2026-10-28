#include "SlowTimeCompat.h"
#if WSTF_BOUNDED_DIAGNOSTICS
#include "OwnershipDiagnostics.h"
#endif


namespace
{
	struct TrackedSpellDefinition
	{
		RE::FormID localFormID;
		std::string_view modName;
		std::string_view label;
	};

	struct ResolvedTrackedSpell
	{
		RE::SpellItem* spell{ nullptr };
		std::string_view label;
	};

	struct ActiveTrackedSlow
	{
		RE::ActiveEffect* effect{ nullptr };
		RE::MagicItem* spell{ nullptr };
		float baselineAtStart{ 1.0f };
		std::uint64_t sequence{ 0 };
		std::string_view label;
#if WSTF_BOUNDED_DIAGNOSTICS
		std::uint32_t diagFormID{ 0 };
		std::uint32_t diagLocalID{ 0 };
		OwnershipDiagnostics::EffectType diagEffectType{ OwnershipDiagnostics::EffectType::None };
#endif

	};

	struct FinishContext
	{
		RE::ActiveEffect* effect{ nullptr };
		bool armed{ false };
		bool overrideApplied{ false };
	};

	struct CompatState
	{
		std::mutex lock;
		bool hooksInstalled{ false };
		std::vector<ResolvedTrackedSpell> trackedSpells;
		std::unordered_map<RE::ActiveEffect*, ActiveTrackedSlow> activeTrackedSlows;
#if WSTF_VERBOSE_LOGGING
		std::unordered_set<RE::FormID> loggedUntrackedStarts;
		std::unordered_set<RE::FormID> loggedUntrackedFinishes;
#endif
		std::uint64_t nextSequence{ 1 };
		RE::ActiveEffect* pendingOverrideEffect{ nullptr };
		float pendingExpectedBaseline{ 1.0f };
		float pendingRestoreBaseline{ 1.0f };
		FinishContext finishContext{};
	};

	constexpr std::array kTrackedSpellDefinitions{
		TrackedSpellDefinition{ 0x86C, "For Honor in Skyrim.esp", "FHIS parry slowdown" },
		TrackedSpellDefinition{ 0x86D, "For Honor in Skyrim.esp", "FHIS follow-up slowdown" },
		TrackedSpellDefinition{ 0x848, "For Honor Balance Patch.esp", "For Honor Reforged follow-up slowdown" }
	};

	constexpr float kEqualityEpsilon = 0.0005f;

	CompatState g_state{};
	std::atomic_bool g_hasActiveSlowTimeEffect{ false };

	REL::Relocation<float*> g_currentMultiplier{ RELOCATION_ID(511882, 388442) };
	REL::Relocation<float*> g_targetMultiplier{ RELOCATION_ID(511883, 388443) };

	bool NearlyEqual(float a_lhs, float a_rhs)
	{
		return std::fabs(a_lhs - a_rhs) <= kEqualityEpsilon;
	}

	void ClearTargetInterpolation()
	{
		if (auto* timer = RE::BSTimer::GetSingleton()) {
			timer->useGlobalTimeMultiplierTarget = false;
		}
	}

	const ResolvedTrackedSpell* FindTrackedSpell(RE::MagicItem* a_spell)
	{
		if (!a_spell) {
			return nullptr;
		}

		std::lock_guard lock(g_state.lock);
		for (const auto& trackedSpell : g_state.trackedSpells) {
			if (trackedSpell.spell == a_spell) {
				return &trackedSpell;
			}
		}

		return nullptr;
	}

	const ActiveTrackedSlow* FindOldestTrackedSlowLocked()
	{
		const ActiveTrackedSlow* oldest = nullptr;
		for (const auto& [_, trackedSlow] : g_state.activeTrackedSlows) {
			if (!oldest || trackedSlow.sequence < oldest->sequence) {
				oldest = &trackedSlow;
			}
		}

		return oldest;
	}

#if WSTF_VERBOSE_LOGGING
	std::string DescribeSpell(RE::MagicItem* a_spell)
	{
		if (!a_spell) {
			return "spell=<null>";
		}

		const auto* form = a_spell->As<RE::TESForm>();
		if (!form) {
			return "spell=<not-a-form>";
		}

		const auto* file = form->GetFile(0);
		const auto modName = file ? file->GetFilename() : std::string_view("<unknown>");
		return std::format(
			"spell={:08X} local={:06X} mod={} name={}",
			form->GetFormID(),
			form->GetLocalFormID(),
			modName,
			form->GetName());
	}
#endif


#if WSTF_BOUNDED_DIAGNOSTICS
	void RecordEffect(OwnershipDiagnostics::Kind kind, OwnershipDiagnostics::Reason reason,
		RE::ActiveEffect* effect, float baseline, float expected, float restore,
		const ActiveTrackedSlow* slow = nullptr, float requested = OwnershipDiagnostics::kUnknown) noexcept
	{
		if (!OwnershipDiagnostics::IsEnabled()) { return; }
		using namespace OwnershipDiagnostics;
		const auto* current = g_currentMultiplier.get();
		const auto* target = g_targetMultiplier.get();
		Event event{ .effect = reinterpret_cast<std::uintptr_t>(effect),
			.current = current ? *current : kUnknown, .requested = std::isfinite(requested) ? requested : target ? *target : kUnknown,
			.currentAfter = current ? *current : kUnknown, .targetAfter = target ? *target : kUnknown,
			.baseline = restore, .expected = expected, .effectBaseline = baseline,
			.formID = slow ? slow->diagFormID : 0,
			.localID = slow ? slow->diagLocalID : 0,
			.flags = static_cast<std::uint16_t>(g_hasActiveSlowTimeEffect.load() ? 8 : 0),
			.flagsValid = 8, .kind = kind, .reason = reason,
			.effectType = slow ? slow->diagEffectType : EffectType::None };
		Record(event);
	}

#endif
	void TrackSlowStart(RE::SlowTimeEffect* a_effect)
	{
		if (!a_effect) {
			return;
		}

		const auto* trackedSpell = FindTrackedSpell(a_effect->spell);
		if (!trackedSpell) {
#if WSTF_VERBOSE_LOGGING
			const auto formID = a_effect->spell ? a_effect->spell->GetFormID() : 0;
			std::lock_guard lock(g_state.lock);
			if (g_state.loggedUntrackedStarts.insert(formID).second) {
				SKSE::log::info(
					"[SlowTimeFix] untracked SlowTimeEffect start baseline={:.3f} {}",
					RE::BSTimer::QGlobalTimeMultiplier(),
					DescribeSpell(a_effect->spell));
			}
#endif
		}

		const float baseline = RE::BSTimer::QGlobalTimeMultiplier();

		std::lock_guard lock(g_state.lock);
		g_state.activeTrackedSlows[a_effect] = ActiveTrackedSlow{
			.effect = a_effect,
			.spell = a_effect->spell,
			.baselineAtStart = baseline,
			.sequence = g_state.nextSequence++,
			.label = trackedSpell ? trackedSpell->label : "external SlowTimeEffect"
		};
		g_hasActiveSlowTimeEffect.store(true, std::memory_order_release);
#if WSTF_BOUNDED_DIAGNOSTICS
		if (OwnershipDiagnostics::IsEnabled()) {
			const auto diagStart = g_state.activeTrackedSlows.find(a_effect);
			diagStart->second.diagFormID = a_effect->spell ? a_effect->spell->GetFormID() : 0;
			diagStart->second.diagLocalID = a_effect->spell ? a_effect->spell->GetLocalFormID() : 0;
			diagStart->second.diagEffectType = trackedSpell ? OwnershipDiagnostics::EffectType::ForHonor : OwnershipDiagnostics::EffectType::Native;
			RecordEffect(OwnershipDiagnostics::Kind::EffectStart,
				trackedSpell ? OwnershipDiagnostics::Reason::ForHonorEffect : OwnershipDiagnostics::Reason::NativeEffect,
				a_effect, baseline, baseline, OwnershipDiagnostics::kUnknown, &diagStart->second);
		}
#endif


#if WSTF_VERBOSE_LOGGING
		SKSE::log::info(
			"[SlowTimeFix] tracking {} start baseline={:.3f}",
			trackedSpell ? trackedSpell->label : "external SlowTimeEffect",
			baseline);
#endif
	}

	void BeginTrackedSlowFinish(RE::SlowTimeEffect* a_effect)
	{
		if (!a_effect) {
			return;
		}

		std::lock_guard lock(g_state.lock);
		g_state.finishContext = {};

		const auto it = g_state.activeTrackedSlows.find(a_effect);
		if (it == g_state.activeTrackedSlows.end()) {
			return;
		}

		g_state.finishContext.effect = a_effect;
		g_state.finishContext.armed = g_state.pendingOverrideEffect == a_effect;
		g_state.finishContext.overrideApplied = false;
#if WSTF_BOUNDED_DIAGNOSTICS
		if (OwnershipDiagnostics::IsEnabled()) {
			RecordEffect(OwnershipDiagnostics::Kind::EffectFinishBegin, OwnershipDiagnostics::Reason::EffectEnding,
				a_effect, it->second.baselineAtStart, g_state.pendingExpectedBaseline, g_state.pendingRestoreBaseline, &it->second);
		}
#endif

	}

	void CompleteTrackedSlowFinish(RE::SlowTimeEffect* a_effect)
	{
		if (!a_effect) {
			return;
		}

		std::lock_guard lock(g_state.lock);

		const bool clearedPendingOverride = g_state.pendingOverrideEffect == a_effect;
		const bool overrideApplied = g_state.finishContext.effect == a_effect && g_state.finishContext.overrideApplied;
		const float expectedBaseline = g_state.pendingExpectedBaseline;
		const float restoreBaseline = g_state.pendingRestoreBaseline;
#if WSTF_VERBOSE_LOGGING
		const auto trackedIt = g_state.activeTrackedSlows.find(a_effect);
		const bool wasTracked = trackedIt != g_state.activeTrackedSlows.end();
		const std::string_view label = wasTracked ? trackedIt->second.label : "tracked slowdown";
		const float trackedBaseline = wasTracked ? trackedIt->second.baselineAtStart : 1.0f;
#endif


#if WSTF_BOUNDED_DIAGNOSTICS
		if (OwnershipDiagnostics::IsEnabled()) {
			const auto diagEnd = g_state.activeTrackedSlows.find(a_effect);
			RecordEffect(diagEnd != g_state.activeTrackedSlows.end() ? OwnershipDiagnostics::Kind::EffectFinishEnd : OwnershipDiagnostics::Kind::Unexpected,
				diagEnd != g_state.activeTrackedSlows.end() ? OwnershipDiagnostics::Reason::NativeFinishReturned : OwnershipDiagnostics::Reason::UntrackedFinish,
				a_effect, diagEnd != g_state.activeTrackedSlows.end() ? diagEnd->second.baselineAtStart : OwnershipDiagnostics::kUnknown,
				expectedBaseline, restoreBaseline, diagEnd != g_state.activeTrackedSlows.end() ? &diagEnd->second : nullptr);
		}
#endif
		g_state.activeTrackedSlows.erase(a_effect);
		g_hasActiveSlowTimeEffect.store(!g_state.activeTrackedSlows.empty(), std::memory_order_release);

		if (g_state.finishContext.effect == a_effect) {
			g_state.finishContext = {};
		}

		if (!clearedPendingOverride) {
#if WSTF_VERBOSE_LOGGING
			if (wasTracked) {
				SKSE::log::info(
					"[SlowTimeFix] {} finished normally baseline={:.3f}",
					label,
					trackedBaseline);
			} else {
				const auto formID = a_effect->spell ? a_effect->spell->GetFormID() : 0;
				if (g_state.loggedUntrackedFinishes.insert(formID).second) {
					SKSE::log::info(
						"[SlowTimeFix] untracked SlowTimeEffect finish {}",
						DescribeSpell(a_effect->spell));
				}
			}
#endif
			return;
		}

		if (const auto* remainingSlow = FindOldestTrackedSlowLocked()) {
			// Keep the restore intent until the last overlapping native effect ends.
			g_state.pendingOverrideEffect = remainingSlow->effect;
			g_state.pendingExpectedBaseline = remainingSlow->baselineAtStart;
#if WSTF_BOUNDED_DIAGNOSTICS
			if (OwnershipDiagnostics::IsEnabled()) {
				RecordEffect(OwnershipDiagnostics::Kind::RestoreTransferred, OwnershipDiagnostics::Reason::OverlappingEffect,
					remainingSlow->effect, remainingSlow->baselineAtStart, remainingSlow->baselineAtStart, restoreBaseline, remainingSlow);
			}
#endif

			return;
		}

		g_state.pendingOverrideEffect = nullptr;
		g_state.pendingExpectedBaseline = 1.0f;
		g_state.pendingRestoreBaseline = 1.0f;

		if (overrideApplied) {
#if WSTF_VERBOSE_LOGGING
			SKSE::log::info(
				"[SlowTimeFix] {} finished with deferred Wheeler restore to={:.3f}",
				label,
				restoreBaseline);
#endif
		}

		// Finish may write current after the intercepted target write. Apply the
		// same baseline guard even when the target redirect already succeeded.
		auto* current = g_currentMultiplier.get();
		auto* target = g_targetMultiplier.get();
		if (!current || !target) {
			return;
		}

		if (!NearlyEqual(*target, expectedBaseline) && !NearlyEqual(*current, expectedBaseline)) {
#if WSTF_BOUNDED_DIAGNOSTICS
			if (OwnershipDiagnostics::IsEnabled()) {
				RecordEffect(OwnershipDiagnostics::Kind::FallbackSkipped, OwnershipDiagnostics::Reason::NewerMultiplier,
					a_effect, OwnershipDiagnostics::kUnknown, expectedBaseline, restoreBaseline);
			}
#endif

			return;
		}

		*target = restoreBaseline;
		*current = restoreBaseline;
		ClearTargetInterpolation();
#if WSTF_BOUNDED_DIAGNOSTICS
		if (OwnershipDiagnostics::IsEnabled()) {
			RecordEffect(OwnershipDiagnostics::Kind::FallbackCompleted, OwnershipDiagnostics::Reason::ExpectedBaseline,
				a_effect, OwnershipDiagnostics::kUnknown, expectedBaseline, restoreBaseline);
		}
#endif


#if WSTF_VERBOSE_LOGGING
		SKSE::log::info(
			"[SlowTimeFix] fallback-restored deferred slowdown finish expected={:.3f} restored={:.3f}",
			expectedBaseline,
			restoreBaseline);
#endif
	}

	struct SlowTimeEffectHooks
	{
		static void Install()
		{
			std::lock_guard lock(g_state.lock);
			if (g_state.hooksInstalled) {
				return;
			}

			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_SlowTimeEffect[0] };
			_Start = vtbl.write_vfunc(0x14, Start);
			_Finish = vtbl.write_vfunc(0x15, Finish);
			g_state.hooksInstalled = true;

			SKSE::log::info("[SlowTimeFix] installed SlowTimeEffect compatibility hooks");
		}

	private:
		static void Start(RE::SlowTimeEffect* a_effect)
		{
			TrackSlowStart(a_effect);
			_Start(a_effect);
		}

		static void Finish(RE::SlowTimeEffect* a_effect)
		{
			BeginTrackedSlowFinish(a_effect);
			_Finish(a_effect);
			CompleteTrackedSlowFinish(a_effect);
		}

		static inline REL::Relocation<decltype(Start)> _Start;
		static inline REL::Relocation<decltype(Finish)> _Finish;
	};
}

namespace SlowTimeCompat
{
	bool HasActiveSlowTimeEffect()
	{
		return g_hasActiveSlowTimeEffect.load(std::memory_order_acquire);
	}

	void InstallHooks()
	{
		SlowTimeEffectHooks::Install();
	}

	void RefreshTrackedForms()
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			SKSE::log::info("[SlowTimeFix] TESDataHandler unavailable; tracked slowdown forms not refreshed");
			return;
		}

		std::vector<ResolvedTrackedSpell> trackedSpells;
		trackedSpells.reserve(kTrackedSpellDefinitions.size());

		for (const auto& definition : kTrackedSpellDefinitions) {
			if (auto* spell = dataHandler->LookupForm<RE::SpellItem>(definition.localFormID, definition.modName)) {
				trackedSpells.push_back(ResolvedTrackedSpell{ .spell = spell, .label = definition.label });
				SKSE::log::info(
					"[SlowTimeFix] tracking spell {} {:08X} from {}",
					definition.label,
					spell->formID,
					definition.modName);
			}
		}

		if (trackedSpells.empty()) {
			SKSE::log::warn("[SlowTimeFix] no tracked For Honor slowdown spells were resolved");
		} else {
			SKSE::log::info("[SlowTimeFix] resolved {} tracked For Honor slowdown spell(s)", trackedSpells.size());
		}

		std::lock_guard lock(g_state.lock);
		g_state.trackedSpells = std::move(trackedSpells);
	}

	bool TryDeferWheelerRestore(float a_currentBefore, float, float a_restoreBaseline, float& a_replacementTarget)
	{
		std::lock_guard lock(g_state.lock);
		const auto* oldestTrackedSlow = FindOldestTrackedSlowLocked();
		if (!oldestTrackedSlow) {
			return false;
		}

		g_state.pendingOverrideEffect = oldestTrackedSlow->effect;
		g_state.pendingExpectedBaseline = oldestTrackedSlow->baselineAtStart;
		g_state.pendingRestoreBaseline = a_restoreBaseline;
		a_replacementTarget = a_currentBefore;
#if WSTF_BOUNDED_DIAGNOSTICS
		if (OwnershipDiagnostics::IsEnabled()) {
			RecordEffect(OwnershipDiagnostics::Kind::RestoreDeferredEffect, OwnershipDiagnostics::Reason::NativeEffect,
				oldestTrackedSlow->effect, oldestTrackedSlow->baselineAtStart, g_state.pendingExpectedBaseline, a_restoreBaseline, oldestTrackedSlow);
		}
#endif


#if WSTF_VERBOSE_LOGGING
		SKSE::log::info(
			"[SlowTimeFix] deferred Wheeler restore during {} current={:.3f} expected_finish={:.3f} restore_to={:.3f}",
			oldestTrackedSlow->label,
			a_currentBefore,
			oldestTrackedSlow->baselineAtStart,
			a_restoreBaseline);
#endif

		return true;
	}

	void MaybeOverrideTrackedFinishRestore(float& a_targetAfter)
	{
		std::lock_guard lock(g_state.lock);
		if (!g_state.finishContext.armed || g_state.finishContext.effect != g_state.pendingOverrideEffect) {
			return;
		}

		if (g_state.activeTrackedSlows.size() != 1 || !NearlyEqual(a_targetAfter, g_state.pendingExpectedBaseline)) {
			return;
		}


#if WSTF_BOUNDED_DIAGNOSTICS
		const float diagRedirectRequested = OwnershipDiagnostics::IsEnabled() ? a_targetAfter : OwnershipDiagnostics::kUnknown;
#endif
		a_targetAfter = g_state.pendingRestoreBaseline;
		g_state.finishContext.overrideApplied = true;
#if WSTF_BOUNDED_DIAGNOSTICS
		if (OwnershipDiagnostics::IsEnabled()) {
			RecordEffect(OwnershipDiagnostics::Kind::EffectRedirect, OwnershipDiagnostics::Reason::ExpectedBaseline,
				g_state.pendingOverrideEffect, OwnershipDiagnostics::kUnknown, g_state.pendingExpectedBaseline, g_state.pendingRestoreBaseline, nullptr, diagRedirectRequested);
		}
#endif


#if WSTF_VERBOSE_LOGGING
		SKSE::log::info(
			"[SlowTimeFix] redirected tracked slowdown finish expected={:.3f} restored={:.3f}",
			g_state.pendingExpectedBaseline,
			g_state.pendingRestoreBaseline);
#endif
	}
}
