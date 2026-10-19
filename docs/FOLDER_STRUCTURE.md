# Project Folder Structure & Architecture Reference

This document provides a comprehensive guide to the repository layout for **phide** (Modern Lightweight C++ IDE). Every directory, module, and file follows strict clean-architecture separation of concerns.

---

## Directory Tree

```
phide/
├── .gitignore                      # Git ignore patterns for MSVC, build artifacts, and cache
├── README.md                       # Project overview, feature breakdown, and quickstart guide
├── docs/                           # High-level architecture and design documentation
│   ├── ARCHITECTURE.md             # Subsystem architecture, message passing, and data models
│   ├── FOLDER_STRUCTURE.md         # Directory and component mapping reference (this file)
│   └── UI_SPECIFICATION.md         # Precise UI specs matching the header and collapsed rail mockup
├── include/                        # Public C++20 interface headers
│   └── phide/
│       ├── app/
│       │   ├── AppConfig.hpp       # Global application configuration, DPI, and window states
│       │   └── Window.hpp          # Frameless window chrome, message routing, and Direct2D renderer
│       ├── ui/
│       │   ├── Theme.hpp           # Color palettes (Light & Dark), fonts, and drawing metrics
│       │   ├── TitleHeader.hpp     # Frameless header bar (Nav, Menu, Title Pill, Actions, Min/Max/Close)
│       │   ├── ActivityRail.hpp    # 52px Collapsed sidebar rail with top/bottom icon clusters
│       │   ├── DrawerPanel.hpp     # Slide-out animated sidebar panel (Files, AI, Git, Symbols)
│       │   ├── EditorCanvas.hpp    # Main workspace code editor (tabs, line numbers, cursor, selection)
│       │   └── FloatingPromptBar.hpp # Floating AI prompt pill (+ Ask AI, model selector, mic, submit)
│       ├── editor/
│       │   ├── Buffer.hpp          # Text buffer management, line indexing, and editing operations
│       │   └── SyntaxToken.hpp     # Token definitions and keyword highlighters (PHP, C++)
│       ├── lsp/
│       │   └── LspClient.hpp       # Language Server Protocol client interface (JSON-RPC)
│       └── workspace/
│           └── WorkspaceManager.hpp # Project file explorer, tree items, and file watcher interface
├── src/                            # Implementation files
│   ├── main.cpp                    # Application entry point (wWinMain), DPI bootstrap & loop
│   ├── app/
│   │   └── Window.cpp              # Win32 window procedure, DWM hit-testing, and paint pipeline
│   ├── ui/
│   │   ├── Theme.cpp               # Theme palette instantiation and GDI/D2D brush initializers
│   │   ├── TitleHeader.cpp         # Custom title bar drawing and hit-testing logic
│   │   ├── ActivityRail.cpp        # Rail icon drawing, hover state handling, and click routing
│   │   ├── DrawerPanel.cpp         # File tree and drawer contents rendering with expand/collapse logic
│   │   ├── EditorCanvas.cpp        # Code rendering, syntax coloring, line numbers, and caret blinking
│   │   └── FloatingPromptBar.cpp   # Pill layout drawing, model selector, and text input management
│   ├── editor/
│   │   └── Buffer.cpp              # Text buffer implementation, UTF-8 decoding, and mutation logic
│   ├── lsp/
│   │   └── LspClient.cpp           # LSP client placeholder, server process lifecycle, and diagnostics
│   └── workspace/
│       └── WorkspaceManager.cpp    # Virtual file system scanning, active project root management
├── scripts/                        # Automation and developer workflows
│   ├── build.bat                   # Automated MSVC x64 build script using vcvars64.bat
│   └── run.bat                     # Convenience launcher to build and execute phide.exe
└── bin/                            # Output directory for compiled binaries and intermediate objects
```

---

## Module Responsibilities

### 1. `include/phide/app/` & `src/app/`
- **Responsibility**: Manages native Windows windowing, High-DPI awareness, custom non-client area hit-testing (`WM_NCHITTEST`), Desktop Window Manager (DWM) composition, and the Direct2D/DirectWrite rendering context.
- **Key Concept**: Eliminates default Windows title bars while preserving native Windows 11 snapping layouts, resize borders, and window animations.

### 2. `include/phide/ui/` & `src/ui/`
- **Responsibility**: Implements the visual components highlighted in the design specification:
  - `TitleHeader`: Implements the top bar with navigation buttons (`←`, `→`), inline menus (`File`, `Edit`, `View`, `Help`), centered document pill (`PHP IDE Concept Design`), action cluster (`Share`, layout toggle, `...`), and caption buttons.
  - `ActivityRail`: Implements the 52px left sidebar with top tools (Home, Chat, History, Files, `@`, `...`) and bottom tools (Extensions grid, User avatar).
  - `DrawerPanel`: Manages the expandable sidebar drawer (280px width) that slides out when a rail icon is clicked.
  - `EditorCanvas`: Handles the main code editing surface, tabs, line numbers, and gutter.
  - `FloatingPromptBar`: Implements the floating AI prompt pill at the bottom of the workspace.

### 3. `include/phide/editor/` & `src/editor/`
- **Responsibility**: Text storage, line indexing, character positioning, cursor navigation, and syntax tokenization.
- **Key Concept**: Fast line retrieval and syntax coloring for PHP and C++ source code.

### 4. `include/phide/lsp/` & `src/lsp/`
- **Responsibility**: Language Server Protocol (LSP) interface for language intelligence (completion, diagnostics, hover, definitions) communicating via JSON-RPC.

### 5. `include/phide/workspace/` & `src/workspace/`
- **Responsibility**: Project directory scanning, file tree representation, and file system watchers.
