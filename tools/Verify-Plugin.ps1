param([Parameter(Mandatory=$true)][string]$Dll,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$b=[IO.File]::ReadAllBytes($Dll)
function u16([int]$p){[BitConverter]::ToUInt16($b,$p)}
function u32([int]$p){[BitConverter]::ToUInt32($b,$p)}
function zstr([int]$p){$end=$p;while($b[$end] -ne 0){$end++};[Text.Encoding]::ASCII.GetString($b,$p,$end-$p)}
function unpack([uint32]$v){'{0}.{1}.{2}.{3}' -f (($v -shr 24)-band 255),(($v -shr 16)-band 255),(($v -shr 4)-band 4095),($v -band 15)}
$pe=u32 0x3c
if((u32 $pe) -ne 0x4550){throw 'Invalid PE signature'}
$machine=u16 ($pe+4);$num=u16 ($pe+6);$optional=$pe+24
if($machine -ne 0x8664 -or (u16 $optional) -ne 0x20b -or ((u16 ($pe+22)) -band 0x2000) -eq 0){throw 'Not an x64 PE32+ DLL'}
$sectionStart=$optional+(u16 ($pe+20))
$sections=@(for($i=0;$i -lt $num;$i++){$s=$sectionStart+40*$i;[pscustomobject]@{VA=(u32 ($s+12));Size=[Math]::Max((u32 ($s+8)),(u32 ($s+16)));Raw=(u32 ($s+20))}})
function offset([uint32]$rva){foreach($s in $sections){if($rva -ge $s.VA -and $rva -lt ($s.VA+$s.Size)){return [int]($s.Raw+$rva-$s.VA)}};throw "Unmapped RVA $rva"}
$exp=offset (u32 ($optional+112));$count=u32 ($exp+24)
$funcs=offset (u32 ($exp+28));$names=offset (u32 ($exp+32));$ords=offset (u32 ($exp+36));$exports=@{}
for($i=0;$i -lt $count;$i++){$name=zstr (offset (u32 ($names+4*$i)));$ord=u16 ($ords+2*$i);$exports[$name]=u32 ($funcs+4*$ord)}
if(-not $exports.ContainsKey('SKSEPlugin_Query') -or -not $exports.ContainsKey('SKSEPlugin_Load') -or -not $exports.ContainsKey('SKSEPlugin_Version')){throw 'Required exports absent'}
$v=offset $exports['SKSEPlugin_Version'];$compat=@(for($i=0;$i -lt 16;$i++){$n=u32 ($v+0x30c+4*$i);if($n){unpack $n}})
$r=[ordered]@{Architecture='x64 PE32+ DLL';Exports=$exports;DataVersion=(u32 $v);PluginVersion=(unpack (u32 ($v+4)));PluginName=(zstr ($v+8));Author=(zstr ($v+0x108));NoStructUse=(($b[$v+0x304]-band 1)-ne 0);AddressLibraryV5=(($b[$v+0x304]-band 2)-ne 0);UsesAddressLibrary=(($b[$v+0x308]-band 1)-ne 0);UsesSignatureScanning=(($b[$v+0x308]-band 2)-ne 0);StructsPost629=(($b[$v+0x308]-band 4)-ne 0);CompatibleVersions=$compat;MinimumSKSEVersion=(unpack (u32 ($v+0x34c)));FileVersion=[Diagnostics.FileVersionInfo]::GetVersionInfo($Dll).FileVersion;SizeBytes=$b.Length;SHA256=(Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash}
if($r.PluginVersion -ne '1.3.0.0' -or $r.FileVersion -ne '1.3.0.0' -or $r.PluginName -ne 'WheelerSlowTimeFix' -or $r.Author -ne 'c0kadam' -or -not $r.NoStructUse -or -not $r.AddressLibraryV5 -or -not $r.UsesAddressLibrary -or ($compat -join '|') -ne '1.5.97.0|1.6.1170.0|1.7.104.0'){throw 'Unexpected SKSE metadata'}
$r | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Output
Get-Content -LiteralPath $Output
