# Compatibility and integration

Alternative Wheeler implementations have partial compatibility with WheelerSlowTimeFix. Normal sequential use has worked in testing, but an INI entry does not guarantee compatible ownership or restoration behavior. Rapid switching between wheels, opening a second wheel before the first closes, and overlapping Skyrim Souls menu transitions can still leave conflicting restore operations.

## What WheelerSlowTimeFix observes

`wheeler.dll` and `SkyrimSoulsRE.dll` are recognized by default. `[Compatibility] AdditionalDLLs` registers additional DLL basenames as Wheeler-like writers. Recognition compares module handles; it does not discover wheel interfaces, receive wheel-open/close notifications or maintain a separate owner for each DLL.

In [TimeTargetWatch.cpp](src/TimeTargetWatch.cpp), `ObserveTargetWrite()` records target transitions and `HandleWheelerOwnershipOnTargetWrite()` maintains the shared wheel claim and saved baseline. A recognized wheel can acquire ownership from normal current speed or a previously observed normal target. The latter covers current=0.980 with previous target=1.000. That target-history inference is disabled while a native `SlowTimeEffect` is active. Missing history remains conservative.

An intercepted normal-speed restore can be deferred while Skyrim Souls slowing is active, then completed on its observed exit write. Without ownership, a restore that would cancel an external slowdown is vetoed. An unrelated target writer can invalidate the wheel claim. `SyncCurrentToTarget()` synchronizes recognized writes and clears target interpolation.

The watchpoint observes writes to the target multiplier, not arbitrary current-only changes. Discovery runs continuously at 500 ms intervals. A newly created thread can write before it is instrumented, and registered debug registers are not periodically revalidated. Initial watchpoint counts do not prove coverage at a later close operation. These are observation limits, not evidence that a particular failure came from thread discovery.

In [SlowTimeCompat.cpp](src/SlowTimeCompat.cpp), the existing `SlowTimeEffect` Start/Finish hooks track native effects, including the vanilla Slow Time shout, and identify selected For Honor spells by plugin filename and local FormID. `TryDeferWheelerRestore()` saves restore intent while an effect remains active. Finish handling transfers it across overlapping effects, redirects a matching final restore, and uses a guarded fallback when either multiplier still matches the expected starting baseline. Other Slow Time implementations are not automatically covered by those lifecycle hooks.

## Where wheel authors should look

Review your actual open, update and close paths, including fade completion, cancellation, menu entry, save/load and delayed callbacks. Do not assume another wheel has the same architecture.

- **Acquisition and baseline:** save the relevant baseline when your own slowdown is acquired. Do not replace it on every update with an already multiplied menu/effect value. Distinguish current and target; a value below 1.0 alone does not identify its owner.
- **Release:** account for every close path and make repeated closes harmless. Check whether a delayed restore from an earlier open can run after a new wheel has acquired time. Serialize your own lifecycle state when callbacks can run on different threads.
- **External changes:** inspect the branch that declines restoration when the multiplier differs from your applied value. Such a difference may be a nested Skyrim Souls factor or a legitimate spell. Preserve that effect, but do not silently lose your outstanding close intent. Any deferred completion needs reliable lifecycle evidence; an unconditional write of 1.0 or a guessed timeout is not a safe substitute.
- **Overlapping lifecycles:** WheelerSlowTimeFix has one shared wheel owner and baseline. It cannot separate wheel A's restore from wheel B's still-active slowdown merely from their DLL names. Prevent overlap in your own integration or explicitly coordinate the lifecycles. Using one wheel at a time remains the recommended arrangement.
- **Write mechanism:** inspect the exact pointer, writer module, thread and order of current/target writes on both open and close. An INI entry cannot compensate for a close that never writes the watched target. Review any additional timer bookkeeping your implementation performs as well.

These are integration checks, not a public WheelerSlowTimeFix callback API or a universal restoration algorithm. Do not remove an external-effect guard without replacing the information that made it necessary.

## Confirmed p-radials limitation

