# Generic literal text patch for the shipped readme.
#
# ASCII-only for the same reason package.ps1 is (PowerShell 5.1 decodes a
# BOM-less .ps1 as ANSI). Both the search text and the replacement come from
# UTF-8 side files; the readme is located by extension because its name is
# Chinese and must not appear as a literal here.
#
#   usage:  powershell -NoProfile -ExecutionPolicy Bypass -File replace_readme.ps1 <oldFile> <newFile> [expectedCount]
$oldF = $args[0]
$newF = $args[1]
$want = if ($args.Count -ge 3) { [int]$args[2] } else { 1 }

function Read-Utf8([string]$path) {
    $s = [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($path))
    if ($s.Length -gt 0 -and $s[0] -eq [char]0xFEFF) { $s = $s.Substring(1) }
    return $s
}

$readme = Get-ChildItem -Path (Join-Path $PSScriptRoot 'dist') -Filter '*.txt' | Select-Object -First 1
if (-not $readme) { Write-Host 'README_MISSING'; exit 1 }

$t   = Read-Utf8 $readme.FullName
$old = (Read-Utf8 $oldF).TrimEnd("`r", "`n")
$new = (Read-Utf8 $newF).TrimEnd("`r", "`n")

if (-not $t.Contains($old)) {
    Write-Host 'OLD_NOT_FOUND'
    exit 1
}

$count = ([regex]::Matches($t, [regex]::Escape($old))).Count
Write-Host ("occurrences=" + $count + " expected=" + $want)
if ($count -ne $want) { Write-Host 'COUNT_MISMATCH'; exit 1 }

$t = $t.Replace($old, $new)
[System.IO.File]::WriteAllText($readme.FullName, $t, (New-Object System.Text.UTF8Encoding($true)))
Write-Host ("readme bytes=" + (Get-Item $readme.FullName).Length)
Write-Host 'README_REPLACE_OK'
