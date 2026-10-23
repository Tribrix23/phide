#include "phide/ui/TerminalPanel.hpp"
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <sstream>
#include <algorithm>

namespace phide::ui {

TerminalTab::~TerminalTab() {
    Terminate();
}

void TerminalTab::Terminate() {
    isAlive = false;
    if (splitTab) {
        splitTab->Terminate();
        splitTab.reset();
    }
    if (hStdinWrite) {
        CloseHandle(hStdinWrite);
        hStdinWrite = NULL;
    }
    if (hProcess) {
        TerminateProcess(hProcess, 0);
        WaitForSingleObject(hProcess, 50);
    }
    if (readerThread.joinable()) {
        CancelSynchronousIo(readerThread.native_handle());
        if (hStdoutRead) {
            CloseHandle(hStdoutRead);
            hStdoutRead = NULL;
        }
        readerThread.join();
    }
    if (stderrReaderThread.joinable()) {
        CancelSynchronousIo(stderrReaderThread.native_handle());
        if (hStderrRead) {
            CloseHandle(hStderrRead);
            hStderrRead = NULL;
        }
        stderrReaderThread.join();
    }
    if (hStdoutRead) {
        CloseHandle(hStdoutRead);
        hStdoutRead = NULL;
    }
    if (hStderrRead) {
        CloseHandle(hStderrRead);
        hStderrRead = NULL;
    }
    if (hProcess) {
        CloseHandle(hProcess);
        hProcess = NULL;
    }
    if (hThread) {
        CloseHandle(hThread);
        hThread = NULL;
    }
}

void TerminalTab::SendInput(const std::string& input) {
    if (!hStdinWrite || !isAlive.load()) return;
    DWORD written = 0;
    WriteFile(hStdinWrite, input.data(), static_cast<DWORD>(input.size()), &written, NULL);
}

void TerminalTab::AppendOutput(const char* data, size_t length, bool isError) {
    if (!data || length == 0 || !isAlive.load()) return;

    // Convert UTF-8 / ANSI to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(length), NULL, 0);
    std::wstring wstr;
    if (wlen > 0) {
        wstr.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(length), &wstr[0], wlen);
    } else {
        // Fallback to ANSI code page
        wlen = MultiByteToWideChar(CP_ACP, 0, data, static_cast<int>(length), NULL, 0);
        if (wlen > 0) {
            wstr.resize(wlen);
            MultiByteToWideChar(CP_ACP, 0, data, static_cast<int>(length), &wstr[0], wlen);
        } else {
            return;
        }
    }

    // Filter out Git Bash non-PTY startup warnings (Image 4)
    if (wstr.find(L"cannot set terminal process group") != std::wstring::npos ||
        wstr.find(L"no job control in this shell") != std::wstring::npos ||
        wstr.find(L"Inappropriate ioctl for device") != std::wstring::npos) {
        return;
    }

    std::lock_guard<std::mutex> lock(bufferMutex);

    if (lines.empty()) {
        lines.push_back({});
    }

    // Parse ANSI escapes and accumulate spans
    D2D1_COLOR_F curColor = isError ? D2D1::ColorF(0.94f, 0.32f, 0.32f, 1.0f) : D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.0f);
    bool bold = isError;

    for (size_t i = 0; i < wstr.length(); ++i) {
        wchar_t ch = wstr[i];

        // OSC (Operating System Command) sequence: \x1b] ... \x07
        if (ch == L'\x1b' && i + 1 < wstr.length() && wstr[i + 1] == L']') {
            size_t end = wstr.find_first_of(L"\x07", i + 2);
            if (end != std::wstring::npos) {
                i = end;
                continue;
            }
        }

        // ANSI Escape sequence: \x1b[ ...
        if (ch == L'\x1b' && i + 1 < wstr.length() && wstr[i + 1] == L'[') {
            size_t j = i + 2;
            while (j < wstr.length() && (wstr[j] < 0x40 || wstr[j] > 0x7E)) {
                j++;
            }
            if (j < wstr.length()) {
                wchar_t cmd = wstr[j];
                std::wstring seq = wstr.substr(i + 2, j - (i + 2));
                if (cmd == L'm') {
                    std::wstringstream sss(seq.empty() ? L"0" : seq);
                    std::wstring codeStr;
                    while (std::getline(sss, codeStr, L';')) {
                        int code = 0;
                        try { if (!codeStr.empty()) code = std::stoi(codeStr); } catch (...) {}
                        if (code == 0) {
                            curColor = D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.0f);
                            bold = false;
                        } else if (code == 1) {
                            bold = true;
                        } else if (code == 22) {
                            bold = false;
                        } else if (code == 30) {
                            curColor = D2D1::ColorF(0.20f, 0.20f, 0.22f, 1.0f);
                        } else if (code == 31 || code == 91) {
                            curColor = D2D1::ColorF(0.92f, 0.30f, 0.30f, 1.0f); // Red
                        } else if (code == 32 || code == 92) {
                            curColor = D2D1::ColorF(0.35f, 0.78f, 0.38f, 1.0f); // Green
                        } else if (code == 33 || code == 93) {
                            curColor = D2D1::ColorF(0.95f, 0.80f, 0.25f, 1.0f); // Yellow
                        } else if (code == 34 || code == 94) {
                            curColor = D2D1::ColorF(0.35f, 0.65f, 0.95f, 1.0f); // Blue
                        } else if (code == 35 || code == 95) {
                            curColor = D2D1::ColorF(0.80f, 0.45f, 0.85f, 1.0f); // Magenta
                        } else if (code == 36 || code == 96) {
                            curColor = D2D1::ColorF(0.30f, 0.78f, 0.85f, 1.0f); // Cyan
                        } else if (code == 37 || code == 97) {
                            curColor = D2D1::ColorF(0.95f, 0.95f, 0.95f, 1.0f); // White
                        } else if (code == 90) {
                            curColor = D2D1::ColorF(0.55f, 0.55f, 0.58f, 1.0f); // Gray
                        } else if (code == 39) {
                            curColor = D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.0f);
                        }
                    }
                } else if (cmd == L'J') {
                    // ANSI Erase in Display: Clear terminal screen & scrollback
                    if (seq == L"2" || seq == L"3" || seq.empty()) {
                        lines.clear();
                        lines.push_back({});
                        scrollOffsetY = 0.0f;
                    }
                } else if (cmd == L'K') {
                    // ANSI Erase in Line
                    if (!lines.empty()) {
                        lines.back().spans.clear();
                        lines.back().rawText.clear();
                    }
                }
                i = j;
                continue;
            }
        }

        if (ch == L'\r') {
            continue;
        }

        if (ch == L'\n') {
            lines.push_back({});
            continue;
        }

        if (ch == L'\b') {
            if (!lines.empty() && !lines.back().rawText.empty()) {
                lines.back().rawText.pop_back();
                if (!lines.back().spans.empty()) {
                    if (!lines.back().spans.back().text.empty()) {
                        lines.back().spans.back().text.pop_back();
                        if (lines.back().spans.back().text.empty()) {
                            lines.back().spans.pop_back();
                        }
                    }
                }
            }
            continue;
        }

        // Add character to last line
        auto& lastLine = lines.back();
        lastLine.rawText += ch;

        // Group into span
        if (!lastLine.spans.empty() &&
            lastLine.spans.back().color.r == curColor.r &&
            lastLine.spans.back().color.g == curColor.g &&
            lastLine.spans.back().color.b == curColor.b &&
            lastLine.spans.back().bold == bold) {
            lastLine.spans.back().text += ch;
        } else {
            TerminalSpan span;
            span.text = std::wstring(1, ch);
            span.color = curColor;
            span.bold = bold;
            lastLine.spans.push_back(std::move(span));
        }
    }

    // Limit scrollback buffer to 2000 lines
    if (lines.size() > 2000) {
        size_t excess = lines.size() - 2000;
        lines.erase(lines.begin(), lines.begin() + excess);
    }

    if (onOutput) {
        onOutput();
    }
}

// =============================================================================
// TerminalPanel Implementation
// =============================================================================

TerminalPanel::TerminalPanel() = default;

TerminalPanel::~TerminalPanel() {
    CloseAllTabs();
}

void TerminalPanel::Open(const std::wstring& workingDir) {
    m_isOpen = true;
    m_defaultWorkingDir = workingDir;
    if (m_height < 320.0f) {
        m_height = 360.0f; // Tall default size matching Image 1
    }
    if (m_availableProfiles.empty()) {
        m_availableProfiles = DetectAvailableShells();
    }
    if (m_tabs.empty()) {
        NewTab(TerminalShellType::PowerShell, workingDir);
    }
}

void TerminalPanel::Close() noexcept {
    m_isOpen = false;
    CloseMenus();
}

void TerminalPanel::Toggle(const std::wstring& workingDir) {
    if (m_isOpen) {
        Close();
    } else {
        Open(workingDir);
    }
}

void TerminalPanel::SetHeight(float h) noexcept {
    m_height = (std::max)(100.0f, (std::min)(800.0f, h));
}

void TerminalPanel::NewTab(TerminalShellType shell, const std::wstring& workingDir) {
    auto tab = std::make_unique<TerminalTab>();
    tab->shellType = shell;
    tab->workingDir = workingDir.empty() ? m_defaultWorkingDir : workingDir;

    size_t count = m_tabs.size() + 1;
    std::wstring shellName = L"powershell";
    if (shell == TerminalShellType::Cmd) shellName = L"cmd";
    else if (shell == TerminalShellType::GitBash) shellName = L"bash";
    else if (shell == TerminalShellType::Node) shellName = L"node";
    else if (shell == TerminalShellType::Python) shellName = L"python";

    tab->title = std::to_wstring(count) + L": " + shellName;
    tab->onOutput = m_outputCallback;

    StartShellProcess(*tab);
    m_tabs.push_back(std::move(tab));
    m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
}

void TerminalPanel::CloseTab(size_t index) {
    if (index >= m_tabs.size()) return;
    m_tabs[index]->Terminate();
    m_tabs.erase(m_tabs.begin() + index);

    if (m_tabs.empty()) {
        m_isOpen = false;
        m_activeTabIdx = 0;
    } else if (m_activeTabIdx >= static_cast<int>(m_tabs.size())) {
        m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
    }
}

void TerminalPanel::CloseAllTabs() {
    for (auto& tab : m_tabs) {
        if (tab) tab->Terminate();
    }
    m_tabs.clear();
    m_isOpen = false;
    m_activeTabIdx = 0;
    CloseMenus();
}

void TerminalPanel::SetActiveTabIdx(int idx) noexcept {
    if (idx >= 0 && idx < static_cast<int>(m_tabs.size())) {
        m_activeTabIdx = idx;
    }
}

TerminalTab* TerminalPanel::GetActiveTab() {
    if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
        auto* primary = m_tabs[m_activeTabIdx].get();
        if (primary && primary->splitTab && primary->activeSplitIndex == 1) {
            return primary->splitTab.get();
        }
        return primary;
    }
    return nullptr;
}

bool TerminalPanel::IsActiveTabSplit() const noexcept {
    if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
        return m_tabs[m_activeTabIdx] && m_tabs[m_activeTabIdx]->splitTab != nullptr;
    }
    return false;
}

