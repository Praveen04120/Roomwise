@echo off
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

echo ============================================================
echo   Building Roomwise - Empty Classroom Finder
echo ============================================================

if not exist "%VCVARS%" goto check_mingw

echo [Build] Initializing Visual Studio BuildTools...
call "%VCVARS%" >nul

echo [Build] Compiling Roomwise with MSVC cl.exe (C++17)...
cl /nologo /std:c++17 /EHsc /O2 /Iinclude /Ibackend backend\main.cpp backend\storage.cpp backend\scheduler.cpp backend\routes.cpp ws2_32.lib crypt32.lib /Fe:roomwise.exe
if errorlevel 1 (
    echo [Build] MSVC compilation failed.
    exit /b 1
)

del /q *.obj 2>nul
echo [Build] Compilation successful: roomwise.exe
exit /b 0

:check_mingw
where g++ >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [Build] Compiling Roomwise with MinGW g++ (C++17)...
    g++ -std=c++17 -O2 -Iinclude -Ibackend backend/main.cpp backend/storage.cpp backend/scheduler.cpp backend/routes.cpp -o roomwise.exe -lws2_32 -lcrypt32 -pthread
    if errorlevel 1 (
        echo [Build] MinGW compilation failed.
        exit /b 1
    )
    echo [Build] Compilation successful: roomwise.exe
    exit /b 0
)

echo [Build Error] Could not find MSVC BuildTools or MinGW g++.
exit /b 1