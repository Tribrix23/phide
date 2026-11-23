/**
 * @file AppConfig.hpp
 * @brief Global configuration constants, UI geometry parameters, and application state.
 * 
 * This header defines all geometric dimensions, layout metrics, and persistent runtime
 * state variables for phide. Metrics are specified in logical pixels and scaled dynamically
 * according to the active system High-DPI ratio.
 */

#pragma once

#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>

namespace phide::app {

/**
 * @struct AppConfig
 * @brief Stores runtime layout metrics, operational flags, and UI states.
 */
struct AppConfig {
    // --- Window Sizing ---
    int initialWidth  = 1280;   ///< Initial window width in logical pixels.
    int initialHeight = 800;    ///< Initial window height in logical pixels.
    int minWidth      = 900;    ///< Minimum allowable window width before layout clipping.
    int minHeight     = 550;    ///< Minimum allowable window height.

    // --- Header Metrics ---
    int headerHeight  = 42;     ///< Height of the integrated frameless title bar.
    int captionBtnWidth = 46;   ///< Width of minimize/maximize/close caption buttons.

    // --- Activity Rail (Left Sidebar) Metrics ---
    int railWidth     = 52;     ///< Fixed width of the collapsed activity rail (matching mockup).
    int railIconSize  = 20;     ///< Size of the vector glyphs within rail buttons.
    int railBtnSize   = 36;     ///< Width and height of the rail button bounding box.

    // --- Drawer Panel Metrics ---
    int drawerWidth   = 330;    ///< Width of the slide-out drawer panel when expanded (Default: 330 logical px for generous label breathing room).
    int rightSidebarWidth = 340; ///< Width of the right sidebar (Runtimes / Extensions panel) when expanded.
    bool isDrawerOpen = false;  ///< True if the contextual sidebar drawer is expanded; false if collapsed.
    int activeRailTab = 0;      ///< Current active rail tab (Default: 0 = Files / Explorer matching Image 2).
    float gitSplitY   = 320.0f; ///< Vertical splitter position between Changes and Graph.

    // --- Terminal Layout & State ---
    int terminalDockPosition = 0;       ///< 0 = Bottom (Panel), 1 = Editor Area, 2 = Right Side
    bool isTerminalOpen = false;        ///< True if terminal is open
    float terminalHeight = 360.0f;      ///< Terminal height when docked at bottom
    float terminalRightWidth = 480.0f;  ///< Terminal width when docked at right side

    // --- View and Appearance Settings ---
    bool showMinimap = true;            ///< True if code minimap is rendered on right of editor canvas.
    bool showBreadcrumbs = true;        ///< True if file breadcrumbs hierarchy is visible above editor.
    bool showLineNumbers = true;        ///< True if line numbers gutter is rendered. Default enabled.
    bool showStatusBar = true;          ///< True if bottom status bar is rendered. Default enabled.
    bool enableMarkdownPreview = true;  ///< True if markdown preview split pane is automatically opened when opening a markdown file. Default enabled.
    bool enableCodeLens = true;         ///< True if CodeLens Run/Debug actions are rendered above functions. Default enabled.
    bool enableGitLens = true;          ///< True if GitLens inline blame and authorship annotations are rendered. Default enabled.
    bool wordWrap = false;              ///< True if word wrap is enabled in editor.
    bool renderWhitespace = false;      ///< True if whitespace dots are rendered.
    bool zenMode = false;               ///< True if Zen Mode (distraction-free full screen) is active.
    bool centeredLayout = false;        ///< True if editor canvas is centered horizontally.
    bool showMenuBar = true;            ///< True if top menu bar items are visible.
    bool sideBarOnRight = false;        ///< True if primary activity rail & drawer are moved to the right.
    bool enableStickyScroll = true;     ///< True if sticky scroll header is enabled.
    bool enablePrivacyMask = false;     ///< True to mask .env values and secret literals on screen.
    bool renderControlChars = false;    ///< True if control characters are rendered.