void TerminalPanel::SplitActiveTab(std::optional<TerminalShellType> shell) {
    if (m_activeTabIdx < 0 || m_activeTabIdx >= static_cast<int>(m_tabs.size())) return;
    auto& primary = m_tabs[m_activeTabIdx];
    if (!primary) return;

    if (primary->splitTab) {
        // Already split - close split pane
        CloseSplitPane(static_cast<size_t>(m_activeTabIdx));
        return;
    }

    TerminalShellType splitShell = shell.value_or(primary->shellType);
    auto split = std::make_unique<TerminalTab>();
    split->shellType = splitShell;
    split->workingDir = primary->workingDir.empty() ? m_defaultWorkingDir : primary->workingDir;
    std::wstring shellName = L"powershell";
    if (splitShell == TerminalShellType::Cmd) shellName = L"cmd";
    else if (splitShell == TerminalShellType::GitBash) shellName = L"bash";
    else if (splitShell == TerminalShellType::Node) shellName = L"node";
    else if (splitShell == TerminalShellType::Python) shellName = L"python";
    split->title = primary->title + L" (split: " + shellName + L")";
    split->onOutput = m_outputCallback;

    StartShellProcess(*split);
    primary->splitTab = std::move(split);
    primary->splitRatio = 0.5f;
    primary->activeSplitIndex = 1; // Focus right split pane
}

void TerminalPanel::CloseSplitPane(size_t tabIdx) {
    if (tabIdx >= m_tabs.size()) return;
    auto& tab = m_tabs[tabIdx];
    if (tab && tab->splitTab) {
        tab->splitTab->Terminate();
        tab->splitTab.reset();
        tab->activeSplitIndex = 0;
    }
}

void TerminalPanel::SetWorkingDir(const std::wstring& dir) {
    m_defaultWorkingDir = dir;
}

