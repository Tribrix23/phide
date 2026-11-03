@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo  Building phide Standalone Offline Installer (PyCharm-Style NSIS Engine)
echo ======================================================================

set "REPO_ROOT=%~dp0.."
cd /d "%REPO_ROOT%"

:: 1. Locate NSIS Compiler (makensis.exe)
set "MAKENSIS_EXE="
if exist "C:\Program Files (x86)\NSIS\makensis.exe" set "MAKENSIS_EXE=C:\Program Files (x86)\NSIS\makensis.exe"
if not defined MAKENSIS_EXE if exist "C:\Program Files\NSIS\makensis.exe" set "MAKENSIS_EXE=C:\Program Files\NSIS\makensis.exe"
if not defined MAKENSIS_EXE (
    where makensis.exe >nul 2>&1
    if !errorlevel! equ 0 set "MAKENSIS_EXE=makensis.exe"
)

if not defined MAKENSIS_EXE (
    echo [ERROR] NSIS compiler (makensis.exe) was not found!
    echo Please install NSIS: winget install NSIS.NSIS
    exit /b 1
)

echo [INFO] NSIS Compiler: "!MAKENSIS_EXE!"

:: 2. Ensure phide.exe is compiled
if not exist "bin\phide.exe" (
    echo [INFO] bin\phide.exe not found, compiling application first...
    call "scripts\build.bat"
    if !errorlevel! neq 0 (
        echo [ERROR] Application compilation failed.
        exit /b !errorlevel!
    )
)

:: 3. Prepare Staged Payload
echo [INFO] Staging all offline dependencies and runtimes...
powershell -NoProfile -ExecutionPolicy Bypass -File "installer\prepare_payload.ps1"
if %errorlevel% neq 0 (
    echo [ERROR] Failed to prepare payload.
    exit /b %errorlevel%
)

:: 4. Ensure Branding Bitmaps are Generated
if not exist "installer\branding\welcome_banner.bmp" (
    echo [INFO] Generating custom branding bitmaps...
    python "installer\branding\generate_branding.py"
)

:: 5. Compile Installer Executable with NSIS
echo [INFO] Compiling installer executable with NSIS Modern UI 2...
"!MAKENSIS_EXE!" "installer\phide_installer.nsi"
if %errorlevel% neq 0 (
    echo [ERROR] NSIS compilation failed!
    exit /b %errorlevel%
)

echo ======================================================================
echo [SUCCESS] phide standalone offline installer created successfully:
echo  - Output: installer\output\phide-Setup-x64.exe
echo ======================================================================
