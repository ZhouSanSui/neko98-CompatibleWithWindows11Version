@echo off
rem ============================================================
rem  Build Neko98.exe
rem
rem  Portable: uses %~dp0, so this folder can live anywhere.
rem  If MinGW is installed elsewhere, edit MINGW= below.
rem
rem  usage:  build_pet.bat
rem
rem  NOTE: this file is deliberately ASCII-only with CRLF line
rem  endings. cmd mis-parses LF-only parenthesised blocks, and a
rem  non-ASCII byte here gets mangled before gcc ever sees it.
rem ============================================================
setlocal

set MINGW=C:\Users\zhousansui\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
set PATH=%MINGW%;%PATH%

if not exist "%MINGW%\g++.exe" goto :nomingw

pushd "%ROOT%"

rem ---- generate a spec that REMOVES gcc's auto default-manifest ----
rem ld otherwise appends its own default-manifest.o next to ours; the two
rem conflict, the merge fails, and ALL manifests are silently dropped
rem (no theme, no HiDPI). -L. and -Wl,--no-default-manifest cannot override it.
%MINGW%\g++.exe -dumpspecs 2>nul | powershell -NoProfile -Command "$input | ForEach-Object { $_ -replace '%%\{!shared:%%:if-exists\(default-manifest\.o%%s\)\}','' }" > no_manifest.spec

rem ---- resources: COFF output (linkable). The .rc is GBK -> -c 936 ----
%MINGW%\windres.exe -c 936 -O coff -o Neko95.o Neko95.rc
if errorlevel 1 goto :fail
%MINGW%\windres.exe -O coff -o manifest_pet.o manifest_pet.rc
if errorlevel 1 goto :fail

rem ---- compile sources ----
rem NOTE: the .cpp/.h sources are UTF-8 (no BOM). -finput-charset=UTF-8 reads
rem them; -fexec-charset=GBK emits the literals as GBK so the Chinese menus
rem show correctly on a GBK(936) Windows. Do NOT drop these flags.
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c Neko98.cpp -o Neko98.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c Neko.cpp -o Neko.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c Pet.cpp -o Pet.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c TransparentPet.cpp -o TransparentPet.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c Tray.cpp -o Tray.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c NekoSettings.cpp -o NekoSettings.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c AutoStart.cpp -o AutoStart.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c Hotkey.cpp -o Hotkey.o
if errorlevel 1 goto :fail

rem ---- link: -specs removes gcc's default manifest; manifest_pet.o is the only one ----
%MINGW%\g++.exe -specs=no_manifest.spec -static -static-libgcc -static-libstdc++ -O2 -mwindows -o Neko98.exe Neko98.o Neko.o Pet.o TransparentPet.o Tray.o NekoSettings.o AutoStart.o Hotkey.o Neko95.o manifest_pet.o -lcomctl32 -luser32 -lgdi32 -lshell32 -lwinmm -lgdiplus -lwtsapi32
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
