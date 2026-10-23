# Wheeler Slow Time Fix

SKSE compatibility plugin for Slow Time interactions between Wheeler, Skyrim Souls RE and selected SlowTimeEffect spells. Original plugin author attribution: **c0kadam**. Existing source attribution is preserved.

## Release candidate

**v1.1.0-rc.1 is a pre-release, not a stable release.** It contains the verified Optimization 02 runtime source. The numeric SKSE/file version remains **1.1.0.0**; the release label identifies this candidate.

Optimization 02 replaces recurring systemwide thread discovery with process-specific Windows Process Snapshotting (PSS), with Toolhelp fallback and retained thread handles. Continuous discovery, the existing 500 ms post-scan sleep, watchpoint coverage and correction logic are preserved. Earlier author-run measurements showed lower discovery-worker CPU cost; this is not a promise of higher FPS. Verbose logging defaults OFF.

| Skyrim runtime | Status |
|---|---|
| AE 1.6.1170 | Author-tested gameplay environment for the earlier Optimization 02 candidate |
| SE 1.5.97 | Declared by plugin metadata; gameplay not verified here |
| 1.7.x | Not supported or claimed by this release |

New release builds are verified by source comparison, compilation and DLL inspection. They are **not gameplay-verified merely by passing those checks**.

## Known limitations

The intermittent ownership-acquisition edge involving current timescale approximately **0.980** remains unresolved. Infinite Slow Time can still occur during aggressive menu transitions. This candidate does not change ownership/restoration thresholds or force timescale to 1.0. Legitimate external slowing and spell compatibility still need regression testing. No universal frametime/FPS improvement is claimed.

## Requirements and installation

Use SKSE64 and Address Library for SKSE Plugins matching your Skyrim runtime, with Wheeler and Skyrim Souls RE for the interaction addressed by this plugin. Install the Microsoft Visual C++ x64 runtime required by SKSE plugins. These dependencies and Skyrim are not bundled.

Use only a binary explicitly attached to the corresponding release; GitHub's automatic source archives are not installable mods. For a binary package, import its ZIP into a dedicated Vortex test mod so `SKSE/Plugins/WheelerSlowTimeFix.dll` appears under the game Data directory. Preserve the original mod installation and enable only one copy of this DLL. Use a disposable save; check startup/loading, repeated Wheeler use, restoration and rapid Favorites/Inventory/Tween transitions. This project never installs or launches Skyrim automatically.

## Build

See [BUILDING.md](BUILDING.md) for the exact pinned CommonLib/OpenVR revisions, installed build dependencies and portable build commands. Supply dependency locations explicitly or via environment variables, and choose a new external build directory. Private validation harnesses, benchmarks and performance recordings are not included.

## History and license

This publication history starts with an independent source snapshot import, not the original development ancestry. Genuine earlier histories and contributor records remain preserved privately. The import does not backdate development, reconstruct missing ancestry or claim sole authorship for its importer.

**SPDX-License-Identifier: GPL-3.0-or-later**

Except where otherwise indicated, the project source, build tools and documentation are licensed under the GNU General Public License, version 3 or (at your option) any later version. See the unmodified official text in [LICENSE](LICENSE). The rights holder explicitly authorized this license for inherited project contributions. External dependencies retain their own licenses and attributions; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

No prebuilt DLL, PDB, ETL, log or private report is tracked in Git.
