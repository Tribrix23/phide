@echo off
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul
if %errorlevel% neq 0 (
    echo Error initializing vcvars64
    exit /b 1
)

cl /nologo /EHsc /std:c++20 /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I include tests\test_verification.cpp src\app\Window.cpp src\ui\Theme.cpp src\ui\LucideIcons.cpp src\ui\TitleHeader.cpp src\ui\PopupMenu.cpp src\ui\ActivityRail.cpp src\ui\DrawerPanel.cpp src\ui\ConfirmDiscardModal.cpp src\ui\EditorCanvas.cpp src\ui\KeyboardShortcutsView.cpp src\ui\ThemePickerModal.cpp src\ui\QuickOpenModal.cpp src\ui\TerminalPanel.cpp src\ui\ExtensionsPanel.cpp src\ui\SplashScreen.cpp src\editor\Buffer.cpp src\editor\SnippetManager.cpp src\editor\SyntaxValidator.cpp src\editor\IntelliSenseEngine.cpp src\workspace\WorkspaceManager.cpp src\lsp\LspClient.cpp src\server\LiveServer.cpp /Fe:bin\test_verification.exe /Fo:bin\ /link /STACK:16777216 /SUBSYSTEM:CONSOLE user32.lib gdi32.lib dwmapi.lib d2d1.lib dwrite.lib ole32.lib shell32.lib comdlg32.lib advapi32.lib windowscodecs.lib ws2_32.lib winhttp.lib