void TerminalPanel::StartShellProcess(TerminalTab& tab) {
    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    HANDLE hStderrRead = NULL, hStderrWrite = NULL;
    HANDLE hStdinRead = NULL, hStdinWrite = NULL;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0);
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0);
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0);
    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

    tab.hStdinWrite = hStdinWrite;
    tab.hStdoutRead = hStdoutRead;
    tab.hStderrRead = hStderrRead;

    // Determine executable command
    std::wstring cmd;
    if (tab.shellType == TerminalShellType::PowerShell) {
        cmd = L"powershell.exe -NoLogo";
    } else if (tab.shellType == TerminalShellType::Cmd) {
        cmd = L"cmd.exe /K";
    } else if (tab.shellType == TerminalShellType::GitBash) {
        if (std::filesystem::exists(L"C:\\Program Files\\Git\\bin\\bash.exe")) {
            cmd = L"\"C:\\Program Files\\Git\\bin\\bash.exe\" --login -i";
        } else if (std::filesystem::exists(L"C:\\Program Files (x86)\\Git\\bin\\bash.exe")) {
            cmd = L"\"C:\\Program Files (x86)\\Git\\bin\\bash.exe\" --login -i";
        } else {
            cmd = L"bash.exe --login -i";
        }
    } else if (tab.shellType == TerminalShellType::Node) {
        if (std::filesystem::exists(L"C:\\Program Files\\nodejs\\node.exe")) {
            cmd = L"\"C:\\Program Files\\nodejs\\node.exe\" -i";
        } else if (std::filesystem::exists(L"C:\\Program Files (x86)\\nodejs\\node.exe")) {
            cmd = L"\"C:\\Program Files (x86)\\nodejs\\node.exe\" -i";
        } else {
            cmd = L"node.exe -i";
        }
    } else if (tab.shellType == TerminalShellType::Python) {
        cmd = L"python.exe -i";
    } else if (tab.shellType == TerminalShellType::DebugConsole) {
        std::filesystem::path p(m_debugTargetFile);
        std::wstring ext = p.extension().wstring();
        for (auto& c : ext) c = towlower(c);
        if (ext == L".py") {
            cmd = L"python.exe -m pdb \"" + m_debugTargetFile + L"\"";
        } else {
            std::wstring nodeExe = L"node.exe";
            if (std::filesystem::exists(L"C:\\Program Files\\nodejs\\node.exe")) {
                nodeExe = L"\"C:\\Program Files\\nodejs\\node.exe\"";
            } else if (std::filesystem::exists(L"C:\\Program Files (x86)\\nodejs\\node.exe")) {
                nodeExe = L"\"C:\\Program Files (x86)\\nodejs\\node.exe\"";
            }
            cmd = nodeExe + L" inspect \"" + m_debugTargetFile + L"\"";
        }
    } else if (tab.shellType == TerminalShellType::LiveServer) {
        CloseHandle(hStdoutWrite);
        CloseHandle(hStderrWrite);
        CloseHandle(hStdinRead);
        tab.isAlive = true;
        return;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.hStdError = hStderrWrite;
    si.hStdOutput = hStdoutWrite;
    si.hStdInput = hStdinRead;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    std::wstring validWorkDir = tab.workingDir;
    std::error_code ec;
    if (!validWorkDir.empty() && !std::filesystem::exists(validWorkDir, ec)) {
        validWorkDir.clear();
    }
    LPCWSTR workDir = validWorkDir.empty() ? nullptr : validWorkDir.c_str();

    BOOL success = CreateProcessW(
        NULL,
        cmdBuf.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
        NULL,
        workDir,
        &si,
        &pi
    );

    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);
    CloseHandle(hStdinRead);

    if (success) {
        tab.hProcess = pi.hProcess;
        tab.hThread = pi.hThread;
        tab.processId = pi.dwProcessId;
        tab.isAlive = true;

        // Background asynchronous stdout reader thread
        TerminalTab* pTab = &tab;
        tab.readerThread = std::thread([pTab, hStdoutRead]() {
            char buffer[4096];
            DWORD bytesRead = 0;
            while (pTab->isAlive.load() && ReadFile(hStdoutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                pTab->AppendOutput(buffer, bytesRead, false);
            }
        });

        // Background asynchronous stderr reader thread (Errors rendered in vibrant RED matching Image 3)
        tab.stderrReaderThread = std::thread([pTab, hStderrRead]() {
            char buffer[4096];
            DWORD bytesRead = 0;
            while (pTab->isAlive.load() && ReadFile(hStderrRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                pTab->AppendOutput(buffer, bytesRead, true);
            }
        });
    } else {
        tab.AppendOutput("Failed to launch terminal shell process.\r\n", 41);
    }
}

// =============================================================================
// Direct2D Rendering
// =============================================================================

std::vector<TerminalSpan> TerminalPanel::TokenizeCommandLine(const std::wstring& cmdLine) {
    std::vector<TerminalSpan> spans;
    if (cmdLine.empty()) return spans;

    size_t i = 0;
    bool isFirstToken = true;

    while (i < cmdLine.length()) {
        // Collect whitespace
        if (cmdLine[i] == L' ' || cmdLine[i] == L'\t') {
            size_t wsStart = i;
            while (i < cmdLine.length() && (cmdLine[i] == L' ' || cmdLine[i] == L'\t')) {
                i++;
            }
            spans.push_back({ cmdLine.substr(wsStart, i - wsStart), D2D1::ColorF(0.85f, 0.85f, 0.85f) });
            continue;
        }

        // Quoted string: "..." or '...'
        if (cmdLine[i] == L'"' || cmdLine[i] == L'\'') {
            wchar_t q = cmdLine[i];
            size_t strStart = i;
            i++;
            while (i < cmdLine.length() && cmdLine[i] != q) {
                if (cmdLine[i] == L'\\' && i + 1 < cmdLine.length()) i += 2;
                else i++;
            }
            if (i < cmdLine.length()) i++;
            spans.push_back({ cmdLine.substr(strStart, i - strStart), D2D1::ColorF(0.60f, 0.85f, 0.60f) });
            isFirstToken = false;
            continue;
        }

        // Word token
        size_t wordStart = i;
        while (i < cmdLine.length() && cmdLine[i] != L' ' && cmdLine[i] != L'\t' && cmdLine[i] != L'"' && cmdLine[i] != L'\'') {
            i++;
        }
        std::wstring word = cmdLine.substr(wordStart, i - wordStart);

        if (word == L"|" || word == L";" || word == L"&&" || word == L"||") {
            spans.push_back({ word, D2D1::ColorF(0.95f, 0.80f, 0.35f) });
            isFirstToken = true;
        } else if (isFirstToken) {
            // Executable command name (cd, git, npm, dir, etc.) -> Vibrant Cyan
            spans.push_back({ word, D2D1::ColorF(0.35f, 0.82f, 0.98f), true });
            isFirstToken = false;
        } else if (word.rfind(L"-", 0) == 0) {
            // Flag/Option (-m, -v, --help) -> Soft Magenta
            spans.push_back({ word, D2D1::ColorF(0.85f, 0.55f, 0.88f) });
        } else {
            // Path, argument, or parameter -> Crisp white/gray
            spans.push_back({ word, D2D1::ColorF(0.88f, 0.88f, 0.90f) });
        }
    }

    return spans;
}

std::vector<TerminalSpan> TerminalPanel::TokenizeCommandLineWithLinuxRules(const std::wstring& text, const std::wstring& workingDir) {
    std::vector<TerminalSpan> spans;
    if (text.empty()) return spans;

    size_t i = 0;
    bool isFirstCommand = true;
    bool isFolderArgCmd = false;

    while (i < text.length()) {
        // Collect whitespace
        if (text[i] == L' ' || text[i] == L'\t') {
            size_t wsStart = i;
            while (i < text.length() && (text[i] == L' ' || text[i] == L'\t')) {
                i++;
            }
            spans.push_back({ text.substr(wsStart, i - wsStart), D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
            continue;
        }

        // Delimiter '>' should NEVER be blue; it is always white!
        if (text[i] == L'>') {
            spans.push_back({ L">", D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
            i++;
            continue;
        }

        // Quoted string: "..." or '...'
        if (text[i] == L'"' || text[i] == L'\'') {
            wchar_t q = text[i];
            size_t strStart = i;
            i++;
            while (i < text.length() && text[i] != q) {
                if (text[i] == L'\\' && i + 1 < text.length()) i += 2;
                else i++;
            }
            if (i < text.length()) i++;
            std::wstring strVal = text.substr(strStart, i - strStart);
            std::wstring unquoted = (strVal.length() >= 2) ? strVal.substr(1, strVal.length() - 2) : strVal;
            bool isRealDir = false;
            if (!unquoted.empty()) {
                std::error_code ec;
                std::filesystem::path p(unquoted);
                if (std::filesystem::is_directory(p, ec)) isRealDir = true;
                else if (!workingDir.empty() && std::filesystem::is_directory(std::filesystem::path(workingDir) / p, ec)) isRealDir = true;
            }
            D2D1_COLOR_F strColor = (isFolderArgCmd && isRealDir) ? D2D1::ColorF(0.38f, 0.68f, 0.98f) : D2D1::ColorF(0.92f, 0.92f, 0.92f);
            spans.push_back({ strVal, strColor, false });
            isFirstCommand = false;
            continue;
        }

        // Word token (stop on whitespace, quotes, or '>')
        size_t wordStart = i;
        while (i < text.length() && text[i] != L' ' && text[i] != L'\t' && text[i] != L'"' && text[i] != L'\'' && text[i] != L'>') {
            i++;
        }
        std::wstring word = text.substr(wordStart, i - wordStart);

        if (word == L"|" || word == L";" || word == L"&&" || word == L"||") {
            spans.push_back({ word, D2D1::ColorF(0.95f, 0.80f, 0.25f), true });
            isFirstCommand = true;
            isFolderArgCmd = false;
        } else if (isFirstCommand) {
            // Command name: Gold / Yellow color matching Image 2
            spans.push_back({ word, D2D1::ColorF(0.95f, 0.80f, 0.25f), true });
            isFirstCommand = false;

            std::wstring lowerWord = word;
            for (auto& c : lowerWord) c = towlower(c);
            if (lowerWord == L"cd" || lowerWord == L"dir" || lowerWord == L"ls" ||
                lowerWord == L"mkdir" || lowerWord == L"md" || lowerWord == L"rmdir" ||
                lowerWord == L"rd" || lowerWord == L"pushd" || lowerWord == L"popd") {
                isFolderArgCmd = true;
            }
        } else {
            // Check if argument is an ACTUAL existing folder on disk!
            // "why its color blue even tho theres no folder like that and the shouldnt be blue"
            bool isFolder = false;
            if (word == L"." || word == L".." || word == L"~") {
                isFolder = true;
            } else if (!word.empty() && word[0] != L'-' && word[0] != L'/') {
                std::error_code ec;
                std::filesystem::path p(word);
                if (std::filesystem::is_directory(p, ec)) {
                    isFolder = true;
                } else if (!workingDir.empty()) {
                    std::filesystem::path combined = std::filesystem::path(workingDir) / p;
                    if (std::filesystem::is_directory(combined, ec)) {
                        isFolder = true;
                    }
                }
            }

            if (isFolder) {
                spans.push_back({ word, D2D1::ColorF(0.38f, 0.68f, 0.98f), false });
            } else {
                spans.push_back({ word, D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
            }
        }
    }

    return spans;
}

std::vector<TerminalSpan> TerminalPanel::FormatTerminalLine(const TerminalLine& line, bool isError, const std::wstring& workingDir) {
    if (isError) {
        return { { line.rawText, D2D1::ColorF(0.94f, 0.32f, 0.32f), false } };
    }

    if (line.spans.size() > 1) {
        return line.spans;
    }

    const std::wstring& text = line.rawText;
    if (text.empty()) {
        return line.spans;
    }

    // 1. Error line detection
    if (text.find(L"Cannot find path") != std::wstring::npos ||
        text.find(L"CategoryInfo") != std::wstring::npos ||
        text.find(L"FullyQualifiedErrorId") != std::wstring::npos ||
        text.rfind(L"+ ", 0) == 0 ||
        text.rfind(L"Error:", 0) == 0 ||
        text.rfind(L"Exception:", 0) == 0 ||
        text.find(L"The term '") != std::wstring::npos) {
        return { { text, D2D1::ColorF(0.94f, 0.32f, 0.32f), false } };
    }

    // 2. PowerShell prompt: "PS C:\path> command"
    if (text.rfind(L"PS ", 0) == 0) {
        size_t gt = text.find(L">");
        if (gt != std::wstring::npos) {
            std::vector<TerminalSpan> out;
            out.push_back({ L"PS", D2D1::ColorF(0.95f, 0.80f, 0.25f), true });
            out.push_back({ L" ", D2D1::ColorF(0.92f, 0.92f, 0.92f), false });

            // Folder path in BLUE
            if (gt > 3) {
                out.push_back({ text.substr(3, gt - 3), D2D1::ColorF(0.38f, 0.68f, 0.98f), false });
            }

            // ">" in white (NEVER blue!)
            out.push_back({ L">", D2D1::ColorF(0.92f, 0.92f, 0.92f), false });

            // Command part after ">"
            if (gt + 1 < text.length()) {
                auto cmdSpans = TokenizeCommandLineWithLinuxRules(text.substr(gt + 1), workingDir);
                out.insert(out.end(), cmdSpans.begin(), cmdSpans.end());
            }
            return out;
        }
    }

    // 3. CMD prompt: "C:\path> command"
    size_t gt = text.find(L">");
    if (gt != std::wstring::npos && gt >= 2 && text[1] == L':') {
        std::vector<TerminalSpan> out;
        out.push_back({ text.substr(0, gt), D2D1::ColorF(0.38f, 0.68f, 0.98f), false });
        out.push_back({ L">", D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
        if (gt + 1 < text.length()) {
            auto cmdSpans = TokenizeCommandLineWithLinuxRules(text.substr(gt + 1), workingDir);
            out.insert(out.end(), cmdSpans.begin(), cmdSpans.end());
        }
        return out;
    }

    // 4. Git Bash / Linux prompt: "user@host:path$ command"
    size_t dollar = text.find(L"$");
    if (dollar != std::wstring::npos && text.find(L"@") != std::wstring::npos) {
        std::vector<TerminalSpan> out;
        size_t atPos = text.find(L"@");
        size_t spaceAfterHost = text.find(L' ', atPos);
        if (spaceAfterHost != std::wstring::npos) {
            out.push_back({ text.substr(0, spaceAfterHost), D2D1::ColorF(0.95f, 0.80f, 0.25f), true });

            size_t pathStart = text.find_first_of(L"/~", spaceAfterHost);
            if (pathStart != std::wstring::npos && pathStart < dollar) {
                out.push_back({ text.substr(spaceAfterHost, pathStart - spaceAfterHost), D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
                size_t pathEnd = text.find_first_of(L" (", pathStart);
                if (pathEnd == std::wstring::npos || pathEnd > dollar) pathEnd = dollar;
                out.push_back({ text.substr(pathStart, pathEnd - pathStart), D2D1::ColorF(0.38f, 0.68f, 0.98f), false });
                out.push_back({ text.substr(pathEnd, dollar - pathEnd + 1), D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
            } else {
                out.push_back({ text.substr(spaceAfterHost, dollar - spaceAfterHost + 1), D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
            }

            if (dollar + 1 < text.length()) {
                auto cmdSpans = TokenizeCommandLineWithLinuxRules(text.substr(dollar + 1), workingDir);
                out.insert(out.end(), cmdSpans.begin(), cmdSpans.end());
            }
            return out;
        }
    }

    // 5. Standalone command line starting with a command (e.g. "cd src", "npm run build")
    std::wstring trimmed = text;
    size_t firstNonSpace = trimmed.find_first_not_of(L" \t");
    if (firstNonSpace != std::wstring::npos) {
        size_t firstWordEnd = trimmed.find_first_of(L" \t", firstNonSpace);
        std::wstring firstWord = trimmed.substr(firstNonSpace, (firstWordEnd == std::wstring::npos) ? std::wstring::npos : (firstWordEnd - firstNonSpace));
        std::wstring lowerFirstWord = firstWord;
        for (auto& c : lowerFirstWord) c = towlower(c);
        if (lowerFirstWord == L"cd" || lowerFirstWord == L"dir" || lowerFirstWord == L"ls" ||
            lowerFirstWord == L"mkdir" || lowerFirstWord == L"rmdir" || lowerFirstWord == L"git" ||
            lowerFirstWord == L"npm" || lowerFirstWord == L"node" || lowerFirstWord == L"python" ||
            lowerFirstWord == L"cargo" || lowerFirstWord == L"cmake" || lowerFirstWord == L"cat") {
            return TokenizeCommandLineWithLinuxRules(text, workingDir);
        }
    }

    // 6. Directory listing line (dir / ls): Folder names in BLUE
    bool isDirEntry = false;
    size_t dirTokenPos = text.find(L"<DIR>");
    if (dirTokenPos == std::wstring::npos) {
        if (text.rfind(L"d----", 0) == 0 || text.rfind(L"d-r--", 0) == 0 || text.rfind(L"drwx", 0) == 0) {
            isDirEntry = true;
            dirTokenPos = 0;
        }
    } else {
        isDirEntry = true;
    }

    if (isDirEntry) {
        size_t lastNonWs = text.find_last_not_of(L" \t");
        if (lastNonWs != std::wstring::npos) {
            size_t nameStart = text.find_last_of(L" \t", lastNonWs);
            if (nameStart != std::wstring::npos && nameStart > dirTokenPos) {
                std::vector<TerminalSpan> out;
                out.push_back({ text.substr(0, nameStart + 1), D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
                out.push_back({ text.substr(nameStart + 1), D2D1::ColorF(0.38f, 0.68f, 0.98f), true });
                return out;
            }
        }
    }

    // 6. Explicit ANSI escape colors
    bool hasCustomAnsi = false;
    for (const auto& sp : line.spans) {
        if (std::abs(sp.color.r - 0.92f) > 0.05f || std::abs(sp.color.g - 0.92f) > 0.05f || std::abs(sp.color.b - 0.92f) > 0.05f) {
            hasCustomAnsi = true;
            break;
        }
    }
    if (hasCustomAnsi) {
        return line.spans;
    }

    return { { text, D2D1::ColorF(0.92f, 0.92f, 0.92f), false } };
}

void TerminalPanel::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                           float clientW, float clientH, float leftOffset) {
    if (!m_isOpen) return;

    float panelX = leftOffset;
    float panelH = m_isFullScreen ? (clientH - config.headerHeight) : m_height;
    float panelY = clientH - panelH;
    float panelW = clientW - leftOffset;

    m_panelBounds = D2D1::RectF(panelX, panelY, panelX + panelW, panelY + panelH);

    // 1. Panel Background
    D2D1_COLOR_F bgColor = theme.IsDarkMode() ? D2D1::ColorF(0.08f, 0.08f, 0.09f, 1.0f) : D2D1::ColorF(0.96f, 0.96f, 0.97f, 1.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    pRT->CreateSolidColorBrush(bgColor, &bgBrush);
    if (bgBrush) pRT->FillRectangle(m_panelBounds, bgBrush.Get());

    // 2. Top Resizing Splitter (4px border)
    m_splitterBounds = D2D1::RectF(panelX, panelY, panelX + panelW, panelY + config.ScaleF(4.0f));
    pRT->DrawLine(
        D2D1::Point2F(panelX, panelY),
        D2D1::Point2F(panelX + panelW, panelY),
        theme.brushBorderSubtle.Get(),
        1.0f
    );

    // 3. Header Toolbar (Image 1)
    float headerH = config.ScaleF(32.0f);
    m_headerBounds = D2D1::RectF(panelX, panelY + config.ScaleF(2.0f), panelX + panelW, panelY + headerH);
    RenderHeader(pRT, theme, config, panelX, panelY + config.ScaleF(2.0f), panelW, headerH - config.ScaleF(2.0f));

    // 4. Terminal Content Area
    float contentY = panelY + headerH;
    float contentH = panelH - headerH;
    m_textAreaBounds = D2D1::RectF(panelX, contentY, panelX + panelW, panelY + panelH);
    RenderContent(pRT, theme, config, panelX, contentY, panelW, contentH);

    // 5. Shell Dropdown Menu (Image 2)
    if (m_showShellMenu) {
        RenderShellMenu(pRT, theme, config);
    }

    // 6. Options Dropdown Menu (Image 4)
    if (m_showOptionsMenu) {
        RenderOptionsMenu(pRT, theme, config);
    }
}

void TerminalPanel::RenderHeader(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                float x, float y, float w, float h) {
    // Subtle header bottom line
    pRT->DrawLine(
        D2D1::Point2F(x, y + h),
        D2D1::Point2F(x + w, y + h),
        theme.brushBorderSubtle.Get(),
        1.0f
    );

    float curX = x + config.ScaleF(12.0f);

    // "Terminal" Title Label (Image 1)
    D2D1_RECT_F rcTitle = D2D1::RectF(curX, y, curX + config.ScaleF(60.0f), y + h);
    pRT->DrawText(L"Terminal", 8, theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(),
                  rcTitle, theme.brushTextPrimary.Get());
    curX += config.ScaleF(68.0f);

    // Tabs List: "Local x", "Local (2) x"
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        auto& tab = m_tabs[i];
        bool isActive = (static_cast<int>(i) == m_activeTabIdx);
        bool isHover = (static_cast<int>(i) == m_hoveredTabIdx);

        float tabW = config.ScaleF(static_cast<float>(tab->title.length() * 7 + 36));
        float tabH = config.ScaleF(24.0f);
        float tabY = y + (h - tabH) / 2.0f;

        tab->tabBounds = D2D1::RectF(curX, tabY, curX + tabW, tabY + tabH);
        D2D1_ROUNDED_RECT rrcTab = D2D1::RoundedRect(tab->tabBounds, 3.5f, 3.5f);

        if (isActive) {
            // Active Tab: Darker rounded pill background (Image 1)
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> activePillBg;
            pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.22f, 0.36f, 1.0f) : D2D1::ColorF(0.85f, 0.90f, 0.98f, 1.0f), &activePillBg);
            if (activePillBg) pRT->FillRoundedRectangle(rrcTab, activePillBg.Get());
        } else if (isHover) {
            pRT->FillRoundedRectangle(rrcTab, theme.brushItemHover.Get());
        }

        // Tab Title Text
        D2D1_RECT_F rcTabText = D2D1::RectF(curX + config.ScaleF(8.0f), tabY, curX + tabW - config.ScaleF(20.0f), tabY + tabH);
        ID2D1Brush* textBrush = isActive ? theme.brushTextWhite.Get() : theme.brushTextSecondary.Get();
        pRT->DrawText(tab->title.c_str(), static_cast<UINT32>(tab->title.length()),
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                      rcTabText, textBrush);

        // Close 'x' button on tab
        float closeX = curX + tabW - config.ScaleF(16.0f);
        float closeY = tabY + tabH / 2.0f;
        tab->closeBounds = D2D1::RectF(closeX - config.ScaleF(6.0f), tabY + config.ScaleF(4.0f),
                                       closeX + config.ScaleF(6.0f), tabY + tabH - config.ScaleF(4.0f));

        float xr = config.ScaleF(3.0f);
        ID2D1Brush* closeBrush = (static_cast<int>(i) == m_hoveredCloseIdx) ? theme.brushCloseHoverBg.Get() : theme.brushTextMuted.Get();
        pRT->DrawLine(D2D1::Point2F(closeX - xr, closeY - xr), D2D1::Point2F(closeX + xr, closeY + xr), closeBrush, 1.2f);
        pRT->DrawLine(D2D1::Point2F(closeX + xr, closeY - xr), D2D1::Point2F(closeX - xr, closeY + xr), closeBrush, 1.2f);

        curX += tabW + config.ScaleF(4.0f);
    }

    // '+' Button (New tab)
    float btnSize = config.ScaleF(20.0f);
    float btnY = y + (h - btnSize) / 2.0f;
    m_newTabBtnBounds = D2D1::RectF(curX, btnY, curX + btnSize, btnY + btnSize);
    if (m_hoverNewTab) {
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_newTabBtnBounds, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
    }
    float ncx = curX + btnSize / 2.0f;
    float ncy = btnY + btnSize / 2.0f;
    float nr = config.ScaleF(4.0f);
    pRT->DrawLine(D2D1::Point2F(ncx - nr, ncy), D2D1::Point2F(ncx + nr, ncy), theme.brushTextPrimary.Get(), 1.4f);
    pRT->DrawLine(D2D1::Point2F(ncx, ncy - nr), D2D1::Point2F(ncx, ncy + nr), theme.brushTextPrimary.Get(), 1.4f);
    curX += btnSize + config.ScaleF(2.0f);

    // 'v' Chevron Dropdown Button (Image 2)
    m_shellDropdownBtnBounds = D2D1::RectF(curX, btnY, curX + btnSize, btnY + btnSize);
    if (m_hoverShellDropdown || m_showShellMenu) {
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_shellDropdownBtnBounds, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
    }
    float scx = curX + btnSize / 2.0f;
    float scy = btnY + btnSize / 2.0f;
    float sr = config.ScaleF(3.0f);
    pRT->DrawLine(D2D1::Point2F(scx - sr, scy - sr * 0.5f), D2D1::Point2F(scx, scy + sr * 0.7f), theme.brushTextSecondary.Get(), 1.3f);
    pRT->DrawLine(D2D1::Point2F(scx, scy + sr * 0.7f), D2D1::Point2F(scx + sr, scy - sr * 0.5f), theme.brushTextSecondary.Get(), 1.3f);

    // =========================================================================
    // Right Controls: '...' (Options) and '-' (Minimize)
    // NOTE: AI Agents is EXCLUDED per user instruction ("DONT INCLIUDE THE ONE IN DRAW X")
    // =========================================================================
    float rightX = x + w - config.ScaleF(8.0f);

    // '-' Minimize / Close Panel Button
    rightX -= btnSize;
    m_minimizeBtnBounds = D2D1::RectF(rightX, btnY, rightX + btnSize, btnY + btnSize);
    if (m_hoverMinimize) {
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_minimizeBtnBounds, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
    }
    float mcx = rightX + btnSize / 2.0f;
    float mcy = btnY + btnSize / 2.0f;
    pRT->DrawLine(D2D1::Point2F(mcx - nr, mcy), D2D1::Point2F(mcx + nr, mcy), theme.brushTextSecondary.Get(), 1.4f);

    // Full Screen / Maximize / Restore Button
    rightX -= (btnSize + config.ScaleF(4.0f));
    m_fullScreenBtnBounds = D2D1::RectF(rightX, btnY, rightX + btnSize, btnY + btnSize);
    if (m_hoverFullScreen) {
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_fullScreenBtnBounds, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
    }
    float fcx = rightX + btnSize / 2.0f;
    float fcy = btnY + btnSize / 2.0f;
    float fSize = config.ScaleF(4.5f);
    if (!m_isFullScreen) {
        // Draw maximize single square box
        pRT->DrawRectangle(D2D1::RectF(fcx - fSize, fcy - fSize, fcx + fSize, fcy + fSize), theme.brushTextSecondary.Get(), 1.2f);
    } else {
        // Draw restore double overlapping boxes
        pRT->DrawRectangle(D2D1::RectF(fcx - fSize + 2.0f, fcy - fSize - 2.0f, fcx + fSize + 2.0f, fcy + fSize - 2.0f), theme.brushTextSecondary.Get(), 1.0f);
        pRT->DrawRectangle(D2D1::RectF(fcx - fSize - 1.0f, fcy - fSize + 1.0f, fcx + fSize - 1.0f, fcy + fSize + 1.0f), theme.brushTextSecondary.Get(), 1.2f);
    }

    // Split Terminal Button [|] (Images 4 & 5)
    rightX -= (btnSize + config.ScaleF(4.0f));
    m_splitBtnBounds = D2D1::RectF(rightX, btnY, rightX + btnSize, btnY + btnSize);
    bool isSplitActive = IsActiveTabSplit();
    if (m_hoverSplitBtn || isSplitActive) {
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_splitBtnBounds, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrc, isSplitActive ? theme.brushBrandAccent.Get() : theme.brushItemHover.Get());
    }
    float scxSplit = rightX + btnSize / 2.0f;
    float scySplit = btnY + btnSize / 2.0f;
    float sSizeSplit = config.ScaleF(4.5f);
    ID2D1Brush* splitBrush = isSplitActive ? theme.brushTextWhite.Get() : theme.brushTextSecondary.Get();
    pRT->DrawRectangle(D2D1::RectF(scxSplit - sSizeSplit, scySplit - sSizeSplit, scxSplit + sSizeSplit, scySplit + sSizeSplit), splitBrush, 1.2f);
    pRT->DrawLine(D2D1::Point2F(scxSplit, scySplit - sSizeSplit), D2D1::Point2F(scxSplit, scySplit + sSizeSplit), splitBrush, 1.2f);

    // '...' Options Menu Button (Circled in Green in Image 3)
    rightX -= (btnSize + config.ScaleF(4.0f));
    m_optionsBtnBounds = D2D1::RectF(rightX, btnY, rightX + btnSize, btnY + btnSize);
    if (m_hoverOptions || m_showOptionsMenu) {
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_optionsBtnBounds, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
    }
    float ocx = rightX + btnSize / 2.0f;
    float ocy = btnY + btnSize / 2.0f;
    float dotR = config.ScaleF(1.3f);
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ocx, ocy - config.ScaleF(5.0f)), dotR, dotR), theme.brushTextSecondary.Get());
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ocx, ocy), dotR, dotR), theme.brushTextSecondary.Get());
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ocx, ocy + config.ScaleF(5.0f)), dotR, dotR), theme.brushTextSecondary.Get());

    // =========================================================================
    // Debug Toolbar Controls: Continue, Step Over, Step Into, Step Out, Restart, Stop
    // Rendered when Debug session is active (Same layout as terminal, fully functional)
    // =========================================================================
    if (m_isDebugActive) {
        float dbgBtnW = config.ScaleF(24.0f);
        float dbgSpacing = config.ScaleF(3.0f);

        // 6. Stop Debugging (Shift+F5)
        rightX -= (dbgBtnW + config.ScaleF(8.0f));
        m_debugStopBounds = D2D1::RectF(rightX, btnY, rightX + dbgBtnW, btnY + btnSize);
        if (m_hoveredDebugAction == DebugAction::Stop) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_debugStopBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        float stcx = rightX + dbgBtnW / 2.0f;
        float stcy = btnY + btnSize / 2.0f;
        float stSize = config.ScaleF(4.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> stopBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.35f, 0.35f), &stopBrush);
        if (stopBrush) {
            pRT->FillRectangle(D2D1::RectF(stcx - stSize, stcy - stSize, stcx + stSize, stcy + stSize), stopBrush.Get());
        }

        // 5. Restart Session (Ctrl+Shift+F5)
        rightX -= (dbgBtnW + dbgSpacing);
        m_debugRestartBounds = D2D1::RectF(rightX, btnY, rightX + dbgBtnW, btnY + btnSize);
        if (m_hoveredDebugAction == DebugAction::Restart) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_debugRestartBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> restartBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.85f, 0.50f), &restartBrush);
        if (restartBrush) {
            float rcx = rightX + dbgBtnW / 2.0f;
            float rcy = btnY + btnSize / 2.0f;
            pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(rcx, rcy), config.ScaleF(4.0f), config.ScaleF(4.0f)), restartBrush.Get(), 1.3f);
        }

        // 4. Step Out (Shift+F11)
        rightX -= (dbgBtnW + dbgSpacing);
        m_debugStepOutBounds = D2D1::RectF(rightX, btnY, rightX + dbgBtnW, btnY + btnSize);
        if (m_hoveredDebugAction == DebugAction::StepOut) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_debugStepOutBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> stepBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.75f, 0.98f), &stepBrush);
        if (stepBrush) {
            float sox = rightX + dbgBtnW / 2.0f;
            float soy = btnY + btnSize / 2.0f;
            pRT->DrawLine(D2D1::Point2F(sox - 3.0f, soy + 3.0f), D2D1::Point2F(sox - 3.0f, soy - 2.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(sox - 3.0f, soy - 2.0f), D2D1::Point2F(sox + 3.0f, soy - 2.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(sox + 1.0f, soy - 5.0f), D2D1::Point2F(sox + 4.0f, soy - 2.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(sox + 1.0f, soy + 1.0f), D2D1::Point2F(sox + 4.0f, soy - 2.0f), stepBrush.Get(), 1.3f);
        }

        // 3. Step Into (F11)
        rightX -= (dbgBtnW + dbgSpacing);
        m_debugStepIntoBounds = D2D1::RectF(rightX, btnY, rightX + dbgBtnW, btnY + btnSize);
        if (m_hoveredDebugAction == DebugAction::StepInto) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_debugStepIntoBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        if (stepBrush) {
            float six = rightX + dbgBtnW / 2.0f;
            float siy = btnY + btnSize / 2.0f;
            pRT->DrawLine(D2D1::Point2F(six - 3.0f, siy - 3.0f), D2D1::Point2F(six - 3.0f, siy + 2.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(six - 3.0f, siy + 2.0f), D2D1::Point2F(six + 3.0f, siy + 2.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(six + 1.0f, siy - 1.0f), D2D1::Point2F(six + 4.0f, siy + 2.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(six + 1.0f, siy + 5.0f), D2D1::Point2F(six + 4.0f, siy + 2.0f), stepBrush.Get(), 1.3f);
        }

        // 2. Step Over (F10)
        rightX -= (dbgBtnW + dbgSpacing);
        m_debugStepOverBounds = D2D1::RectF(rightX, btnY, rightX + dbgBtnW, btnY + btnSize);
        if (m_hoveredDebugAction == DebugAction::StepOver) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_debugStepOverBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        if (stepBrush) {
            float sox = rightX + dbgBtnW / 2.0f;
            float soy = btnY + btnSize / 2.0f;
            pRT->DrawLine(D2D1::Point2F(sox - 4.0f, soy + 2.0f), D2D1::Point2F(sox - 2.0f, soy - 3.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(sox - 2.0f, soy - 3.0f), D2D1::Point2F(sox + 2.0f, soy - 3.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(sox + 2.0f, soy - 3.0f), D2D1::Point2F(sox + 4.0f, soy + 2.0f), stepBrush.Get(), 1.3f);
        }

        // 1. Continue (F5)
        rightX -= (dbgBtnW + dbgSpacing);
        m_debugContinueBounds = D2D1::RectF(rightX, btnY, rightX + dbgBtnW, btnY + btnSize);
        if (m_hoveredDebugAction == DebugAction::Continue) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_debugContinueBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> contBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.85f, 0.45f), &contBrush);
        if (contBrush) {
            float cx = rightX + dbgBtnW / 2.0f;
            float cy = btnY + btnSize / 2.0f;
            pRT->DrawLine(D2D1::Point2F(cx - 3.0f, cy - 4.0f), D2D1::Point2F(cx + 4.0f, cy), contBrush.Get(), 1.5f);
            pRT->DrawLine(D2D1::Point2F(cx + 4.0f, cy), D2D1::Point2F(cx - 3.0f, cy + 4.0f), contBrush.Get(), 1.5f);
            pRT->DrawLine(D2D1::Point2F(cx - 3.0f, cy + 4.0f), D2D1::Point2F(cx - 3.0f, cy - 4.0f), contBrush.Get(), 1.5f);
        }
    } else {
        m_debugContinueBounds = {};
        m_debugStepOverBounds = {};
        m_debugStepIntoBounds = {};
        m_debugStepOutBounds = {};
        m_debugRestartBounds = {};
        m_debugStopBounds = {};
    }
}

void TerminalPanel::RenderContent(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                 float x, float y, float w, float h) {
    if (m_activeTabIdx < 0 || m_activeTabIdx >= static_cast<int>(m_tabs.size())) return;
    auto* primaryTab = m_tabs[m_activeTabIdx].get();
    if (!primaryTab) return;

    if (primaryTab->splitTab) {
        float splitterW = config.ScaleF(4.0f);
        float totalW = w;
        float leftW = (totalW - splitterW) * primaryTab->splitRatio;
        float rightW = totalW - splitterW - leftW;

        D2D1_RECT_F leftBounds = D2D1::RectF(x, y, x + leftW, y + h);
        primaryTab->paneBounds = leftBounds;

        D2D1_RECT_F splitterBounds = D2D1::RectF(x + leftW, y, x + leftW + splitterW, y + h);
        primaryTab->splitSplitterBounds = splitterBounds;

        D2D1_RECT_F rightBounds = D2D1::RectF(x + leftW + splitterW, y, x + totalW, y + h);
        primaryTab->splitTab->paneBounds = rightBounds;

        // Render left pane
        RenderSinglePane(pRT, theme, config, primaryTab, leftBounds, primaryTab->activeSplitIndex == 0);

        // Render vertical splitter bar
        ID2D1Brush* splitterBrush = m_isSplitDragging ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get();
        pRT->FillRectangle(splitterBounds, splitterBrush);

        // Render right pane
        RenderSinglePane(pRT, theme, config, primaryTab->splitTab.get(), rightBounds, primaryTab->activeSplitIndex == 1);
    } else {
        primaryTab->paneBounds = D2D1::RectF(x, y, x + w, y + h);
        RenderSinglePane(pRT, theme, config, primaryTab, primaryTab->paneBounds, true);
    }
}

void TerminalPanel::RenderSinglePane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     TerminalTab* tab, const D2D1_RECT_F& bounds, bool isFocused) {
    if (!tab) return;

    pRT->PushAxisAlignedClip(bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Active pane highlight border (when split)
    if (IsActiveTabSplit() && isFocused) {
        pRT->DrawRectangle(bounds, theme.brushBrandAccent.Get(), 1.0f);
    }

    // Split pane badge and close button (if split and this is the right split tab)
    if (IsActiveTabSplit() && tab != m_tabs[m_activeTabIdx].get()) {
        float badgeW = config.ScaleF(static_cast<float>(tab->title.length() * 6 + 28));
        float badgeH = config.ScaleF(18.0f);
        float badgeX = bounds.right - badgeW - config.ScaleF(8.0f);
        float badgeY = bounds.top + config.ScaleF(4.0f);
        D2D1_RECT_F rcBadge = D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH);
        D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(rcBadge, 3.0f, 3.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBrush;
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.16f, 0.20f, 0.90f) : D2D1::ColorF(0.90f, 0.92f, 0.95f, 0.90f), &badgeBrush);
        if (badgeBrush) pRT->FillRoundedRectangle(rrcBadge, badgeBrush.Get());

        D2D1_RECT_F rcBadgeText = D2D1::RectF(badgeX + config.ScaleF(6.0f), badgeY, badgeX + badgeW - config.ScaleF(14.0f), badgeY + badgeH);
        pRT->DrawText(tab->title.c_str(), static_cast<UINT32>(tab->title.length()),
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                      rcBadgeText, theme.brushTextSecondary.Get());

        // Split close button 'x'
        float cx = badgeX + badgeW - config.ScaleF(8.0f);
        float cy = badgeY + badgeH / 2.0f;
        float cr = config.ScaleF(3.0f);
        tab->splitCloseBounds = D2D1::RectF(cx - cr - 2.0f, cy - cr - 2.0f, cx + cr + 2.0f, cy + cr + 2.0f);
        pRT->DrawLine(D2D1::Point2F(cx - cr, cy - cr), D2D1::Point2F(cx + cr, cy + cr), theme.brushTextMuted.Get(), 1.2f);
        pRT->DrawLine(D2D1::Point2F(cx + cr, cy - cr), D2D1::Point2F(cx - cr, cy + cr), theme.brushTextMuted.Get(), 1.2f);
    }

    std::lock_guard<std::mutex> lock(tab->bufferMutex);

    float padX = config.ScaleF(12.0f);
    float padY = config.ScaleF(6.0f);
    float padBottom = config.ScaleF(22.0f); // Breathing room so characters do not stick to bottom edge (Image 2)
    float lineH = config.ScaleF(17.0f);

    float availableH = (bounds.bottom - bounds.top) - padY - padBottom;
    float maxVisibleLines = availableH > 0.0f ? (availableH / lineH) : 1.0f;

    size_t totalLines = tab->lines.size();
    size_t maxScroll = (totalLines > static_cast<size_t>(maxVisibleLines)) ? (totalLines - static_cast<size_t>(maxVisibleLines)) : 0;
    if (tab->scrollOffsetLines > static_cast<int>(maxScroll)) {
        tab->scrollOffsetLines = static_cast<int>(maxScroll);
    }
    if (tab->scrollOffsetLines < 0) {
        tab->scrollOffsetLines = 0;
    }
    size_t startLine = (maxScroll > 0) ? (maxScroll - static_cast<size_t>(tab->scrollOffsetLines)) : 0;

    m_visibleUrls.clear();

    float curY = bounds.top + padY;
    float curX = bounds.left + padX;

    // DirectWrite character advance width
    float charW = config.ScaleF(7.8f);
    if (theme.dwriteFactory && theme.fontCode) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(L"M", 1, theme.fontCode.Get(), 1000.0f, lineH, &pLayout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                charW = tm.widthIncludingTrailingWhitespace;
            }
        }
    }

    for (size_t l = startLine; l < totalLines && (curY + lineH) <= (bounds.bottom - padBottom + 2.0f); ++l) {
        const auto& line = tab->lines[l];
        curX = bounds.left + padX;

        // Auto-detect error lines
        bool isLineError = false;
        if (line.rawText.find(L"Cannot find path") != std::wstring::npos ||
            line.rawText.find(L"CategoryInfo") != std::wstring::npos ||
            line.rawText.find(L"FullyQualifiedErrorId") != std::wstring::npos ||
            line.rawText.rfind(L"+ ", 0) == 0 ||
            line.rawText.rfind(L"Error:", 0) == 0 ||
            line.rawText.rfind(L"Exception:", 0) == 0 ||
            line.rawText.find(L"The term '") != std::wstring::npos) {
            isLineError = true;
        }

        auto formattedSpans = FormatTerminalLine(line, isLineError, tab->workingDir);

        for (const auto& span : formattedSpans) {
            float spanW = static_cast<float>(span.text.length()) * charW;
            D2D1_RECT_F rcSpan = D2D1::RectF(curX, curY, curX + spanW + 50.0f, curY + lineH);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> spanBrush;
            pRT->CreateSolidColorBrush(span.color, &spanBrush);
            if (spanBrush) {
                pRT->DrawText(span.text.c_str(), static_cast<UINT32>(span.text.length()),
                              theme.fontCode.Get(), rcSpan, spanBrush.Get());
            }
            curX += spanW;
        }

        // Detect hyperlinks (http://, https://) in line for Ctrl+Click navigation
        size_t searchPos = 0;
        while (searchPos < line.rawText.length()) {
            size_t pHttp = line.rawText.find(L"http://", searchPos);
            size_t pHttps = line.rawText.find(L"https://", searchPos);
            size_t uStart = std::wstring::npos;
            if (pHttp != std::wstring::npos && pHttps != std::wstring::npos) {
                uStart = (std::min)(pHttp, pHttps);
            } else if (pHttp != std::wstring::npos) {
                uStart = pHttp;
            } else {
                uStart = pHttps;
            }

            if (uStart == std::wstring::npos) break;

            size_t uEnd = uStart;
            while (uEnd < line.rawText.length()) {
                wchar_t uc = line.rawText[uEnd];
                if (uc == L' ' || uc == L'\t' || uc == L'\r' || uc == L'\n' || uc == L'"' || uc == L'\'' || uc == L'<' || uc == L'>' || uc == L')' || uc == L']') {
                    break;
                }
                uEnd++;
            }

            if (uEnd > uStart) {
                std::wstring uStr = line.rawText.substr(uStart, uEnd - uStart);
                float uX = bounds.left + padX + static_cast<float>(uStart) * charW;
                float uW = static_cast<float>(uStr.length()) * charW;
                D2D1_RECT_F uRc = D2D1::RectF(uX, curY, uX + uW, curY + lineH);
                m_visibleUrls.push_back({ uStr, uRc });

                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                    pRT->DrawLine(
                        D2D1::Point2F(uX, curY + lineH - 2.0f),
                        D2D1::Point2F(uX + uW, curY + lineH - 2.0f),
                        theme.brushBrandAccent.Get() ? theme.brushBrandAccent.Get() : theme.brushTextPrimary.Get(),
                        1.2f
                    );
                }
            }
            searchPos = uEnd + 1;
        }

        // Render typed command line in real-time
        if (l == totalLines - 1 && !tab->currentInputLine.empty()) {
            auto inputSpans = TokenizeCommandLineWithLinuxRules(tab->currentInputLine, tab->workingDir);
            for (const auto& span : inputSpans) {
                float spanW = static_cast<float>(span.text.length()) * charW;
                D2D1_RECT_F rcSpan = D2D1::RectF(curX, curY, curX + spanW + 50.0f, curY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> spanBrush;
                pRT->CreateSolidColorBrush(span.color, &spanBrush);
                if (spanBrush) {
                    pRT->DrawText(span.text.c_str(), static_cast<UINT32>(span.text.length()),
                                  theme.fontCode.Get(), rcSpan, spanBrush.Get());
                }
                curX += spanW;
            }
        }

        // Blinking cursor '█' on the last active output line
        if (isFocused && l == totalLines - 1) {
            bool blinkOn = ((GetTickCount64() / 500) % 2 == 0);
            if (blinkOn) {
                float cursorW = config.ScaleF(7.5f);
                float cursorH = lineH - config.ScaleF(2.0f);
                D2D1_RECT_F rcCursor = D2D1::RectF(curX, curY + 1.0f, curX + cursorW, curY + 1.0f + cursorH);
                pRT->FillRectangle(rcCursor, theme.brushTextPrimary.Get());
            }
        }

        curY += lineH;
    }

    // Vertical scrollbar on right if content overflows
    if (totalLines > static_cast<size_t>(maxVisibleLines) && maxScroll > 0) {
        float sbW = config.ScaleF(4.0f);
        float sbRight = bounds.right - config.ScaleF(2.0f);
        float sbLeft = sbRight - sbW;
        float sbTop = bounds.top + padY;
        float sbH = availableH;

        float thumbH = (std::max)(config.ScaleF(14.0f), sbH * (maxVisibleLines / static_cast<float>(totalLines)));
        float thumbY = sbTop + (static_cast<float>(maxScroll - tab->scrollOffsetLines) / static_cast<float>(maxScroll)) * (sbH - thumbH);

        D2D1_ROUNDED_RECT rrcSb = D2D1::RoundedRect(D2D1::RectF(sbLeft, thumbY, sbRight, thumbY + thumbH), 2.0f, 2.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sbBrush;
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.40f, 0.42f, 0.48f, 0.6f) : D2D1::ColorF(0.65f, 0.65f, 0.70f, 0.7f), &sbBrush);
        if (sbBrush) pRT->FillRoundedRectangle(rrcSb, sbBrush.Get());
    }

    // Bottom div under terminal so letters don't stick to bottom border (Image 2)
    D2D1_RECT_F bottomDiv = D2D1::RectF(bounds.left, bounds.bottom - padBottom, bounds.right, bounds.bottom);
    D2D1_COLOR_F bottomDivColor = theme.IsDarkMode() ? D2D1::ColorF(0.07f, 0.07f, 0.08f, 1.0f) : D2D1::ColorF(0.94f, 0.94f, 0.95f, 1.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> divBrush;
    pRT->CreateSolidColorBrush(bottomDivColor, &divBrush);
    if (divBrush) pRT->FillRectangle(bottomDiv, divBrush.Get());
    pRT->DrawLine(D2D1::Point2F(bounds.left, bounds.bottom - padBottom), D2D1::Point2F(bounds.right, bounds.bottom - padBottom), theme.brushBorderSubtle.Get(), 0.5f);

    pRT->PopAxisAlignedClip();
}

