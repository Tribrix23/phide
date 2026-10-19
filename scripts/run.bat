@echo off
setlocal

if not exist "bin\phide.exe" (
    echo [INFO] Binary not found. Triggering build first...
    call "scripts\build.bat"
    if %errorlevel% neq 0 exit /b %errorlevel%
)

echo [INFO] Launching phide.exe...
start "" "bin\phide.exe"
