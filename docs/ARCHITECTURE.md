# System Architecture & Technical Specifications

This document outlines the software design, threading model, graphics pipeline, and interaction lifecycle for the **phide** C++ IDE.

---

## 1. High-Level Subsystem Architecture

```
+-----------------------------------------------------------------------------------+
|                                 Window Subsystem                                  |
|   (DWM Non-Client Hit-Testing, Win32 Message Loop, DPI Virtualization)            |
+-----------------------------------------------------------------------------------+
       |                                       |                               |
       v                                       v                               v
+------------------+                 +--------------------+         +--------------------+
|   TitleHeader    |                 |    ActivityRail    |         |    DrawerPanel     |
| (Nav, Menu, Pill,|                 | (52px Icon Rail,   |         | (Slide-out Tree,   |
|  Actions, Capts) |                 |  Clusters, State)  |         |  AI Chat, History) |
+------------------+                 +--------------------+         +--------------------+
       |                                       |                               |
       +---------------------------------------+-------------------------------+
                                       |
                                       v
                     +-----------------------------------+
                     |      Direct2D / DirectWrite       |
                     |         Graphics Pipeline         |
                     +-----------------------------------+
                                       ^
                                       |
       +-------------------------------+-------------------------------+
       |                               |                               |
+------------------+         +--------------------+         +--------------------+
|   EditorCanvas   |         | FloatingPromptBar  |         | WorkspaceManager   |
| (Piece Buffer,   |         | (AI Input Pill,    |         | (File Tree, VFS,   |
|  Syntax Colors)  |         |  Model Dropdown)   |         |  File Watcher)     |
+------------------+         +--------------------+         +--------------------+
       |                               |                               |
       v                               v                               v
+------------------+         +--------------------+         +--------------------+
|  Buffer Engine   |         |     AI Streamer    |         |    LSP Client      |
| (Lines, Keystrokes)        |  (HTTP/SSE Async)  |         | (Clangd, Inteleph) |
+------------------+         +--------------------+         +--------------------+
```

---

## 2. Rendering & Graphics Pipeline (Direct2D + DirectWrite)

To deliver instant startup, crisp typography at all DPI scaling levels (100% to 250%), and zero-latency input response, **phide** renders via **Direct2D (ID2D1RenderTarget / ID2D1HwndRenderTarget)** and **DirectWrite (IDWriteFactory)**:

1. **Hardware Acceleration**: GPU rasterization of rounded pills, vector SVG/path icons, and shadows via Direct2D geometry sinks.
2. **Sub-pixel Text Antialiasing**: DirectWrite text formats (`Segoe UI`, `Inter`, `JetBrains Mono`) rendered with ClearType/sub-pixel smoothing.
3. **Dirty-Rect Invalidation**: The window repaints only dirty regions during caret blinking or text insertion to conserve battery and CPU resources.

---

## 3. Frameless Window Non-Client Handling (`WM_NCHITTEST`)

Modern IDEs avoid dated operating system title bars by drawing custom chrome across the client area while enabling native Windows 11 dark mode caption controls:

```
+-----------------------------------------------------------------------------------+
| File Edit View Project Help            phide — C++ & PHP IDE              [_][□][✕]|
| <--- HTCLIENT (Menus) --->             <-- HTCAPTION (Drag) -->          <-Native->|
+-----------------------------------------------------------------------------------+
| R (48)|                                                                           |
| A     |                                                                           |
| I     |                       Clean Dark Workspace                                |
| L     |                            (#1E1E1E)                                      |
|       |                                                                           |
+-----------------------------------------------------------------------------------+
```

- When the cursor is on the top-left menu items, `WindowProc` returns `HTCLIENT` so menus receive clicks.
- When the cursor is on neutral header space, it returns `HTCAPTION`, allowing the user to click and drag the entire window.
- When the cursor is in the top-right ~138px margin, it delegates directly to `DefWindowProc`, enabling native Windows 11 Dark Caption buttons with Snap Layouts flyout on maximize without duplicate buttons.
- When the cursor is on the window edges (6px boundary), it returns `HTLEFT`, `HTRIGHT`, `HTTOP`, `HTBOTTOM`, etc., for native resizing.

---

## 4. Activity Rail & Drawer State Machine

```
              +-------------------------------------+
              |      Rail: Collapsed (Width = 52px)  |
              |       Drawer: Hidden (Width = 0px)  |
              +-------------------------------------+
                                 |
           User clicks Rail Icon | (e.g. Files / Chat)
                                 v
              +-------------------------------------+
              |      Rail: Active (Width = 52px)    |
              |     Drawer: Slide-In (Width = 280px)|
              +-------------------------------------+
                                 |
       User clicks SAME icon     |     User clicks DIFFERENT icon
       (or click toggle button)  |     (e.g. Chat -> History)
                                 v                                 v
              +-----------------------+     +-------------------------------+
              | Return to Collapsed   |     | Switch drawer content tab     |
              | (Drawer animates to 0)|     | (Drawer remains open at 280px)|
              +-----------------------+     +-------------------------------+
```

---

## 5. Text Buffer & Syntax Coloring

- **Buffer Representation**: Fast line-indexed buffer with UTF-8 character metrics, line length arrays, and instantaneous line lookup.
- **Syntax Highlighting**:
  - Token scanner identifying language keywords (`function`, `class`, `public`, `return`, `namespace`, `echo`, `new`, `std::vector`, `auto`, etc.), strings, comments, and numbers.
  - Formatted via DirectWrite text layouts with syntax color brushes.

---

## 6. Language Server Protocol (LSP) Engine

- **Transport**: JSON-RPC 2.0 messages over standard I/O pipes to language server executables:
  - C++: `clangd`
  - PHP: `intelephense` or `phpactor`
- **Asynchronous Execution**: Background worker threads read incoming LSP notifications (`textDocument/publishDiagnostics`, completion items) and post events to the UI thread via `PostMessageW(WM_APP_LSP_EVENT)`.
