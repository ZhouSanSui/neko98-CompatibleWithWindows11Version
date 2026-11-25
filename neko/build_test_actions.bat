@echo off
rem ============================================================
rem  Build build_test_actions.exe
rem
rem  Portable: uses %~dp0. The production objects it links are rebuilt
rem  EVERY time: an "if not exist" guard once silently linked a stale
rem  object and produced test results that had nothing to do with the
rem  source under test. Do not reintroduce that guard.
rem ============================================================
setlocal

set MINGW=C:\Users\zhousansui\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
set PATH=%MINGW%;%PATH%

if not exist "%MINGW%\g++.exe" goto :nomingw

pushd "%ROOT%"

%MINGW%\g++.exe __CFLAGS__ -c Actions.cpp -o Actions.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe __CFLAGS__ -c HotkeyList.cpp -o HotkeyList.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe __CFLAGS__ -c NekoSettings.cpp -o NekoSettings.o
if errorlevel 1 goto :fail

%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c build_test_actions.cpp -o build_test_actions.o
if errorlevel 1 goto :fail

%MINGW%\g++.exe -static -static-libgcc -static-libstdc++ -O2 -o build_test_actions.exe build_test_actions.o Actions.o HotkeyList.o NekoSettings.o -ladvapi32 -luser32 -lshell32
if errorlevel 1 goto :fail

echo ACTIONS_TEST_BUILD_OK
popd
goto :eof

:nomingw
echo BUILD_FAILED - g++.exe not found at:
echo   %MINGW%
exit /b 1

:fail
echo BUILD_FAILED
popd
exit /b 1
