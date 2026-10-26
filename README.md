# Wheeler Slow Time Fix 1.3.0

An SKSE plugin that addresses Slow Time conflicts between Wheeler-compatible wheels and Skyrim Souls RE, with compatibility for selected For Honor Slow Time effects.

**Supported runtimes:** Skyrim SE 1.5.97 and Skyrim AE 1.6.1170 and 1.7.104.

**Requirements:** SKSE and Address Library for your Skyrim runtime, plus compatible wheel and Skyrim Souls RE builds.

Wheeler and Skyrim Souls RE are recognized by default. To enable additional Wheeler-compatible modules, create `Data/SKSE/Plugins/WheelerSlowTimeFix.ini`:

```ini
[Compatibility]
AdditionalDLLs=GlassworkFavorites.dll,p-radials.dll
```

Use comma-separated DLL basenames. Names are case-insensitive, surrounding whitespace is trimmed, and duplicates are ignored. Paths and wildcards are invalid. Up to 64 additional names are supported. Restart the game after changing the INI. A missing INI or empty list keeps only built-in compatibility; `favwheel.dll` is not recognized automatically.

Additional DLLs must use Wheeler-compatible time-scale behavior. Adding an arbitrary DLL does not guarantee compatibility or load that DLL.

**Multiple Wheeler Variants — Limited Support**

Multiple Wheeler variants can be used in the same game session, and normal sequential use generally works as expected. However, overlapping wheel usage or extremely rapid switching may cause conflicting slow-time and restoration operations. This is a known limitation, and using one wheel at a time is recommended.

**Known issue:** Slow Time may occasionally remain active after rapid menu transitions.

GitHub distributes source code only through release source archives. Compiled releases are distributed separately on Nexus Mods.

[Build Instructions](BUILDING.md) · [GPL-3.0-or-later License](LICENSE)