    struct ZenPreState {
        bool isDrawerOpen = false;
        bool showStatusBar = true;
        bool centeredLayout = false;
        bool isZoomed = false;
    } zenPreState;
    bool isGraphColored = false;        ///< True if file network graph nodes are colored by file type (Default false per user prompt).
    float fontSize = 15.0f;             ///< Editor font size in points (Default: 15px).
    float lineHeightMultiplier = 1.45f; ///< Line height relative to font size.
    float zoomLevel = 1.0f;             ///< Zoom multiplier (1.0 = 100%).

    // --- Extended Editor & Workbench Settings ---
    std::wstring fontFamily = L"Koda Mono";            ///< Editor font family name (Default: Koda Mono).
    int tabSize = 4;                                    ///< Number of spaces per tab.
    bool insertSpaces = true;                           ///< Insert spaces when pressing Tab.
    std::wstring autoSave = L"afterDelay";              ///< "off", "afterDelay", "onFocusChange", "onWindowChange"
    int autoSaveDelay = 600;                            ///< Delay in ms for autosave afterDelay.
    std::wstring cursorStyle = L"line";                 ///< "line", "block", "underline"
    std::wstring renderWhitespaceMode = L"selection";   ///< "none", "boundary", "selection", "trailing", "all"
    bool formatOnSave = true;                           ///< Automatically format document on save (Shift+Alt+F). Default enabled.
    bool prettierOnSave = false;                        ///< Automatically run Prettier asynchronously on save. Disabled by default.
    bool updateImportsOnRename = true;                  ///< Automatically update imports across workspace files when renaming or moving. Default enabled.

    // --- Code Runner Extension Settings ---
    std::wstring codeRunnerExecutorPython = L"python -u"; ///< Executor for Python files
    bool codeRunnerRunInTerminal = true;                  ///< Run code in integrated terminal
    bool codeRunnerClearPreviousOutput = false;           ///< Clear previous output before running
    bool codeRunnerSaveFileBeforeRun = true;              ///< Save file before running

    // --- Python Language & Linter Extension Settings (Flake8 / Ruff / Pylance / AST) ---
    bool pythonLintingEnabled = true;                     ///< Real-time Python syntax & undefined variable linter
    bool pythonCheckUndefinedVariables = true;            ///< Flag missing variables and undefined symbols (F821 / NameError)
    bool pythonHighlightTerminalErrors = true;            ///< Auto-highlight execution errors & tracebacks from terminal on the editor line
    std::wstring pythonLinter = L"builtin (ast/flake8)";  ///< Active linter engine (builtin, flake8, ruff)

    // --- Language Intelligence & LSP Server Paths ---
    bool enableInlayHints = true;                         ///< Show parameter name and inferred type inlay hints
    bool enableLspHover = true;                           ///< Show rich Markdown hover cards on symbol dwell
    bool enableSignatureHelp = true;                      ///< Show active parameter signature hints on '(' and ','
    bool enableExternalLinters = true;                    ///< Aggregate eslint / tsc / phpstan / ruff diagnostics into Problems panel
    std::wstring lspPathTypeScript;                       ///< Optional custom executable path for TypeScript/JS LSP server
    std::wstring lspPathPython;                           ///< Optional custom executable path for Python LSP server
    std::wstring lspPathPhp;                              ///< Optional custom executable path for PHP LSP server
    std::wstring lspPathCpp;                              ///< Optional custom executable path for C/C++ LSP server

    // --- Editor Editing Behavior ---
    std::wstring autoClosingBrackets = L"always";         ///< "always", "languageDefined", "never"
    std::wstring autoClosingQuotes = L"always";           ///< "always", "languageDefined", "never"
    bool enableLinkedEditing = true;                      ///< Auto-update matching HTML/JSX closing tags
    bool enableBracketPairColorization = true;            ///< Colorize matching bracket pairs by nesting depth
    bool enableCodeFolding = true;                        ///< Show gutter fold chevrons and allow collapsing blocks
    std::wstring multiCursorModifier = L"alt";            ///< "alt" or "ctrlCmd"