// =============================================================================
// Shell Dropdown Menu (Image 2)
// =============================================================================

void TerminalPanel::RenderShellMenu(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config) {
    if (m_availableProfiles.empty()) {
        m_availableProfiles = DetectAvailableShells();
    }

    float itemH = config.ScaleF(24.0f);
    float menuW = config.ScaleF(190.0f);
    float menuH = config.ScaleF(static_cast<float>(m_availableProfiles.size() * 24 + 14));
    float menuX = m_shellDropdownBtnBounds.left;
    float menuY = m_shellDropdownBtnBounds.bottom + config.ScaleF(2.0f);

    m_shellMenuBounds = D2D1::RectF(menuX, menuY, menuX + menuW, menuY + menuH);
    D2D1_ROUNDED_RECT rrcMenu = D2D1::RoundedRect(m_shellMenuBounds, 4.0f, 4.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> menuBgBrush;
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.15f, 0.18f, 0.98f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f), &menuBgBrush);
    if (menuBgBrush) pRT->FillRoundedRectangle(rrcMenu, menuBgBrush.Get());
    pRT->DrawRoundedRectangle(rrcMenu, theme.brushBorderSubtle.Get(), 1.0f);

    float itemY = menuY + config.ScaleF(6.0f);

    for (size_t i = 0; i < m_availableProfiles.size(); ++i) {
        const auto& profile = m_availableProfiles[i];
        D2D1_RECT_F rcItem = D2D1::RectF(menuX + 4.0f, itemY, menuX + menuW - 4.0f, itemY + itemH);
        if (static_cast<int>(i) == m_hoveredShellMenuItem) {
            D2D1_ROUNDED_RECT rrcItem = D2D1::RoundedRect(rcItem, 3.0f, 3.0f);
            pRT->FillRoundedRectangle(rrcItem, theme.brushBrandAccent.Get());
        }

        std::wstring label = profile.name;
        D2D1_RECT_F rcText = D2D1::RectF(menuX + config.ScaleF(14.0f), itemY, menuX + menuW - 4.0f, itemY + itemH);
        ID2D1Brush* brush = (static_cast<int>(i) == m_hoveredShellMenuItem) ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
        pRT->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                      rcText, brush);

        itemY += itemH;
    }
}

