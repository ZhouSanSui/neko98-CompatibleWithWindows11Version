@echo off
rem Build the movement / mixed-DPI seam regression test.
rem It links the REAL production objects (Neko.o, TransparentPet.o, Pet.o) and the
rem real icons (Neko95.o) so the test drives shipped code, not a copy.
setlocal
set MINGW=C:\Users\zhousansui\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
set PATH=%MINGW%;%PATH%
pushd %ROOT%

if not exist "%MINGW%\g++.exe" (
  echo MINGW_NOT_FOUND
  exit /b 1
)

rem Rebuild the production objects UNCONDITIONALLY. Guarding these with
rem "if not exist" once silently linked a stale object and produced a bogus pass.
%MINGW%\windres.exe -c 936 -O coff -o Neko95.o Neko95.rc
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c TransparentPet.cpp -o TransparentPet.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c Pet.cpp -o Pet.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c NekoSettings.cpp -o NekoSettings.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c Neko.cpp -o Neko.o
if errorlevel 1 goto :fail

%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c test_movement.cpp -o test_movement.o
if errorlevel 1 goto :fail

rem console subsystem so we can read the results
%MINGW%\g++.exe -static -static-libgcc -static-libstdc++ -O2 -o test_movement.exe test_movement.o Neko.o TransparentPet.o Pet.o NekoSettings.o Neko95.o -lcomctl32 -luser32 -lgdi32 -lshell32 -lwinmm -lgdiplus -ladvapi32
if errorlevel 1 goto :fail

echo MOVEMENT_TEST_BUILD_OK
goto :eof

:fail
echo MOVEMENT_TEST_BUILD_FAILED
exit /b 1
