@echo off
setlocal
set MINGW=C:\Users\zhousansui\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
set PATH=%MINGW%;%PATH%

pushd "%ROOT%"

rem Always rebuild ProxyToggle.o: a stale object silently linked against older
rem sources is the trap that already produced bogus test results once.
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c ProxyToggle.cpp -o ProxyToggle.o
if errorlevel 1 goto :fail

%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c test_proxy.cpp -o test_proxy.o
if errorlevel 1 goto :fail

%MINGW%\g++.exe -static -static-libgcc -static-libstdc++ -O2 -o test_proxy.exe test_proxy.o ProxyToggle.o -lwininet -ladvapi32 -luser32
if errorlevel 1 goto :fail

echo PROXY_TEST_BUILD_OK
goto :eof

:fail
echo PROXY_TEST_BUILD_FAILED
exit /b 1