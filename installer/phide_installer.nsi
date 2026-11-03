; ==============================================================================
;  phide Setup Script (NSIS Modern UI 2)
;  JetBrains / PyCharm Style Architecture with phide Cosmic Dark Aesthetic
;  100% Offline Standalone Installer - Zero External Downloads Required
; ==============================================================================

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "WinMessages.nsh"

; ------------------------------------------------------------------------------
; General Configuration
; ------------------------------------------------------------------------------
Name "Phide 2026.a"
BrandingText "Phide 2026.a"
OutFile "output\phide-Setup-x64.exe"
InstallDir "$PROGRAMFILES64\phide"
InstallDirRegKey HKLM "Software\phide" "InstallDir"
RequestExecutionLevel admin
Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma

; ------------------------------------------------------------------------------
; Interface & Branding Settings (PyCharm Layout with phide Style)
; ------------------------------------------------------------------------------
!define MUI_ICON "..\assets\phide.ico"
!define MUI_UNICON "..\assets\phide.ico"

!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "branding\header_logo.bmp"
!define MUI_HEADERIMAGE_RIGHT
!define MUI_WELCOMEFINISHPAGE_BITMAP "branding\welcome_banner.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "branding\welcome_banner.bmp"

!define MUI_ABORTWARNING

; Welcome Page Texts
!define MUI_WELCOMEPAGE_TITLE "Welcome to Phide 2026.a Setup"
!define MUI_WELCOMEPAGE_TEXT "Setup will guide you through the installation of Phide 2026.a (Direct2D Native Desktop IDE).$\r$\n$\r$\nAll development runtimes and dependencies (PHP 8.5, Composer, MariaDB/MySQL, and Nginx) are completely bundled offline—no secondary downloads or internet connections are required.$\r$\n$\r$\nClick Next to continue."

; Directory Page Texts
!define MUI_DIRECTORYPAGE_TEXT_TOP "Setup will install Phide 2026.a in the following folder. To install in a different folder, click Browse and select another folder. Click Next to continue."

; Installation Options Header
!define PAGE_OPTIONS_TITLE "Installation Options"
!define PAGE_OPTIONS_SUBTITLE "Configure your Phide 2026.a installation"

; Installing Page Settings (Show Details button like PyCharm)
!define MUI_INSTFILESPAGE_FINISHHEADER_TEXT "Installation Complete"
!define MUI_INSTFILESPAGE_FINISHHEADER_SUBTEXT "Phide 2026.a and all offline runtimes have been successfully installed."

; Finish Page Settings
!define MUI_FINISHPAGE_RUN "$INSTDIR\phide.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Run Phide 2026.a"

; ------------------------------------------------------------------------------
; Variables for Custom "Installation Options" Page
; ------------------------------------------------------------------------------
Var Dialog
Var HwndGroupDesktop
Var HwndChkDesktop
Var HwndGroupPath
Var HwndChkPath
Var HwndGroupContext
Var HwndChkContextFolder
Var HwndChkContextFile
Var HwndGroupAssoc
Var HwndChkAssocPhp
Var HwndChkAssocPy
Var HwndChkAssocJs

Var StateDesktop
Var StatePath
Var StateContextFolder
Var StateContextFile
Var StateAssocPhp
Var StateAssocPy
Var StateAssocJs

