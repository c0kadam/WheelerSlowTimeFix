# Build instructions

Required: Visual Studio 2022 with MSVC x64 and a Windows SDK, CMake >=3.22 and C++20 support. The verified compiler is MSVC 19.44.35215.0 with SDK 10.0.26100.0 and CMake 4.2.0.

Use an isolated CommonLibSSE-NG checkout at **c4672d5d70183f151e1dc470e73a747b78511304**, with its real OpenVR gitlink initialized at **ebdea152f8aac77e9a6db29682b81d762159df7e**. Do not upgrade dependencies or copy arbitrary headers. The wrapper verifies both commits and rejects tracked modifications.

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

Choose an absent external build directory. The wrapper unsets `CompiledPluginsPath`; an empty-but-defined CMake variable is unsafe because the retained CMake checks whether it is defined. The wrapper inspects the fresh cache and generated plugin project before compiling, and builds only the DLL target. Do not invoke a legacy configuration with a deployment variable set. No install target or game launch is used.

## Release artifact privacy

For v1.1.0-rc.1, the numeric SKSE/file version is 1.1.0.0 and the generated release label is 1.1.0-rc.1. The Release linker embeds only `WheelerSlowTimeFix.pdb`, not its local directory.

Source-location strings can still contain source/dependency paths. Prepare public binaries from a neutral layout, for example `W:/source`, `W:/deps/CommonLibSSE` and `W:/build-release`, with the same pinned dependencies. The workspace can be exposed through a temporary Windows drive mapping. Verify source hashes before building, then inspect ASCII/UTF-16 strings and the PE debug record for personal directories, credentials and internal paths. Never attach a private PDB or an unchecked binary.

PDBs and compiled binaries can embed absolute source/build paths. They are not included in this source staging repository. Review generated artifacts and applicable dependency notices separately before any future binary distribution. Byte-identical rebuilds and gameplay equivalence are not claimed.
