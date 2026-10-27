#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <optional>
#include <windows.h>
#include <filesystem>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @brief Shell type for integrated terminal tab.
 */
enum class TerminalShellType {
    PowerShell,
    Cmd,
    GitBash,
    Node,
    Python,
    LiveServer,
    DebugConsole
};

/**
 * @brief Debug execution toolbar actions (Continue, Step Over, Step Into, Step Out, Restart, Stop).
 */
enum class DebugAction {
    None,
    Continue,  ///< Continue execution (F5)
    StepOver,  ///< Step over next instruction (F10)
    StepInto,  ///< Step into function call (F11)
    StepOut,   ///< Step out of current function (Shift+F11)
    Restart,   ///< Restart debug session (Ctrl+Shift+F5)
    Stop       ///< Stop debugging session (Shift+F5)
};

/**
 * @brief Bounding box and target for clickable hyperlinks (e.g. http://localhost:5500).
 */
struct TerminalUrlHit {
    std::wstring url;
    D2D1_RECT_F bounds{};
};

/**
 * @struct TerminalProfileDef
 * @brief Represents a detected installed shell runtime on the user's PC.
 */
struct TerminalProfileDef {
    std::wstring name;
    TerminalShellType type;
    std::wstring exePath;
    std::wstring args;
    bool isAvailable = false;
};

/**
 * @brief Automatically detects available shells installed on user's machine (cmd, powershell, git bash, node, python).
 */