; ------------------------------------------------------------------------------
; Pages Flow
; ------------------------------------------------------------------------------
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
Page custom PageInstallationOptions PageInstallationOptionsLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------------------
; Custom "Installation Options" Page Callback (PyCharm Style)
; ------------------------------------------------------------------------------
Function PageInstallationOptions
    !insertmacro MUI_HEADER_TEXT "${PAGE_OPTIONS_TITLE}" "${PAGE_OPTIONS_SUBTITLE}"
    nsDialogs::Create 1018
    Pop $Dialog
    ${If} $Dialog == error
        Abort
    ${EndIf}

    ; --- Group 1: Create Desktop Shortcut (Top Left) ---
    ${NSD_CreateGroupBox} 0u 5u 142u 42u "Create Desktop Shortcut"
    Pop $HwndGroupDesktop

    ${NSD_CreateCheckbox} 10u 20u 120u 12u "64-bit launcher"
    Pop $HwndChkDesktop
    ${NSD_SetState} $HwndChkDesktop ${BST_CHECKED}

    ; --- Group 2: Update PATH variable (Top Right) ---
    ${NSD_CreateGroupBox} 150u 5u 148u 42u "Update PATH variable (restart needed)"
    Pop $HwndGroupPath

    ${NSD_CreateCheckbox} 160u 20u 130u 12u "Add launchers dir to the PATH"
    Pop $HwndChkPath
    ${NSD_SetState} $HwndChkPath ${BST_CHECKED}

    ; --- Group 3: Update Context Menu (Bottom Left) ---
    ${NSD_CreateGroupBox} 0u 54u 142u 64u "Update context menu"
    Pop $HwndGroupContext

    ${NSD_CreateCheckbox} 10u 70u 125u 12u 'Add "Open Folder as Project"'
    Pop $HwndChkContextFolder
    ${NSD_SetState} $HwndChkContextFolder ${BST_CHECKED}

    ${NSD_CreateCheckbox} 10u 88u 125u 12u 'Add "Open with phide"'
    Pop $HwndChkContextFile
    ${NSD_SetState} $HwndChkContextFile ${BST_CHECKED}

    ; --- Group 4: Create Associations (Bottom Right) ---
    ${NSD_CreateGroupBox} 150u 54u 148u 64u "Create Associations"
    Pop $HwndGroupAssoc

    ${NSD_CreateCheckbox} 160u 70u 55u 12u ".php"
    Pop $HwndChkAssocPhp
    ${NSD_SetState} $HwndChkAssocPhp ${BST_CHECKED}

    ${NSD_CreateCheckbox} 225u 70u 55u 12u ".py"
    Pop $HwndChkAssocPy
    ${NSD_SetState} $HwndChkAssocPy ${BST_CHECKED}

    ${NSD_CreateCheckbox} 160u 88u 120u 12u ".js / .ts / .json"
    Pop $HwndChkAssocJs
    ${NSD_SetState} $HwndChkAssocJs ${BST_CHECKED}

    nsDialogs::Show
FunctionEnd

Function PageInstallationOptionsLeave
    ${NSD_GetState} $HwndChkDesktop $StateDesktop
    ${NSD_GetState} $HwndChkPath $StatePath
    ${NSD_GetState} $HwndChkContextFolder $StateContextFolder
    ${NSD_GetState} $HwndChkContextFile $StateContextFile
    ${NSD_GetState} $HwndChkAssocPhp $StateAssocPhp
    ${NSD_GetState} $HwndChkAssocPy $StateAssocPy
    ${NSD_GetState} $HwndChkAssocJs $StateAssocJs
FunctionEnd

