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
; ------------------------------------------------------------------------------
; General Configuration
; ------------------------------------------------------------------------------
!define APP_NAME "Phide"
!define APP_VERSION "2026.b"

Name "${APP_NAME} ${APP_VERSION}"
BrandingText "${APP_NAME} ${APP_VERSION}"
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
!define MUI_WELCOMEPAGE_TITLE "Welcome to ${APP_NAME} ${APP_VERSION} Setup"
!define MUI_WELCOMEPAGE_TEXT "Setup will guide you through the installation of ${APP_NAME} ${APP_VERSION} (Direct2D Native Desktop IDE).$\r$\n$\r$\nAll development runtimes and dependencies (PHP 8.5, Composer, MariaDB/MySQL, and Nginx) are completely bundled offline - no secondary downloads or internet connections are required.$\r$\n$\r$\nClick Next to continue."

; Directory Page Texts
!define MUI_DIRECTORYPAGE_TEXT_TOP "Setup will install ${APP_NAME} ${APP_VERSION} in the following folder. To install in a different folder, click Browse and select another folder. Click Next to continue."

; Installation Options Header
!define PAGE_OPTIONS_TITLE "Installation Options"
!define PAGE_OPTIONS_SUBTITLE "Configure your ${APP_NAME} ${APP_VERSION} installation"

; Installing Page Settings (Show Details button like PyCharm)
!define MUI_INSTFILESPAGE_FINISHHEADER_TEXT "Installation Complete"
!define MUI_INSTFILESPAGE_FINISHHEADER_SUBTEXT "${APP_NAME} ${APP_VERSION} and all offline runtimes have been successfully installed."

; Finish Page Settings
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION LaunchApp
!define MUI_FINISHPAGE_RUN_TEXT "Run ${APP_NAME} ${APP_VERSION}"

Function LaunchApp
    ExecShell "" "$INSTDIR\phide.exe"
FunctionEnd

; ------------------------------------------------------------------------------
; Variables for Detection & Maintenance Flow
; ------------------------------------------------------------------------------
Var IsInstalled
Var InstalledDir
Var InstalledVersion
Var InstallMode        ; "SAME", "UPDATE", "DOWNGRADE"

Var DialogExisting
Var HwndExistingDesc
Var HwndExistingGroup
Var HwndRadioAction1
Var HwndRadioAction2
Var HwndExistingDetail
Var StateAction1
Var StateAction2

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
Page custom PageExistingInstall PageExistingInstallLeave
!define MUI_PAGE_CUSTOMFUNCTION_PRE DirectoryPre
!insertmacro MUI_PAGE_DIRECTORY
Page custom PageInstallationOptions PageInstallationOptionsLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------------------
; Initialization and Existing Installation Detection
; ------------------------------------------------------------------------------
Function .onInit
    ${If} ${RunningX64}
        SetRegView 64
    ${EndIf}

    StrCpy $IsInstalled 0
    StrCpy $InstalledDir ""
    StrCpy $InstalledVersion ""
    StrCpy $InstallMode ""

    ; Check registry for existing installation
    ReadRegStr $InstalledDir HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "InstallLocation"
    ${If} $InstalledDir == ""
        ReadRegStr $InstalledDir HKLM "Software\phide" "InstallDir"
    ${EndIf}
    ${If} $InstalledDir == ""
        ReadRegStr $InstalledDir HKCU "Software\phide" "InstallDir"
    ${EndIf}
    ${If} $InstalledDir == ""
        ${If} ${FileExists} "$PROGRAMFILES64\phide\phide.exe"
            StrCpy $InstalledDir "$PROGRAMFILES64\phide"
        ${EndIf}
    ${EndIf}

    ${If} $InstalledDir != ""
    ${AndIf} ${FileExists} "$InstalledDir\phide.exe"
        StrCpy $IsInstalled 1

        ; Read installed version
        ReadRegStr $InstalledVersion HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "DisplayVersion"
        ${If} $InstalledVersion == ""
            ReadRegStr $InstalledVersion HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "DisplayVersion"
        ${EndIf}
        ${If} $InstalledVersion == ""
            StrCpy $InstalledVersion "2026.a"
        ${EndIf}

        ; Compare installed version with current installer version (2026.b)
        System::Call "shlwapi::StrCmpLogicalW(w '$InstalledVersion', w '${APP_VERSION}') i .r0"
        ${If} $0 == 0
            StrCpy $InstallMode "SAME"
        ${ElseIf} $0 < 0
            StrCpy $InstallMode "UPDATE"
        ${Else}
            StrCpy $InstallMode "DOWNGRADE"
        ${EndIf}

        StrCpy $INSTDIR $InstalledDir
    ${EndIf}
FunctionEnd

Function DirectoryPre
    ${If} $IsInstalled == 1
        ; Use existing installation directory, skip directory selection dialog
        Abort
    ${EndIf}
FunctionEnd

