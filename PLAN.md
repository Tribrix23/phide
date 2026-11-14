# Phide Developer Feature Plan & Codebase Audit (50 Core Features)

This plan inventories **50 high-value, everyday software engineering IDE features**—excluding AI/LLM integrations, chatbots, and gimmicks—audited directly against Phide's current C++20 source tree (`src/` and `include/phide/`).

> **Audit Note:** Early design sketches in `docs/` (`ARCHITECTURE.md`, `UI_SPECIFICATION.md`) predate the active implementation. All statuses below reflect actual wired behavior verified in `src/` and `include/phide/`.

---

## Executive Summary

| Category | Implemented | Partially Implemented | Not Implemented | Total |
| :--- | :---: | :---: | :---: | :---: |
| 1. Core Text Editing & Selection | 9 | 0 | 0 | 9 |
| 2. Navigation, Search & Refactoring | 9 | 0 | 0 | 9 |
| 3. Language Intelligence & Code Quality | 1 | 5 | 1 | 7 |
| 4. Debugging, Testing & Build Tasks | 2 | 3 | 2 | 7 |
| 5. Source Control (Git & Diffing) | 3 | 1 | 2 | 6 |
| 6. Terminal, Local Stack, API & DB Tooling | 4 | 2 | 0 | 6 |
| 7. Remote, Workspace, Extensibility & Performance | 2 | 2 | 2 | 6 |
| **Total** | **30** | **13** | **7** | **50** |

**Legend**
- **Implemented** — Core workflow exists end-to-end in UI and backend code.
- **Partially Implemented** — Working foundation exists, but important developer sub-features or edge cases remain unfinished or stubbed.
- **Not Implemented** — Missing from the codebase, or exists only as an empty UI/menu stub.

---

## 1. Core Text Editing & Selection (1–9)