    // --- Files, Formatting & Workspace Search ---
    std::wstring defaultEol = L"auto";                    ///< "auto", "LF", "CRLF"
    std::wstring defaultEncoding = L"UTF-8";              ///< "UTF-8", "UTF-8 with BOM", "UTF-16 LE", "ISO-8859-1"
    bool trimTrailingWhitespace = false;                  ///< Trim trailing whitespace on save
    bool insertFinalNewline = false;                      ///< Append final newline when saving files
    bool enableEditorConfig = true;                       ///< Respect .editorconfig rules per workspace file
    bool autoIgnorePhideDir = true;                       ///< Ensure .phide/ is excluded in .gitignore and ignore files
    bool searchUseIgnoreFiles = true;                     ///< Respect .gitignore / .ignore during workspace search
    int largeFileMmapThresholdMB = 8;                     ///< Threshold (MB) above which files open via memory-mapped viewer

    // --- Language & IntelliSense Providers ---
    bool enableEmmet = true;                              ///< Enable Emmet abbreviation expansion in HTML/JSX/CSS
    bool enableTailwindIntelliSense = true;               ///< Suggest Tailwind CSS utility classes
    bool enableDbSchemaIntelliSense = true;               ///< Suggest tables/columns from active SQL connection

    // --- Git & Source Control ---
    bool gitDiffSideBySide = true;                        ///< Prefer side-by-side diff viewer in Git drawer
    bool gitSmartStashExcludePhide = true;                ///< Exclude .phide state and recover untracked overlaps on stash Apply/Pop
    bool gitAutoRefreshGraph = true;                      ///< Refresh File Network graph automatically on workspace changes

    // --- Debug, Database & Live Server ---
    bool debugInlineValues = true;                        ///< Display runtime variable values inline while debugging
    bool debugOpenOnBreak = true;                         ///< Focus paused file/line automatically when breakpoint hits
    int sqlPageSize = 50;                                 ///< Rows displayed per page in SQL Query Console
    int liveServerPort = 5500;                            ///< Default HTTP port for integrated Live Server
    bool liveServerHotReload = true;                      ///< Broadcast CSS/HTML hot-reload events over WebSocket/SSE

    // Extended Editor, Appearance, Search, Terminal, Extensions, Git & Debug Settings
    bool smoothScrolling = true;
    bool scrollBeyondLastLine = true;
    std::wstring cursorBlinking = L"blink";               ///< "blink", "smooth", "phase", "solid"
    bool highlightActiveIndentGuide = true;
    bool showIndentGuides = true;
    bool highlightMatchingBrackets = true;
    bool formatOnPaste = false;
    bool trimAutoWhitespace = true;

    bool showActivityBar = true;
    bool showEditorTabs = true;
    bool showCommandCenter = true;
    int explorerTreeIndent = 14;

    std::wstring hotExit = L"onExit";                     ///< "off", "onExit", "onExitAndWindowClose"
    bool explorerHideDotfiles = false;
    bool searchDefaultCaseSensitive = false;
    bool searchDefaultWholeWord = false;
    int searchMaxResults = 2000;

    bool enableQuickSuggestions = true;
    bool enableSnippetSuggestions = true;
    bool cycleParameterHints = true;
    bool validateDiagnosticsOnType = true;

    std::wstring terminalDefaultShell = L"PowerShell";    ///< "PowerShell", "Command Prompt", "Git Bash", "WSL"
    int terminalFontSize = 14;
    int terminalScrollbackLines = 5000;
    std::wstring terminalCursorStyle = L"block";          ///< "block", "line", "underline"
    bool terminalCopyOnSelection = false;
    bool codeRunnerPreserveFocus = true;

