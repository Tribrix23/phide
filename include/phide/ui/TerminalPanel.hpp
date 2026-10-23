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
    Python
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

    HANDLE hProcess = NULL;
    HANDLE hThread = NULL;
    HANDLE hStdinWrite = NULL;
    HANDLE hStdoutRead = NULL;
    DWORD processId = 0;
    std::atomic<bool> isAlive{false};
    std::thread readerThread;

    std::vector<TerminalLine> lines;
    std::wstring currentInputLine;
    float scrollOffsetY = 0.0f;
    std::mutex bufferMutex;
    std::function<void()> onOutput;

    // Hit-testing bounds
    D2D1_RECT_F tabBounds{};
    D2D1_RECT_F closeBounds{};

    ~TerminalTab();
    void Terminate();
    void SendInput(const std::string& input);
    void AppendOutput(const char* data, size_t length);
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

    // Tab Management
    void NewTab(TerminalShellType shell, const std::wstring& workingDir);
    void CloseTab(size_t index);
    void CloseAllTabs();
    [[nodiscard]] size_t GetTabCount() const noexcept { return m_tabs.size(); }
    [[nodiscard]] int GetActiveTabIdx() const noexcept { return m_activeTabIdx; }
    void SetActiveTabIdx(int idx) noexcept;
    TerminalTab* GetActiveTab();

    // Direct2D Rendering
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float clientW, float clientH, float leftOffset);

    // Hit-testing & Mouse Event Handling
    [[nodiscard]] bool HitTest(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestSplitter(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestButtons(POINT pt) const noexcept;
    [[nodiscard]] bool HitTestTextArea(POINT pt) const noexcept;

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
            if (tab) tab->onOutput = m_outputCallback;
        }
    }

    /**
     * @brief Tokenizes and colors terminal command lines (e.g. cd, git, npm, flags, strings).
     */
    static std::vector<TerminalSpan> TokenizeCommandLine(const std::wstring& cmdLine);

    // Menus & Profiles
    [[nodiscard]] const std::vector<TerminalProfileDef>& GetAvailableProfiles() const noexcept { return m_availableProfiles; }
    [[nodiscard]] bool IsShellMenuOpen() const noexcept { return m_showShellMenu; }
    [[nodiscard]] bool IsOptionsMenuOpen() const noexcept { return m_showOptionsMenu; }
    void CloseMenus() noexcept { m_showShellMenu = false; m_showOptionsMenu = false; m_showEngineSubmenu = false; }

private:
    void StartShellProcess(TerminalTab& tab);
    void RenderHeader(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                      float x, float y, float w, float h);
    void RenderContent(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                       float x, float y, float w, float h);
    void RenderShellMenu(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config);
    void RenderOptionsMenu(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config);

    bool m_isOpen = false;
    float m_height = 240.0f;
    bool m_isResizing = false;
    float m_dragStartY = 0.0f;
    float m_heightStart = 0.0f;

    std::vector<std::unique_ptr<TerminalTab>> m_tabs;
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
    D2D1_RECT_F m_optionsBtnBounds{};
    D2D1_RECT_F m_fullScreenBtnBounds{};
    D2D1_RECT_F m_minimizeBtnBounds{};

    bool m_hoverNewTab = false;
    bool m_hoverShellDropdown = false;
    bool m_hoverOptions = false;
    bool m_hoverFullScreen = false;
    bool m_hoverMinimize = false;
    bool m_isFullScreen = false;

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
};

} // namespace phide::ui
