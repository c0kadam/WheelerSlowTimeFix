# Wheeler Slow Time Fix 1.3.0

An SKSE plugin that addresses Slow Time conflicts between Wheeler and Skyrim Souls RE, with compatibility for selected For Honor Slow Time effects.

<p align="center">
  <a href="https://www.nexusmods.com/skyrimspecialedition/mods/174828"><img alt="Nexus Mods" src="https://img.shields.io/badge/Nexus%20Mods-Download-DA8E35?logo=nexusmods&logoColor=white"></a>
</p>

**Supported runtimes:** Skyrim SE 1.5.97 and Skyrim AE 1.6.1170 and 1.7.104.

**Requirements:** SKSE and Address Library for your Skyrim runtime, plus compatible wheel and Skyrim Souls RE builds.

Wheeler and Skyrim Souls RE are recognized by default. To add other Wheeler-compatible plugins, create `Data/SKSE/Plugins/WheelerSlowTimeFix.ini`:

```ini
[Compatibility]
AdditionalDLLs=GlassworkFavorites.dll

[Diagnostics]
Enabled=false
```

Use comma-separated DLL basenames. Names are case-insensitive, surrounding whitespace is trimmed, and duplicates are ignored. Paths and wildcards are invalid. Up to 64 additional names are supported. Restart the game after changing the INI. A missing INI or empty list keeps only built-in compatibility; `favwheel.dll` is not recognized automatically.

Listed DLLs are handled by the existing Wheeler correction logic and must use compatible time-scale behavior. Adding an arbitrary plugin to the list does not load it or guarantee compatibility.

**Diagnostics**

Set `Enabled=true` under `[Diagnostics]` to record bounded ownership and restoration diagnostics in `WheelerSlowTimeFix.log`. Diagnostics are off by default, including when the file or setting is missing, invalid or unreadable. The setting is read once at startup, so restart Skyrim after changing it. Normal startup, warning and error messages remain available with diagnostics off.

**Slow Time compatibility**

Wheel ownership can be recognized from a previously observed normal target even when the current multiplier is slightly below normal speed, including the 0.980 case.

The general restore safeguard prevents a wheel from cancelling an external slowdown when it requests normal speed without owning the slowdown. This can protect the vanilla Slow Time shout and slowdowns from other mods, without tracking their FormIDs, but compatibility with every implementation is not guaranteed.

For Honor handling is separate: selected spells are recognized by plugin filename and local FormID, then tracked through `SlowTimeEffect` start and finish. Wheel restoration is deferred while a tracked effect is active. When it finishes, a matching restore write is redirected to the saved wheel baseline. If that redirect was not applied, a guarded fallback restores both multipliers only when either still matches the effect's starting baseline.

**Multiple Wheeler variants**

Normal sequential use of multiple Wheeler variants in the same session generally works. Overlapping wheels or rapid switching can cause conflicting slow-time restoration operations, so use one wheel at a time.

**p-radials limitation**

p-radials may skip its own restoration when Skyrim Souls changes the multiplier while the wheel is active. This has been observed on Skyrim 1.6.1170 and 1.7.104 and can leave Slow Time active. WheelerSlowTimeFix does not correct this case.

GitHub distributes source code only through release source archives. Compiled releases are distributed separately on Nexus Mods.

[Build Instructions](BUILDING.md) · [GPL-3.0-or-later License](LICENSE)