// =============================================================================
// Options Dropdown Menu (Image 4)
// =============================================================================

void TerminalPanel::RenderOptionsMenu(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config) {
    float menuW = config.ScaleF(160.0f);
    float menuH = config.ScaleF(230.0f);
    float menuX = m_optionsBtnBounds.right - menuW;
    float menuY = m_optionsBtnBounds.bottom + config.ScaleF(2.0f);

    m_optionsMenuBounds = D2D1::RectF(menuX, menuY, menuX + menuW, menuY + menuH);
    D2D1_ROUNDED_RECT rrcMenu = D2D1::RoundedRect(m_optionsMenuBounds, 4.0f, 4.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> menuBgBrush;
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.15f, 0.18f, 0.98f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f), &menuBgBrush);
    if (menuBgBrush) pRT->FillRoundedRectangle(rrcMenu, menuBgBrush.Get());
    pRT->DrawRoundedRectangle(rrcMenu, theme.brushBorderSubtle.Get(), 1.0f);

    struct MenuItemDef {
        const wchar_t* title;
        bool hasSubmenu;
        bool isSep;
    };
    MenuItemDef items[] = {
        { L"Terminal Engine", true, false },
        { L"⚙ Settings", false, false },
        { L"", false, true },
        { L"Close All", false, false },
        { L"✓ Show Toolbar", false, false },
        { L"Group Tabs", false, false },
        { L"View Mode", true, false },
        { L"Move to", true, false },
        { L"Resize", true, false },
        { L"", false, true },
        { L"Remove from Sidebar", false, false }
    };

    float itemY = menuY + config.ScaleF(4.0f);
    float itemH = config.ScaleF(20.0f);

    for (int i = 0; i < 11; ++i) {
        if (items[i].isSep) {
            pRT->DrawLine(D2D1::Point2F(menuX + 6.0f, itemY + itemH / 2.0f),
                          D2D1::Point2F(menuX + menuW - 6.0f, itemY + itemH / 2.0f),
                          theme.brushBorderSubtle.Get(), 1.0f);
            itemY += config.ScaleF(6.0f);
            continue;
        }

        D2D1_RECT_F rcItem = D2D1::RectF(menuX + 4.0f, itemY, menuX + menuW - 4.0f, itemY + itemH);
        if (i == m_hoveredOptionsMenuItem) {
            D2D1_ROUNDED_RECT rrcItem = D2D1::RoundedRect(rcItem, 3.0f, 3.0f);
            pRT->FillRoundedRectangle(rrcItem, theme.brushBrandAccent.Get());
        }

        D2D1_RECT_F rcText = D2D1::RectF(menuX + config.ScaleF(12.0f), itemY, menuX + menuW - 20.0f, itemY + itemH);
        ID2D1Brush* brush = (i == m_hoveredOptionsMenuItem) ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
        pRT->DrawText(items[i].title, static_cast<UINT32>(wcslen(items[i].title)),
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                      rcText, brush);

        if (items[i].hasSubmenu) {
            float arrX = menuX + menuW - config.ScaleF(12.0f);
            float arrY = itemY + itemH / 2.0f;
            float ar = config.ScaleF(3.0f);
            pRT->DrawLine(D2D1::Point2F(arrX - ar, arrY - ar), D2D1::Point2F(arrX, arrY), brush, 1.2f);
            pRT->DrawLine(D2D1::Point2F(arrX, arrY), D2D1::Point2F(arrX - ar, arrY + ar), brush, 1.2f);
        }

        itemY += itemH;
    }

    // Engine Submenu (Image 4): "✓ Reworked 2025", "Classic"
    if (m_showEngineSubmenu || m_hoveredOptionsMenuItem == 0) {
        float subW = config.ScaleF(130.0f);
        float subH = config.ScaleF(56.0f);
        float subX = menuX - subW - config.ScaleF(2.0f);
        float subY = menuY + config.ScaleF(4.0f);

        m_engineSubmenuBounds = D2D1::RectF(subX, subY, subX + subW, subY + subH);
        D2D1_ROUNDED_RECT rrcSub = D2D1::RoundedRect(m_engineSubmenuBounds, 4.0f, 4.0f);
        if (menuBgBrush) pRT->FillRoundedRectangle(rrcSub, menuBgBrush.Get());
        pRT->DrawRoundedRectangle(rrcSub, theme.brushBorderSubtle.Get(), 1.0f);

        const wchar_t* subItems[] = { L"✓ Reworked 2025", L"  Classic" };
        float subItemY = subY + config.ScaleF(4.0f);
        for (int s = 0; s < 2; ++s) {
            D2D1_RECT_F rcSubItem = D2D1::RectF(subX + 4.0f, subItemY, subX + subW - 4.0f, subItemY + itemH);
            if (s == m_hoveredEngineSubmenuItem) {
                D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcSubItem, 3.0f, 3.0f);
                pRT->FillRoundedRectangle(rrc, theme.brushBrandAccent.Get());
            }
            ID2D1Brush* subBrush = (s == m_hoveredEngineSubmenuItem) ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
            pRT->DrawText(subItems[s], static_cast<UINT32>(wcslen(subItems[s])),
                          theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                          rcSubItem, subBrush);
            subItemY += itemH;
        }
    }
}

