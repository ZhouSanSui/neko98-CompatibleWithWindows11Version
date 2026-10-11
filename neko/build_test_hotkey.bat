@echo off
rem Build the global-hotkey runtime test. Links the REAL Hotkey.o and
rem NekoSettings.o so the test exercises shipped code against the real registry
rem key and the real RegisterHotKey API.
rem
rem NOTE: the production objects are rebuilt unconditionally on purpose. An
rem earlier version guarded them with "if not exist", and after editing the
rem sources the test silently relinked a STALE object and reported nonsense.
setlocal
set MINGW=C:\Users\zhousansui\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
set PATH=%MINGW%;%PATH%
pushd %ROOT%

%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c Hotkey.cpp -o Hotkey.o
if errorlevel 1 goto :fail

%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c NekoSettings.cpp -o NekoSettings.o
if errorlevel 1 goto :fail

%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c test_hotkey.cpp -o test_hotkey.o
if errorlevel 1 goto :fail

%MINGW%\g++.exe -static -static-libgcc -static-libstdc++ -O2 -o test_hotkey.exe test_hotkey.o Hotkey.o NekoSettings.o -ladvapi32 -luser32
if errorlevel 1 goto :fail

echo HOTKEY_TEST_BUILD_OK
goto :eof

:fail
echo HOTKEY_TEST_BUILD_FAILED
exit /b 1
