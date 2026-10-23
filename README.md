# Wheeler Slow Time Fix

An SKSE plugin for the slow-time interaction between Wheeler, Skyrim Souls RE and selected SlowTimeEffect spells. Plugin author attribution: **c0kadam**. Current plugin metadata: **1.1.0.0**.

This source distribution contains the verified **Optimization 02 development source**, not an approved release. Verbose runtime diagnostics default OFF. Thread discovery uses process-specific Windows Process Snapshotting with Toolhelp fallback and retained thread handles; continuous 500 ms post-scan sleep and existing correction behavior are preserved.

## Status

Real Skyrim measurements show substantially lower discovery-worker CPU cost than the original systemwide enumeration implementation. This does not establish a universal FPS improvement. The author reported a successful casual-gameplay/menu-transition smoke test; compilation does not prove restoration correctness.

An intermittent ownership-acquisition edge involving current timescale approximately 0.980 remains unresolved. Legitimate external slowing, repeated restoration, late-thread coverage and longer-session resource behavior still require functional regression testing. Skyrim AE 1.6.1170 is included in the existing compatibility declarations.

## Build

Use MSVC x64 / Visual Studio 2022, CMake and the exact pinned CommonLib/OpenVR dependencies described in [BUILDING.md](BUILDING.md). Supply dependency locations explicitly or through environment variables. Builds must use a new external directory. The wrapper rejects deployment commands and explicitly disables verbose logging.

## Publication history and attribution

This is a fresh source-distribution import prepared for publication, not the original development history. Earlier source checkpoints, contributor records and genuine archived Git histories are retained privately. This import does not reconstruct missing ancestry, backdate commits or assert that all development occurred at the publication-import date. Existing source attribution is retained; a new import commit identifies its importer, not every prior contributor.

## License status

No project license has yet been selected. Publication remains blocked pending the rights holder's licensing decision. Dependency licenses do not license the plugin itself. See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for dependency attribution.

No prebuilt DLL, PDB, runtime log, performance trace or private provenance record is included. No automatic Skyrim deployment is part of this source distribution.