// =============================================================================
// Hit-testing & Mouse Event Handling
// =============================================================================

bool TerminalPanel::HitTest(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    return pt.x >= m_panelBounds.left && pt.x <= m_panelBounds.right &&
           pt.y >= m_panelBounds.top && pt.y <= m_panelBounds.bottom;
}

bool TerminalPanel::HitTestSplitter(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    return pt.x >= m_splitterBounds.left && pt.x <= m_splitterBounds.right &&
           pt.y >= m_splitterBounds.top - 2 && pt.y <= m_splitterBounds.bottom + 2;
}

bool TerminalPanel::HitTestSplitButton(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    return pt.x >= m_splitBtnBounds.left && pt.x <= m_splitBtnBounds.right &&
           pt.y >= m_splitBtnBounds.top && pt.y <= m_splitBtnBounds.bottom;
}

bool TerminalPanel::HitTestTerminalSplitter(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
        const auto& tab = m_tabs[m_activeTabIdx];
        if (tab && tab->splitTab) {
            const auto& sb = tab->splitSplitterBounds;
            return pt.x >= sb.left - 2 && pt.x <= sb.right + 2 &&
                   pt.y >= sb.top && pt.y <= sb.bottom;
        }
    }
    return false;
}

bool TerminalPanel::HitTestUrl(POINT pt, std::wstring* outUrl) const {
    if (!m_isOpen) return false;
    auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
        return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
    };
    for (const auto& hit : m_visibleUrls) {
        if (inRect(pt, hit.bounds)) {
            if (outUrl) *outUrl = hit.url;
            return true;
        }
    }
    return false;
}

