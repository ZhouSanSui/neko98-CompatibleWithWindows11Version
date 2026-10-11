# Insert a Chinese block from a UTF-8 side file into the shipped readme.
#
# This script is deliberately pure ASCII, for the same reason package.ps1 is:
# Windows PowerShell 5.1 decodes a BOM-less .ps1 using the machine's ANSI
# codepage, so Chinese literals (and Chinese PATH text) would be corrupted.
# The readme is therefore located by extension, not by name, and the Chinese
# content is read from a UTF-8 side file.
#
# Anchor "【顺带修复的其他问题】" and marker "劫持" are built from codepoints.

$readme = Get-ChildItem -Path (Join-Path $PSScriptRoot 'dist') -Filter '*.txt' | Select-Object -First 1
if (-not $readme) { Write-Host 'README_MISSING'; exit 1 }

$blockF = Join-Path $PSScriptRoot 'packaging\readme_autostart_block.txt'
if (-not (Test-Path $blockF)) { Write-Host 'BLOCK_MISSING'; exit 1 }

# "【顺带修复的其他问题】"
$anchor = [string][char]0x3010 + [char]0x987A + [char]0x5E26 + [char]0x4FEE +
          [char]0x590D + [char]0x7684 + [char]0x5176 + [char]0x4ED6 +
          [char]0x95EE + [char]0x9898 + [char]0x3011 + "`n`n"

$t = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($readme.FullName))
if ($t.Length -gt 0 -and $t[0] -eq [char]0xFEFF) { $t = $t.Substring(1) }

if (-not $t.Contains($anchor)) { Write-Host 'ANCHOR_MISS'; exit 1 }

$block = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($blockF))
$block = $block.TrimEnd("`r", "`n")

$t = $t.Replace($anchor, $anchor + $block + "`n")
[System.IO.File]::WriteAllText($readme.FullName, $t, (New-Object System.Text.UTF8Encoding($true)))

# "劫持"
$marker = [string][char]0x52AB + [char]0x6301
Write-Host ("readme bytes=" + (Get-Item $readme.FullName).Length)
if ($t.Contains($marker)) { 'README_PATCH_OK' } else { 'README_PATCH_FAILED'; exit 1 }