; ------------------------------------------------------------------------------
; Main Installation Section
; ------------------------------------------------------------------------------
Section "MainSection" SEC01
    SectionIn RO
    SetOutPath "$INSTDIR"

    ; 1. Install all staged offline files (IDE, Runtimes, Servers, Assets)
    DetailPrint "Extracting Phide 2026.a and bundled offline runtimes..."
    File /r "payload\*"

    ; 1b. Configure runtime permissions so standard user can run MySQL and Nginx without elevation
    DetailPrint "Configuring offline runtime permissions..."
    ExecWait 'icacls "$INSTDIR\runtimes\mysql\data" /grant *S-1-1-0:(OI)(CI)F /T /Q'
    ExecWait 'icacls "$INSTDIR\include\phide\server\nginx\logs" /grant *S-1-1-0:(OI)(CI)F /T /Q'
    ExecWait 'icacls "$INSTDIR\include\phide\server\nginx\temp" /grant *S-1-1-0:(OI)(CI)F /T /Q'

    ; 2. Generate Uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\phide" "InstallDir" "$INSTDIR"

    ; 3. Start Menu Shortcuts
    CreateDirectory "$SMPROGRAMS\phide"
    CreateShortcut "$SMPROGRAMS\phide\phide.lnk" "$INSTDIR\phide.exe" "" "$INSTDIR\assets\phide.ico" 0
    CreateShortcut "$SMPROGRAMS\phide\phide (New Window).lnk" "$INSTDIR\phide.exe" "--new-window" "$INSTDIR\assets\phide.ico" 0
    CreateShortcut "$SMPROGRAMS\phide\Uninstall phide.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0

    ; 4. Desktop Shortcut (if option checked)
    ${If} $StateDesktop == ${BST_CHECKED}
        DetailPrint "Creating Desktop Shortcut..."
        CreateShortcut "$DESKTOP\phide.lnk" "$INSTDIR\phide.exe" "" "$INSTDIR\assets\phide.ico" 0
    ${EndIf}

    ; 5. PATH Environment Registration (if option checked)
    ${If} $StatePath == ${BST_CHECKED}
        DetailPrint "Registering phide and bundled PHP in PATH..."
        ReadRegStr $0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
        ${If} $0 != ""
            WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$0;$INSTDIR;$INSTDIR\runtimes\php"
        ${Else}
            WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$INSTDIR;$INSTDIR\runtimes\php"
        ${EndIf}
        SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=3000
    ${EndIf}

    ; 6. Context Menu - Open Folder as Project
    ${If} $StateContextFolder == ${BST_CHECKED}
        DetailPrint "Registering folder context menus..."
        WriteRegStr HKCR "Directory\shell\phide" "" 'Open Folder as phide Project'
        WriteRegStr HKCR "Directory\shell\phide" "Icon" '"$INSTDIR\assets\phide.ico"'
        WriteRegStr HKCR "Directory\shell\phide\command" "" '"$INSTDIR\phide.exe" "%1"'

        WriteRegStr HKCR "Directory\Background\shell\phide" "" 'Open with phide'
        WriteRegStr HKCR "Directory\Background\shell\phide" "Icon" '"$INSTDIR\assets\phide.ico"'
        WriteRegStr HKCR "Directory\Background\shell\phide\command" "" '"$INSTDIR\phide.exe" "%V"'
    ${EndIf}

    ; 7. Context Menu - Open with phide (Files)
    ${If} $StateContextFile == ${BST_CHECKED}
        DetailPrint "Registering file context menu..."
        WriteRegStr HKCR "*\shell\phide" "" 'Open with phide'
        WriteRegStr HKCR "*\shell\phide" "Icon" '"$INSTDIR\assets\phide.ico"'
        WriteRegStr HKCR "*\shell\phide\command" "" '"$INSTDIR\phide.exe" "%1"'
    ${EndIf}

    ; 8. File Associations - PHP
    ${If} $StateAssocPhp == ${BST_CHECKED}
        DetailPrint "Registering PHP file associations..."
        WriteRegStr HKCR ".php" "" "phide.php"
        WriteRegStr HKCR ".phtml" "" "phide.php"
        WriteRegStr HKCR "phide.php" "" "PHP Source File"
        WriteRegStr HKCR "phide.php\DefaultIcon" "" '"$INSTDIR\assets\phide.ico"'
        WriteRegStr HKCR "phide.php\shell\open\command" "" '"$INSTDIR\phide.exe" "%1"'
    ${EndIf}

    ; 9. File Associations - Python
    ${If} $StateAssocPy == ${BST_CHECKED}
        DetailPrint "Registering Python file associations..."
        WriteRegStr HKCR ".py" "" "phide.py"
        WriteRegStr HKCR ".pyw" "" "phide.py"
        WriteRegStr HKCR "phide.py" "" "Python Source File"
        WriteRegStr HKCR "phide.py\DefaultIcon" "" '"$INSTDIR\assets\phide.ico"'
        WriteRegStr HKCR "phide.py\shell\open\command" "" '"$INSTDIR\phide.exe" "%1"'
    ${EndIf}

    ; 10. File Associations - Web / Script
    ${If} $StateAssocJs == ${BST_CHECKED}
        DetailPrint "Registering JavaScript/TypeScript/JSON file associations..."
        WriteRegStr HKCR ".js" "" "phide.js"
        WriteRegStr HKCR ".jsx" "" "phide.js"
        WriteRegStr HKCR ".ts" "" "phide.js"
        WriteRegStr HKCR ".tsx" "" "phide.js"
        WriteRegStr HKCR ".json" "" "phide.js"
        WriteRegStr HKCR "phide.js" "" "JavaScript/TypeScript/JSON Source"
        WriteRegStr HKCR "phide.js\DefaultIcon" "" '"$INSTDIR\assets\phide.ico"'
        WriteRegStr HKCR "phide.js\shell\open\command" "" '"$INSTDIR\phide.exe" "%1"'
    ${EndIf}

    ; 11. Register in Windows Add/Remove Programs
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "DisplayName" "Phide 2026.a"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "DisplayIcon" '"$INSTDIR\assets\phide.ico"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "DisplayVersion" "2026.a"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "Publisher" "Phide Studio"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "InstallLocation" "$INSTDIR"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "NoRepair" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "EstimatedSize" 256000
SectionEnd

; ------------------------------------------------------------------------------
; Uninstallation Section
; ------------------------------------------------------------------------------
Section "Uninstall"
    ; Remove Desktop & Start Menu Shortcuts
    Delete "$DESKTOP\phide.lnk"
    RMDir /r "$SMPROGRAMS\phide"

    ; Clean Context Menus
    DeleteRegKey HKCR "Directory\shell\phide"
    DeleteRegKey HKCR "Directory\Background\shell\phide"
    DeleteRegKey HKCR "*\shell\phide"

    ; Clean File Associations
    DeleteRegKey HKCR "phide.php"
    DeleteRegKey HKCR "phide.py"
    DeleteRegKey HKCR "phide.js"

    ; Remove from Add/Remove Programs
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide"
    DeleteRegKey HKLM "Software\phide"

    ; Remove files and directories
    RMDir /r "$INSTDIR\assets"
    RMDir /r "$INSTDIR\include"
    RMDir /r "$INSTDIR\runtimes"
    Delete "$INSTDIR\*.*"
    RMDir "$INSTDIR"
SectionEnd
