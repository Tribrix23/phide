@echo off
setlocal enabledelayedexpansion

set "VCVARS_BAT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "!VCVARS_BAT!" set "VCVARS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "!VCVARS_BAT!" set "VCVARS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not exist "!VCVARS_BAT!" set "VCVARS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"

call "!VCVARS_BAT!" >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Failed to init MSVC x64
    exit /b %errorlevel%
)

set "SOURCES=tests\test_verification.cpp src\ui\Theme.cpp src\ui\LucideIcons.cpp src\ui\TitleHeader.cpp src\ui\PopupMenu.cpp src\ui\ActivityRail.cpp src\ui\DrawerPanel.cpp src\ui\EditorCanvas.cpp src\ui\KeyboardShortcutsView.cpp src\ui\ThemePickerModal.cpp src\ui\QuickOpenModal.cpp src\ui\TerminalPanel.cpp src\editor\Buffer.cpp src\editor\SnippetManager.cpp src\editor\SyntaxValidator.cpp src\editor\IntelliSenseEngine.cpp src\workspace\WorkspaceManager.cpp src\lsp\LspClient.cpp src\server\LiveServer.cpp"

cl /nologo /EHsc /std:c++20 /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I include !SOURCES! /Fe:bin\test_verification.exe /Fo:bin\ /link /SUBSYSTEM:CONSOLE user32.lib gdi32.lib dwmapi.lib d2d1.lib dwrite.lib ole32.lib shell32.lib comdlg32.lib advapi32.lib windowscodecs.lib ws2_32.lib

if %errorlevel% equ 0 (
    echo [RUNNING] bin\test_verification.exe
    bin\test_verification.exe
)
