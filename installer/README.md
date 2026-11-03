# phide Standalone Offline Installer

This directory contains the automated toolchain for generating **phide-Setup-x64.exe**, an all-in-one standalone Windows installer for **phide**.

---

## Key Highlights

- **JetBrains / PyCharm-Style Modern UI**:
  - Direct launch into installer—no generic "Select install mode" prompt.
  - Bespoke **phide Cosmic Dark** artwork sidebar and header branding.
  - Structured **Installation Options** page with 4 grouped sections (Desktop Shortcut, PATH registration, Explorer context menus, and File Associations).
  - Detailed live extraction status with **Show details** view.

- **100% Offline & Self-Contained**:
  - The user never needs to download anything separately.
  - Bundles the native **phide.exe** IDE binary.
  - Bundles **PHP 8.5 (NTS x64)** and **Composer 2.10.3**.
  - Bundles **MariaDB / MySQL 10.4** database server engine with initial clean schema.
  - Bundles embedded **Nginx 1.30** web server.
  - Bundles embedded **phpMyAdmin 5.2.3** administration panel.
  - Bundles all typography fonts (KodaMono, FontAwesome Brands/Solid, Seti Icons).
  - Bundles all default extensions and snippets.
  - Bundles Visual C++ CRT runtime DLLs for direct execution on any fresh Windows machine.

- **System Integration**:
  - Desktop shortcut with high-resolution `phide.ico`.
  - Start Menu program group and uninstaller (`Uninstall.exe`).
  - Windows Explorer right-click context menu: `"Open Folder as phide Project"` and `"Open with phide"` for files, folders, and folder background.
  - File associations for `.php`, `.phtml`, `.cpp`, `.h`, `.hpp`, `.js`, `.ts`, `.json`.
  - Automatic `PATH` configuration for `phide`, `php`, and `composer`.
  - Clean uninstallation via Windows Settings / Add or Remove Programs.

---

## One-Click Installer Build

Run the following command from the repository root:

```cmd
installer\build_installer.bat
```

Or from PowerShell:

```powershell
.\installer\build_installer.bat
```

The resulting standalone setup program will be output to:

```
installer\output\phide-Setup-x64.exe
```

---

## File Structure

```
installer/
├── build_installer.bat             # One-click automated build pipeline
├── prepare_payload.ps1             # Stages all offline runtimes, tools, and assets
├── phide_installer.nsi             # PyCharm-style NSIS Modern UI 2 setup script
├── branding/
│   ├── generate_branding.py        # Python generator for high-res BMP installer assets
│   ├── welcome_banner.bmp          # 164x314 cosmic sidebar artwork
│   └── header_logo.bmp             # 150x57 header banner logo
├── output/                         # Destination directory for the generated installer .exe
│   └── phide-Setup-x64.exe
└── README.md                       # Architecture documentation
```
