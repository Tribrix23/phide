@echo off
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul
if %errorlevel% neq 0 (
    echo Error initializing vcvars64
    exit /b 1
)

cl /nologo /EHsc /std:c++20 /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I include /c src\ui\LucideIcons.cpp /Fo:bin\LucideIcons.obj
if %errorlevel% neq 0 exit /b %errorlevel%

cl /nologo /EHsc /std:c++20 /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I include /c src\ui\DrawerPanel.cpp /Fo:bin\DrawerPanel.obj
if %errorlevel% neq 0 exit /b %errorlevel%

cl /nologo /EHsc /std:c++20 /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I include /c src\ui\DatabaseSettingsModal.cpp /Fo:bin\DatabaseSettingsModal.obj
if %errorlevel% neq 0 exit /b %errorlevel%

cl /nologo /EHsc /std:c++20 /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I include /c src\ui\TitleHeader.cpp /Fo:bin\TitleHeader.obj
if %errorlevel% neq 0 exit /b %errorlevel%

cl /nologo /EHsc /std:c++20 /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I include /c src\app\Window.cpp /Fo:bin\Window.obj
if %errorlevel% neq 0 exit /b %errorlevel%

cl /nologo /EHsc /std:c++20 /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I include /c src\editor\IntelliSenseEngine.cpp /Fo:bin\IntelliSenseEngine.obj
if %errorlevel% neq 0 exit /b %errorlevel%

cl /nologo /EHsc /std:c++20 /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /I include /c tests\test_verification.cpp /Fo:bin\test_verification.obj
if %errorlevel% neq 0 exit /b %errorlevel%

link /nologo /STACK:16777216 /SUBSYSTEM:CONSOLE /OUT:bin\test_verification.exe bin\test_verification.obj bin\Window.obj bin\Theme.obj bin\LucideIcons.obj bin\TitleHeader.obj bin\PopupMenu.obj bin\ActivityRail.obj bin\DrawerPanel.obj bin\ConfirmDiscardModal.obj bin\DatabaseSettingsModal.obj bin\EditorCanvas.obj bin\KeyboardShortcutsView.obj bin\ThemePickerModal.obj bin\QuickOpenModal.obj bin\TerminalPanel.obj bin\ExtensionsPanel.obj bin\SplashScreen.obj bin\Buffer.obj bin\SnippetManager.obj bin\SyntaxValidator.obj bin\IntelliSenseEngine.obj bin\WorkspaceManager.obj bin\LspClient.obj bin\LiveServer.obj user32.lib gdi32.lib dwmapi.lib d2d1.lib dwrite.lib ole32.lib shell32.lib comdlg32.lib advapi32.lib windowscodecs.lib ws2_32.lib winhttp.lib