| # | Feature | Status | Current Codebase State | Missing / Next Work |
| :---: | :--- | :--- | :--- | :--- |
| 1 | **Multi-Cursor Editing (`Ctrl+D`, `Alt+Click`, Column Cursors)** | **Implemented** | `OpenDocument` & `Window.cpp` implement cumulative-delta single-pass multi-cursor editing (`MultiCursorTypeChar`, `MultiCursorBackspace`, `MultiCursorDeleteForward`, `MultiCursorInsertNewline`, `MultiCursorMove`), `Alt+Click` (`AddCursorAt`), `Ctrl+Alt+Up`/`Down` (`AddCursorVertical`), `Ctrl+Shift+L` / `Ctrl+F2` (`SelectAllOccurrences`), and multi-cursor clipboard cut/copy/paste & `Tab`/`Shift+Tab`. | Complete (covered by `[TEST 125]`). |
| 2 | **Line Manipulation (`Alt+Up`/`Down`, Duplicate, Delete Line)** | **Implemented** | `Buffer` & `Window.cpp` implement `Alt+Up`/`Down` (`MoveLinesUp`/`Down`), `Shift+Alt+Up`/`Down` (`DuplicateLines`), `Ctrl+Shift+K` (`DeleteLine`), `Ctrl+J` (`JoinLines`), `SortLines` (Asc/Desc), `TrimTrailingWhitespace`, pair wrapping, and multi-line indent/outdent. | Complete (covered by `[TEST 125]`). |
| 3 | **Auto-Closing Pairs, Smart Indentation & Linked Tag Editing** | **Implemented** | `OpenDocument` & `Window.cpp` handle auto-closing pairs, pair overtyping, pair backspace, and wrapping (`()`, `[]`, `{}`, `""`, `''`, `` `` ``) consistently for both single and same-line multi-cursor edits without breaking tags or bounds; `TryAutoRenameMatchingTag` safely guards single-cursor HTML edits. | Complete (covered by `[TEST 74]` & `[TEST 125]`). |
| 4 | **Line & Block Comment Toggling (`Ctrl+/`, `Shift+Alt+A`)** | **Implemented** | `Buffer::ToggleLineCommentRange` / `ToggleBlockCommentRange` and `Window::DoToggleLineComment` / `DoToggleBlockComment` toggle per-line and multi-cursor/range comments cleanly across JS, PHP, HTML, CSS, Python, SQL, and mixed PHP/HTML templates. | Complete (covered by `[TEST 16]` & `[TEST 125]`). |
| 5 | **Code Folding & Region Persistence** | **Implemented** | `EditorCanvas::GetFoldRange` & gutter chevrons support `#region` / `// #region` / `/* #region */` / `<!-- #region -->` markers, blocks, comments, and indentation; `FoldAll`, `UnfoldAll`, and `FoldLevel(1..3)` wired to `Ctrl+K Ctrl+0/J/1/2/3`, menus, and Palette; `Window::SaveSessionState` / `LoadSessionState` persist `foldedDocs`. | Complete (covered by `[TEST 125]`). |
| 6 | **Document & Selection Formatting (`Shift+Alt+F`)** | **Implemented** | `LspClient::RequestFormatting` & `RequestRangeFormatting` route `textDocument/formatting` & `textDocument/rangeFormatting` when LSP is active; `FormatEngine::FormatDocument` & `FormatRange` honor local `.prettierrc`/`clang-format`/`ruff` configs and provide native PSR-12/HTML/JS/range fallbacks. | Complete (covered by `[TEST 125]`). |
| 7 | **Box / Column (Rectangular) Selection (`Shift+Alt+Drag`)** | **Implemented** | `OpenDocument` (`isColumnSelection`, `SetColumnSelection`, `MoveColumnSelection`), `EditorCanvas` (`HitTestCodeArea(..., clampToLineEnd=false)`, rectangular highlight & virtual carets), and `Window.cpp` (`Shift+Alt+Drag` mouse & `Shift+Alt+Arrow` keyboard) support virtual columns and rectangular copy/cut/paste/typing. | Complete (covered by `[TEST 125]`). |
| 8 | **Encoding & Line Ending Switcher UI (`UTF-8`, `CRLF`/`LF`)** | **Implemented** | `Buffer` (`TextEncoding`: `UTF-8`, `UTF-8 with BOM`, `UTF-16 LE`, `UTF-16 BE`, `Windows-1252`), `ConvertLineEnding`, `ConvertEncoding`, and `ReopenWithEncoding` roundtrip files on disk; status bar displays active encoding/EOL and `PopupMenu::OpenEncodingMenu` / `OpenIndentationMenu` switch encoding, EOL, spaces/tabs, and tab size. | Complete (covered by `[TEST 125]`). |
| 9 | **Bracket Pair Matching, Scope Guides & Jump to Bracket** | **Implemented** | `EditorCanvas` renders active rainbow brackets, vertical indentation scope guides, and matching HTML/PHP open-close delimiter highlights; `EditorCanvas::FindMatchingBracket` & `Window::DoJumpToMatchingBracket` (`Ctrl+Shift+\`) jump between brackets and HTML/PHP tags. | Complete (covered by `[TEST 125]`). |

---

## 2. Navigation, Search & Refactoring (10–18)

| # | Feature | Status | Current Codebase State | Missing / Next Work |
| :---: | :--- | :--- | :--- | :--- |
| 10 | **Multi-Pane 2D Split Editor & Drag-Drop Tab Snapping** | **Implemented** | `EditorSplitLayout` (`OpenDocument.hpp`) supports up to 4 panes (`TwoColumns`, `Grid2x2`, etc.) with draggable 2D splitters, tab scrolling, `SnapBarSlot` drop zones (`Ctrl+\`), plus keyboard focus-cycling shortcuts (`Ctrl+1..4`, `Ctrl+K Ctrl+Left/Right`, Window menu). | Complete (covered by `[TEST 126]`). |
| 11 | **Inline Per-Editor Find & Replace Bar (`Ctrl+F`, `Ctrl+H`)** | **Implemented** | Floating Direct2D `InlineFindBar` (`EditorOverlays.cpp` & `Window.cpp`) with live match highlighting in `RenderLines` & overview ruler, `F3`/`Shift+F3` stepping, Regex/Case/Word/Find-in-Selection/Preserve-Case toggles, single & multi-match replacement with Undo. | Complete (covered by `[TEST 126]`). |
| 12 | **Workspace-Wide Search & Replace (`Ctrl+Shift+F`, `Ctrl+Shift+H`)** | **Implemented** | `DrawerPanel` threaded search/replace with Regex/Case/Word/OpenEditors/`IgnoreEngine` filters, live search & replace on unsaved dirty open buffers (`SetOpenDocSnapshots`, `SetOnApplyOpenDocReplace`), `Preserve Case` replacement (`ApplyPreserveCase`), and Replace All diff-preview confirmation (`m_replaceAllConfirmPending`). | Complete (covered by `[TEST 126]`). |
| 13 | **Quick Open (`Ctrl+P`), Command Palette (`Ctrl+Shift+P`) & Go to Line (`:line`)** | **Implemented** | `QuickOpenModal` supports fuzzy-ranked (`ScoreFuzzyMatch`) open/recent/workspace files (up to 1000, skipping `.phide`), `:line:col` navigation (`ParseLineColSpec`), `>` command palette, `@` document symbols, `#` workspace symbols (`WorkspaceSymbolIndex`), and real `task ` / `debug ` providers (`DiscoverWorkspaceTasks`, `DiscoverLaunchConfigurations`). | Complete (covered by `[TEST 126]`). |
| 14 | **Go to Definition (`F12`) & Inline Peek Definition** | **Implemented** | `Window::DoGoToDefinition` (`F12`), `DoPeekDefinition` (`Alt+F12`), `DoTypeDefinition`, and `DoImplementation` (`Ctrl+F12`) combine LSP requests (`RequestDefinitionLocations`/`TypeDefinition`/`Implementation`) with async `WorkspaceSymbolIndex` (up to 1800 files) and inline Peek widget preview. | Complete (covered by `[TEST 126]`). |
| 15 | **Document Symbol Outline Tree & Interactive Breadcrumbs (`@`)** | **Implemented** | Interactive Breadcrumbs dropdowns (`HandleBreadcrumbClick`) for sibling files/folders & document symbols, collapsible Explorer Outline tree (`DrawerPanel::HitTestOutline`/`ScrollOutline`), QuickOpen `@` outline (`ExtractDocumentSymbols`), and `LspClient::RequestDocumentSymbols`. | Complete (covered by `[TEST 126]`). |
| 16 | **Find All References (`Shift+F12`), Call Hierarchy & Code Metrics** | **Implemented** | `DoFindReferences` (`Shift+F12` + `RequestReferences`/`FindWorkspaceReferences`), `DoCallHierarchy` (`PrepareCallHierarchy`/`Incoming`/`Outgoing` + `BuildStaticCallHierarchy`), and `DoCodeMetrics` (`ComputeFileMetrics`/`ComputeWorkspaceMetrics`) wired to `MoreCallHierarchy`/`MoreCodeHierarchy`/`MoreTraceCode`/`MoreCodeMetrics`/`MoreDependencyGraph` and rendered in interactive Peek panel. | Complete (covered by `[TEST 126]`). |
| 17 | **Cross-File Symbol Rename (`F2`)** | **Implemented** | `LspClient::PrepareRename` + `RequestRename` and `BuildStaticRenamePlan` paired with interactive floating `InlineRenameWidget` (diff preview + apply) updating dirty open buffers across groups (`Undo`-safe) and closed files on disk (`ApplyCrossFileRename`). | Complete (covered by `[TEST 126]`). |
| 18 | **Cursor Navigation History (`Alt+Left`/`Right`) & Line Bookmarks** | **Implemented** | Back/forward `NavCheckpoint` stacks (`Alt+Left`/`Alt+Right`, definitions, search jumps, tab switches) and toggleable/persistent `OpenDocument::bookmarks` (`Ctrl+Alt+K`/`N`/`P`, `Alt+Click` gutter, cyan gutter ribbon, `.phide/bookmarks.json` with automatic `IgnoreEngine::EnsurePhideIgnoredForPath`). | Complete (covered by `[TEST 126]`). |

---

## 3. Language Intelligence & Code Quality (19–25)

| # | Feature | Status | Current Codebase State | Missing / Next Work |
| :---: | :--- | :--- | :--- | :--- |
| 19 | **Language Server Protocol (LSP) Client & Multi-Server Lifecycle** | **Partially Implemented** | `LspClient` spawns and communicates over JSON-RPC stdio pipes (auto-discovering `intelephense`, `phpactor`, `pyright-langserver`, `pylsp`, `clangd`, `typescript-language-server`) for `initialize`, `didOpen`/`didChange` (full text)/`didSave`/`didClose`, `completion`, `definition`, and `publishDiagnostics`. | Support concurrent language servers per workspace (`EnsureServerForLanguage` currently shuts down and switches a single active server), incremental document sync, `hover`, `signatureHelp`, `references`, `rename`, `documentSymbol`, `codeAction`, and custom server paths. |
| 20 | **Built-in IntelliSense, Snippets, Emmet & Live `.env`/DB Schema Completion** | **Implemented** | `IntelliSenseEngine` combines `WebIntelliSenseData`, `PhpIntelliSenseData`, `PythonIntelliSenseData`, `ReactIntelliSenseData`, `SnippetManager`, `.env` keys (`RefreshWorkspaceEnvVars`), MySQL schema (`RefreshDatabaseSchemaAsync`), `TailwindHelper`, and `LspClient` results. | Continue expanding static TypeScript/framework definitions when LSP is absent. |
| 21 | **Auto-Import & Import Organization (`Organize Imports`)** | **Partially Implemented** | `SmartImportHelper` inserts relative imports on Explorer drag-drop and updates imports on Explorer file rename (`ExecuteRenameAndUpdateImports`); `Window::DoOrganizeImports` alphabetically sorts a leading `import`/`#include`/`require` block. | Add unused import removal, grouped stdlib/vendor/relative sorting (ESM, Python `from ...`, PHP `use ...`), and auto-import insertion for unresolved symbols. |
| 22 | **Rich Hover Documentation & Signature Parameter Help** | **Partially Implemented** | `EditorCanvas` displays diagnostic hover popovers and CSS Peek definition popovers (`Window::UpdateCssPeekHover`). | Implement `textDocument/hover` symbol type/doc cards, `textDocument/signatureHelp` active parameter hints (`Ctrl+Shift+Space`), and formatted Markdown/code rendering inside tooltips. |
| 23 | **Inline Diagnostics & Filterable Problems Panel (`Ctrl+Shift+M`)** | **Partially Implemented** | `SyntaxValidator::Validate`, `LspClient` diagnostics, and terminal tracebacks feed `OpenDocument::diagnostics`, drawn as squiggly underlines (`EditorCanvas`), gutter dots, minimap marks, breadcrumb badge, and Explorer error badges (`UpdateDrawerErrorFiles`). | Build a dedicated bottom **Problems Panel** (`Errors`/`Warnings`/`Info` filters, workspace search, click-to-jump, `F8`/`Shift+F8` next/prev diagnostic) and external linter output aggregation. |
| 24 | **Code Actions (`Ctrl+.`) & Structural Refactoring (`Ctrl+Shift+R`)** | **Not Implemented** | `Window::DoRefactor` displays `L"No refactorings available"`; `LspClient` has no `textDocument/codeAction`. | Support lightbulb gutter indicator, `Ctrl+.` Quick Fix menu (LSP code actions + local fixes like add missing import/extract variable/method), and `WorkspaceEdit` execution. |
| 25 | **CodeLens, Color Picker, Interactive TODOs & Inlay Hints** | **Partially Implemented** | `EditorCanvas` renders clickable `Run \| Debug` CodeLens (`supportsCodeLens`), inline GitLens blame (`EnsureGitBlameLoaded`), inline color swatches -> `ColorPickerModal`, clickable `[ ]` TODO checkboxes, `importCostMap`, and rendering loop for `OpenDocument::inlayHints`. | Populate `doc.inlayHints` via LSP `textDocument/inlayHint` (currently nothing adds hints to `doc.inlayHints`) and support clickable reference-count / test-run CodeLens actions. |

---

## 4. Debugging, Testing & Build Tasks (26–32)

| # | Feature | Status | Current Codebase State | Missing / Next Work |
| :---: | :--- | :--- | :--- | :--- |
| 26 | **Interactive Step Debugger & Breakpoint Gutter (`F5`, `F9`, `F10`, `F11`)** | **Partially Implemented** | `TerminalPanel` (`SelectDebugBackendForFile`, `BuildBreakpointCommands`, `ParseDebugPausedLine`) drives `pdb` (`.py`/`.pyw`) and `node inspect` (`.js`/`.mjs`/`.cjs`/`.ts`) over ConPTY, syncing gutter `OpenDocument::breakpoints`, `debugPausedLine`, and Continue/Step/Stop commands. | Replace raw CLI prompt parsing with a **Debug Adapter Protocol (DAP)** client supporting `launch.json`, C/C++ (`cppdbg`/LLDB/GDB), PHP (`Xdebug`), conditional breakpoints, hit counts, and logpoints. |
| 27 | **Debugger Variables, Watch, Call Stack & Loaded Scripts UI** | **Not Implemented** | Debugging is strictly stdin/stdout CLI interaction inside `TerminalPanel`; no visual inspector panels exist. | Build structured Variables (Locals/Globals/Closure expandable trees), Watch expressions, Call Stack frame switcher, and Exception Breakpoint checkboxes. |
| 28 | **Integrated Test Explorer & Gutter Test Status** | **Not Implemented** | `MenuActionId::ToolsRunTests` simply writes `cmd.exe /c scripts\run_test.bat\r\n` to the terminal. | Add test discovery and live pass/fail/duration UI + gutter icons for Pytest/unittest, Jest/Vitest, PHPUnit, and CTest. |
| 29 | **Configurable Build & Project Task Runner (`tasks.json` / Script Discovery)** | **Partially Implemented** | `MenuActionId::ToolsBuildProject` runs `scripts\build.bat`; `Window` detects `ProjectFramework` to run framework servers; `TitleHeader` hosts Run/Restart/Stop buttons. | Parse `.vscode/tasks.json`, `package.json` `scripts`, `composer.json` `scripts`, `Makefile`, and CMake targets into a runnable Task Picker with pre-launch dependencies. |
| 30 | **Terminal Problem Matchers & Clickable Compiler Error Links** | **Partially Implemented** | `TerminalPanel` intercepts Python/Node/PHP runtime tracebacks (`Window` `onExecutionError`) to highlight matching open editor lines, and supports `Ctrl+Click` on HTTP URLs. | Parse `file:line:col` compiler/linter errors (`MSVC`, `clang`, `gcc`, `tsc`, `eslint`, `phpstan`) as clickable terminal links that open files directly at the column and populate Problems diagnostics. |
| 31 | **Stateful Jupyter Notebook Runner (`.ipynb`)** | **Implemented** | `NotebookView` + `NotebookKernel` render Markdown/Code cells, persist `.ipynb` structure, and execute cells against a live background Python kernel with stdout/stderr/output rendering. | Optional polish: kernel selector across detected Python venvs and inline image output rendering. |
| 32 | **Built-in Side-by-Side Live Markdown Preview** | **Implemented** | `EditorCanvas::RenderMarkdownPreview` and `Window::OpenMarkdownPreviewSplit` (`AppConfig::enableMarkdownPreview`) auto-sync preview panes beside `.md` editors. | Optional polish: synchronized scroll position and GFM task-list/table refinements. |

---

## 5. Source Control (Git & Diffing) (33–38)

| # | Feature | Status | Current Codebase State | Missing / Next Work |
| :---: | :--- | :--- | :--- | :--- |
| 33 | **Git Working Tree, Staging, Commits, Sync & Stash** | **Partially Implemented** | `DrawerPanel` handles async stage/unstage/discard (single & all), commit, uncommit last commit, push/pull/fetch, `StashChangesAsync`, status bar branch switcher (`GetGitBranches`/`CheckoutGitBranch`), and `SemanticCommitEngine`. | Add Stash List inspector (`apply`, `pop`, `drop`), partial hunk/line staging and revert from diffs, amend commit option, create/delete branch/tag dialogs, rebase, and cherry-pick. |
| 34 | **Side-by-Side Diff Viewer & Multi-File Commit Diff Inspector** | **Implemented** | `Window::OpenDiffDocument`, `OpenCommitDiffDocument`, `ComputeDiffHunks`, and `EditorCanvas::RenderDiffView`/`RenderMultiFileDiffView` display aligned side-by-side diffs with live edit hunk recomputation. | Optional polish: inline (unified) diff toggle and next/prev diff change (`F7`/`Shift+F7`) navigation shortcuts. |
| 35 | **3-Way Git Merge Conflict Resolver UI** | **Not Implemented** | No parser, CodeLens (`Accept Current` / `Incoming` / `Both`), or 3-way conflict view exists for `<<<<<<<` / `=======` / `>>>>>>>` markers. | Detect unmerged Git status (`UU`/`AA`), highlight conflict blocks inline with one-click accept actions, and add an optional 3-pane merge editor. |
| 36 | **Visual Git Commit Graph & Commit File Browser** | **Implemented** | `DrawerPanel::ComputeGitGraphLanes` and `LoadCommitFiles` render colored branch/merge lane graphs and expand commits to view changed files and commit diffs. | Optional polish: branch/author filter search box and graph search. |
| 37 | **Ad-Hoc Compare Two Files (`Compare With Selected`)** | **Not Implemented** | `MenuActionId::CtxCompareWithSelected` in `Window.cpp` displays a `MessageBoxW` with both file paths instead of opening a diff editor. | Wire `CtxCompareWithSelected` directly to `OpenDiffDocument` / `RenderDiffView` for arbitrary workspace file comparisons. |
| 38 | **Pre-Commit Secret/Bloat Guard & Context `.gitignore` Generator** | **Implemented** | `Window::RunPreCommitSecretAndBloatCheck` (`PreCommitGuardModalState`) inspects staged payloads before commit; `IgnoreEngine` evaluates ignore files; `DrawerPanel::GenerateContextAwareGitignore` creates project ignore rules. | Keep secret signature patterns extensible via user/plugin config. |

---

## 6. Terminal, Local Stack, API & DB Tooling (39–44)

| # | Feature | Status | Current Codebase State | Missing / Next Work |
| :---: | :--- | :--- | :--- | :--- |
| 39 | **Multi-Tab & Split Integrated ConPTY Terminal** | **Implemented** | `TerminalPanel` provides multi-tab and split ConPTY/pipe terminals (`PowerShell`, `Cmd`, `GitBash`, `Node`, `Python`, `LiveServer`, `DebugConsole`, `Custom`), ANSI color spans, clipboard selection, and bottom/right/editor/fullscreen layouts. | Minor polish: configurable scrollback limit, find-in-terminal (`Ctrl+F` inside terminal), and named profile persistence. |
| 40 | **Native HTTP / GraphQL / SSE API Tester (`ApiTesterView`)** | **Implemented** | `ApiTesterView` executes WinHTTP requests (`GET`/`POST`/`PUT`/`PATCH`/`DELETE`/`HEAD`/`OPTIONS`), Params, Bearer/API Key/Basic auth, headers with autocomplete, JSON/Form/GraphQL bodies, SSE streaming, timings, and cURL import (`TryImportCurlCommand`). | Optional polish: saved request collections synced with `.http`/OpenAPI files and environment variable sets (`{{baseUrl}}`). |
| 41 | **Local Live Server & PHP/Nginx/MySQL Stack Orchestration** | **Partially Implemented** | `LiveServer` serves static workspace files via WinSock HTTP/1.1; `Window` manages bundled PHP/Nginx/MySQL/phpMyAdmin processes (`PortConflictModalState` detects/kills blocking processes); `phide_prepend.php`/`phide_append.php` inject `__phide_mtime` polling auto-reload for PHP/Nginx HTML responses. | Add WebSocket/SSE live-reload script injection inside static `LiveServer.cpp` (currently static HTTP-only) and per-project virtual host/PHP version presets. |
| 42 | **Integrated SQL Query Console & Database Explorer** | **Partially Implemented** | `Window::RefreshDatabaseSchemaAsync` reads live MySQL schema for IntelliSense; `DatabaseSettingsModal` manages MySQL/phpMyAdmin ports, `.sql` imports, PDO/MySQLi snippet copy, and launches phpMyAdmin externally. | Build an in-IDE SQL query tab, paginated result grid, table/column tree browser, and SQLite/PostgreSQL connectivity. |
| 43 | **Runtime Switcher & Package Manager (`pip`, `composer`, `npm`)** | **Implemented** | `ExtensionsPanel` detects/switches Python venvs and PHP/Node runtimes with install/uninstall workflows; `PackageManagerView` searches/installs packages and previews docs via WebView2. | Optional polish: outdated-package badge and one-click `pip`/`npm`/`composer` upgrade actions. |
| 44 | **Dependency Topology Graph & Live Resource Monitor** | **Implemented** | `GraphView` renders a zoomable/pannable force-directed workspace graph persisted in `.phide/graph_layout.json`; `EditorCanvas::RenderMonitorView` (`QueryActiveDevPorts`, `UpdateProjectStorageTelemetry`) reports active dev ports, disk footprint, and CPU/RAM/GPU metrics. | Optional polish: one-click kill-port action inside Monitor view. |

---

## 7. Remote, Workspace, Extensibility & Performance (45–50)

| # | Feature | Status | Current Codebase State | Missing / Next Work |
| :---: | :--- | :--- | :--- | :--- |
| 45 | **Remote Development (SSH, WSL & Docker Dev Containers)** | **Partially Implemented** | `RemoteWindowModal` + `DrawerPanel` Remote Explorer + `Window::ConnectWslDistro` discover `~/.ssh/config` hosts, WSL distros (`\\wsl$\` workspace mount), and Docker Dev Containers with SSH directory browse, SCP transfer, and remote terminal tabs. | Move beyond SCP sync + local LSP to transparent remote VFS caching and running remote LSP/debug/Git processes inside SSH/container targets. |
| 46 | **File System Watcher, External Conflict Detection & Explorer Undo/Redo** | **Implemented** | `Window::StartWorkspaceWatcher` (`ReadDirectoryChangesW`) + `CheckOpenDocumentsDiskChanges` surface `hasExternalDiskConflict`; `ExplorerUndoManager` supports `Ctrl+Z`/`Ctrl+Y` for file/folder create/delete/rename/move; Explorer file drag generates imports via `SmartImportHelper`. | Add a 2-button reload/diff banner directly above editors with `hasExternalDiskConflict`. |
| 47 | **Crash-Safe Hot Exit & Session State Recovery** | **Implemented** | `Window::SaveSessionState`/`LoadSessionState` restore folder, split panes, active tabs, and layout flags; `SaveDirtyBuffersRecovery`/`RestoreDirtyBuffersRecovery` recover unsaved buffers and untitled tabs across crashes/restarts. | Ensure atomic temp-file + rename writes when saving recovery files under heavy I/O. |
| 48 | **`.editorconfig` Enforcement & Multi-Root Workspaces** | **Not Implemented** | `Theme.cpp` maps an icon for `.editorconfig`, but no parser applies directory `indent_style`/`indent_size`/`end_of_line`/`trim_trailing_whitespace`; `DrawerPanel` and `Window` assume a single workspace root (`m_openFolderPath`). | Implement `.editorconfig` hierarchical resolution per saved/opened file and `.phide-workspace` multi-root Explorer/Search/Git aggregation. |
| 49 | **Plugin ABI, Theme Picker & Editable Keybindings** | **Partially Implemented** | `PluginManager` loads native C ABI `.dll`s (`PhidePluginABI.h`) and JSON mods (commands, `TryHandleKeyBinding`, snippets, completions, formatters, linters, status bar items); `ThemePickerModal` offers 13 themes; `SettingsView` saves app settings. | Make `KeyboardShortcutsView` interactive/editable (`user_keybindings.json` chord/conflict detection instead of read-only lookup), add `.phide/settings.json` per-workspace overrides, and add a GUI snippet editor. |
| 50 | **Large-File Virtualized Buffer & Memory-Mapped Viewing (`>50 MB`)** | **Not Implemented** | `Buffer` (`Buffer.hpp`/`Buffer.cpp`) loads full files into `std::vector<std::wstring>`, decodes all UTF-8 upfront, and pushes full `m_lines` copies onto `m_undoStack` (up to 200 snapshots). | Migrate `Buffer` undo storage from full-document vector snapshots to delta/operation records and add read-only memory-mapped viewing (or Piece Table / Rope chunks) for multi-megabyte logs and bundles. |

---

## Recommended Implementation Roadmap

### Phase P0 — Immediate Quick Wins (Stubs Already Wired in UI)
1. **Wire `CtxCompareWithSelected`** (`Window.cpp`) directly to `OpenDiffDocument` / `RenderDiffView` (#37).
2. **Add Line Move/Duplicate & Jump-to-Matching-Bracket** (`Alt+Up`/`Down`, `Shift+Alt+Down`, `Ctrl+Shift+\`) in `Window.cpp` + `Buffer` (#2, #9).
3. **Make Status Bar EOL/Indentation/Ln:Col Pills Interactive** to switch `LF`/`CRLF` and tab settings (#8).
4. **Add Inline Per-Editor Find/Replace Bar** (`Ctrl+F`/`Ctrl+H`) cleanly separated from sidebar Search (#11).

### Phase P1 — High-Impact Editing & Language Server Parity
5. **Upgrade `LspClient` to Full Core LSP** (`hover`, `signatureHelp`, `references`, `rename`, `documentSymbol`, `codeAction`, `inlayHint`, incremental sync, and concurrent per-language servers) (#14, #15, #16, #17, #19, #22, #24, #25).
6. **Complete Multi-Cursor (`Alt+Click`, `Ctrl+Alt+Up`/`Down`, `Ctrl+Shift+L`) & Range Formatting** (#1, #6).
7. **Add Workspace Problems Panel (`Ctrl+Shift+M`) & Terminal Problem Matchers** (#23, #30).

### Phase P2 — Debugging, Testing & Git Completeness
8. **3-Way Git Merge Conflict Inline Actions + Stash/Hunk Staging** (#33, #35).
9. **DAP Debugger Backend + Variables / Watch / Call Stack Inspector Panels** (#26, #27).
10. **Project Task & Unit Test Runner UI (`package.json`/`composer.json`/Pytest/PHPUnit/Jest)** (#28, #29).

### Phase P3 — Architecture, Workspaces & Scale
11. **Delta Undo + Piece-Table/Memory-Mapped Large-File Mode** (#50).
12. **`.editorconfig` + Multi-Root Workspaces + Editable Keybindings + Embedded SQL Console + Static Live-Reload** (#41, #42, #48, #49).
