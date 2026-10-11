@echo off
rem ASCII-only wrapper: cmd mangles non-ASCII script text under codepage 936,
rem so all Chinese strings live in package.ps1 (read as UTF-8 by PowerShell).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package.ps1" %*
if errorlevel 1 exit /b 1