DebugAction TerminalPanel::HitTestDebugButtons(POINT pt) const noexcept {
    if (!m_isOpen || !m_isDebugActive) return DebugAction::None;
    auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
        return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
    };
    if (inRect(pt, m_debugContinueBounds)) return DebugAction::Continue;
    if (inRect(pt, m_debugStepOverBounds)) return DebugAction::StepOver;
    if (inRect(pt, m_debugStepIntoBounds)) return DebugAction::StepInto;
    if (inRect(pt, m_debugStepOutBounds)) return DebugAction::StepOut;
    if (inRect(pt, m_debugRestartBounds)) return DebugAction::Restart;
    if (inRect(pt, m_debugStopBounds)) return DebugAction::Stop;
    return DebugAction::None;
}

void TerminalPanel::StartLiveServerSession(const std::wstring& localUrl, const std::wstring& networkUrl) {
    if (!m_isOpen) {
        Open(m_defaultWorkingDir);
    }

    int liveTabIdx = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->shellType == TerminalShellType::LiveServer) {
            liveTabIdx = static_cast<int>(i);
            break;
        }
    }

    TerminalTab* tab = nullptr;
    if (liveTabIdx >= 0) {
        m_activeTabIdx = liveTabIdx;
        tab = m_tabs[liveTabIdx].get();
    } else {
        auto newTab = std::make_unique<TerminalTab>();
        newTab->shellType = TerminalShellType::LiveServer;
        newTab->title = L"⚡ Live Server";
        newTab->workingDir = m_defaultWorkingDir;
        newTab->isAlive = true;
        newTab->onOutput = m_outputCallback;
        m_tabs.push_back(std::move(newTab));
        m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
        tab = m_tabs.back().get();
    }

    if (!tab) return;

    {
        std::lock_guard<std::mutex> lock(tab->bufferMutex);
        tab->lines.clear();

        // Line 1: Header banner
        TerminalLine l1;
        l1.rawText = L"[Live Server] Server running at " + localUrl;
        l1.spans.push_back({ L"[Live Server] ", D2D1::ColorF(0.30f, 0.85f, 0.45f), true });
        l1.spans.push_back({ L"Server running and watching for changes", D2D1::ColorF(0.85f, 0.85f, 0.85f), false });
        tab->lines.push_back(std::move(l1));

        // Line 2: Empty line
        tab->lines.push_back({});

        // Line 3: Local URL
        TerminalLine l3;
        l3.rawText = L"  > Local:   " + localUrl;
        l3.spans.push_back({ L"  > Local:   ", D2D1::ColorF(0.95f, 0.80f, 0.25f), true });
        l3.spans.push_back({ localUrl, D2D1::ColorF(0.35f, 0.75f, 1.0f), true });
        tab->lines.push_back(std::move(l3));

        // Line 4: Network URL
        TerminalLine l4;
        l4.rawText = L"  > Network: " + networkUrl;
        l4.spans.push_back({ L"  > Network: ", D2D1::ColorF(0.95f, 0.80f, 0.25f), true });
        l4.spans.push_back({ networkUrl, D2D1::ColorF(0.35f, 0.75f, 1.0f), true });
        tab->lines.push_back(std::move(l4));

        // Line 5: Empty line
        tab->lines.push_back({});

        // Line 6: Tip to Ctrl + Click
        TerminalLine l6;
        l6.rawText = L"  Hold Ctrl and click on any link above to open in your browser.";
        l6.spans.push_back({ L"  Hold ", D2D1::ColorF(0.65f, 0.65f, 0.70f), false });
        l6.spans.push_back({ L"Ctrl + Click", D2D1::ColorF(1.0f, 0.85f, 0.30f), true });
        l6.spans.push_back({ L" on any link above to open in your browser.", D2D1::ColorF(0.65f, 0.65f, 0.70f), false });
        tab->lines.push_back(std::move(l6));

        tab->lines.push_back({});
        tab->scrollOffsetLines = 0;
    }

    if (m_outputCallback) m_outputCallback();
}

void TerminalPanel::ExecuteCommandLine(const std::wstring& command) {
    if (!m_isOpen) {
        Open(m_defaultWorkingDir);
    }
    if (m_tabs.empty()) {
        NewTab(TerminalShellType::PowerShell, m_defaultWorkingDir);
    }
    TerminalTab* tab = GetActiveTab();
    if (!tab) return;

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, command.c_str(), -1, NULL, 0, NULL, NULL);
    if (utf8Len > 1) {
        std::string utf8(utf8Len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, command.c_str(), -1, &utf8[0], utf8Len, NULL, NULL);
        utf8 += "\r\n";
        tab->SendInput(utf8);
    }
}

void TerminalPanel::StartDebugSession(const std::wstring& filePath, const std::wstring& workingDir) {
    if (!m_isOpen) {
        Open(workingDir);
    }
    m_isDebugActive = true;
    m_debugTargetFile = filePath;

    // Terminate existing debug console tab if open
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->shellType == TerminalShellType::DebugConsole) {
            CloseTab(i);
            break;
        }
    }

    auto debugTab = std::make_unique<TerminalTab>();
    debugTab->shellType = TerminalShellType::DebugConsole;
    debugTab->title = L"🐞 Debug Console";
    debugTab->workingDir = workingDir.empty() ? m_defaultWorkingDir : workingDir;
    debugTab->onOutput = m_outputCallback;

    {
        std::lock_guard<std::mutex> lock(debugTab->bufferMutex);
        TerminalLine l0;
        l0.rawText = L"[Debugger] Starting interactive debug session: " + filePath;
        l0.spans.push_back({ L"[Debugger] ", D2D1::ColorF(0.95f, 0.50f, 0.20f), true });
        l0.spans.push_back({ L"Starting session for " + filePath, D2D1::ColorF(0.85f, 0.85f, 0.85f), false });
        debugTab->lines.push_back(std::move(l0));
    }

    StartShellProcess(*debugTab);
    m_tabs.push_back(std::move(debugTab));
    m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);

    if (m_outputCallback) m_outputCallback();
}

void TerminalPanel::TriggerDebugAction(DebugAction action) {
    if (!m_isDebugActive) return;
    TerminalTab* tab = nullptr;
    for (auto& t : m_tabs) {
        if (t && t->shellType == TerminalShellType::DebugConsole) {
            tab = t.get();
            break;
        }
    }
    if (!tab && m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
        tab = m_tabs[m_activeTabIdx].get();
    }
    if (!tab) return;

    switch (action) {
    case DebugAction::Continue:
        tab->SendInput("c\r\n");
        break;
    case DebugAction::StepOver:
        tab->SendInput("n\r\n");
        break;
    case DebugAction::StepInto:
        tab->SendInput("s\r\n");
        break;
    case DebugAction::StepOut:
        tab->SendInput("o\r\n");
        break;
    case DebugAction::Restart:
        tab->SendInput("restart\r\n");
        break;
    case DebugAction::Stop:
        StopDebugSession();
        break;
    default:
        break;
    }
}

void TerminalPanel::StopDebugSession() {
    m_isDebugActive = false;
    for (auto& tab : m_tabs) {
        if (tab && tab->shellType == TerminalShellType::DebugConsole) {
            tab->SendInput(".exit\r\n");
            tab->Terminate();
        }
    }
    if (m_outputCallback) m_outputCallback();
}

bool TerminalPanel::HitTestButtons(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
        return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
    };
    if (m_isDebugActive && HitTestDebugButtons(pt) != DebugAction::None) {
        return true;
    }
    if (inRect(pt, m_newTabBtnBounds) || inRect(pt, m_shellDropdownBtnBounds) ||
        inRect(pt, m_splitBtnBounds) || inRect(pt, m_optionsBtnBounds) ||
        inRect(pt, m_fullScreenBtnBounds) || inRect(pt, m_minimizeBtnBounds)) {
        return true;
    }
    for (const auto& tab : m_tabs) {
        if (inRect(pt, tab->tabBounds) || inRect(pt, tab->closeBounds)) {
            return true;
        }
        if (tab->splitTab && inRect(pt, tab->splitTab->splitCloseBounds)) {
            return true;
        }
    }
    if (m_showShellMenu && inRect(pt, m_shellMenuBounds)) return true;
    if (m_showOptionsMenu && inRect(pt, m_optionsMenuBounds)) return true;
    if (m_showEngineSubmenu && inRect(pt, m_engineSubmenuBounds)) return true;
    return false;
}

bool TerminalPanel::HitTestTextArea(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    return pt.x >= m_textAreaBounds.left && pt.x <= m_textAreaBounds.right &&
           pt.y >= m_textAreaBounds.top && pt.y <= m_textAreaBounds.bottom;
}

