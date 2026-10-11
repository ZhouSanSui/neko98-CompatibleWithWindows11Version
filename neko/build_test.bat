@echo off
rem Build the CTransparentPet render unit test.
rem It links the REAL production objects (TransparentPet.o, Pet.o) plus the
rem real icon resources (Neko95.o) so the test exercises shipped code/assets.
setlocal
set MINGW=C:\Users\zhousansui\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%
set PATH=%MINGW%;%PATH%
pushd %ROOT%

rem Always recompile the production objects. These used to be guarded by
rem "if not exist", which silently linked a STALE TransparentPet.o after editing
rem the production source and made the test pass against old code.
%MINGW%\windres.exe -c 936 -O coff -o Neko95.o Neko95.rc
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c TransparentPet.cpp -o TransparentPet.o
if errorlevel 1 goto :fail
%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c Pet.cpp -o Pet.o
if errorlevel 1 goto :fail

%MINGW%\g++.exe -O2 -DWIN32 -D_WIN32 -Wno-write-strings -finput-charset=UTF-8 -fexec-charset=GBK -c test_render.cpp -o test_render.o
if errorlevel 1 goto :fail

rem console subsystem so we can read the results
%MINGW%\g++.exe -static -static-libgcc -static-libstdc++ -O2 -o test_render.exe test_render.o TransparentPet.o Pet.o Neko95.o -lcomctl32 -luser32 -lgdi32 -lshell32 -lwinmm -lgdiplus
if errorlevel 1 goto :fail

echo TEST_BUILD_OK
goto :eof

:fail
echo TEST_BUILD_FAILED
exit /b 1
