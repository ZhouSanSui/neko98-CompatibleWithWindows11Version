@echo off
rem ============================================================
rem  Build NekoCFG.exe   (the Chinese settings program)
rem
rem  Portable: uses %~dp0, so this folder can live anywhere.
rem  If MinGW is installed elsewhere, edit MINGW= below.
rem
rem  usage:  build_cfg.bat
rem
rem  NOTE: this file is deliberately ASCII-only with CRLF line
rem  endings. cmd mis-parses LF-only parenthesised blocks, and a
rem  non-ASCII byte here gets mangled before gcc ever sees it.
rem ============================================================
setlocal

set MINGW=C:\Users\zhousansui\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
set CFG=%~dp0
if "%CFG:~-1%"=="\" set CFG=%CFG:~0,-1%
set ROOT=%CFG%\..
set PATH=%MINGW%;%PATH%

if not exist "%MINGW%\g++.exe" goto :nomingw

pushd "%CFG%"

rem ---- generate a spec that REMOVES gcc's auto default-manifest ----
rem ld otherwise appends its own default-manifest.o next to ours; the two
rem conflict, the merge fails, and ALL manifests are silently dropped
rem (no theme, no HiDPI). -L. and -Wl,--no-default-manifest cannot override it.
%MINGW%\g++.exe -dumpspecs 2>nul | powershell -NoProfile -Command "$input | ForEach-Object { $_ -replace '%%\{!shared:%%:if-exists\(default-manifest\.o%%s\)\}','' }" > no_manifest.spec

rem ---- resources: GBK rc -> COFF; manifest -> COFF ----
%MINGW%\windres.exe -c 936 -O coff -o NekoCFG_resource.o NekoCFG.rc
if errorlevel 1 goto :fail
%MINGW%\windres.exe -O coff -o manifest_cfg.o manifest_cfg.rc
if errorlevel 1 goto :fail

rem ---- compile sources ----
rem NOTE: the .cpp/.h sources are UTF-8 (no BOM). -finput-charset=UTF-8 tells
rem gcc how to read them; -fexec-charset=GBK emits the literals as GBK so the
rem Chinese dialogs display correctly on a GBK(936) system. Do NOT drop these.
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c NekoCFG.cpp -o NekoCFG.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c NekoCFG_Tabs.cpp -o NekoCFG_Tabs.o
if errorlevel 1 goto :fail

rem CNekoSettings lives in the parent dir (shared with Neko98)
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c "%ROOT%\NekoSettings.cpp" -o NekoSettings.o
if errorlevel 1 goto :fail

rem ---- the "start with Windows" helper is shared with Neko98 too ----
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c "%ROOT%\AutoStart.cpp" -o AutoStart.o
if errorlevel 1 goto :fail

rem ---- the global hot key helpers are shared with Neko98 too ----
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c "%ROOT%\Hotkey.cpp" -o Hotkey.o
if errorlevel 1 goto :fail

rem ---- link: -specs removes gcc's default manifest; manifest_cfg.o is the only one ----
%MINGW%\g++.exe -specs=no_manifest.spec -static -static-libgcc -static-libstdc++ -O2 -mwindows -o NekoCFG.exe NekoCFG.o NekoCFG_Tabs.o NekoSettings.o AutoStart.o Hotkey.o NekoCFG_resource.o manifest_cfg.o -lcomctl32 -luser32 -lgdi32 -lshell32 -lwinmm -lgdiplus
if errorlevel 1 goto :fail

echo LINK_OK
popd
goto :eof

:nomingw
echo BUILD_FAILED - g++.exe not found at:
echo   %MINGW%
echo Edit MINGW= at the top of this file to point at your MinGW bin folder.
exit /b 1

:fail
echo BUILD_FAILED
popd
exit /b 1
