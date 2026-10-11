# ============================================================
#  Package Neko98 for Windows 11
#
#  Produces:
#    dist\Neko98.exe    directly-runnable pet (no installer needed)
#    dist\NekoCFG.exe   Chinese settings program
#    dist\<readme>.txt  readme (UTF-8 with BOM)
#    neko98-win11.zip   portable archive
#    neko98-win11.exe   7-Zip self-extracting installer
#
#  This script is deliberately pure ASCII. Windows PowerShell 5.1 decodes a
#  BOM-less .ps1 using the ANSI codepage, which would corrupt any Chinese
#  literal; the same applies to .bat files run by cmd. All Chinese text
#  therefore lives in separate UTF-8 files:
#     dist\<chinese-readme>.txt          (readme, shipped to the user)
#     packaging\sfx_config.template.txt  (installer prompt strings)
#  and is read with an explicit encoding.
#
#  usage:  package.bat        (wrapper; bypasses the script execution policy)
#     or:  pwsh -File package.ps1
# ============================================================

$ErrorActionPreference = 'Stop'

$Root   = $PSScriptRoot
$SevenZ = 'C:\Program Files\7-Zip\7z.exe'
$Sfx    = 'C:\Program Files\7-Zip\7z.sfx'
$Dist   = Join-Path $Root 'dist'

function Fail($msg) { Write-Host "FAILED: $msg" -ForegroundColor Red; exit 1 }

Write-Host '[1/6] checking inputs...'
$pet = Join-Path $Root 'Neko98.exe'
$cfg = Join-Path $Root 'NekoCFG\NekoCFG.exe'
foreach ($p in @($pet, $cfg)) { if (-not (Test-Path $p)) { Fail "missing $p" } }
if (-not (Test-Path $SevenZ)) { Fail "missing 7-Zip at $SevenZ" }
if (-not (Test-Path $Sfx))    { Fail "missing 7z.sfx at $Sfx" }

Write-Host '[2/6] refreshing dist ...'
if (-not (Test-Path $Dist)) { New-Item -ItemType Directory -Path $Dist | Out-Null }
Copy-Item $pet (Join-Path $Dist 'Neko98.exe')  -Force
Copy-Item $cfg (Join-Path $Dist 'NekoCFG.exe') -Force

# Find the readme by extension rather than by name: naming it literally here
# would require this script to contain non-ASCII text.
$readme = Get-ChildItem -Path $Dist -Filter '*.txt' | Select-Object -First 1
if ($readme) {
    # Re-write with a UTF-8 BOM so any editor detects the encoding even when
    # the machine's OEM codepage is not Chinese.
    $text = Get-Content -Raw -Encoding UTF8 $readme.FullName
    [System.IO.File]::WriteAllText($readme.FullName, $text,
        (New-Object System.Text.UTF8Encoding($true)))
}

Write-Host '[3/6] building portable zip ...'
$zip = Join-Path $Root 'neko98-win11.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
# Use Python's zipfile rather than `7z a -tzip`: 7-Zip stores the Chinese
# readme name as raw GBK bytes with the ZIP "UTF-8 name" flag (bit 0x800)
# CLEARED, so Windows Explorer and most non-CP936 extractors show mojibake.
# zipfile writes UTF-8 names and sets the flag, as the ZIP spec expects.
$zipPy = Join-Path $Root 'make_zip.py'
if (-not (Test-Path $zipPy)) { Fail "missing $zipPy" }
$py = (Get-Command python3 -ErrorAction SilentlyContinue)
if (-not $py) { $py = (Get-Command python -ErrorAction SilentlyContinue) }
if (-not $py) { Fail 'python3 is required to build the portable zip' }
& $py.Source $zipPy $Dist $zip
if ($LASTEXITCODE -ne 0) { Fail 'zip creation' }

Write-Host '[4/6] building self-extracting installer ...'
# 7-Zip SFX layout: 7z.sfx + config.txt + payload.7z
# The config MUST be UTF-8 *with BOM*: that is how 7-Zip decides the encoding,
# otherwise the Chinese Title / BeginPrompt prompt shows as garbage.
$tpl = Join-Path $Root 'packaging\sfx_config.template.txt'
if (-not (Test-Path $tpl)) { Fail "missing $tpl" }
$cfgTxt = Join-Path $Root 'sfx_config.txt'
$cfgText = Get-Content -Raw -Encoding UTF8 $tpl
[System.IO.File]::WriteAllText($cfgTxt, $cfgText,
    (New-Object System.Text.UTF8Encoding($true)))

$payload = Join-Path $Root 'payload.7z'
if (Test-Path $payload) { Remove-Item $payload -Force }
& $SevenZ a -t7z -mx=9 $payload (Join-Path $Dist '*') | Out-Null
if ($LASTEXITCODE -ne 0) { Fail 'payload creation' }

$outExe = Join-Path $Root 'neko98-win11.exe'
if (Test-Path $outExe) { Remove-Item $outExe -Force }
$bytes  = [System.IO.File]::ReadAllBytes($Sfx)
$bytes += [System.IO.File]::ReadAllBytes($cfgTxt)
$bytes += [System.IO.File]::ReadAllBytes($payload)
[System.IO.File]::WriteAllBytes($outExe, $bytes)

Write-Host '[5/6] verifying the installer extracts ...'
$tmp = Join-Path $env:TEMP 'neko_sfx_verify'
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
& $SevenZ x "-o$tmp" -y $outExe | Out-Null
if ($LASTEXITCODE -ne 0) { Fail 'installer does not extract' }
foreach ($n in @('Neko98.exe', 'NekoCFG.exe')) {
    if (-not (Test-Path (Join-Path $tmp $n))) { Fail "installer payload missing $n" }
}
$ex = Get-ChildItem -Path $tmp -Filter '*.txt'
if (-not $ex) { Fail 'installer payload missing the readme' }
# the readme must have survived as valid Chinese text
$probe = Get-Content -Raw -Encoding UTF8 $ex[0].FullName
if ($probe -notmatch '[\u4e00-\u9fff]') { Fail 'readme lost its Chinese text' }
Remove-Item $tmp -Recurse -Force

Remove-Item $payload, $cfgTxt -Force -ErrorAction SilentlyContinue

Write-Host '[6/6] results:'
foreach ($f in @((Join-Path $Dist 'Neko98.exe'),
                 (Join-Path $Dist 'NekoCFG.exe'),
                 $readme.FullName, $zip, $outExe)) {
    if (Test-Path $f) {
        $i = Get-Item $f
        Write-Host ('   {0,-22} {1,10} bytes' -f $i.Name, $i.Length)
    }
}
Write-Host 'PACKAGE_OK'
