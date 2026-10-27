@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo  Building phide - Web Development IDE (Direct2D Native Desktop)
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
set "SOURCES=src\main.cpp src\app\Window.cpp src\ui\Theme.cpp src\ui\LucideIcons.cpp src\ui\TitleHeader.cpp src\ui\PopupMenu.cpp src\ui\ActivityRail.cpp src\ui\DrawerPanel.cpp src\ui\ConfirmDiscardModal.cpp src\ui\DatabaseSettingsModal.cpp src\ui\EditorCanvas.cpp src\ui\KeyboardShortcutsView.cpp src\ui\PackageManagerView.cpp src\ui\ThemePickerModal.cpp src\ui\ColorPickerModal.cpp src\ui\QuickOpenModal.cpp src\ui\TerminalPanel.cpp src\ui\ExtensionsPanel.cpp src\ui\SplashScreen.cpp src\editor\Buffer.cpp src\editor\SnippetManager.cpp src\editor\SyntaxValidator.cpp src\editor\IntelliSenseEngine.cpp src\workspace\WorkspaceManager.cpp src\lsp\LspClient.cpp src\server\LiveServer.cpp"

:: 4. Compile Resources and C++20 Sources with MSVC
if exist "assets\phide.rc" (
    rc /nologo /fo "bin\phide.res" "assets\phide.rc"
)
echo [INFO] Compiling C++20 sources with MSVC cl.exe...
cl /nologo /EHsc /std:c++20 /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I include /I third_party\webview2\build\native\include !SOURCES! /Fe:bin\phide.exe /Fo:bin\ /link /SUBSYSTEM:WINDOWS bin\phide.res third_party\webview2\build\native\x64\WebView2LoaderStatic.lib user32.lib gdi32.lib dwmapi.lib d2d1.lib dwrite.lib ole32.lib shell32.lib comdlg32.lib advapi32.lib windowscodecs.lib ws2_32.lib winhttp.lib

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