bool TerminalPanel::OnMouseDown(POINT pt, const app::AppConfig& config, const std::wstring& workingDir) {
    (void)config;
    if (!m_isOpen) return false;

    auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
        return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
    };

    // Check Ctrl + Click on URL links
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        std::wstring clickedUrl;
        if (HitTestUrl(pt, &clickedUrl)) {
            ShellExecuteW(NULL, L"open", clickedUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
            return true;
        }
    }

    // Check Debug Toolbar Actions
    if (m_isDebugActive) {
        DebugAction dbgAct = HitTestDebugButtons(pt);
        if (dbgAct != DebugAction::None) {
            TriggerDebugAction(dbgAct);
            return true;
        }
    }

    // 1. Check Splitter Resize
    if (HitTestSplitter(pt)) {
        m_isResizing = true;
        m_dragStartY = static_cast<float>(pt.y);
        m_heightStart = m_height;
        return true;
    }

    // 2. Check Split Button [|]
    if (inRect(pt, m_splitBtnBounds)) {
        SplitActiveTab();
        return true;
    }

    // 3. Check Split Pane Close button
    if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
        auto& primary = m_tabs[m_activeTabIdx];
        if (primary && primary->splitTab && inRect(pt, primary->splitTab->splitCloseBounds)) {
            CloseSplitPane(static_cast<size_t>(m_activeTabIdx));
            return true;
        }
    }

    // 4. Check Terminal Splitter Drag (vertical divider between panes)
    if (HitTestTerminalSplitter(pt)) {
        m_isSplitDragging = true;
        m_splitDragStartX = static_cast<float>(pt.x);
        if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
            m_splitRatioStart = m_tabs[m_activeTabIdx]->splitRatio;
        }
        return true;
    }

    // 5. Check Pane Focus Selection
    if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
        auto& primary = m_tabs[m_activeTabIdx];
        if (primary && primary->splitTab) {
            if (inRect(pt, primary->paneBounds)) {
                primary->activeSplitIndex = 0;
                return true;
            } else if (inRect(pt, primary->splitTab->paneBounds)) {
                primary->activeSplitIndex = 1;
                return true;
            }
        }
    }

    // 6. Check Shell Menu Items (Image 2)
    if (m_showShellMenu) {
        if (inRect(pt, m_shellMenuBounds)) {
            float relY = pt.y - (m_shellMenuBounds.top + 6.0f);
            float itemH = 24.0f;
            int idx = static_cast<int>(relY / itemH);
            if (idx >= 0 && idx < static_cast<int>(m_availableProfiles.size())) {
                NewTab(m_availableProfiles[idx].type, workingDir);
            }
            m_showShellMenu = false;
            return true;
        } else {
            m_showShellMenu = false;
        }
    }

    // 7. Check Options Menu Items (Image 4)
    if (m_showOptionsMenu) {
        if (inRect(pt, m_optionsMenuBounds)) {
            float relY = pt.y - (m_optionsMenuBounds.top + 4.0f);
            float itemH = 20.0f;
            int idx = static_cast<int>(relY / itemH);
            if (idx == 0) {
                m_showEngineSubmenu = !m_showEngineSubmenu;
                return true;
            } else if (idx == 3) {
                CloseAllTabs();
            } else if (idx == 10) {
                Close();
            }
            m_showOptionsMenu = false;
            m_showEngineSubmenu = false;
            return true;
        } else if (m_showEngineSubmenu && inRect(pt, m_engineSubmenuBounds)) {
            m_showEngineSubmenu = false;
            m_showOptionsMenu = false;
            return true;
        } else {
            m_showOptionsMenu = false;
            m_showEngineSubmenu = false;
        }
    }

    // Check Full Screen / Maximize Button
    if (inRect(pt, m_fullScreenBtnBounds)) {
        ToggleFullScreen();
        return true;
    }

    // 8. Check '-' Minimize Button
    if (inRect(pt, m_minimizeBtnBounds)) {
        Close();
        return true;
    }

    // 9. Check '...' Options Button
    if (inRect(pt, m_optionsBtnBounds)) {
        m_showOptionsMenu = !m_showOptionsMenu;
        m_showShellMenu = false;
        return true;
    }

    // 10. Check 'v' Shell Dropdown Button
    if (inRect(pt, m_shellDropdownBtnBounds)) {
        m_showShellMenu = !m_showShellMenu;
        m_showOptionsMenu = false;
        return true;
    }

    // 11. Check '+' New Tab Button
    if (inRect(pt, m_newTabBtnBounds)) {
        NewTab(TerminalShellType::PowerShell, workingDir);
        return true;
    }

    // 12. Check Tabs and Tab Close Buttons
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (inRect(pt, m_tabs[i]->closeBounds)) {
            CloseTab(i);
            return true;
        }
        if (inRect(pt, m_tabs[i]->tabBounds)) {
            m_activeTabIdx = static_cast<int>(i);
            return true;
        }
    }

    // 13. Focus Terminal Content Area
    if (HitTestTextArea(pt)) {
        return true;
    }

    return false;
}

bool TerminalPanel::OnMouseMove(POINT pt, bool isLButtonDown, float clientH) {
    if (!m_isOpen) return false;

    if (m_isResizing && isLButtonDown) {
        float deltaY = m_dragStartY - static_cast<float>(pt.y);
        SetHeight(m_heightStart + deltaY);
        return true;
    }

    if (m_isSplitDragging && isLButtonDown) {
        if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
            auto& primary = m_tabs[m_activeTabIdx];
            if (primary && primary->splitTab) {
                float totalW = m_textAreaBounds.right - m_textAreaBounds.left;
                if (totalW > 50.0f) {
                    float deltaX = static_cast<float>(pt.x) - m_splitDragStartX;
                    float newRatio = m_splitRatioStart + (deltaX / totalW);
                    primary->splitRatio = (std::max)(0.2f, (std::min)(0.8f, newRatio));
                }
            }
        }
        return true;
    }

    auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
        return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
    };

    m_hoverNewTab = inRect(pt, m_newTabBtnBounds);
    m_hoverShellDropdown = inRect(pt, m_shellDropdownBtnBounds);
    m_hoverSplitBtn = inRect(pt, m_splitBtnBounds);
    m_hoverOptions = inRect(pt, m_optionsBtnBounds);
    m_hoverFullScreen = inRect(pt, m_fullScreenBtnBounds);
    m_hoverMinimize = inRect(pt, m_minimizeBtnBounds);

    if (m_isDebugActive) {
        m_hoveredDebugAction = HitTestDebugButtons(pt);
    } else {
        m_hoveredDebugAction = DebugAction::None;
    }

    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && HitTestUrl(pt)) {
        SetCursor(LoadCursor(NULL, IDC_HAND));
    }

    m_hoveredTabIdx = -1;
    m_hoveredCloseIdx = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (inRect(pt, m_tabs[i]->tabBounds)) {
            m_hoveredTabIdx = static_cast<int>(i);
        }
        if (inRect(pt, m_tabs[i]->closeBounds)) {
            m_hoveredCloseIdx = static_cast<int>(i);
        }
    }

    if (m_showShellMenu && inRect(pt, m_shellMenuBounds)) {
        float relY = pt.y - (m_shellMenuBounds.top + 6.0f);
        m_hoveredShellMenuItem = static_cast<int>(relY / 24.0f);
    } else {
        m_hoveredShellMenuItem = -1;
    }

    if (m_showOptionsMenu && inRect(pt, m_optionsMenuBounds)) {
        float relY = pt.y - (m_optionsMenuBounds.top + 4.0f);
        m_hoveredOptionsMenuItem = static_cast<int>(relY / 20.0f);
    } else {
        m_hoveredOptionsMenuItem = -1;
    }

    return false;
}

void TerminalPanel::OnMouseUp(POINT pt) {
    (void)pt;
    m_isResizing = false;
    m_isSplitDragging = false;
}

bool TerminalPanel::OnMouseWheel(POINT pt, short delta) {
    if (!m_isOpen) return false;
    if (m_textAreaBounds.bottom > m_textAreaBounds.top && !HitTestTextArea(pt)) return false;
    TerminalTab* tab = GetActiveTab();
    if (!tab) return false;

    int lineDelta = (delta > 0) ? 3 : -3;
    tab->scrollOffsetLines += lineDelta;
    if (tab->scrollOffsetLines < 0) tab->scrollOffsetLines = 0;
    return true;
}

// =============================================================================
// Keyboard Input Handling
// =============================================================================

bool TerminalPanel::OnChar(wchar_t ch) {
    if (!m_isOpen) return false;
    TerminalTab* tab = GetActiveTab();
    if (!tab) return false;

    if (ch == L'\r' || ch == L'\n') {
        std::wstring trimmed = tab->currentInputLine;
        while (!trimmed.empty() && (trimmed.front() == L' ' || trimmed.front() == L'\t')) trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && (trimmed.back() == L' ' || trimmed.back() == L'\t')) trimmed.pop_back();
        std::wstring lower = trimmed;
        for (auto& c : lower) c = towlower(c);
        if (lower == L"cls" || lower == L"clear" || lower == L"clear-host") {
            std::lock_guard<std::mutex> lock(tab->bufferMutex);
            tab->lines.clear();
            tab->lines.push_back({});
            tab->scrollOffsetY = 0.0f;
            tab->scrollOffsetLines = 0;
        }

        // Add to history
        if (!trimmed.empty()) {
            if (tab->commandHistory.empty() || tab->commandHistory.back() != tab->currentInputLine) {
                tab->commandHistory.push_back(tab->currentInputLine);
            }
        }
        tab->historyIndex = -1;

        // For Node and Python, append user's command to lines so it remains visible above output
        if (tab->shellType == TerminalShellType::Node || tab->shellType == TerminalShellType::Python) {
            std::lock_guard<std::mutex> lock(tab->bufferMutex);
            if (!tab->lines.empty()) {
                tab->lines.back().rawText += tab->currentInputLine;
                tab->lines.back().spans.push_back({ tab->currentInputLine, D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
                tab->lines.push_back({});
            }
        }

        // Convert currentInputLine to UTF-8 and send complete line + newline to process
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, tab->currentInputLine.c_str(), -1, NULL, 0, NULL, NULL);
        if (utf8Len > 1) {
            std::string utf8(utf8Len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, tab->currentInputLine.c_str(), -1, &utf8[0], utf8Len, NULL, NULL);
            utf8 += "\r\n";
            tab->SendInput(utf8);
        } else {
            tab->SendInput("\r\n");
        }

        tab->currentInputLine.clear();
        tab->scrollOffsetLines = 0; // pin back to bottom
        return true;
    } else if (ch == L'\b') {
        if (!tab->currentInputLine.empty()) {
            tab->currentInputLine.pop_back();
        }
        return true;
    } else if (ch == L'\t') {
        tab->currentInputLine += L"    ";
        return true;
    } else if (ch >= 32) {
        tab->currentInputLine += ch;
        return true;
    }
    return true;
}

bool TerminalPanel::OnKeyDown(UINT vk, bool isCtrl, bool isShift) {
    (void)isShift;
    if (!m_isOpen) return false;
    TerminalTab* tab = GetActiveTab();
    if (!tab) return false;

    if (isCtrl && (vk == 'C' || vk == 'c')) {
        tab->SendInput("\x03"); // SIGINT interrupt
        return true;
    }

    if (isCtrl && (vk == 'V' || vk == 'v')) {
        // Paste from clipboard
        if (OpenClipboard(NULL)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                LPCWSTR pText = static_cast<LPCWSTR>(GlobalLock(hData));
                if (pText) {
                    tab->currentInputLine += pText;
                    GlobalUnlock(hData);
                }
            }
            CloseClipboard();
        }
        return true;
    }

    if (vk == VK_BACK) {
        return true; // Swallowed so editor buffer never sees VK_BACK; WM_CHAR handles terminal backspace
    } else if (vk == VK_RETURN) {
        return true; // Swallowed so editor buffer never sees VK_RETURN; WM_CHAR handles terminal newline
    } else if (vk == VK_TAB) {
        return true; // Swallowed so editor buffer never sees VK_TAB; WM_CHAR handles terminal tab
    } else if (vk == VK_DELETE) {
        tab->SendInput("\x1b[3~");
        return true;
    } else if (vk == VK_LEFT) {
        tab->SendInput("\x1b[D");
        return true;
    } else if (vk == VK_RIGHT) {
        tab->SendInput("\x1b[C");
        return true;
    } else if (vk == VK_HOME) {
        tab->SendInput("\x1b[H");
        return true;
    } else if (vk == VK_END) {
        tab->SendInput("\x1b[F");
        return true;
    } else if (vk == VK_UP) {
        if (!tab->commandHistory.empty()) {
            if (tab->historyIndex == -1) {
                tab->historyIndex = static_cast<int>(tab->commandHistory.size()) - 1;
            } else if (tab->historyIndex > 0) {
                tab->historyIndex--;
            }
            if (tab->historyIndex >= 0 && tab->historyIndex < static_cast<int>(tab->commandHistory.size())) {
                tab->currentInputLine = tab->commandHistory[tab->historyIndex];
            }
        }
        return true;
    } else if (vk == VK_DOWN) {
        if (tab->historyIndex >= 0) {
            if (tab->historyIndex + 1 < static_cast<int>(tab->commandHistory.size())) {
                tab->historyIndex++;
                tab->currentInputLine = tab->commandHistory[tab->historyIndex];
            } else {
                tab->historyIndex = -1;
                tab->currentInputLine.clear();
            }
        }
        return true;
    }

    return false;
}

} // namespace phide::ui