; ------------------------------------------------------------------------------
; Custom "Existing Installation / Maintenance" Page Callback
; ------------------------------------------------------------------------------
Function PageExistingInstall
    ${If} $IsInstalled == 0
        Abort ; Fresh install - skip this page completely
    ${EndIf}

    ${If} $InstallMode == "SAME"
        !insertmacro MUI_HEADER_TEXT "Phide is Already Installed" "Choose to reinstall or uninstall Phide ${APP_VERSION}"
    ${ElseIf} $InstallMode == "UPDATE"
        !insertmacro MUI_HEADER_TEXT "Update Phide Installation" "An earlier version of Phide was detected on this computer"
    ${Else}
        !insertmacro MUI_HEADER_TEXT "Downgrade Phide Installation" "A newer version of Phide was detected on this computer"
    ${EndIf}

    nsDialogs::Create 1018
    Pop $DialogExisting
    ${If} $DialogExisting == error
        Abort
    ${EndIf}

    ${If} $InstallMode == "SAME"
        ${NSD_CreateLabel} 0u 0u 300u 28u "Phide ${APP_VERSION} is currently installed at:$\r$\n$InstalledDir$\r$\n$\r$\nPlease select the action you want to perform:"
    ${ElseIf} $InstallMode == "UPDATE"
        ${NSD_CreateLabel} 0u 0u 300u 28u "An earlier version (Phide $InstalledVersion) is installed at:$\r$\n$InstalledDir$\r$\nSetup can update this installation to Phide ${APP_VERSION}."
    ${Else}
        ${NSD_CreateLabel} 0u 0u 300u 28u "A newer version (Phide $InstalledVersion) is installed at:$\r$\n$InstalledDir$\r$\nWarning: Running this installer will downgrade Phide to ${APP_VERSION}."
    ${EndIf}
    Pop $HwndExistingDesc

    ${NSD_CreateGroupBox} 0u 34u 300u 58u "Available Actions"
    Pop $HwndExistingGroup

    ${If} $InstallMode == "SAME"
        ${NSD_CreateRadioButton} 12u 48u 280u 12u "&Reinstall Phide ${APP_VERSION} (repair / refresh all components)"
    ${ElseIf} $InstallMode == "UPDATE"
        ${NSD_CreateRadioButton} 12u 48u 280u 12u "&Update to Phide ${APP_VERSION} (recommended - preserves databases & settings)"
    ${Else}
        ${NSD_CreateRadioButton} 12u 48u 280u 12u "&Downgrade to Phide ${APP_VERSION} (replace newer version with ${APP_VERSION})"
    ${EndIf}
    Pop $HwndRadioAction1
    ${NSD_SetState} $HwndRadioAction1 ${BST_CHECKED}

    ${If} $InstallMode == "SAME"
        ${NSD_CreateRadioButton} 12u 66u 280u 12u "&Uninstall Phide ${APP_VERSION} from this computer"
    ${Else}
        ${NSD_CreateRadioButton} 12u 66u 280u 12u "&Uninstall existing Phide $InstalledVersion from this computer"
    ${EndIf}
    Pop $HwndRadioAction2

    ${If} $InstallMode == "SAME"
        ${NSD_CreateLabel} 0u 98u 300u 32u "Reinstalling will refresh all files and runtimes while leaving your workspaces, databases, and configuration intact. Click Next to proceed."
    ${ElseIf} $InstallMode == "UPDATE"
        ${NSD_CreateLabel} 0u 98u 300u 32u "Updating will seamlessly replace the core IDE and offline runtimes with Phide ${APP_VERSION}. All user projects, workspaces, and databases are preserved."
    ${Else}
        ${NSD_CreateLabel} 0u 98u 300u 32u "Downgrading will install Phide ${APP_VERSION} over $InstalledVersion. Your workspaces and databases will remain preserved."
    ${EndIf}
    Pop $HwndExistingDetail

    nsDialogs::Show
FunctionEnd

Function PageExistingInstallLeave
    ${NSD_GetState} $HwndRadioAction1 $StateAction1
    ${NSD_GetState} $HwndRadioAction2 $StateAction2

    ${If} $StateAction2 == ${BST_CHECKED}
        MessageBox MB_YESNO|MB_ICONQUESTION "Are you sure you want to uninstall Phide $InstalledVersion?" IDYES do_uninstall
        Abort

    do_uninstall:
        ${If} ${FileExists} "$InstalledDir\Uninstall.exe"
            ExecShell "" "$InstalledDir\Uninstall.exe"
            Quit
        ${Else}
            MessageBox MB_ICONSTOP "Uninstaller could not be found at:$\r$\n$InstalledDir\Uninstall.exe"
            Abort
        ${EndIf}
    ${Else}
        StrCpy $INSTDIR $InstalledDir
    ${EndIf}
FunctionEnd

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

    ; Ensure phide is not running
check_phide_loop:
    nsExec::ExecToStack 'cmd.exe /c "tasklist /FI \"IMAGENAME eq phide.exe\" 2>NUL | find /I \"phide.exe\""'
    Pop $0
    Pop $1
    ${If} $0 == 0
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Phide is currently running. Please close Phide and click Retry to continue, or Cancel to abort." IDRETRY check_phide_loop
        Abort
    ${EndIf}

    ; 1. Install all staged offline files (IDE, Runtimes, Servers, Assets)
    DetailPrint "Extracting ${APP_NAME} ${APP_VERSION} and bundled offline runtimes..."
    ; Preserve existing user databases across installer updates
    ${If} ${FileExists} "$INSTDIR\runtimes\mysql\data\ibdata1"
        DetailPrint "Existing user databases detected, preserving user databases across update..."
        CreateDirectory "$INSTDIR\runtimes\mysql\__data_preserve"
        CopyFiles /SILENT "$INSTDIR\runtimes\mysql\data\*" "$INSTDIR\runtimes\mysql\__data_preserve"
    ${EndIf}

    File /r "payload\*"

    ; Restore preserved user databases if this was an update
    ${If} ${FileExists} "$INSTDIR\runtimes\mysql\__data_preserve\ibdata1"
        DetailPrint "Restoring preserved user databases..."
        CopyFiles /SILENT "$INSTDIR\runtimes\mysql\__data_preserve\*" "$INSTDIR\runtimes\mysql\data"
        RMDir /r "$INSTDIR\runtimes\mysql\__data_preserve"
    ${EndIf}

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
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "DisplayName" "${APP_NAME} ${APP_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "DisplayIcon" '"$INSTDIR\assets\phide.ico"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\phide" "DisplayVersion" "${APP_VERSION}"
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