The tested p-radials implementation can skip its own restore when Skyrim Souls changes the multiplier while the wheel is active. This was observed on Skyrim 1.6.1170 and 1.7.104; WheelerSlowTimeFix does not fix it.

The public [Slowtime::Begin(), End() and Update() implementation](https://github.com/Ipsutnu/p-radials/blob/f342ae60b64e2f4c6e6bdac7fec64c228c0cef90/src/Utils/Slowtime.cpp) uses target relocation IDs 511883/388443. `End()` restores only if that target matches its owned value within 0.0025, then clears its active state even when restoration was skipped. For example, Skyrim Souls can multiply 0.150 by 0.14 to produce 0.021. Closing at that value takes the skip branch. The subsequent menu exit can leave the wheel's 0.150 baseline in place.

There is then no normal-target restore request for WheelerSlowTimeFix to intercept or defer. p-radials authors should inspect that skip branch, active-state reset and close-call ordering. The appropriate correction must retain external effects and establish when the wheel's contribution can safely be removed. The confirmed behavior applies to the tested build and cited source; it is not a claim about every future p-radials version.

## Reading diagnostics and testing integration

Enable diagnostics before startup:

```ini
[Diagnostics]
Enabled=true
```

Restart Skyrim and check the selected mode in `WheelerSlowTimeFix.log`. Missing, invalid or unreadable settings leave diagnostics OFF. Normal startup, warnings and errors remain available independently. See [OwnershipDiagnostics.h](src/OwnershipDiagnostics.h) and [OwnershipDiagnostics.cpp](src/OwnershipDiagnostics.cpp) for the fixed queue, suppression and session limits. Formatting and file writes occur outside the exception handler.

- `owner-acquired` identifies the observed acquisition reason and saved baseline. `owner=1` represents the shared claim, not proof that a particular wheel is still visible.
- `restore-deferred-menu` or `restore-deferred-effect` indicates that a restore request reached the handler. `pending=1` records a pending menu restore. `restore-completed` reports synchronized multipliers for an eligible observed transition.
- `restore-blocked` can be legitimate external-effect protection. Native/For Honor start, finish, redirect and fallback events explain effect deferral; unknown fields are printed as `?` or `nan`.
- Event timestamps are capture times, not disk-flush times. Repeated decisions can be suppressed, and records can be dropped or capped. An absent record alone does not prove no write occurred. Compare the wheel's own restore/skip log, the event loss notices and subsequent state.

Use a disposable save. Test each wheel alone, then sequential use, both wheel/menu close orders, cancellation and repeated cycles. Include native Slow Time and selected For Honor effects starting and ending before, during and after a wheel. Test overlap separately rather than assuming sequential success covers it. Preserve matching wheel, Skyrim Souls and fix logs with DLL hashes before another launch overwrites them. Runtime names alone do not establish binary identity.

## Public implementation references

These pinned sources illustrate different implementations, not interchangeable integration contracts:

- [Original Wheeler open, fade-close and close paths](https://github.com/D7ry/wheeler/blob/5124a10a24a841886fa3a16510764325f37c69f8/src/bin/Wheeler/Wheeler.cpp): `OpenWheeler()` checks the reported multiplier before applying slowing; `TryCloseWheeler()` and `CloseWheeler()` can request normal speed.
- [Wheeler Refined time helpers](https://github.com/c0kadam/Wheeler-Refined/blob/ee3e51a13671792d4fc1525c044c54d2a7d500f7/src/bin/Utilities/Utils.cpp): `Time::SGTM()` updates timer bookkeeping and both multipliers. Review your own helper and write order rather than assuming target-only behavior.
- [Skyrim Souls menu slow-motion handler](https://github.com/Vermunds/SkyrimSoulsRE/blob/efcea27d9d9da9ad1f0b79f823f1a93e846672d2/src/SlowMotionHandler.cpp): menu entry multiplies the current value and copies it to the other multiplier; exit divides by the stored menu factor. A menu exit can therefore preserve an existing wheel baseline instead of returning to 1.0.
