@echo off
if not exist roomwise.exe (
    echo [Roomwise] Binary roomwise.exe not found. Running build.bat first...
    call build.bat
    if errorlevel 1 exit /b 1
)

echo [Roomwise] Starting Roomwise Empty Classroom Finder on http://localhost:3000 ...
roomwise.exe --port 3000