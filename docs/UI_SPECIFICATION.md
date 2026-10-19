# UI Specification: Dark IDE Frameless Header & Collapsed Activity Rail

This specification defines the visual and interaction metrics for **phide** in Dark Theme mode.

---

## 1. Window Chrome & Canvas Metrics

- **Window Style**: Modern borderless frameless window with Windows 11 rounded corners and native dark caption controls (`DWMWA_USE_IMMERSIVE_DARK_MODE`).
- **Theme**: Dark IDE palette (VS Code / JetBrains inspired).
- **Backgrounds**:
  - Main Workspace: `#1E1E1E`
  - Title Bar Header: `#181818`
  - Activity Rail (Sidebar): `#141414`
  - Slide-out Drawer: `#181818`
- **Borders & Dividers**: `1px solid #2C2C2C`
- **Typography & Brushes**:
  - Text Primary: `#D8D8D8`
  - Text Secondary: `#949494`
  - Text Muted: `#666666`
  - Item Hover: `#2A2D2E`
  - Item Active: `#37373D` with `#0078D4` blue left indicator line (2.5px width)

---

## 2. Integrated Header Bar (Top)

- **Height**: 42px.
- **Left Zone**: Standard IDE Menu Bar (`File`, `Edit`, `View`, `Project`, `Help`).
  - Font: Segoe UI, 12px Regular, `#949494`. Hover: `#2A2D2E` rounded rectangle with `#D8D8D8` text.
- **Center Zone**: Project / Application Title:
  - `phide — C++ & PHP IDE` (12px Semi-bold, `#949494`).
  - Draggable non-client area (`HTCAPTION`).
- **Right Zone**: Native Windows 11 Caption Controls.
  - Reserved 138px margin for native Windows DWM Minimize, Maximize (Snap Layouts), and Close buttons.
  - No duplicate custom buttons.

---

## 3. Collapsed Activity Rail (Left Sidebar)

- **Width**: 48px fixed.
- **Background**: `#141414`.
- **Right Divider**: `1px solid #2C2C2C`.
- **Items**:
  1. **Explorer (Files)** (`\u25A4`): Toggles project file tree drawer.
  2. **Search** (`\u26B2`): Toggles workspace search drawer.
  3. **Source Control (Git)** (`\u23F1`): Toggles version control drawer.
  4. **Run & Debug** (`\u25B6`): Toggles build and debug perspective.
- **Interaction**:
  - Clicking any perspective icon expands the 280px drawer panel.
  - Clicking the active perspective icon collapses the drawer back into the rail.
  - Active perspective shows a 2.5px vertical `#0078D4` accent line on the left window edge.

---

## 4. Main Workspace Canvas

- Clean, distraction-free dark workspace (`#1E1E1E`).
- Centered watermark and quick keybinding hints:
  - `Show Explorer — Click sidebar icon`
  - `Open Workspace — Ctrl+O`
  - `Command Palette — Ctrl+Shift+P`
