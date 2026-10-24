param(
    [Parameter(Mandatory)][string]$NewBuildDirectory,
    [string]$CommonLibPath=$env:CommonLibSSEPath_NG,
    [string]$VcpkgRoot=$env:VCPKG_ROOT,
    [string]$SpdlogDir=$env:SPDLOG_DIR,
    [string]$RapidCsvIncludeDir=$env:RAPIDCSV_INCLUDE_DIRS,
    [string]$PrefixPath=$env:CMAKE_PREFIX_PATH,
    [string]$ResourceCompiler
)
$ErrorActionPreference='Stop'
foreach($entry in @(@('CommonLibPath/CommonLibSSEPath_NG',$CommonLibPath),@('VcpkgRoot/VCPKG_ROOT',$VcpkgRoot))){
    if([string]::IsNullOrWhiteSpace($entry[1])){throw "Supply $($entry[0]) explicitly"}
}
$source=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$build=[IO.Path]::GetFullPath($NewBuildDirectory)
if(Test-Path -LiteralPath $build){throw 'Choose a completely new external build directory'}
if($build -eq $source -or $build.StartsWith($source+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Build outside the source tree'}
if((git -C $CommonLibPath rev-parse HEAD) -ne '12d61f3c39f7d4ba1b1ab543df2bca0e613dc747'){throw 'Unexpected CommonLib revision'}
if((git -C (Join-Path $CommonLibPath 'extern/openvr') rev-parse HEAD) -ne '60eb187801956ad277f1cae6680e3a410ee0873b'){throw 'Unexpected pinned OpenVR revision'}
if(git -C $CommonLibPath status --porcelain --untracked-files=no){throw 'CommonLib tracked sources are modified'}
if(git -C (Join-Path $CommonLibPath 'extern/openvr') status --porcelain --untracked-files=no){throw 'OpenVR tracked sources are modified'}
$env:CommonLibSSEPath_NG=$CommonLibPath
$env:VCPKG_ROOT=$VcpkgRoot
$env:SPDLOG_DIR=$SpdlogDir
$env:RAPIDCSV_INCLUDE_DIRS=$RapidCsvIncludeDir
Remove-Item Env:CompiledPluginsPath -ErrorAction SilentlyContinue
if([string]::IsNullOrWhiteSpace($PrefixPath)){
    $PrefixPath=(Join-Path $VcpkgRoot 'packages/directxtk_x64-windows-static-md')+';'+(Join-Path $VcpkgRoot 'packages/directxmath_x64-windows-static-md')
}
$PrefixPath=$PrefixPath.Replace('\','/')
$configure=@('--preset','vs2022-windows','-S',$source,'-B',$build,'-A','x64',"-DCMAKE_PREFIX_PATH=$PrefixPath",'-DVCPKG_APPLOCAL_DEPS=OFF','-DWSTF_VERBOSE_LOGGING=OFF','-DCOMMONLIB_PREBUILT=OFF','-DCOMMONLIB_PREBUILT_MULTICONFIG=OFF')
if($ResourceCompiler){$configure+='-DCMAKE_RC_COMPILER:FILEPATH='+$ResourceCompiler.Replace('\','/')}
& cmake @configure
if($LASTEXITCODE){throw 'Configuration failed'}
if(Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') -Pattern '^CompiledPluginsPath:'){throw 'Deployment variable must be absent, not empty'}
if(Select-String -LiteralPath (Join-Path $build 'WheelerSlowTimeFix.vcxproj') -Pattern 'PostBuildEvent|copy_if_different|SKSE[/\\]Plugins'){throw 'Deployment command detected'}
& cmake --build $build --config Release --target WheelerSlowTimeFix --parallel 1 -- /v:minimal /p:UseMultiToolTask=true /p:MultiProcMaxCount=1
if($LASTEXITCODE){throw 'Build failed'}
