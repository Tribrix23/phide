@echo off
set "VCVARS_BAT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS_BAT%" (
    set "VCVARS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
call "%VCVARS_BAT%"

set "SOURCES=tests\test_verification.cpp src\app\Window.cpp src\ui\Theme.cpp src\ui\LucideIcons.cpp src\ui\TitleHeader.cpp src\ui\PopupMenu.cpp src\ui\ActivityRail.cpp src\ui\DrawerPanel.cpp src\ui\ConfirmDiscardModal.cpp src\ui\DatabaseSettingsModal.cpp src\ui\EditorCanvas.cpp src\ui\KeyboardShortcutsView.cpp src\ui\PackageManagerView.cpp src\ui\NotebookView.cpp src\ui\GraphView.cpp src\ui\ThemePickerModal.cpp src\ui\ColorPickerModal.cpp src\ui\QuickOpenModal.cpp src\ui\TerminalPanel.cpp src\ui\ExtensionsPanel.cpp src\ui\SplashScreen.cpp src\editor\Buffer.cpp src\editor\SnippetManager.cpp src\editor\SyntaxValidator.cpp src\editor\IntelliSenseEngine.cpp src\workspace\WorkspaceManager.cpp src\lsp\LspClient.cpp src\server\LiveServer.cpp"

cl.exe /nologo /std:c++20 /EHsc /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I include /I src /I third_party\webview2\build\native\include %SOURCES% /Fe:bin\test_verification.exe /link third_party\webview2\build\native\x64\WebView2LoaderStatic.lib user32.lib gdi32.lib dwmapi.lib d2d1.lib dwrite.lib ole32.lib shell32.lib comdlg32.lib advapi32.lib shlwapi.lib windowscodecs.lib ws2_32.lib winhttp.lib propsys.lib
