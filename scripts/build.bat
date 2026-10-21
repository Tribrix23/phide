@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo  Building phide - Modern C++ IDE (Direct2D Native Desktop)
echo ======================================================================

:: 1. Locate Visual Studio x64 Environment
set "VCVARS_BAT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not exist "!VCVARS_BAT!" (
    set "VCVARS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)

if not exist "!VCVARS_BAT!" (
    set "VCVARS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
)

if not exist "!VCVARS_BAT!" (
    set "VCVARS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)

if not exist "!VCVARS_BAT!" (
    echo [ERROR] Visual Studio x64 Build Tools not found!
    echo Please install Visual Studio 2022 Build Tools with C++ workload.
    exit /b 1
)

echo [INFO] Initializing MSVC x64 Developer Environment...
call "!VCVARS_BAT!" >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Failed to initialize MSVC x64 environment.
    exit /b %errorlevel%
)

:: 2. Ensure Output Directory and Assets Exist
if not exist "bin" mkdir "bin"
if not exist "bin\assets\fonts" mkdir "bin\assets\fonts"
if exist "assets" (
    xcopy /y /e /i "assets" "bin\assets" >nul 2>&1
)

:: 3. Collect Source Files
set "SOURCES=src\main.cpp src\app\Window.cpp src\ui\Theme.cpp src\ui\LucideIcons.cpp src\ui\TitleHeader.cpp src\ui\PopupMenu.cpp src\ui\ActivityRail.cpp src\ui\DrawerPanel.cpp src\ui\EditorCanvas.cpp src\ui\KeyboardShortcutsView.cpp src\ui\ThemePickerModal.cpp src\editor\Buffer.cpp src\workspace\WorkspaceManager.cpp src\lsp\LspClient.cpp"

:: 4. Compile with MSVC (C++20, Optimizations, Unicode, Native Subsystem)
echo [INFO] Compiling C++20 sources with MSVC cl.exe...
cl /nologo /EHsc /std:c++20 /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /I include !SOURCES! /Fe:bin\phide.exe /Fo:bin\ /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib dwmapi.lib d2d1.lib dwrite.lib ole32.lib shell32.lib comdlg32.lib advapi32.lib windowscodecs.lib

if %errorlevel% equ 0 (
    echo ======================================================================
    echo [SUCCESS] Build succeeded: bin\phide.exe
    echo ======================================================================
) else (
    echo ======================================================================
    echo [ERROR] Build failed! Check compiler diagnostics above.
    echo ======================================================================
    exit /b %errorlevel%
)
