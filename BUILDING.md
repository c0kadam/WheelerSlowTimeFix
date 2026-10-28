# Build instructions

Required: Visual Studio 2022 with MSVC x64 and a Windows SDK, CMake >=3.22 and C++23 support. The verified compiler is MSVC 19.44.35215.0 with SDK 10.0.26100.0 and CMake 4.2.0.

Use an isolated CommonLibSSE-NG **v13.1.0** checkout at **12d61f3c39f7d4ba1b1ab543df2bca0e613dc747**, with its OpenVR submodule initialized at **60eb187801956ad277f1cae6680e3a410ee0873b**. Do not upgrade dependencies or copy arbitrary headers. The wrapper verifies both commits and rejects tracked modifications.

CommonLib's enabled patch safety implementation compiles the hde64 instruction decoder from **MinHook v1.3.4**, commit **c3fcafdc10146beb5919319d0683e44e3c30d537**. CMake FetchContent fetches that tag from the MinHook repository during configuration, so Git access to the upstream source is required unless a verified local FetchContent source is supplied. This uses the decoder, not the complete MinHook hooking library. Do not disable patch safety to avoid resolving this dependency.

The existing configuration requires installed spdlog (and bundled fmt), rapidcsv headers, DirectXTK and DirectXMath through the established vcpkg/CMake package arrangement. These dependencies are external; none is vendored here. The original manifest mode remains OFF and the target triplet is x64-windows. DirectX package prefixes default relative to the supplied vcpkg root; use `-PrefixPath` for an equivalent installation layout.

Set the following variables to your own installed locations, then run from this repository:

```powershell
# Set these values in your shell before invoking the build:
# $CommonLibPath, $VcpkgRoot, $SpdlogConfigDirectory, $RapidCsvHeaders, $BuildDirectory
.\tools\Build-Release.ps1 -NewBuildDirectory $BuildDirectory `
    -CommonLibPath $CommonLibPath -VcpkgRoot $VcpkgRoot `
    -SpdlogDir $SpdlogConfigDirectory -RapidCsvIncludeDir $RapidCsvHeaders

$Dll = Join-Path $BuildDirectory 'Release/WheelerSlowTimeFix.dll'
.\tools\Verify-Plugin.ps1 -Dll $Dll -Output (Join-Path $BuildDirectory 'verified-metadata.json')
Get-FileHash -LiteralPath $Dll -Algorithm SHA256
```

Alternatively use environment variables `CommonLibSSEPath_NG`, `VCPKG_ROOT`, `SPDLOG_DIR`, `RAPIDCSV_INCLUDE_DIRS` and optional `CMAKE_PREFIX_PATH`. If SDK resource compiler discovery requires an override, pass `-ResourceCompiler` with your installed `rc.exe` location. No private machine-specific default paths are required or stored in the source.

Choose an absent external build directory. The wrapper configures x64 Release with verbose logging disabled, inspects the fresh cache and generated plugin project, and builds only the DLL target. Standard builds include INI-controlled bounded diagnostics by default; runtime diagnostics remain OFF unless `[Diagnostics] Enabled=true` is set in the INI before startup. No additional compiler flag or source patch is required. Build outputs remain in the external build directory; neither the DLL nor the PDB is automatically deployed. No install target or game launch is used.

For a local v1.3.0 test mod, copy the verified DLL to `SKSE/Plugins/WheelerSlowTimeFix.dll` and place the example `WheelerSlowTimeFix.ini` from the [README](README.md) beside it. The INI can enable Glasswork Favorites without changing the built-in Wheeler and Skyrim Souls handling. See the README for the diagnostic toggle and the known p-radials restoration limitation. Keep PDBs and build outputs outside the test mod.

## Release artifact privacy

For v1.3.0, the numeric SKSE/file version is 1.3.0.0 and the generated release label is 1.3.0. The Release linker embeds only `WheelerSlowTimeFix.pdb`, not its local directory.

Source-location strings can still contain source/dependency paths. Build the plugin and statically linked dependencies from a neutral layout, for example `W:/source`, `W:/deps/CommonLibSSE` and `W:/build-release`, with the same pinned dependencies. The workspace can be exposed through a temporary Windows drive mapping. Reusing a library built from private paths can retain those paths in the final DLL. Verify source hashes before building, then inspect ASCII/UTF-16 strings and the PE debug record for personal directories, credentials and internal paths. Do not distribute private PDBs.

GitHub releases distribute source archives only. Do not commit or upload DLLs, PDBs, build directories, logs, captures or private verification artifacts to the GitHub repository or its releases.

Before Nexus Mods distribution, independently verify the exact Release DLL's architecture, SKSE exports, version/runtime declarations, SHA256 and binary privacy, and validate it in the supported game environments. An earlier candidate's verification does not certify a new build. Include the applicable licenses and notices and provide the corresponding-source access described in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Byte-identical rebuilds and gameplay equivalence are not claimed.