    bool extensionsAutoCheckUpdates = true;
    bool extensionsAutoUpdate = false;
    bool phpLintingEnabled = true;
    bool jsTsLintingEnabled = true;
    bool cssLintingEnabled = true;
    bool markdownLintEnabled = true;

    bool gitEnabled = true;
    bool gitAutoFetch = false;
    bool gitConfirmSync = false;
    bool gitSmartCommit = true;
    bool gitShowInlineBlame = true;
    bool gitExplorerDecorations = true;

    bool debugBreakpointsEverywhere = false;
    bool debugFloatingToolbar = true;
    bool coverageShowGutter = true;
    bool markdownSyncScroll = true;
    bool dbAutoDiscoverServers = true;
    int dbQueryTimeoutSec = 15;
    bool liveServerAutoOpenBrowser = true;
    bool liveServerHttps = false;
    std::wstring liveServerRoot = L"/";
    std::wstring editorRulers = L"off";                  ///< Comma-separated column rulers (e.g., "80", "80, 120") or "off".

    /**
     * @brief Parses `editorRulers` into sorted unique positive column numbers (capped at 8 guides).
     */
    [[nodiscard]] std::vector<int> ParseEditorRulers() const {
        std::vector<int> cols;
        if (editorRulers.empty() || editorRulers == L"off" || editorRulers == L"none") {
            return cols;
        }
        int current = -1;
        bool isNegative = false;
        auto flushToken = [&]() {
            if (!isNegative && current > 0 && current <= 500) {
                if (std::find(cols.begin(), cols.end(), current) == cols.end() && cols.size() < 8) {
                    cols.push_back(current);
                }
            }
            current = -1;
            isNegative = false;
        };
        for (wchar_t ch : editorRulers) {
            if (ch >= L'0' && ch <= L'9') {
                if (current < 0) current = 0;
                if (current <= 500) {
                    current = current * 10 + (ch - L'0');
                }
            } else if (ch == L'-' && current < 0) {
                isNegative = true;
            } else {
                flushToken();
            }
        }
        flushToken();
        std::sort(cols.begin(), cols.end());
        return cols;
    }

    // --- Floating Prompt Bar Metrics ---
    int promptBarWidth  = 540;  ///< Width of the bottom floating AI prompt pill.
    int promptBarHeight = 44;   ///< Height of the floating prompt pill.
    int promptBarBottomMargin = 20; ///< Distance from the bottom of the window canvas.

    // --- Active Document Info ---
    std::wstring documentTitle = L"PHP IDE Concept Design"; ///< Document/project title shown in header pill.
    std::wstring activeModelName = L"High \u25BE";            ///< Active AI model selector label.

    // --- Inline Diagnostics & Blame Polish (I4, I5) ---
    bool showErrorLens = true;   ///< Render single-line error/warning messages inline at end of code lines.
    bool showGutterBlame = false; ///< Render dedicated compact Author · age blame column in editor gutter.

    // --- High-DPI Scaling ---
    float dpiScale = 1.0f;      ///< Current system DPI scale factor (1.0 = 96 DPI, 1.5 = 144 DPI, etc.).

    /**
     * @brief Scales a logical pixel value to physical pixels according to current DPI.
     * @param logicalPx Value in 96-DPI logical coordinates.
     * @return Scaled value in physical screen pixels.
     */
    [[nodiscard]] constexpr int Scale(int logicalPx) const noexcept {
        return static_cast<int>(logicalPx * dpiScale + 0.5f);
    }

    /**
     * @brief Scales a floating-point coordinate according to current DPI.
     * @param logicalVal Coordinate in logical units.
     * @return Scaled coordinate in physical units.
     */
    [[nodiscard]] constexpr float ScaleF(float logicalVal) const noexcept {
        return logicalVal * dpiScale;
    }
};

} // namespace phide::app
