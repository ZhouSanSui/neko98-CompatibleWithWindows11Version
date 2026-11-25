@echo off
rem ============================================================
rem  Build NekoCFG.exe (the settings program)
rem
rem  Portable: uses %~dp0. Run this from the NekoCFG\ folder, or from
rem  the parent folder with an explicit path. It generates its own
rem  no_manifest.spec, so it does not depend on build_pet.bat having
rem  been run first (it used to, silently).
rem
rem  usage:  cd NekoCFG  &&  build_cfg.bat
rem ============================================================
setlocal

set MINGW=C:\Users\zhousansui\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
set PATH=%MINGW%;%PATH%

if not exist "%MINGW%\g++.exe" goto :nomingw

pushd "%ROOT%"

rem ---- generate the spec that removes gcc's auto default-manifest ----
%MINGW%\g++.exe -dumpspecs 2>nul | powershell -NoProfile -Command "$input | ForEach-Object { $_ -replace '%%\{!shared:%%:if-exists\(default-manifest\.o%%s\)\}','' }" > no_manifest.spec

rem ---- resources ----
rem NekoCFG.rc / NekoCFG_resource.* are GBK, so windres needs -c 936.
rem manifest_cfg.rc is ASCII and only carries the application manifest.
%MINGW%\windres.exe -c 936 -O coff -o NekoCFG_resource.o NekoCFG.rc
if errorlevel 1 goto :fail
%MINGW%\windres.exe -O coff -o manifest_cfg.o manifest_cfg.rc
if errorlevel 1 goto :fail

rem ---- compile the settings program ----
rem The .cpp/.h files are UTF-8 without BOM, hence -finput-charset=UTF-8.
rem The two shared modules are compiled from the PARENT folder.
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c NekoCFG.cpp -o NekoCFG.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c NekoCFG_Tabs.cpp -o NekoCFG_Tabs.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c "%ROOT%\..\NekoSettings.cpp" -o NekoSettings.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c "%ROOT%\..\AutoStart.cpp" -o AutoStart.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c "%ROOT%\..\Hotkey.cpp" -o Hotkey.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c "%ROOT%\..\HotkeyList.cpp" -o HotkeyList.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c "%ROOT%\..\Actions.cpp" -o Actions.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c "%ROOT%\..\ProxyToggle.cpp" -o ProxyToggle.o
if errorlevel 1 goto :fail

rem ---- link ----
%MINGW%\g++.exe -specs=no_manifest.spec -static -static-libgcc -static-libstdc++ -O2 -mwindows -o NekoCFG.exe NekoCFG.o NekoCFG_Tabs.o NekoSettings.o AutoStart.o Hotkey.o HotkeyList.o Actions.o ProxyToggle.o NekoCFG_resource.o manifest_cfg.o -lcomctl32 -luser32 -lgdi32 -lshell32 -lwinmm -lgdiplus -lwininet
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
