# Wheeler Slow Time Fix 1.3.0

An SKSE plugin that addresses Slow Time conflicts between Wheeler and Skyrim Souls RE, with compatibility for selected For Honor Slow Time effects.

**Supported runtimes:** Skyrim SE 1.5.97 and Skyrim AE 1.6.1170 and 1.7.104.

**Requirements:** SKSE and Address Library for your Skyrim runtime, plus compatible wheel and Skyrim Souls RE builds.

Wheeler and Skyrim Souls RE are recognized by default. To add other Wheeler-compatible plugins, create `Data/SKSE/Plugins/WheelerSlowTimeFix.ini`:

```ini
[Compatibility]
AdditionalDLLs=GlassworkFavorites.dll,p-radials.dll
```

Use comma-separated DLL basenames. Names are case-insensitive, surrounding whitespace is trimmed, and duplicates are ignored. Paths and wildcards are invalid. Up to 64 additional names are supported. Restart the game after changing the INI. A missing INI or empty list keeps only built-in compatibility; `favwheel.dll` is not recognized automatically.

Listed DLLs are handled by the existing Wheeler correction logic and must use compatible time-scale behavior. Adding an arbitrary plugin to the list does not load it or guarantee compatibility.

**Multiple Wheeler Variants — Limited Support**

Normal sequential use of multiple Wheeler variants in the same session generally works. Overlapping wheels or rapid switching can cause conflicting slow-time restoration operations, so use one wheel at a time.

**Known issue:** Slow Time may occasionally remain active after rapid menu transitions.

GitHub distributes source code only through release source archives. Compiled releases are distributed separately on Nexus Mods.

[Build Instructions](BUILDING.md) · [GPL-3.0-or-later License](LICENSE)
