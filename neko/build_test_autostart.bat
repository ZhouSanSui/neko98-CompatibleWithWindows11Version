@echo off
rem Build the auto-start runtime test. Links the REAL AutoStart.o so the test
rem exercises shipped code against the actual HKCU Run key.
setlocal
set MINGW=C:\Users\zhousansui\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
set PATH=%MINGW%;%PATH%
pushd %ROOT%

rem NOTE: AutoStart.o is rebuilt unconditionally on purpose. An earlier version
rem guarded it with "if not exist", and after editing AutoStart.cpp the test
rem silently relinked a STALE object and reported nonsense.

%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c AutoStart.cpp -o AutoStart.o
if errorlevel 1 goto :fail

%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c test_autostart.cpp -o test_autostart.o
if errorlevel 1 goto :fail

%MINGW%\g++.exe -static -static-libgcc -static-libstdc++ -O2 -o test_autostart.exe test_autostart.o AutoStart.o -ladvapi32 -luser32
if errorlevel 1 goto :fail

echo AUTOSTART_TEST_BUILD_OK
goto :eof

:fail
echo AUTOSTART_TEST_BUILD_FAILED
exit /b 1