inline std::vector<TerminalProfileDef> DetectAvailableShells() {
    std::vector<TerminalProfileDef> profiles;

    // 1. Windows PowerShell
    wchar_t sysDir[MAX_PATH] = { 0 };
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring psPath = std::wstring(sysDir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    bool hasPs = std::filesystem::exists(psPath) || (SearchPathW(NULL, L"powershell.exe", NULL, 0, NULL, NULL) > 0);
    profiles.push_back({ L"Windows PowerShell", TerminalShellType::PowerShell, hasPs ? psPath : L"powershell.exe", L"-NoLogo", hasPs });

    // 2. Command Prompt
    std::wstring cmdPath = std::wstring(sysDir) + L"\\cmd.exe";
    bool hasCmd = std::filesystem::exists(cmdPath) || (SearchPathW(NULL, L"cmd.exe", NULL, 0, NULL, NULL) > 0);
    profiles.push_back({ L"Command Prompt", TerminalShellType::Cmd, hasCmd ? cmdPath : L"cmd.exe", L"/K", hasCmd });

    // 3. Git Bash (auto-detected from Program Files or PATH)
    std::vector<std::wstring> bashCandidates = {
        L"C:\\Program Files\\Git\\bin\\bash.exe",
        L"C:\\Program Files (x86)\\Git\\bin\\bash.exe",
        L"C:\\Program Files\\Git\\usr\\bin\\bash.exe"
    };
    std::wstring bashPath;
    bool hasBash = false;
    for (const auto& cand : bashCandidates) {
        if (std::filesystem::exists(cand)) {
            bashPath = cand;
            hasBash = true;
            break;
        }
    }
    if (!hasBash && SearchPathW(NULL, L"bash.exe", NULL, 0, NULL, NULL) > 0) {
        bashPath = L"bash.exe";
        hasBash = true;
    }
    if (hasBash) {
        profiles.push_back({ L"Git Bash", TerminalShellType::GitBash, bashPath, L"--login -i", true });
    }

    // 4. Node.js Terminal (auto-detected from Program Files or PATH)
    std::vector<std::wstring> nodeCandidates = {
        L"C:\\Program Files\\nodejs\\node.exe",
        L"C:\\Program Files (x86)\\nodejs\\node.exe"
    };
    std::wstring nodePath;
    bool hasNode = false;
    for (const auto& cand : nodeCandidates) {
        if (std::filesystem::exists(cand)) {
            nodePath = cand;
            hasNode = true;
            break;
        }
    }
    if (!hasNode && SearchPathW(NULL, L"node.exe", NULL, 0, NULL, NULL) > 0) {
        nodePath = L"node.exe";
        hasNode = true;
    }
    if (hasNode) {
        profiles.push_back({ L"Node.js", TerminalShellType::Node, nodePath, L"", true });
    }

    // 5. Python Terminal (if installed)
    if (SearchPathW(NULL, L"python.exe", NULL, 0, NULL, NULL) > 0 || SearchPathW(NULL, L"py.exe", NULL, 0, NULL, NULL) > 0) {
        profiles.push_back({ L"Python", TerminalShellType::Python, L"python.exe", L"-i", true });
    }

    return profiles;
}

/**
 * @brief Represents a single colored segment in a terminal output line.
 */
struct TerminalSpan {
    std::wstring text;
    D2D1_COLOR_F color;
    bool bold = false;
};

/**
 * @brief Formatted terminal line with colored spans.
 */
struct TerminalLine {
    std::vector<TerminalSpan> spans;
    std::wstring rawText;
};

/**
 * @brief Represents an interactive terminal tab session.
 */
struct TerminalTab {
    std::wstring title;
    TerminalShellType shellType = TerminalShellType::PowerShell;
    std::wstring workingDir;
    std::wstring associatedFilePath;
    bool isServerTab = false;
    bool canClose = true;
    bool hasError = false;
    std::wstring lastErrorText;
    std::function<void(const std::wstring&, const std::wstring&)> onErrorCallback;

    HANDLE hProcess = NULL;
    HANDLE hThread = NULL;
    HANDLE hStdinWrite = NULL;
    HANDLE hStdoutRead = NULL;
    HANDLE hStderrRead = NULL;
    DWORD processId = 0;
    std::atomic<bool> isAlive{false};
    std::thread readerThread;
    std::thread stderrReaderThread;

    std::vector<TerminalLine> lines;
    std::wstring currentInputLine;
    float scrollOffsetY = 0.0f;
    int scrollOffsetLines = 0;
    std::vector<std::wstring> commandHistory;
    int historyIndex = -1;
    std::mutex bufferMutex;
    std::function<void()> onOutput;

    // Split Terminal support (Image 4 & 5)
    std::unique_ptr<TerminalTab> splitTab;
    float splitRatio = 0.5f;
    int activeSplitIndex = 0; // 0 = primary left, 1 = splitTab right
    D2D1_RECT_F paneBounds{};
    D2D1_RECT_F splitSplitterBounds{};
    D2D1_RECT_F splitCloseBounds{};

    // Hit-testing bounds
    D2D1_RECT_F tabBounds{};
    D2D1_RECT_F closeBounds{};

    ~TerminalTab();
    void Terminate();
    void SendInput(const std::string& input);
    void AppendOutput(const char* data, size_t length, bool isError = false);
};

/**
 * @brief Modern Integrated Terminal Panel docking at bottom of IDE window.
 *
 * Implements interactive terminal execution for PowerShell, Command Prompt, and Git Bash
 * with multi-tab management, shell switching menu, engine options, ANSI coloring,
 * and smooth Direct2D rendering matching PyCharm / VS Code.
 */
class TerminalPanel {
public:
    TerminalPanel();
    ~TerminalPanel();

    void Open(const std::wstring& workingDir);
    void Close() noexcept;
    void Toggle(const std::wstring& workingDir);
    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }

    [[nodiscard]] float GetHeight() const noexcept { return m_height; }
    void SetHeight(float h) noexcept;
    [[nodiscard]] bool IsResizing() const noexcept { return m_isResizing; }

    // Fullscreen / Overlay Mode
    [[nodiscard]] bool IsFullScreen() const noexcept { return m_isFullScreen; }
    void SetFullScreen(bool fs) noexcept { m_isFullScreen = fs; }
    void ToggleFullScreen() noexcept { m_isFullScreen = !m_isFullScreen; }
    [[nodiscard]] float GetFullScreenProgress() const noexcept { return m_fullScreenProgress; }
    void SetFullScreenProgress(float progress) noexcept { m_fullScreenProgress = progress; }
    void SetOnToggleFullScreen(std::function<void(bool)> cb) { m_onToggleFullScreen = std::move(cb); }

    // Tab & Split Management (Images 4 & 5)
    void NewTab(TerminalShellType shell, const std::wstring& workingDir);
    void SplitActiveTab(std::optional<TerminalShellType> shell = std::nullopt);
    void CloseSplitPane(size_t tabIdx);
    void CloseTab(size_t index);
    void CloseAllTabs();
    [[nodiscard]] size_t GetTabCount() const noexcept { return m_tabs.size(); }
    [[nodiscard]] int GetActiveTabIdx() const noexcept { return m_activeTabIdx; }
    void SetActiveTabIdx(int idx) noexcept;
    TerminalTab* GetActiveTab();
    [[nodiscard]] bool IsActiveTabSplit() const noexcept;

    // URL Link Navigation (Ctrl + Click)
    [[nodiscard]] bool HitTestUrl(POINT pt, std::wstring* outUrl = nullptr) const;

    // Live Server and Execution
    void OpenLogTab(const std::wstring& logTitle, const std::vector<std::wstring>& logLines);
    void OpenGitLogTab(const std::vector<std::wstring>& logLines);
    void AppendGitLogLines(const std::vector<std::wstring>& newLines);
    void StartLiveServerSession(const std::wstring& localUrl, const std::wstring& networkUrl, const std::wstring& filePath = L"", const std::wstring& fileName = L"");
    void StartServerSession(const std::wstring& serverTitle, const std::wstring& command, const std::wstring& workingDir, const std::wstring& projectRoot);
    void TerminateServerSession(const std::wstring& projectRoot);
    [[nodiscard]] bool HasServerError(const std::wstring& projectRoot = L"") const;
    [[nodiscard]] std::wstring GetServerErrorText(const std::wstring& projectRoot = L"") const;
    [[nodiscard]] TerminalTab* FindServerTab(const std::wstring& projectRoot);
    bool CloseTabByAssociatedFile(const std::wstring& filePath);
    void SetOnLiveServerTabClosed(std::function<void(const std::wstring&)> cb) { m_onLiveServerTabClosed = std::move(cb); }
    void SetOnServerError(std::function<void(const std::wstring&, const std::wstring&)> cb) { m_onServerError = std::move(cb); }
    void SetOnMinimize(std::function<void()> cb) { m_onMinimize = std::move(cb); }
    void ExecuteCommandLine(const std::wstring& command);

    // Debug Session Management (Same layout as terminal, fully functional)
    void StartDebugSession(const std::wstring& filePath, const std::wstring& workingDir);
    void ShowDebugConsole();
    void TriggerDebugAction(DebugAction action);
    void StopDebugSession();
    [[nodiscard]] bool IsDebugActive() const noexcept { return m_isDebugActive; }
    [[nodiscard]] DebugAction HitTestDebugButtons(POINT pt) const noexcept;

    // Direct2D Rendering
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float clientW, float clientH, float leftOffset, float animatedHeight = -1.0f);

    // Hit-testing & Mouse Event Handling
    [[nodiscard]] bool HitTest(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestSplitter(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestButtons(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestTextArea(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestSplitButton(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestTerminalSplitter(POINT pt) const noexcept;
    [[nodiscard]] const D2D1_RECT_F& GetMinimizeBtnBounds() const noexcept { return m_minimizeBtnBounds; }

    bool OnMouseDown(POINT pt, const app::AppConfig& config, const std::wstring& workingDir);
    bool OnMouseMove(POINT pt, bool isLButtonDown, float clientH);
    void OnMouseUp(POINT pt);
    bool OnMouseWheel(POINT pt, short delta);

    // Keyboard Input Handling
    bool OnChar(wchar_t ch);
    bool OnKeyDown(UINT vk, bool isCtrl, bool isShift);

    void SetWorkingDir(const std::wstring& dir);
    void SetOutputCallback(std::function<void()> cb) {
        m_outputCallback = std::move(cb);
        for (auto& tab : m_tabs) {
            if (tab) {
                tab->onOutput = m_outputCallback;
                if (tab->splitTab) tab->splitTab->onOutput = m_outputCallback;
            }
        }
    }

    /**
     * @brief Tokenizes and colors terminal command lines (e.g. cd, git, npm, flags, strings).
     */
    static std::vector<TerminalSpan> TokenizeCommandLine(const std::wstring& cmdLine);
    static std::vector<TerminalSpan> FormatTerminalLine(const TerminalLine& line, bool isError, const std::wstring& workingDir = L"");
    static std::vector<TerminalSpan> TokenizeCommandLineWithLinuxRules(const std::wstring& text, const std::wstring& workingDir = L"");

    // Menus & Profiles
    [[nodiscard]] const std::vector<TerminalProfileDef>& GetAvailableProfiles() const noexcept { return m_availableProfiles; }
    [[nodiscard]] bool IsShellMenuOpen() const noexcept { return m_showShellMenu; }
    [[nodiscard]] bool IsOptionsMenuOpen() const noexcept { return m_showOptionsMenu; }
    void CloseMenus() noexcept { m_showShellMenu = false; m_showOptionsMenu = false; m_showEngineSubmenu = false; }

private:
    void StartShellProcess(TerminalTab& tab);
    void RenderHeader(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                      float x, float y, float w, float h, float cornerRadius = 0.0f);
    void RenderContent(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                       float x, float y, float w, float h);
    void RenderSinglePane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          TerminalTab* tab, const D2D1_RECT_F& bounds, bool isFocused);
    void RenderShellMenu(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config);
    void RenderOptionsMenu(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config);

    bool m_isOpen = false;
    float m_height = 360.0f; // Taller default height matching Image 1
    bool m_isResizing = false;
    float m_dragStartY = 0.0f;
    float m_heightStart = 0.0f;

    std::vector<std::unique_ptr<TerminalTab>> m_tabs;
    mutable std::mutex m_tabsMutex;
    std::vector<TerminalProfileDef> m_availableProfiles;
    std::function<void()> m_outputCallback;
    int m_activeTabIdx = 0;
    int m_hoveredTabIdx = -1;
    int m_hoveredCloseIdx = -1;
    std::wstring m_defaultWorkingDir;

    // Header buttons
    D2D1_RECT_F m_panelBounds{};
    D2D1_RECT_F m_splitterBounds{};
    D2D1_RECT_F m_headerBounds{};
    D2D1_RECT_F m_textAreaBounds{};

    D2D1_RECT_F m_newTabBtnBounds{};
    D2D1_RECT_F m_shellDropdownBtnBounds{};
    D2D1_RECT_F m_splitBtnBounds{};
    D2D1_RECT_F m_optionsBtnBounds{};
    D2D1_RECT_F m_fullScreenBtnBounds{};
    D2D1_RECT_F m_minimizeBtnBounds{};

    bool m_hoverNewTab = false;
    bool m_hoverShellDropdown = false;
    bool m_hoverSplitBtn = false;
    bool m_hoverOptions = false;
    bool m_hoverFullScreen = false;
    bool m_hoverMinimize = false;
    bool m_isFullScreen = false;

    // Terminal Splitter dragging (Image 5)
    bool m_isSplitDragging = false;
    float m_splitDragStartX = 0.0f;
    float m_splitRatioStart = 0.5f;

    // Shell Dropdown Menu (Image 2)
    bool m_showShellMenu = false;
    D2D1_RECT_F m_shellMenuBounds{};
    int m_hoveredShellMenuItem = -1;

    // Options Menu (Image 4)
    bool m_showOptionsMenu = false;
    D2D1_RECT_F m_optionsMenuBounds{};
    int m_hoveredOptionsMenuItem = -1;
    bool m_showEngineSubmenu = false;
    D2D1_RECT_F m_engineSubmenuBounds{};
    int m_hoveredEngineSubmenuItem = -1;

    int m_engineType = 0; // 0 = Reworked 2025, 1 = Classic
    bool m_showToolbar = true;

    // Debugging controls & state
    bool m_isDebugActive = false;
    std::wstring m_debugTargetFile;
    D2D1_RECT_F m_debugContinueBounds{};
    D2D1_RECT_F m_debugStepOverBounds{};
    D2D1_RECT_F m_debugStepIntoBounds{};
    D2D1_RECT_F m_debugStepOutBounds{};
    D2D1_RECT_F m_debugRestartBounds{};
    D2D1_RECT_F m_debugStopBounds{};
    DebugAction m_hoveredDebugAction = DebugAction::None;
    D2D1_RECT_F m_clearConsoleBtnBounds{};
    bool m_hoverClearConsole = false;
    mutable std::vector<TerminalUrlHit> m_visibleUrls;
    std::function<void(const std::wstring&)> m_onLiveServerTabClosed;
    std::function<void(const std::wstring&, const std::wstring&)> m_onServerError;
    std::function<void()> m_onMinimize;
    std::function<void(bool)> m_onToggleFullScreen;
    float m_fullScreenProgress = 0.0f;
};

} // namespace phide::ui
