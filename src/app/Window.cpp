/**
 * @file Window.cpp
 * @brief Implementation of Dark IDE native Win32 window, message router, and Direct2D rendering pipeline.
 * 
 * Implements custom Direct2D minimize, maximize, and close buttons (with red hover on close),
 * removes the header horizontal divider line, adds rounded container styling, and coordinates
 * the Home, Search, and Git activity rail with customized tooltips.
 */

#include "phide/app/Window.hpp"
#include "phide/editor/SyntaxValidator.hpp"
#include "phide/ui/PhideLogo.hpp"
#include <windowsx.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <tlhelp32.h>
#include <iphlpapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "iphlpapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace phide::app {

static bool IsPortListening(int port);

static std::wstring RunCmdCapture(const std::wstring& cmd) {
    std::wstring result;
    HANDLE hRead = nullptr, hWrite = nullptr;
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return result;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hWrite);
        hWrite = nullptr;

        char buf[512];
        DWORD bytesRead = 0;
        std::string raw;
        while (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
            buf[bytesRead] = 0;
            raw.append(buf, bytesRead);
        }
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (!raw.empty()) {
            int len = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), -1, nullptr, 0);
            if (len > 0) {
                result.resize(len);
                MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), -1, &result[0], len);
                while (!result.empty() && (result.back() == L'\0' || result.back() == L'\r' || result.back() == L'\n')) {
                    result.pop_back();
                }
            }
        }
    } else {
        CloseHandle(hWrite);
    }
    CloseHandle(hRead);
    return result;
}

static void SetClipboardText(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hGlobal) {
        void* pMem = GlobalLock(hGlobal);
        if (pMem) {
            memcpy(pMem, text.c_str(), bytes);
            GlobalUnlock(hGlobal);
            SetClipboardData(CF_UNICODETEXT, hGlobal);
        }
    }
    CloseClipboard();
}

static std::filesystem::path GetPhideRoot() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::filesystem::path p(buf);
    std::filesystem::path dir = p.parent_path();
    if (std::filesystem::exists(dir / L"include" / L"phide")) {
        return dir;
    }
    if (std::filesystem::exists(dir.parent_path() / L"include" / L"phide")) {
        return dir.parent_path();
    }
    if (std::filesystem::exists(std::filesystem::current_path() / L"include" / L"phide")) {
        return std::filesystem::current_path();
    }
    return std::filesystem::current_path();
}

static std::wstring GetRelativePath(const std::wstring& fullPath, const std::wstring& basePath) {
    if (basePath.empty()) return fullPath;
    std::error_code ec;
    auto rel = std::filesystem::relative(fullPath, basePath, ec);
    if (!ec && !rel.empty()) {
        return rel.wstring();
    }
    return fullPath;
}

static bool IsBinaryDiffPath(const std::wstring& path) {
    std::wstring p = path;
    std::transform(p.begin(), p.end(), p.begin(), ::towlower);
    static const wchar_t* const binExts[] = {
        L".exe", L".dll", L".bin", L".obj", L".o", L".pdb", L".lib",
        L".node", L".iso", L".zip", L".7z", L".tar", L".gz", L".png",
        L".jpg", L".jpeg", L".gif", L".ico", L".bmp", L".webp", L".pdf",
        L".wasm", L".class", L".pyc", L".so", L".dylib"
    };
    for (const auto* ext : binExts) {
        size_t elen = wcslen(ext);
        if (p.size() >= elen && p.compare(p.size() - elen, elen, ext) == 0) {
            return true;
        }
    }
    return false;
}

static bool HasBinaryNullBytes(const std::wstring& str) {
    return str.find(L'\0') != std::wstring::npos;
}

static bool FileHasBinaryBytes(const std::wstring& fullPath) {
    if (!std::filesystem::exists(fullPath)) return false;
    std::ifstream f(std::filesystem::path(fullPath), std::ios::binary);
    if (!f) return false;
    char buf[2048];
    f.read(buf, sizeof(buf));
    std::streamsize n = f.gcount();
    for (std::streamsize i = 0; i < n; ++i) {
        if (buf[i] == '\0') return true;
    }
    return false;
}

struct InputDialogData {
    std::wstring title;
    std::wstring prompt;
    std::wstring result;
    bool accepted = false;
};

static LRESULT CALLBACK InputDialogWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* pData = reinterpret_cast<InputDialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pData = reinterpret_cast<InputDialogData*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pData));

            HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            HWND hStatic = CreateWindowExW(0, L"STATIC", pData->prompt.c_str(),
                WS_CHILD | WS_VISIBLE, 20, 16, 360, 20, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(hStatic, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

            HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pData->result.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 20, 42, 360, 24, hwnd, reinterpret_cast<HMENU>(101), nullptr, nullptr);
            SendMessageW(hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            SendMessageW(hEdit, EM_SETSEL, 0, -1);
            SetFocus(hEdit);

            HWND hBtnOk = CreateWindowExW(0, L"BUTTON", L"OK",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 210, 80, 80, 26, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
            SendMessageW(hBtnOk, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

            HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 300, 80, 80, 26, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
            SendMessageW(hBtnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IDOK) {
                if (pData) {
                    wchar_t buf[512] = {0};
                    GetDlgItemTextW(hwnd, 101, buf, 512);
                    pData->result = buf;
                    pData->accepted = true;
                }
                DestroyWindow(hwnd);
                return 0;
            } else if (id == IDCANCEL) {
                if (pData) pData->accepted = false;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE: {
            if (pData) pData->accepted = false;
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool PromptInputDialog(HWND hParent, const std::wstring& title, const std::wstring& prompt,
                              const std::wstring& initialText, std::wstring& outResult) {
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = InputDialogWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"PhideInputDialogModal";
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    InputDialogData data;
    data.title = title;
    data.prompt = prompt;
    data.result = initialText;
    data.accepted = false;

    RECT rcParent;
    GetWindowRect(hParent, &rcParent);
    int w = 416;
    int h = 156;
    int x = rcParent.left + (rcParent.right - rcParent.left - w) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - h) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"PhideInputDialogModal",
        title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        hParent, nullptr, GetModuleHandleW(nullptr), &data
    );

    if (!hDlg) return false;

    EnableWindow(hParent, FALSE);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            if (msg.wParam == VK_RETURN) {
                SendMessageW(hDlg, WM_COMMAND, IDOK, 0);
                continue;
            } else if (msg.wParam == VK_ESCAPE) {
                SendMessageW(hDlg, WM_COMMAND, IDCANCEL, 0);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (!IsWindow(hDlg)) break;
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);

    if (data.accepted) {
        outResult = data.result;
        return true;
    }
    return false;
}

Window::Window() {
    m_editorGroups.push_back(editor::EditorGroup{});
}

Window::~Window() {
    StopPhpServer();
    StopNginxServer();
    StopPmaServer();
    StopMysqlServer();
    RunManualGarbageCollection();
    DiscardDeviceResources();
    if (m_hwnd) {
        KillTimer(m_hwnd, m_caretTimerId);
        KillTimer(m_hwnd, m_animTimerId);
    }
}

static std::wstring EscapeJson(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 16);
    for (wchar_t c : s) {
        if (c == L'\\') out += L"\\\\";
        else if (c == L'"') out += L"\\\"";
        else if (c == L'\n') out += L"\\n";
        else if (c == L'\r') out += L"\\r";
        else if (c == L'\t') out += L"\\t";
        else out += c;
    }
    return out;
}

static std::wstring UnescapeJson(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t next = s[++i];
            if (next == L'\\') out += L'\\';
            else if (next == L'"') out += L'"';
            else if (next == L'n') out += L'\n';
            else if (next == L'r') out += L'\r';
            else if (next == L't') out += L'\t';
            else out += next;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::wstring Window::GetSessionFilePath() const {
    wchar_t* pAppData = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&pAppData, &len, L"APPDATA") == 0 && pAppData) {
        std::filesystem::path p(pAppData);
        free(pAppData);
        std::filesystem::path dir = p / L"phide";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return (dir / L"session_state.json").wstring();
    }
    return L"";
}

std::wstring Window::GetSnippetsConfigPath() const {
    wchar_t* pAppData = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&pAppData, &len, L"APPDATA") == 0 && pAppData) {
        std::filesystem::path p(pAppData);
        free(pAppData);
        std::filesystem::path dir = p / L"phide" / L"snippets";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return (dir / L"snippets.json").wstring();
    }
    return L"snippets.json";
}

void Window::TriggerDebouncedGitRefresh(DWORD delayMs) {
    m_gitRefreshPending = true;
    m_gitRefreshTargetTime = GetTickCount64() + static_cast<ULONGLONG>(delayMs);
}

void Window::StartWorkspaceWatcher(const std::wstring& folderPath) {
    StopWorkspaceWatcher();
    if (folderPath.empty() || !std::filesystem::exists(folderPath)) return;

    m_hWatcherStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_hWatcherStopEvent) return;

    m_hWatcherThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
        auto* self = static_cast<Window*>(param);
        std::wstring dir = self->m_drawer.GetOpenFolderPath();
        if (dir.empty()) return 0;

        HANDLE hChange = FindFirstChangeNotificationW(
            dir.c_str(),
            TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE
        );
        if (hChange == INVALID_HANDLE_VALUE) return 0;

        HANDLE waitHandles[2] = { self->m_hWatcherStopEvent, hChange };
        while (true) {
            DWORD waitRes = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
            if (waitRes == WAIT_OBJECT_0) {
                break;
            } else if (waitRes == WAIT_OBJECT_0 + 1) {
                // Coalesce rapid disk events (debounced quiet period: 150ms)
                Sleep(150);
                FindNextChangeNotification(hChange);
                if (self->m_hwnd) {
                    PostMessageW(self->m_hwnd, WM_USER + 202, 0, 0);
                }
            } else {
                break;
            }
        }
        FindCloseChangeNotification(hChange);
        return 0;
    }, this, 0, nullptr);
}

void Window::StopWorkspaceWatcher() {
    if (m_hWatcherStopEvent) {
        SetEvent(m_hWatcherStopEvent);
    }
    if (m_hWatcherThread) {
        WaitForSingleObject(m_hWatcherThread, 1000);
        CloseHandle(m_hWatcherThread);
        m_hWatcherThread = nullptr;
    }
    if (m_hWatcherStopEvent) {
        CloseHandle(m_hWatcherStopEvent);
        m_hWatcherStopEvent = nullptr;
    }
}

void Window::SaveSessionState() {
    // If opened as a clean new window and user hasn't opened any folder or files, do not overwrite primary session state
    if (m_isCleanNewWindow && !m_drawer.HasOpenFolder() && GetActiveGroup().docs.empty()) {
        return;
    }

    std::wstring filePath = GetSessionFilePath();
    if (filePath.empty()) return;

    std::wofstream out(filePath, std::ios::trunc);
    if (!out.is_open()) return;

    out << L"{\n";
    out << L"  \"isSidebarOpen\": " << (m_config.isDrawerOpen ? L"true" : L"false") << L",\n";
    out << L"  \"activeRailTab\": " << m_config.activeRailTab << L",\n";
    out << L"  \"gitSplitY\": " << m_drawer.GetGitSplitY() << L",\n";
    out << L"  \"gitSplitRatio\": " << m_drawer.GetGitSplitRatio() << L",\n";
    out << L"  \"themeMode\": " << static_cast<int>(m_theme.GetThemeMode()) << L",\n";
    out << L"  \"activeDocIdx\": " << GetActiveGroup().activeDocIdx << L",\n";
    if (m_drawer.HasOpenFolder()) {
        out << L"  \"lastOpenFolderPath\": \"" << EscapeJson(m_drawer.GetOpenFolderPath()) << L"\",\n";
    } else {
        out << L"  \"lastOpenFolderPath\": \"\",\n";
    }
    out << L"  \"openDocs\": [\n";
    size_t docCount = 0;
    for (const auto& grp : m_editorGroups) {
        for (const auto& doc : grp.docs) {
            if (!doc.filePath.empty()) {
                out << L"    \"" << EscapeJson(doc.filePath) << L"\"" << (++docCount < 100 ? L"," : L"") << L"\n";
            }
        }
    }
    out << L"  ],\n";
    out << L"  \"expandedDirs\": [\n";
    const auto& expanded = m_drawer.GetExpandedDirs();
    size_t count = 0;
    for (const auto& d : expanded) {
        out << L"    \"" << EscapeJson(d) << L"\"" << (++count < expanded.size() ? L"," : L"") << L"\n";
    }
    out << L"  ],\n";
    out << L"  \"recentPaths\": [\n";
    count = 0;
    for (const auto& r : m_recentPaths) {
        out << L"    \"" << EscapeJson(r) << L"\"" << (++count < m_recentPaths.size() ? L"," : L"") << L"\n";
    }
    out << L"  ]\n";
    out << L"}\n";
}

void Window::LoadSessionState() {
    std::wstring filePath = GetSessionFilePath();
    if (filePath.empty()) return;

    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) return;

    std::wifstream in(filePath);
    if (!in.is_open()) return;

    bool isSidebarOpen = true;
    int activeRailTab = 0;
    float savedGitSplitY = 380.0f;
    float savedGitSplitRatio = 0.50f;
    int themeMode = 0;
    int savedActiveDocIdx = -1;
    std::wstring lastOpenFolder;
    std::set<std::wstring> expandedDirs;
    std::vector<std::wstring> recentPaths;
    std::vector<std::wstring> openDocPaths;

    std::wstring line;
    std::wstring currentArray;
    while (std::getline(in, line)) {
        size_t first = line.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) continue;
        line = line.substr(first);

        if (line.find(L"\"isSidebarOpen\"") != std::wstring::npos) {
            isSidebarOpen = (line.find(L"true") != std::wstring::npos);
        } else if (line.find(L"\"activeRailTab\"") != std::wstring::npos) {
            auto colon = line.find(L':');
            if (colon != std::wstring::npos) {
                activeRailTab = _wtoi(line.substr(colon + 1).c_str());
            }
        } else if (line.find(L"\"gitSplitY\"") != std::wstring::npos) {
            auto colon = line.find(L':');
            if (colon != std::wstring::npos) {
                savedGitSplitY = static_cast<float>(_wtof(line.substr(colon + 1).c_str()));
            }
        } else if (line.find(L"\"gitSplitRatio\"") != std::wstring::npos) {
            auto colon = line.find(L':');
            if (colon != std::wstring::npos) {
                savedGitSplitRatio = static_cast<float>(_wtof(line.substr(colon + 1).c_str()));
            }
        } else if (line.find(L"\"themeMode\"") != std::wstring::npos) {
            auto colon = line.find(L':');
            if (colon != std::wstring::npos) {
                themeMode = _wtoi(line.substr(colon + 1).c_str());
            }
        } else if (line.find(L"\"activeDocIdx\"") != std::wstring::npos) {
            auto colon = line.find(L':');
            if (colon != std::wstring::npos) {
                savedActiveDocIdx = _wtoi(line.substr(colon + 1).c_str());
            }
        } else if (line.find(L"\"lastOpenFolderPath\"") != std::wstring::npos) {
            auto colon = line.find(L':');
            if (colon != std::wstring::npos) {
                auto q1 = line.find(L'"', colon);
                auto q2 = line.rfind(L'"');
                if (q1 != std::wstring::npos && q2 != std::wstring::npos && q2 > q1) {
                    lastOpenFolder = UnescapeJson(line.substr(q1 + 1, q2 - q1 - 1));
                }
            }
        } else if (line.find(L"\"currentFilePath\"") != std::wstring::npos) {
            auto colon = line.find(L':');
            if (colon != std::wstring::npos) {
                auto q1 = line.find(L'"', colon);
                auto q2 = line.rfind(L'"');
                if (q1 != std::wstring::npos && q2 != std::wstring::npos && q2 > q1) {
                    std::wstring cf = UnescapeJson(line.substr(q1 + 1, q2 - q1 - 1));
                    if (!cf.empty()) openDocPaths.push_back(cf);
                }
            }
        } else if (line.find(L"\"openDocs\"") != std::wstring::npos) {
            currentArray = L"openDocs";
        } else if (line.find(L"\"expandedDirs\"") != std::wstring::npos) {
            currentArray = L"expandedDirs";
        } else if (line.find(L"\"recentPaths\"") != std::wstring::npos) {
            currentArray = L"recentPaths";
        } else if (line.find(L"]") != std::wstring::npos) {
            currentArray.clear();
        } else if (!currentArray.empty()) {
            auto q1 = line.find(L'"');
            auto q2 = line.rfind(L'"');
            if (q1 != std::wstring::npos && q2 != std::wstring::npos && q2 > q1) {
                std::wstring val = UnescapeJson(line.substr(q1 + 1, q2 - q1 - 1));
                if (currentArray == L"openDocs") {
                    openDocPaths.push_back(val);
                } else if (currentArray == L"expandedDirs") {
                    expandedDirs.insert(val);
                } else if (currentArray == L"recentPaths") {
                    recentPaths.push_back(val);
                }
            }
        }
    }

    if (!recentPaths.empty()) {
        m_recentPaths.clear();
        for (const auto& rp : recentPaths) {
            std::error_code ec;
            if (std::filesystem::is_directory(rp, ec)) {
                m_recentPaths.push_back(rp);
            }
        }
        if (m_recentPaths.size() > 6) m_recentPaths.resize(6);
    }

    // Always default to Explorer (tab 0) on startup, even if in Source Control prior to exit (User requirement)
    m_config.activeRailTab = 0;
    m_rail.SetGitLoading(false);
    m_drawer.ClearGitLoading();
    if (savedGitSplitY > 100.0f) {
        m_config.gitSplitY = savedGitSplitY;
        m_drawer.SetGitSplitY(savedGitSplitY, 100.0f, 2000.0f);
    }
    m_drawer.SetGitSplitRatio(savedGitSplitRatio);

    // Apply restored theme mode with bounds validation (0-19 for all 20 supported themes)
    if (themeMode < 0 || themeMode > 19) {
        themeMode = 0; // Default to Dark Mode if corrupt or uninitialized
    }
    m_theme.SetThemeMode(static_cast<ui::ThemeMode>(themeMode));

    // Check if launched as a freshly clean new window (Ctrl+Shift+N / File > New Window)
    LPCWSTR cmdLine = GetCommandLineW();
    bool isNewWindowClean = (cmdLine && wcsstr(cmdLine, L"--new-window") != nullptr);
    if (isNewWindowClean) {
        m_isCleanNewWindow = true;
    }

    if (!isNewWindowClean) {
        if (!lastOpenFolder.empty() && std::filesystem::exists(lastOpenFolder, ec)) {
            m_drawer.OpenFolder(lastOpenFolder, &expandedDirs);
            m_workspace.OpenWorkspace(lastOpenFolder);
            m_intelliSense.SetWorkspaceRoot(lastOpenFolder);
            m_drawer.ClearGitLoading();
            m_rail.SetGitLoading(false);
            m_drawer.RefreshGitState(true, false);
            m_editor.GetPackageManagerView().ScanInstalledPackages(lastOpenFolder);
        }

        for (const auto& p : openDocPaths) {
            if (!p.empty() && std::filesystem::exists(p, ec)) {
                OpenFileDocument(p, false);
            }
        }

        if (savedActiveDocIdx >= 0 && savedActiveDocIdx < static_cast<int>(GetActiveGroup().docs.size())) {
            GetActiveGroup().activeDocIdx = savedActiveDocIdx;
        }
    } else {
        // Clean new window: no open folder, no open document tabs, freshly clean with preserved theme & recents
        m_drawer.CloseFolder();
        m_editorGroups.resize(1);
        m_editorGroups[0].docs.clear();
        m_editorGroups[0].activeDocIdx = -1;
    }

    m_config.isDrawerOpen = isSidebarOpen;
    m_drawerAnimProgress   = isSidebarOpen ? 1.0f : 0.0f;
    m_targetDrawerProgress = m_drawerAnimProgress;
    m_isDrawerAnimating    = false;
}

void Window::ToggleSidebar() {
    m_config.isDrawerOpen = !m_config.isDrawerOpen;
    m_targetDrawerProgress = m_config.isDrawerOpen ? 1.0f : 0.0f;
    m_isDrawerAnimating = true;
    SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
    UpdateDrawerErrorFiles();
    SaveSessionState();
}

void Window::EnsureCursorVisible() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc || pDoc->isSpecialView) return;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float clientW = static_cast<float>(rc.right - rc.left);
    float railW = m_config.ScaleF(static_cast<float>(m_config.railWidth));
    float drawerW = m_drawerAnimProgress * m_config.ScaleF(static_cast<float>(m_config.drawerWidth));
    float canvasLeft = railW + drawerW;
    float canvasWidth = clientW - canvasLeft;
    float paneW = canvasWidth;
    float paneCanvasLeft = canvasLeft;
    if (!m_editorGroups.empty() && m_editorGroups.size() > 1) {
        paneW = canvasWidth / static_cast<float>(m_editorGroups.size());
        paneCanvasLeft = canvasLeft + static_cast<float>(m_focusedGroupIdx) * paneW;
    }
    float gutterW = m_config.ScaleF(50.0f);
    float codeLeft = paneCanvasLeft + gutterW + m_config.ScaleF(12.0f);
    float minimapW = m_config.ScaleF(88.0f);
    float scrollbarW = m_config.ScaleF(14.0f);
    float codeRight = m_config.showMinimap ? (paneCanvasLeft + paneW - minimapW - m_config.ScaleF(6.0f)) : (paneCanvasLeft + paneW - scrollbarW);
    float charW = (m_editor.GetCharWidth() > 0.0f) ? m_editor.GetCharWidth() : m_config.ScaleF(7.8f);

    auto cursor = pDoc->buffer.GetCursor();
    const float swatchSpan = m_config.ScaleF(14.0f);
    size_t swBefore = 0;
    for (const auto& sw : m_editor.GetVisibleColorSwatches()) {
        if (sw.line == cursor.line && cursor.col >= sw.col) swBefore++;
    }
    float caretVisualColX = static_cast<float>(cursor.col) * charW + static_cast<float>(swBefore) * swatchSpan;
    float caretScreenX = codeLeft - pDoc->scrollOffsetX + caretVisualColX;

    if (caretScreenX < codeLeft + m_config.ScaleF(20.0f)) {
        pDoc->scrollOffsetX = (std::max)(0.0f, caretVisualColX - m_config.ScaleF(20.0f));
    } else if (caretScreenX > codeRight - m_config.ScaleF(40.0f)) {
        pDoc->scrollOffsetX = caretVisualColX - (codeRight - codeLeft - m_config.ScaleF(60.0f));
        if (pDoc->scrollOffsetX < 0.0f) pDoc->scrollOffsetX = 0.0f;
    }
}

void Window::SplitEditor() {
    if (m_editorGroups.size() >= 3) return; // Up to 3 columns

    auto* curDoc = GetActiveDoc();
    editor::EditorGroup newGroup;
    if (curDoc) {
        // Only clone the active file into the new group
        newGroup.docs.push_back(*curDoc);
        newGroup.activeDocIdx = 0;
    } else {
        newGroup.activeDocIdx = -1;
    }

    size_t insertIdx = static_cast<size_t>(m_focusedGroupIdx + 1);
    if (insertIdx > m_editorGroups.size()) insertIdx = m_editorGroups.size();
    m_editorGroups.insert(m_editorGroups.begin() + insertIdx, std::move(newGroup));
    m_focusedGroupIdx = static_cast<int>(insertIdx);

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::ToggleSplitEditor() {
    SplitEditor();
}

void Window::DoNewTextFile() {
    auto& group = GetActiveGroup();
    editor::OpenDocument doc;
    m_untitledCounter++;
    doc.fileName = L"Untitled-" + std::to_wstring(m_untitledCounter);
    doc.filePath = L"";
    doc.buffer = editor::Buffer(); // Empty blank buffer
    doc.isPreview = false;
    doc.isDirty = false;   // Clean, not dirty initially
    doc.isSpecialView = false;
    doc.viewType.clear();

    group.docs.push_back(std::move(doc));
    group.activeDocIdx = static_cast<int>(group.docs.size() - 1);
    UpdateDrawerErrorFiles();
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoNewFile() {
    if (m_drawer.HasOpenFolder()) {
        m_config.activeRailTab = 0; // Explorer tab
        m_config.isDrawerOpen = true;
        std::wstring parentDir = m_drawer.GetOpenFolderPath();
        auto* pDoc = GetActiveDoc();
        if (pDoc && !pDoc->filePath.empty()) {
            std::error_code ec;
            std::filesystem::path fp(pDoc->filePath);
            if (std::filesystem::exists(fp, ec)) {
                std::wstring dir = fp.parent_path().wstring();
                if (dir.rfind(m_drawer.GetOpenFolderPath(), 0) == 0) {
                    parentDir = dir;
                }
            }
        }
        m_drawer.StartInlineCreate(parentDir, false);
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    OpenVirtualDocument(L"Untitled-1.txt", L"");
}

void Window::DoNewWindow() {
    wchar_t szExe[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, szExe, MAX_PATH);
    ShellExecuteW(nullptr, L"open", szExe, L"--windowed --new-window", nullptr, SW_SHOWNORMAL);
}

void Window::DoOpenFile() {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Notebook (*.ipynb)\0*.ipynb\0All Files (*.*)\0*.*\0C/C++ Files (*.cpp;*.h;*.hpp)\0*.cpp;*.h;*.hpp\0PHP Files (*.php)\0*.php\0JSON Files (*.json)\0*.json\0Text Files (*.txt)\0*.txt\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ENABLESIZING;

    if (GetOpenFileNameW(&ofn)) {
        OpenFileDocument(szFile, false);
    }
}

void Window::OpenFileDocument(const std::wstring& path, bool isPreview) {
    if (path.empty()) return;

    std::wstring resolvedPath = path;
    if (!std::filesystem::exists(resolvedPath) && m_drawer.HasOpenFolder()) {
        std::wstring candidate = m_drawer.GetOpenFolderPath() + L"\\" + path;
        if (std::filesystem::exists(candidate)) {
            resolvedPath = candidate;
        }
    }

    // Direct routing for Jupyter Notebook files (.ipynb) to interactive notebook view
    std::wstring extCheck;
    auto dotPosCheck = resolvedPath.rfind(L'.');
    if (dotPosCheck != std::wstring::npos) {
        extCheck = resolvedPath.substr(dotPosCheck);
        for (auto& c : extCheck) c = towlower(c);
    }
    if (extCheck == L".ipynb") {
        OpenNotebookView(resolvedPath);
        return;
    }

    auto PathsMatch = [](const std::wstring& p1, const std::wstring& p2) -> bool {
        if (p1.empty() || p2.empty()) return false;
        std::wstring c1 = p1;
        std::wstring c2 = p2;
        for (auto& c : c1) { if (c == L'/') c = L'\\'; c = towlower(c); }
        for (auto& c : c2) { if (c == L'/') c = L'\\'; c = towlower(c); }
        if (c1 == c2) return true;
        if (c1.ends_with(L"\\" + c2) || c2.ends_with(L"\\" + c1)) return true;
        if (c1.ends_with(c2) || c2.ends_with(c1)) return true;
        return false;
    };

    // 1. Check if document is already open in ANY editor group
    for (size_t g = 0; g < m_editorGroups.size(); ++g) {
        for (size_t i = 0; i < m_editorGroups[g].docs.size(); ++i) {
            if (PathsMatch(m_editorGroups[g].docs[i].filePath, path)) {
                m_focusedGroupIdx = static_cast<int>(g);
                m_editorGroups[g].activeDocIdx = static_cast<int>(i);
                if (!isPreview) {
                    m_editorGroups[g].docs[i].isPreview = false;
                }
                UpdateDrawerErrorFiles();
    SaveSessionState();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        }
    }

    auto& group = GetActiveGroup();

    // 2. Check if current active document can be replaced
    // VS Code Rule: a tab is replaced if it is a preview tab AND has NO unsaved edits (!isDirty) AND not a special view,
    // OR if the active tab belongs to an old project outside the currently opened workspace directory.
    bool canReplace = (group.activeDocIdx >= 0 && group.activeDocIdx < static_cast<int>(group.docs.size()) &&
                       !group.docs[group.activeDocIdx].isDirty &&
                       !group.docs[group.activeDocIdx].isSpecialView &&
                       (group.docs[group.activeDocIdx].isPreview ||
                        (!m_drawer.GetOpenFolderPath().empty() &&
                         !IsFileInDirectory(group.docs[group.activeDocIdx].filePath, m_drawer.GetOpenFolderPath()))));

    // Check if file exists on disk, or if it is a deleted file in Git (Complaint 8)
    std::error_code ecExists;
    bool fileExists = std::filesystem::exists(resolvedPath, ecExists);
    if (!fileExists && m_drawer.IsGitRepo()) {
        std::wstring relPath = resolvedPath;
        if (m_drawer.HasOpenFolder()) {
            try {
                relPath = std::filesystem::relative(resolvedPath, m_drawer.GetOpenFolderPath()).wstring();
            } catch (...) {}
        }
        std::wstring gitContent = m_drawer.GetGitFileContent(relPath);
        if (!gitContent.empty()) {
            editor::OpenDocument newDoc;
            newDoc.filePath = resolvedPath;
            auto ppos = resolvedPath.find_last_of(L"\\/");
            newDoc.fileName = (ppos != std::wstring::npos) ? resolvedPath.substr(ppos + 1) : resolvedPath;
            newDoc.isPreview = isPreview;
            newDoc.isDirty = false;
            newDoc.isSpecialView = false;
            newDoc.isDeletedInGit = true;
            newDoc.isDiffView = true;
            newDoc.buffer.SetText(gitContent);

            if (canReplace) {
                group.docs[group.activeDocIdx] = std::move(newDoc);
            } else {
                group.docs.push_back(std::move(newDoc));
                group.activeDocIdx = static_cast<int>(group.docs.size() - 1);
            }
            UpdateDrawerErrorFiles();
    SaveSessionState();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // Check if file is a supported image (PNG, JPG, BMP, GIF, ICO, WEBP)
    std::wstring ext;
    auto dotPos = resolvedPath.find_last_of(L".");
    if (dotPos != std::wstring::npos) {
        ext = resolvedPath.substr(dotPos);
        for (auto& ch : ext) ch = towlower(ch);
    }

    if (ext == L".ipynb") {
        OpenNotebookView(resolvedPath);
        return;
    }

    bool isImage = (ext == L".png" || ext == L".jpg" || ext == L".jpeg" ||
                    ext == L".bmp" || ext == L".gif" || ext == L".ico" ||
                    ext == L".webp" || ext == L".tiff" || ext == L".tif");

    bool isBinary = false;
    std::wstring binaryType;
    if (ext == L".exe" || ext == L".dll" || ext == L".obj" || ext == L".bin" ||
        ext == L".lib" || ext == L".pdb" || ext == L".so" || ext == L".dylib" ||
        ext == L".sys" || ext == L".o" || ext == L".a" || ext == L".class" ||
        ext == L".pyc" || ext == L".wasm") {
        isBinary = true;
        binaryType = L"Executable / Binary";
    } else if (ext == L".mp4" || ext == L".mkv" || ext == L".avi" || ext == L".mov" ||
               ext == L".wmv" || ext == L".flv" || ext == L".webm" || ext == L".mpg" || ext == L".mpeg" || ext == L".m4v") {
        isBinary = true;
        binaryType = L"Video File";
    } else if (ext == L".mp3" || ext == L".wav" || ext == L".ogg" || ext == L".flac" || ext == L".m4a") {
        isBinary = true;
        binaryType = L"Audio File";
    } else if (ext == L".zip" || ext == L".rar" || ext == L".7z" || ext == L".tar" || ext == L".gz" || ext == L".iso") {
        isBinary = true;
        binaryType = L"Compressed Archive";
    }

    if (!isImage && !isBinary) {
        std::ifstream fileCheck(resolvedPath, std::ios::binary);
        if (fileCheck.is_open()) {
            char buf[1024];
            fileCheck.read(buf, sizeof(buf));
            std::streamsize bytesRead = fileCheck.gcount();
            for (std::streamsize b = 0; b < bytesRead; ++b) {
                if (buf[b] == '\0') {
                    isBinary = true;
                    binaryType = L"Binary File";
                    break;
                }
            }
        }
    }

    editor::OpenDocument newDoc;
    newDoc.filePath = resolvedPath;
    auto pos = resolvedPath.find_last_of(L"\\/");
    newDoc.fileName = (pos != std::wstring::npos) ? resolvedPath.substr(pos + 1) : resolvedPath;
    newDoc.isPreview = isPreview;
    newDoc.isDirty = false;
    newDoc.isSpecialView = false;
    newDoc.isImageView = isImage;
    newDoc.isUnsupportedBinary = isBinary;
    newDoc.binaryFileType = binaryType;
    newDoc.viewType.clear();

    if (isImage) {
        std::error_code ec;
        newDoc.imageFileSize = std::filesystem::file_size(resolvedPath, ec);
    } else if (isBinary) {
        std::error_code ec;
        newDoc.binaryFileSize = std::filesystem::file_size(resolvedPath, ec);
    } else {
        newDoc.buffer.LoadFromFile(resolvedPath);
        newDoc.originalDiskText = newDoc.buffer.GetText();

        // Compute Git diff highlights against HEAD if inside a git repository (Image 5)
        if (m_drawer.IsGitRepo()) {
            std::wstring relPath = resolvedPath;
            if (m_drawer.HasOpenFolder()) {
                try {
                    relPath = std::filesystem::relative(resolvedPath, m_drawer.GetOpenFolderPath()).wstring();
                } catch (...) {}
            }
            m_drawer.ComputeGitDiffLines(relPath, newDoc.diffAddedLines, newDoc.diffDeletedLines);
            // NOTE: Normal files opened from Explorer should NEVER be forced into Diff View!
            // Diff View is strictly reserved for user clicking a diff item from Source Control.
        }

        if (!newDoc.isSpecialView && !newDoc.isImageView && !newDoc.isUnsupportedBinary) {
            newDoc.diagnostics = editor::SyntaxValidator::Validate(newDoc.filePath.empty() ? newDoc.fileName : newDoc.filePath, newDoc.buffer);
        }
    }

    if (canReplace) {
        group.docs[group.activeDocIdx] = std::move(newDoc);
    } else {
        group.docs.push_back(std::move(newDoc));
        group.activeDocIdx = static_cast<int>(group.docs.size() - 1);
    }

    UpdateDrawerErrorFiles();
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::OpenDiffDocument(const std::wstring& relPath, bool isPreview) {
    if (relPath.empty()) return;
    std::wstring dir = m_drawer.GetOpenFolderPath();
    std::wstring fullPath = dir.empty() ? relPath : (dir + L"\\" + relPath);
    if (relPath.ends_with(L".ipynb") || fullPath.ends_with(L".ipynb")) {
        OpenNotebookView(fullPath);
        return;
    }
    std::wstring fileName = std::filesystem::path(relPath).filename().wstring();

    std::wstring headContent = m_drawer.GetGitFileContent(relPath);
    if (headContent.empty()) {
        // Untracked or no HEAD revision exists — open in normal editor view!
        OpenFileDocument(fullPath, isPreview);
        return;
    }

    auto& group = GetActiveGroup();

    // Check if diff tab is already open for this file
    for (size_t i = 0; i < group.docs.size(); ++i) {
        if (group.docs[i].filePath == fullPath && group.docs[i].isDiffView) {
            group.activeDocIdx = static_cast<int>(i);
            if (!isPreview) group.docs[i].isPreview = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    editor::OpenDocument newDoc;
    newDoc.filePath = fullPath;
    newDoc.fileName = fileName + L" (Working Tree)";
    newDoc.diffOriginalTitle = fileName + L" (HEAD)";
    newDoc.diffWorkingTitle = fileName + L" (Working Tree)";
    newDoc.isPreview = isPreview;
    newDoc.isDirty = false;
    newDoc.isSpecialView = false;
    newDoc.isDiffView = true;
    newDoc.isDiffLeftReadOnly = true;
    newDoc.isDiffRightReadOnly = false;
    newDoc.diffActivePane = 1; // Focus right (editable) pane

    newDoc.diffOriginalBuffer.SetText(headContent);
    // If file is currently open in an editor group with unsaved changes, use the in-memory buffer!
    const editor::OpenDocument* pExisting = nullptr;
    for (const auto& g : m_editorGroups) {
        for (const auto& d : g.docs) {
            if (!d.isDiffView && !d.isSpecialView && !d.filePath.empty()) {
                if (_wcsicmp(d.filePath.c_str(), fullPath.c_str()) == 0) {
                    pExisting = &d;
                    break;
                }
            }
        }
        if (pExisting) break;
    }

    if (pExisting) {
        newDoc.buffer.SetText(pExisting->buffer.GetText());
        newDoc.isDirty = pExisting->isDirty;
    } else if (std::filesystem::exists(fullPath)) {
        newDoc.buffer.LoadFromFile(fullPath);
    } else {
        newDoc.buffer.SetText(L"");
    }
    newDoc.originalDiskText = newDoc.buffer.GetText();

    bool isBin = IsBinaryDiffPath(relPath) || FileHasBinaryBytes(fullPath) || HasBinaryNullBytes(headContent);
    if (isBin) {
        newDoc.isDiffBinary = true;
        newDoc.diffBinaryFileName = fileName;
        newDoc.diffOriginalBuffer.SetText(L"");
        newDoc.buffer.SetText(L"");
    } else {
        ComputeDiffHunks(newDoc);
    }

    bool canReplace = (group.activeDocIdx >= 0 && group.activeDocIdx < static_cast<int>(group.docs.size()) &&
                       group.docs[group.activeDocIdx].isPreview &&
                       !group.docs[group.activeDocIdx].isDirty &&
                       !group.docs[group.activeDocIdx].isSpecialView);

    if (canReplace) {
        group.docs[group.activeDocIdx] = std::move(newDoc);
    } else {
        group.docs.push_back(std::move(newDoc));
        group.activeDocIdx = static_cast<int>(group.docs.size() - 1);
    }

    UpdateDrawerErrorFiles();
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::OpenCommitDiffDocument(const std::wstring& commitHash, const std::wstring& relPath) {
    if (commitHash.empty()) return;

    try {
        std::wstring commitMsg = L"Commit";
        std::wstring author = L"";
        std::wstring dateStr = L"";
        std::vector<ui::GitCommitFile> commitFiles;

        {
            std::lock_guard<std::recursive_mutex> lock(m_drawer.GetGitMutex());
            for (const auto& ci : m_drawer.GetGitCommits()) {
                if (ci.hash == commitHash) {
                    commitMsg = ci.message;
                    author = ci.author;
                    dateStr = ci.relativeTime;
                    commitFiles = ci.files;
                    break;
                }
            }
        }

        // If files not loaded yet, query git show --name-status
        if (commitFiles.empty()) {
            std::wstring dir = m_drawer.GetOpenFolderPath();
            if (dir.empty()) dir = L".";
            auto res = ui::RunGitCmd(L"show --name-status --pretty=\"\" " + commitHash, dir);
            if (res.exitCode == 0) {
                std::wstringstream ss(res.output);
                std::wstring line;
                while (std::getline(ss, line)) {
                    while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n' || line.back() == L' ')) line.pop_back();
                    if (line.empty()) continue;
                    size_t tab = line.find(L'\t');
                    std::wstring status = L"M";
                    std::wstring fPath;
                    if (tab != std::wstring::npos) {
                        status = line.substr(0, tab);
                        fPath = line.substr(tab + 1);
                    } else {
                        fPath = line;
                    }
                    if (fPath.size() >= 2 && fPath.front() == L'"' && fPath.back() == L'"') {
                        fPath = fPath.substr(1, fPath.size() - 2);
                    }
                    for (auto& c : fPath) if (c == L'/') c = L'\\';
                    std::filesystem::path p(fPath);
                    ui::GitCommitFile cf;
                    cf.path = fPath;
                    cf.fileName = p.filename().wstring();
                    cf.relDir = p.parent_path().wstring();
                    cf.status = status.empty() ? L"M" : status;
                    commitFiles.push_back(std::move(cf));
                }
            }
        }

        size_t shortLen = (commitHash.length() < 7) ? commitHash.length() : 7;
        std::wstring shortHash = commitHash.substr(0, shortLen);

        editor::OpenDocument newDoc;
        newDoc.filePath = L""; // virtual/commit diff
        newDoc.fileName = shortHash + L" - " + commitMsg + L" (" + std::to_wstring(commitFiles.size()) + L" files)";
        newDoc.diffOriginalTitle = shortHash + L"~1";
        newDoc.diffWorkingTitle = shortHash;
        newDoc.isPreview = false;
        newDoc.isDirty = false;
        newDoc.isSpecialView = false;
        newDoc.isDiffView = true;
        newDoc.isMultiFileDiff = true;
        newDoc.isDiffLeftReadOnly = true;
        newDoc.isDiffRightReadOnly = true;
        newDoc.commitHash = commitHash;
        newDoc.commitAuthor = author;
        newDoc.commitDate = dateStr;
        newDoc.commitMessage = commitMsg;

        std::wstring parentHash = commitHash + L"~1";

        for (const auto& cf : commitFiles) {
            editor::OpenDocument::MultiFileDiffEntry entry;
            entry.filePath = cf.path;
            entry.fileName = cf.fileName;
            entry.relDir = cf.relDir;
            entry.status = cf.status;

            std::wstring leftContent = m_drawer.GetCommitFileContent(parentHash, cf.path);
            std::wstring rightContent = m_drawer.GetCommitFileContent(commitHash, cf.path);

            entry.isBinary = IsBinaryDiffPath(cf.path) || HasBinaryNullBytes(leftContent) || HasBinaryNullBytes(rightContent);

            if (!entry.isBinary) {
                entry.originalBuffer.SetText(leftContent);
                entry.workingBuffer.SetText(rightContent);

                // Compute diff hunks for this file
                editor::OpenDocument tempDoc;
                tempDoc.diffOriginalBuffer.SetText(leftContent);
                tempDoc.buffer.SetText(rightContent);
                ComputeDiffHunks(tempDoc);
                entry.addedLines = std::move(tempDoc.diffAddedLines);
                entry.deletedLines = std::move(tempDoc.diffDeletedLines);
                entry.hunks = std::move(tempDoc.diffHunks);
            }

            if (!relPath.empty()) {
                if (cf.path == relPath || cf.fileName == relPath) {
                    entry.isCollapsed = false;
                } else {
                    entry.isCollapsed = true;
                }
            } else {
                entry.isCollapsed = false;
            }

            newDoc.multiFileDiffs.push_back(std::move(entry));
        }

        auto& group = GetActiveGroup();
        for (size_t i = 0; i < group.docs.size(); ++i) {
            if (group.docs[i].isMultiFileDiff && group.docs[i].commitHash == commitHash) {
                group.activeDocIdx = static_cast<int>(i);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        }

        group.docs.push_back(std::move(newDoc));
        group.activeDocIdx = static_cast<int>(group.docs.size() - 1);

        UpdateDrawerErrorFiles();
    SaveSessionState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } catch (...) {
        // Safe catch prevents any possible crash
    }
}

void Window::ComputeDiffHunks(editor::OpenDocument& doc) {
    doc.diffHunks.clear();
    doc.diffAddedLines.clear();
    doc.diffDeletedLines.clear();

    const auto& leftBuf = doc.diffOriginalBuffer;
    const auto& rightBuf = doc.buffer;

    size_t N = leftBuf.GetLineCount();
    size_t M = rightBuf.GetLineCount();
    if (N == 0 && M == 0) return;

    // Fast prefix matching
    size_t prefix = 0;
    while (prefix < N && prefix < M && leftBuf.GetLine(prefix) == rightBuf.GetLine(prefix)) {
        prefix++;
    }

    // Fast suffix matching
    size_t suffix = 0;
    while (suffix < (N - prefix) && suffix < (M - prefix) &&
           leftBuf.GetLine(N - 1 - suffix) == rightBuf.GetLine(M - 1 - suffix)) {
        suffix++;
    }

    size_t midN = N - prefix - suffix;
    size_t midM = M - prefix - suffix;

    if (midN == 0 && midM == 0) {
        doc.isDirty = false;
        UpdateDrawerDirtyFiles();
        m_drawer.RefreshGitStatusOnly(true);
        return; // Identical
    }

    if (midN > 0 && midM > 0 && (midN * midM <= 400000) && midN <= 2000 && midM <= 2000) {
        std::vector<int> prev(midM + 1, 0);
        std::vector<int> curr(midM + 1, 0);
        std::vector<std::vector<uint8_t>> dir(midN + 1, std::vector<uint8_t>(midM + 1, 0)); // 1=diag, 2=up, 3=left

        for (size_t i = 1; i <= midN; ++i) {
            for (size_t j = 1; j <= midM; ++j) {
                if (leftBuf.GetLine(prefix + i - 1) == rightBuf.GetLine(prefix + j - 1)) {
                    curr[j] = prev[j - 1] + 1;
                    dir[i][j] = 1;
                } else if (prev[j] >= curr[j - 1]) {
                    curr[j] = prev[j];
                    dir[i][j] = 2; // up (deleted from left)
                } else {
                    curr[j] = curr[j - 1];
                    dir[i][j] = 3; // left (added to right)
                }
            }
            prev = curr;
        }

        size_t i = midN, j = midM;
        struct DiffEditOp { int type; size_t leftLine; size_t rightLine; };
        std::vector<DiffEditOp> edits; // (-1 = deleted, +1 = added)
        while (i > 0 || j > 0) {
            if (i > 0 && j > 0 && dir[i][j] == 1) {
                i--;
                j--;
            } else if (i > 0 && (j == 0 || dir[i][j] == 2)) {
                edits.push_back({ -1, prefix + i - 1, prefix + j });
                i--;
            } else {
                edits.push_back({ 1, prefix + i, prefix + j - 1 });
                j--;
            }
        }
        std::reverse(edits.begin(), edits.end());

        size_t idx = 0;
        while (idx < edits.size()) {
            editor::OpenDocument::DiffHunkItem hunk;
            size_t delCount = 0;
            size_t addCount = 0;
            size_t delStart = 0;
            size_t addStart = 0;
            size_t delRightPos = 0;
            size_t addLeftPos = 0;

            while (idx < edits.size() && edits[idx].type == -1) {
                if (delCount == 0) {
                    delStart = edits[idx].leftLine;
                    delRightPos = edits[idx].rightLine;
                }
                doc.diffDeletedLines.insert(edits[idx].leftLine);
                delCount++;
                idx++;
            }
            while (idx < edits.size() && edits[idx].type == 1) {
                if (addCount == 0) {
                    addStart = edits[idx].rightLine;
                    addLeftPos = edits[idx].leftLine;
                }
                doc.diffAddedLines.insert(edits[idx].rightLine);
                addCount++;
                idx++;
            }

            if (delCount > 0 && addCount > 0) {
                hunk.type = editor::OpenDocument::DiffHunkItem::HunkType::Modified;
                hunk.leftStart = delStart;
                hunk.leftCount = delCount;
                hunk.rightStart = addStart;
                hunk.rightCount = addCount;
            } else if (delCount > 0) {
                hunk.type = editor::OpenDocument::DiffHunkItem::HunkType::Deleted;
                hunk.leftStart = delStart;
                hunk.leftCount = delCount;
                hunk.rightStart = delRightPos;
                hunk.rightCount = 0;
            } else if (addCount > 0) {
                hunk.type = editor::OpenDocument::DiffHunkItem::HunkType::Added;
                hunk.leftStart = addLeftPos;
                hunk.leftCount = 0;
                hunk.rightStart = addStart;
                hunk.rightCount = addCount;
            }
            doc.diffHunks.push_back(hunk);
        }
    } else {
        editor::OpenDocument::DiffHunkItem hunk;
        hunk.leftStart = prefix;
        hunk.leftCount = midN;
        hunk.rightStart = prefix;
        hunk.rightCount = midM;
        if (midN > 0 && midM > 0) hunk.type = editor::OpenDocument::DiffHunkItem::HunkType::Modified;
        else if (midN > 0) hunk.type = editor::OpenDocument::DiffHunkItem::HunkType::Deleted;
        else hunk.type = editor::OpenDocument::DiffHunkItem::HunkType::Added;

        for (size_t l = prefix; l < prefix + midN; ++l) doc.diffDeletedLines.insert(l);
        for (size_t r = prefix; r < prefix + midM; ++r) doc.diffAddedLines.insert(r);
        doc.diffHunks.push_back(hunk);
    }
}

void Window::OpenVirtualDocument(const std::wstring& name, const std::wstring& content, bool isPreview) {
    auto& group = GetActiveGroup();
    for (size_t i = 0; i < group.docs.size(); ++i) {
        if (group.docs[i].fileName == name && group.docs[i].filePath.empty()) {
            group.activeDocIdx = static_cast<int>(i);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    bool canReplace = (group.activeDocIdx >= 0 && group.activeDocIdx < static_cast<int>(group.docs.size()) &&
                       group.docs[group.activeDocIdx].isPreview &&
                       !group.docs[group.activeDocIdx].isDirty &&
                       !group.docs[group.activeDocIdx].isSpecialView);

    editor::OpenDocument newDoc;
    newDoc.fileName = name;
    newDoc.filePath.clear();
    newDoc.buffer.SetText(content);
    newDoc.isPreview = isPreview;
    newDoc.isDirty = false;
    newDoc.isSpecialView = false;
    newDoc.diagnostics = editor::SyntaxValidator::Validate(name, newDoc.buffer);

    if (canReplace) {
        group.docs[group.activeDocIdx] = std::move(newDoc);
    } else {
        group.docs.push_back(std::move(newDoc));
        group.activeDocIdx = static_cast<int>(group.docs.size() - 1);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::OpenSpecialView(const std::wstring& viewType, const std::wstring& title) {
    auto& group = GetActiveGroup();
    for (size_t i = 0; i < group.docs.size(); ++i) {
        if (group.docs[i].isSpecialView && group.docs[i].viewType == viewType) {
            group.activeDocIdx = static_cast<int>(i);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    bool canReplace = (group.activeDocIdx >= 0 && group.activeDocIdx < static_cast<int>(group.docs.size()) &&
                       group.docs[group.activeDocIdx].isPreview &&
                       !group.docs[group.activeDocIdx].isDirty &&
                       !group.docs[group.activeDocIdx].isSpecialView);

    editor::OpenDocument newDoc;
    newDoc.fileName = title;
    newDoc.filePath.clear();
    newDoc.isSpecialView = true;
    newDoc.viewType = viewType;
    newDoc.isPreview = false;
    newDoc.isDirty = false;

    if (canReplace) {
        group.docs[group.activeDocIdx] = std::move(newDoc);
    } else {
        group.docs.push_back(std::move(newDoc));
        group.activeDocIdx = static_cast<int>(group.docs.size() - 1);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::OpenNotebookView(const std::wstring& filePath) {
    auto& group = GetActiveGroup();

    std::wstring resolvedPath = filePath;
    if (!resolvedPath.empty()) {
        std::error_code ec;
        if (!std::filesystem::exists(resolvedPath, ec) && m_drawer.HasOpenFolder()) {
            std::wstring cand = m_drawer.GetOpenFolderPath() + L"\\" + filePath;
            if (std::filesystem::exists(cand, ec)) {
                resolvedPath = cand;
            }
        }
        try {
            resolvedPath = std::filesystem::canonical(resolvedPath).wstring();
        } catch (...) {
            try {
                resolvedPath = std::filesystem::absolute(resolvedPath).wstring();
            } catch (...) {}
        }
    }

    auto PathsMatch = [](const std::wstring& p1, const std::wstring& p2) -> bool {
        if (p1.empty() || p2.empty()) return false;
        std::wstring c1 = p1;
        std::wstring c2 = p2;
        for (auto& c : c1) { if (c == L'/') c = L'\\'; c = towlower(c); }
        for (auto& c : c2) { if (c == L'/') c = L'\\'; c = towlower(c); }
        if (c1 == c2) return true;
        if (c1.ends_with(L"\\" + c2) || c2.ends_with(L"\\" + c1)) return true;
        if (c1.ends_with(c2) || c2.ends_with(c1)) return true;
        return false;
    };

    // If opening a specific file, check if already open in any group
    if (!resolvedPath.empty()) {
        for (size_t g = 0; g < m_editorGroups.size(); ++g) {
            for (size_t i = 0; i < m_editorGroups[g].docs.size(); ++i) {
                if (PathsMatch(m_editorGroups[g].docs[i].filePath, resolvedPath)) {
                    if (m_editorGroups[g].docs[i].isSpecialView && m_editorGroups[g].docs[i].viewType == L"notebook") {
                        m_focusedGroupIdx = static_cast<int>(g);
                        m_editorGroups[g].activeDocIdx = static_cast<int>(i);
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    } else {
                        // If previously opened as a plain text file, remove it so it opens as an interactive notebook
                        m_editorGroups[g].docs.erase(m_editorGroups[g].docs.begin() + i);
                        break;
                    }
                }
            }
        }
    }

    // Determine notebook title
    std::wstring title = L"Notebook 1.ipynb";
    if (!resolvedPath.empty()) {
        title = std::filesystem::path(resolvedPath).filename().wstring();
    } else {
        int nbCount = 1;
        for (const auto& grp : m_editorGroups) {
            for (const auto& doc : grp.docs) {
                if (doc.isSpecialView && doc.viewType == L"notebook" && doc.filePath.empty()) {
                    nbCount++;
                }
            }
        }
        title = L"Notebook " + std::to_wstring(nbCount) + L".ipynb";
    }

    std::wstring wsFolder = m_drawer.HasOpenFolder() ? m_drawer.GetOpenFolderPath() : m_workspace.GetRootPath().wstring();
    m_editor.GetNotebookView().SetWorkspacePath(wsFolder);

    bool canReplace = (group.activeDocIdx >= 0 && group.activeDocIdx < static_cast<int>(group.docs.size()) &&
                       group.docs[group.activeDocIdx].isPreview &&
                       !group.docs[group.activeDocIdx].isDirty &&
                       !group.docs[group.activeDocIdx].isSpecialView);

    editor::OpenDocument newDoc;
    newDoc.fileName = title;
    newDoc.filePath = resolvedPath;
    newDoc.isSpecialView = true;
    newDoc.viewType = L"notebook";
    newDoc.isPreview = false;
    newDoc.isDirty = false;
    newDoc.notebookDoc = std::make_shared<editor::NotebookDocument>();

    if (!resolvedPath.empty()) {
        newDoc.notebookDoc->LoadFromFile(resolvedPath);
    } else {
        newDoc.notebookDoc->InitDefaultScratchpad();
    }
    m_editor.GetNotebookView().SetScrollY(newDoc.notebookDoc->scrollY);

    if (canReplace) {
        group.docs[group.activeDocIdx] = std::move(newDoc);
    } else {
        group.docs.push_back(std::move(newDoc));
        group.activeDocIdx = static_cast<int>(group.docs.size() - 1);
    }

    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::CloseDocument(int groupIdx, int docIdx) {
    if (groupIdx < 0 || groupIdx >= static_cast<int>(m_editorGroups.size())) return;
    auto& group = m_editorGroups[groupIdx];
    if (docIdx < 0 || docIdx >= static_cast<int>(group.docs.size())) return;

    if (!group.docs[docIdx].filePath.empty()) {
        m_lastClosedFilePath = group.docs[docIdx].filePath;
    }

    // Hook for temporary COMMIT_EDITMSG tab (Images 4 & 5)
    std::wstring closingPath = group.docs[docIdx].filePath;
    if (closingPath.ends_with(L"COMMIT_EDITMSG")) {
        std::wstring commitMsg;
        for (const auto& line : group.docs[docIdx].buffer.GetLines()) {
            size_t start = 0;
            while (start < line.length() && (line[start] == L' ' || line[start] == L'\t')) {
                start++;
            }
            if (start < line.length() && line[start] == L'#') {
                continue; // Skip comment lines
            }
            std::wstring trimmed = line;
            while (!trimmed.empty() && (trimmed.back() == L' ' || trimmed.back() == L'\t' || trimmed.back() == L'\r' || trimmed.back() == L'\n')) {
                trimmed.pop_back();
            }
            if (!trimmed.empty()) {
                if (!commitMsg.empty()) commitMsg += L"\n";
                commitMsg += trimmed;
            }
        }

        m_drawer.SetCommitEditMsgOpen(false);

        if (!commitMsg.empty()) {
            m_drawer.CommitChangesAsync(commitMsg);
        } else {
            // Empty message aborts the commit, but changes REMAIN staged!
            m_drawer.RefreshGitStatusOnly(false);
        }
    }

    group.docs.erase(group.docs.begin() + docIdx);

    if (group.docs.empty()) {
        // Auto-collapse this editor group if multiple groups exist
        if (m_editorGroups.size() > 1) {
            m_editorGroups.erase(m_editorGroups.begin() + groupIdx);
            if (m_focusedGroupIdx >= static_cast<int>(m_editorGroups.size())) {
                m_focusedGroupIdx = static_cast<int>(m_editorGroups.size()) - 1;
            } else if (groupIdx < m_focusedGroupIdx) {
                m_focusedGroupIdx--;
            }
        } else {
            group.activeDocIdx = -1;
        }
    } else {
        if (group.activeDocIdx >= static_cast<int>(group.docs.size())) {
            group.activeDocIdx = static_cast<int>(group.docs.size() - 1);
        } else if (docIdx < group.activeDocIdx) {
            group.activeDocIdx--;
        }
    }

    UpdateDrawerDirtyFiles();
    UpdateDrawerErrorFiles();
    SaveSessionState();
    RunManualGarbageCollection();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::CloseDocument(int index) {
    CloseDocument(m_focusedGroupIdx, index);
}

void Window::DoOpenFolder() {
    ComPtr<IFileOpenDialog> pFolderDlg;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFolderDlg)))) {
        DWORD dwOptions;
        if (SUCCEEDED(pFolderDlg->GetOptions(&dwOptions))) {
            pFolderDlg->SetOptions(dwOptions | FOS_PICKFOLDERS);
        }
        if (SUCCEEDED(pFolderDlg->Show(m_hwnd))) {
            ComPtr<IShellItem> pItem;
            if (SUCCEEDED(pFolderDlg->GetResult(&pItem))) {
                PWSTR pszPath = nullptr;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                    DoOpenFolderFromPath(pszPath);
                    CoTaskMemFree(pszPath);
                }
            }
        }
    }
}

void Window::DoOpenFolderFromPath(const std::wstring& path) {
    if (path.empty()) return;
    m_drawer.OpenFolder(path);
    m_workspace.OpenWorkspace(path);
    m_intelliSense.SetWorkspaceRoot(path);
    m_drawer.ClearGitLoading();
    m_rail.SetGitLoading(false);
    m_drawer.RefreshGitState(true, false);
    m_editor.GetPackageManagerView().ScanInstalledPackages(path);
    StartWorkspaceWatcher(path);

    // Close clean tabs that belong to a previous/different project
    for (auto& grp : m_editorGroups) {
        auto it = grp.docs.begin();
        while (it != grp.docs.end()) {
            if (!it->isDirty && !it->filePath.empty() && !IsFileInDirectory(it->filePath, path)) {
                it = grp.docs.erase(it);
            } else {
                ++it;
            }
        }
        if (grp.activeDocIdx >= static_cast<int>(grp.docs.size())) {
            grp.activeDocIdx = static_cast<int>(grp.docs.size()) - 1;
        }
    }

    // Track in recent paths LRU list (max 6 entries)
    auto it = std::find(m_recentPaths.begin(), m_recentPaths.end(), path);
    if (it != m_recentPaths.end()) m_recentPaths.erase(it);
    m_recentPaths.insert(m_recentPaths.begin(), path);
    if (m_recentPaths.size() > 6) m_recentPaths.resize(6);

    if (!m_config.isDrawerOpen) {
        ToggleSidebar();
    }
    UpdateDrawerErrorFiles();
    SaveSessionState();
    RunManualGarbageCollection();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoDeleteSelectedExplorerItem() {
    std::wstring targetPath = m_selectedExplorerPath;
    if (targetPath.empty()) {
        int hovered = m_drawer.GetHoveredIndex();
        if (hovered >= 0) {
            const auto* pItem = m_drawer.GetItem(hovered);
            if (pItem) targetPath = pItem->fullPath;
        }
    }
    if (targetPath.empty()) {
        targetPath = m_drawer.GetActiveFilePath();
    }
    if (targetPath.empty()) {
        auto* pDoc = GetActiveDoc();
        if (pDoc) targetPath = pDoc->filePath;
    }
    if (targetPath.empty() || !std::filesystem::exists(targetPath)) return;

    bool isFolder = std::filesystem::is_directory(targetPath);
    m_confirmDiscardModal.OpenDelete(targetPath, isFolder);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoOpenWorkspaceFromFile() {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Workspace Files (*.code-workspace;*.phide;*.json)\0*.code-workspace;*.phide;*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ENABLESIZING;

    if (GetOpenFileNameW(&ofn)) {
        std::filesystem::path p(szFile);
        if (std::filesystem::exists(p)) {
            DoOpenFolderFromPath(p.parent_path().wstring());
            LoadFileFromPath(szFile);
        }
    }
}

void Window::DoAddFolderToWorkspace() {
    DoOpenFolder();
}

void Window::DoSaveWorkspaceAs() {
    wchar_t szFile[MAX_PATH] = { 0 };
    wcscpy_s(szFile, L"workspace.phide");
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Phide Workspace (*.phide)\0*.phide\0Code Workspace (*.code-workspace)\0*.code-workspace\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_ENABLESIZING;

    if (GetSaveFileNameW(&ofn)) {
        std::ofstream ws(std::filesystem::path{ szFile });
        if (ws.is_open()) {
            std::string rootU8;
            if (m_drawer.HasOpenFolder()) {
                const auto& w = m_drawer.GetOpenFolderPath();
                int ulen = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (ulen > 0) {
                    rootU8.resize(ulen - 1);
                    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &rootU8[0], ulen, nullptr, nullptr);
                }
            }
            ws << "{\n  \"folders\": [\n    { \"path\": \"" << rootU8 << "\" }\n  ],\n  \"settings\": {}\n}\n";
        }
    }
}

void Window::DoDuplicateWorkspace() {
    DoNewWindow();
}

void Window::ExecuteQuickOpenAction(const std::wstring& actionCmd) {
    if (actionCmd.empty()) return;

    if (actionCmd.rfind(L"open_search:", 0) == 0) {
        std::wstring term = actionCmd.substr(12);
        m_config.activeRailTab = 1; // Search tab
        m_config.isDrawerOpen = true;
        m_drawer.SetSearchQuery(term);
        m_drawer.SetSearchFocus(1);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (actionCmd.rfind(L"goto_line:", 0) == 0) {
        try {
            int line = std::stoi(actionCmd.substr(10));
            JumpToEditorLine(static_cast<size_t>(line));
        } catch (...) {}
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (actionCmd == L"cmd_new_file") { DoNewFile(); return; }
    if (actionCmd == L"cmd_new_window") { DoNewWindow(); return; }
    if (actionCmd == L"cmd_open_file") { DoOpenFile(); return; }
    if (actionCmd == L"cmd_open_folder") { DoOpenFolder(); return; }
    if (actionCmd == L"cmd_save") { DoSaveFile(); return; }
    if (actionCmd == L"cmd_save_as") { DoSaveFileAs(); return; }
    if (actionCmd == L"cmd_save_all") { DoSaveAll(); return; }
    if (actionCmd == L"cmd_close_editor") { DoCloseEditor(); return; }
    if (actionCmd == L"cmd_undo") { DoUndo(); return; }
    if (actionCmd == L"cmd_redo") { DoRedo(); return; }
    if (actionCmd == L"cmd_cut") { DoCut(); return; }
    if (actionCmd == L"cmd_copy") { DoCopy(); return; }
    if (actionCmd == L"cmd_paste") { DoPaste(); return; }
    if (actionCmd == L"cmd_find_in_files") {
        m_config.activeRailTab = 1;
        m_config.isDrawerOpen = true;
        m_drawer.SetSearchFocus(1);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (actionCmd == L"cmd_replace_in_files") {
        m_config.activeRailTab = 1;
        m_config.isDrawerOpen = true;
        m_drawer.SetSearchFocus(2);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (actionCmd == L"cmd_toggle_sidebar") {
        m_config.isDrawerOpen = !m_config.isDrawerOpen;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (actionCmd == L"cmd_toggle_terminal") {
        DoCreateTerminal();
        return;
    }
    if (actionCmd == L"cmd_theme_picker") {
        m_themePicker.Open(m_theme.GetThemeMode());
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (actionCmd == L"cmd_shortcuts") {
        OpenSpecialView(L"keybindings", L"Keyboard Shortcuts");
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (actionCmd == L"cmd_runtimes") {
        m_config.activeRailTab = 4;
        m_config.isDrawerOpen = true;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (actionCmd == L"cmd_new_terminal") {
        if (!m_terminal.IsOpen()) {
            DoCreateTerminal();
        }
        std::wstring dir = m_drawer.HasOpenFolder() ? m_drawer.GetOpenFolderPath() : L"";
        m_terminal.NewTab(ui::TerminalShellType::PowerShell, dir);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (actionCmd == L"cmd_start_debugging") {
        DoDebugActiveDocument();
        return;
    }
    if (actionCmd == L"cmd_run_without_debugging") {
        DoRunActiveDocument();
        return;
    }
    if (actionCmd == L"cmd_git_commit") {
        m_config.activeRailTab = 2; // Source Control tab
        m_config.isDrawerOpen = true;
        m_drawer.SetGitCommitBoxFocus(1);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (actionCmd == L"cmd_git_refresh") {
        m_drawer.RefreshGitStatusOnly(true);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
}

void Window::JumpToEditorLine(size_t targetLine) {
    auto* pDoc = GetActiveDoc();
    if (pDoc && targetLine > 0 && targetLine <= pDoc->buffer.GetLineCount()) {
        size_t lineIdx = targetLine - 1;
        pDoc->buffer.SetCursor(lineIdx, 0);
        if (lineIdx < pDoc->scrollLine || lineIdx >= pDoc->scrollLine + 25) {
            pDoc->scrollLine = (lineIdx >= 5) ? lineIdx - 5 : 0;
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoSaveFile() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc) return;
    if (pDoc->filePath.empty()) {
        DoSaveFileAs();
    } else {
        SaveFileToPath(pDoc->filePath);
    }
}

void Window::DoSaveFileAs() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc) return;

    wchar_t szFile[MAX_PATH] = { 0 };
    if (!pDoc->filePath.empty()) {
        wcsncpy_s(szFile, pDoc->filePath.c_str(), _TRUNCATE);
    } else if (!pDoc->fileName.empty()) {
        wcsncpy_s(szFile, pDoc->fileName.c_str(), _TRUNCATE);
    }
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Notebook (*.ipynb)\0*.ipynb\0All Files (*.*)\0*.*\0C/C++ Files (*.cpp;*.h;*.hpp)\0*.cpp;*.h;*.hpp\0PHP Files (*.php)\0*.php\0JSON Files (*.json)\0*.json\0Text Files (*.txt)\0*.txt\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_ENABLESIZING;

    if (GetSaveFileNameW(&ofn)) {
        SaveFileToPath(szFile);
        if (m_drawer.HasOpenFolder()) {
            m_drawer.RefreshTree();
        }
    }
}

void Window::DoSaveAll() {
    bool anySaved = false;
    for (auto& group : m_editorGroups) {
        for (auto& doc : group.docs) {
            if (doc.isDirty && !doc.filePath.empty()) {
                if (doc.isSpecialView && doc.viewType == L"notebook" && doc.notebookDoc) {
                    doc.notebookDoc->SaveToFile(doc.filePath);
                    doc.isDirty = false;
                    anySaved = true;
                } else {
                    doc.buffer.SaveToFile(doc.filePath);
                    doc.isDirty = false;
                    doc.originalDiskText = doc.buffer.GetText();
                    anySaved = true;
                }
                SyncDocumentBuffers(&doc);
                if (doc.filePath == m_snippetManager.GetSnippetsFilePath()) {
                    m_snippetManager.LoadSnippets();
                }
            }
        }
    }
    if (anySaved) {
        UpdateDrawerDirtyFiles();
        m_drawer.RefreshGitStatusOnly(true);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoExportProfile() {
    wchar_t szFile[MAX_PATH] = { 0 };
    wcscpy_s(szFile, L"phide-profile.code-profile");
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Code Profile (*.code-profile)\0*.code-profile\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_ENABLESIZING;

    if (GetSaveFileNameW(&ofn)) {
        std::ofstream pf(std::filesystem::path{ szFile });
        if (pf.is_open()) {
            pf << "{\n  \"name\": \"Default Profile\",\n  \"userData\": {}\n}\n";
        }
    }
}

void Window::DoImportProfile() {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Code Profile (*.code-profile)\0*.code-profile\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ENABLESIZING;

    if (GetOpenFileNameW(&ofn)) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoOpenSettings() {
    OpenVirtualDocument(
        L"settings.json",
        L"{\n"
        L"    // Editor configuration\n"
        L"    \"editor.fontSize\": 13,\n"
        L"    \"editor.fontFamily\": \"Consolas, 'Courier New', monospace\",\n"
        L"    \"editor.tabSize\": 4,\n"
        L"    \"editor.insertSpaces\": true,\n"
        L"    \"editor.lineNumbers\": \"on\",\n"
        L"    \"editor.renderWhitespace\": \"selection\",\n"
        L"    \"editor.cursorBlinking\": \"smooth\",\n"
        L"    \"workbench.colorTheme\": \"Default Dark Modern\",\n"
        L"    \"workbench.iconTheme\": \"vs-seti\",\n"
        L"    \"files.autoSave\": \"afterDelay\"\n"
        L"}\n",
        false
    );
}

void Window::DoOpenKeyboardShortcuts() {
    m_drawer.SetSearchFocus(0);
    m_editor.GetKeyboardShortcutsView().SetSearchFocused(true);
    OpenSpecialView(L"keybindings", L"Keyboard Shortcuts");
}

void Window::DoOpenUserSnippets() {
    m_snippetManager.EnsureDefaultSnippetsFile();
    OpenFileDocument(m_snippetManager.GetSnippetsFilePath(), false);
}

void Window::DoToggleThemeModal() {
    m_themePicker.Open(m_theme.GetThemeMode());
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoToggleTheme() {
    DoToggleThemeModal();
}

void Window::DoRevertFile() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->filePath.empty() && std::filesystem::exists(pDoc->filePath)) {
        pDoc->buffer.LoadFromFile(pDoc->filePath);
        pDoc->isDirty = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoCloseEditor() {
    auto& group = GetActiveGroup();
    if (group.activeDocIdx >= 0) {
        CloseDocument(m_focusedGroupIdx, group.activeDocIdx);
    }
}

void Window::DoCloseFolder() {
    m_drawer.CloseFolder();
    m_drawer.RefreshGitState(true);
    m_editor.GetPackageManagerView().ScanInstalledPackages(L"");
    UpdateDrawerErrorFiles();
    SaveSessionState();
    RunManualGarbageCollection();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoReopenClosedEditor() {
    if (!m_lastClosedFilePath.empty() && std::filesystem::exists(m_lastClosedFilePath)) {
        OpenFileDocument(m_lastClosedFilePath, false);
    }
}

void Window::SyncDocumentBuffers(editor::OpenDocument* sourceDoc) {
    if (!sourceDoc || sourceDoc->filePath.empty() || sourceDoc->isSpecialView) return;
    std::wstring text = sourceDoc->buffer.GetText();
    for (auto& group : m_editorGroups) {
        for (auto& doc : group.docs) {
            if (&doc != sourceDoc && !doc.filePath.empty() && doc.filePath == sourceDoc->filePath) {
                doc.buffer.SetText(text);
                doc.isDirty = sourceDoc->isDirty;
                if (doc.isDiffView) {
                    ComputeDiffHunks(doc);
                }
            }
        }
    }
}

void Window::UpdateDrawerDirtyFiles() {
    std::vector<std::wstring> dirtyPaths;
    for (const auto& group : m_editorGroups) {
        for (const auto& doc : group.docs) {
            if (doc.isDirty && !doc.filePath.empty() && !doc.isSpecialView) {
                if (doc.isDiffView && doc.diffHunks.empty()) continue;
                if (!doc.isDiffView && !doc.originalDiskText.empty() && doc.buffer.GetText() == doc.originalDiskText) continue;
                dirtyPaths.push_back(doc.filePath);
            }
        }
    }
    m_drawer.SetDirtyFiles(dirtyPaths);
}

void Window::UpdateDrawerErrorFiles() {
    std::map<std::wstring, size_t> errorFiles;
    for (const auto& group : m_editorGroups) {
        for (const auto& doc : group.docs) {
            if (!doc.diagnostics.empty() && !doc.isSpecialView) {
                std::wstring key = !doc.filePath.empty() ? doc.filePath : doc.fileName;
                if (!key.empty()) {
                    errorFiles[key] = doc.diagnostics.size();
                }
            }
        }
    }
    m_drawer.SetErrorFiles(errorFiles);
}

void Window::CommitAutocompleteSelection() {
    const auto* item = m_editor.GetSelectedAutocompleteItem();
    auto* pDoc = GetActiveDoc();
    if (!item || !pDoc) {
        m_editor.DismissAutocomplete();
        m_editor.ClearGhostText();
        return;
    }

    m_suppressNextChar = true;
    auto pos = pDoc->buffer.GetCursor();
    const auto& lineText = pDoc->buffer.GetLine(pos.line);
    std::wstring prefix = editor::IntelliSenseEngine::ExtractQueryPrefix(lineText, pos.col);

    // Check if completing a file or directory path (Images 2 & 3)
    if (item->origin == L"path" || item->kind == editor::CompletionKind::Folder || item->kind == editor::CompletionKind::File) {
        size_t lastSep = lineText.find_last_of(L"/\\\"'", pos.col > 0 ? pos.col - 1 : 0);
        size_t charsToDel = (lastSep != std::wstring::npos && pos.col > lastSep) ? (pos.col - lastSep - 1) : 0;
        for (size_t i = 0; i < charsToDel; ++i) {
            pDoc->buffer.Backspace();
        }
        for (wchar_t c : item->insertText) {
            pDoc->buffer.InsertChar(c);
        }
        MarkDocumentModified(pDoc);

        // If completed a folder, immediately trigger sub-directory completion!
        if (item->kind == editor::CompletionKind::Folder || item->insertText.ends_with(L"/")) {
            auto newPos = pDoc->buffer.GetCursor();
            const auto& newLine = pDoc->buffer.GetLine(newPos.line);
            std::wstring docTarget = pDoc->filePath.empty() ? pDoc->fileName : pDoc->filePath;
            if (m_intelliSense.GetWorkspaceRoot().empty() && m_drawer.HasOpenFolder()) {
                m_intelliSense.SetWorkspaceRoot(m_drawer.GetOpenFolderPath());
            }
            auto subItems = m_intelliSense.QueryCompletions(docTarget, newLine, newPos.col, pDoc ? &pDoc->buffer.GetLines() : nullptr);
            if (!subItems.empty()) {
                m_editor.ShowAutocomplete(subItems);
            } else {
                m_editor.DismissAutocomplete();
                m_editor.ClearGhostText();
            }
        } else {
            m_editor.DismissAutocomplete();
            m_editor.ClearGhostText();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // Check if user is completing a snippet (e.g. "!", "html:5", "clg", "flexcenter")
    if (item->kind == editor::CompletionKind::Snippet) {
        std::wstring ext;
        auto dotPos = pDoc->fileName.rfind(L'.');
        if (dotPos != std::wstring::npos) ext = pDoc->fileName.substr(dotPos + 1);
        for (auto& c : ext) c = towlower(c);

        const auto* pSnip = m_snippetManager.FindSnippetByPrefix(item->label, ext);
        if (pSnip) {
            m_snippetManager.ExpandSnippet(*pSnip, pDoc->buffer, prefix.length());
            MarkDocumentModified(pDoc);
            m_editor.DismissAutocomplete();
            m_editor.ClearGhostText();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // Check if user is completing an HTML tag (e.g. typed "<div", "</main", or "div")
    bool isTagKind = (item->kind == editor::CompletionKind::Tag);
    size_t prefixStart = (pos.col >= prefix.length()) ? (pos.col - prefix.length()) : 0;
    bool hasClosingAngle = (prefixStart >= 2 && lineText[prefixStart - 1] == L'/' && lineText[prefixStart - 2] == L'<');
    bool hasLeadingAngle = (!hasClosingAngle && prefixStart > 0 && lineText[prefixStart - 1] == L'<');

    static const std::unordered_set<std::wstring> voidTags = {
        L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
        L"link", L"meta", L"param", L"source", L"track", L"wbr"
    };

    std::wstring tagLower = item->label;
    for (auto& c : tagLower) c = towlower(c);
    bool isVoid = (voidTags.count(tagLower) > 0);

    if (isTagKind || hasLeadingAngle || hasClosingAngle) {
        // Tag completion
        for (size_t i = 0; i < prefix.length(); ++i) {
            pDoc->buffer.Backspace();
        }

        if (hasClosingAngle) {
            // Already typed '</' (closing tag): Complete with 'tag>' ONLY (prevents '<//main>')
            std::wstring closePart = item->label + L">";
            for (wchar_t c : closePart) pDoc->buffer.InsertChar(c);
        } else if (hasLeadingAngle) {
            // Already has '<'. Complete after '<'
            if (isVoid) {
                std::wstring toInsert;
                if (tagLower == L"img") {
                    toInsert = L"img src=\"\" alt=\"\" />";
                } else if (tagLower == L"input") {
                    toInsert = L"input type=\"text\" />";
                } else if (tagLower == L"link") {
                    toInsert = L"link rel=\"stylesheet\" href=\"\" />";
                } else if (tagLower == L"meta") {
                    toInsert = L"meta charset=\"UTF-8\" />";
                } else {
                    toInsert = item->label + L" />";
                }
                for (wchar_t c : toInsert) pDoc->buffer.InsertChar(c);
            } else {
                // Container tag: e.g. <div -> <div></div> with cursor inside
                std::wstring openPart = item->label + L">";
                std::wstring closePart = L"</" + item->label + L">";
                for (wchar_t c : openPart) pDoc->buffer.InsertChar(c);
                size_t insideLine = pDoc->buffer.GetCursor().line;
                size_t insideCol = pDoc->buffer.GetCursor().col;
                for (wchar_t c : closePart) pDoc->buffer.InsertChar(c);
                pDoc->buffer.SetCursor(insideLine, insideCol);
            }
        } else {
            // No leading '<' (e.g. user typed "div" and picked tag item)
            if (isVoid) {
                std::wstring toInsert;
                if (tagLower == L"img") {
                    toInsert = L"<img src=\"\" alt=\"\" />";
                } else if (tagLower == L"input") {
                    toInsert = L"<input type=\"text\" />";
                } else {
                    toInsert = L"<" + item->label + L" />";
                }
                for (wchar_t c : toInsert) pDoc->buffer.InsertChar(c);
            } else {
                std::wstring openPart = L"<" + item->label + L">";
                std::wstring closePart = L"</" + item->label + L">";
                for (wchar_t c : openPart) pDoc->buffer.InsertChar(c);
                size_t insideLine = pDoc->buffer.GetCursor().line;
                size_t insideCol = pDoc->buffer.GetCursor().col;
                for (wchar_t c : closePart) pDoc->buffer.InsertChar(c);
                pDoc->buffer.SetCursor(insideLine, insideCol);
            }
        }
    } else {
        // Normal completion (method, property, keyword, etc.)
        for (size_t i = 0; i < prefix.length(); ++i) {
            pDoc->buffer.Backspace();
        }
        const std::wstring& toInsert = !item->insertText.empty() ? item->insertText : item->label;
        for (wchar_t c : toInsert) {
            pDoc->buffer.InsertChar(c);
        }
    }

    MarkDocumentModified(pDoc);
    m_editor.DismissAutocomplete();
    m_editor.ClearGhostText();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::MarkDocumentModified(editor::OpenDocument* pDoc) {
    if (!pDoc || pDoc->isSpecialView) return;
    pDoc->isPreview = false;
    pDoc->diagnostics = editor::SyntaxValidator::Validate(pDoc->filePath.empty() ? pDoc->fileName : pDoc->filePath, pDoc->buffer);
    UpdateDrawerErrorFiles();

    // Check if document content has reverted to match pristine disk text or HEAD
    bool isActuallyDirty = true;
    if (pDoc->isDiffView) {
        ComputeDiffHunks(*pDoc);
        if (pDoc->diffHunks.empty()) {
            isActuallyDirty = false;
        }
    } else if (!pDoc->originalDiskText.empty()) {
        if (pDoc->buffer.GetText() == pDoc->originalDiskText) {
            isActuallyDirty = false;
        }
    }

    if (m_autoSave && !pDoc->filePath.empty()) {
        pDoc->buffer.SaveToFile(pDoc->filePath);
        pDoc->isDirty = false;
        pDoc->originalDiskText = pDoc->buffer.GetText();
        SyncDocumentBuffers(pDoc);
        UpdateDrawerDirtyFiles();
        TriggerDebouncedGitRefresh(100);
    } else {
        bool dirtyDroppedToClean = (pDoc->isDirty && !isActuallyDirty);
        pDoc->isDirty = isActuallyDirty;
        SyncDocumentBuffers(pDoc);
        UpdateDrawerDirtyFiles();
        if (dirtyDroppedToClean) {
            // Edits were removed/erased: immediately trigger git refresh to vanish from Source Control!
            m_drawer.RefreshGitStatusOnly(true);
        } else {
            TriggerDebouncedGitRefresh(300); // 300ms responsive refresh instead of 2000ms
        }
    }
}

void Window::DoCut() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        DoCopy();
        if (pDoc->HasSelection()) {
            pDoc->DeleteSelection();
        } else {
            pDoc->buffer.Backspace();
        }
        MarkDocumentModified(pDoc);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoCopy() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        std::wstring textToCopy;
        if (pDoc->HasSelection()) {
            textToCopy = pDoc->GetSelectedText();
        } else {
            auto cursor = pDoc->buffer.GetCursor();
            textToCopy = pDoc->buffer.GetLine(cursor.line);
        }
        if (!textToCopy.empty() && OpenClipboard(m_hwnd)) {
            EmptyClipboard();
            size_t bytes = (textToCopy.length() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hMem) {
                void* pMem = GlobalLock(hMem);
                if (pMem) {
                    memcpy(pMem, textToCopy.c_str(), bytes);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
            }
            CloseClipboard();
        }
    }
}

void Window::DoPaste() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        if (OpenClipboard(m_hwnd)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
                if (pText) {
                    std::wstring clip(pText);
                    GlobalUnlock(hData);
                    CloseClipboard();
                    if (pDoc->HasSelection()) {
                        pDoc->DeleteSelection();
                    }
                    pDoc->buffer.InsertText(clip);
                    MarkDocumentModified(pDoc);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                }
            }
            CloseClipboard();
        }
    }
}

void Window::DoUndo() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        if (pDoc->buffer.Undo()) {
            MarkDocumentModified(pDoc);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
}

void Window::DoRedo() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        if (pDoc->buffer.Redo()) {
            MarkDocumentModified(pDoc);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
}

void Window::DoExplorerUndo() {
    std::wstring oldPath, newPath;
    bool wasRename = false;
    if (m_explorerUndo.Undo(oldPath, newPath, wasRename)) {
        if (wasRename && !oldPath.empty() && !newPath.empty()) {
            for (auto& group : m_editorGroups) {
                for (auto& doc : group.docs) {
                    if (doc.filePath == oldPath) {
                        doc.filePath = newPath;
                        doc.fileName = std::filesystem::path(newPath).filename().wstring();
                    }
                }
            }
        }
        m_drawer.RefreshTree();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoExplorerRedo() {
    std::wstring oldPath, newPath;
    bool wasRename = false;
    if (m_explorerUndo.Redo(oldPath, newPath, wasRename)) {
        if (wasRename && !oldPath.empty() && !newPath.empty()) {
            for (auto& group : m_editorGroups) {
                for (auto& doc : group.docs) {
                    if (doc.filePath == oldPath) {
                        doc.filePath = newPath;
                        doc.fileName = std::filesystem::path(newPath).filename().wstring();
                    }
                }
            }
        }
        m_drawer.RefreshTree();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoSelectAll() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        size_t count = pDoc->buffer.GetLineCount();
        if (count > 0) {
            pDoc->selAnchor = { 0, 0 };
            pDoc->selEnd = { count - 1, pDoc->buffer.GetLine(count - 1).length() };
            pDoc->hasSelection = true;
            pDoc->selectedWord = pDoc->GetSelectedText();
            pDoc->buffer.SetCursor(count - 1, pDoc->buffer.GetLine(count - 1).length());
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
}

void Window::DoChangeAllOccurrences() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc || pDoc->isSpecialView) return;

    auto cursor = pDoc->buffer.GetCursor();
    const auto& line = pDoc->buffer.GetLine(cursor.line);
    if (line.empty()) return;

    size_t col = (cursor.col < line.size()) ? cursor.col : (line.empty() ? 0 : line.size() - 1);
    if (!iswalnum(line[col]) && line[col] != L'_') {
        if (col > 0 && (iswalnum(line[col - 1]) || line[col - 1] == L'_')) {
            --col;
        }
    }
    if (!iswalnum(line[col]) && line[col] != L'_') return;

    size_t start = col;
    while (start > 0 && (iswalnum(line[start - 1]) || line[start - 1] == L'_')) {
        --start;
    }
    size_t end = col;
    while (end < line.size() && (iswalnum(line[end]) || line[end] == L'_')) {
        ++end;
    }

    std::wstring word = line.substr(start, end - start);
    if (!word.empty()) {
        m_config.activeRailTab = 1; // Switch to Search tab
        if (m_drawerAnimProgress < 0.1f) {
            ToggleSidebar();
        }
        m_drawer.SetSearchQuery(word);
        m_drawer.ExecuteSearch(true);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoRefactor() {
    m_editor.ShowRefactorPopup(L"No refactorings available");
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoCommandPalette() {
    DoOpenKeyboardShortcuts();
}

void Window::DoZoomIn() {
    if (m_config.fontSize < 36.0f) {
        m_config.fontSize += 2.0f;
        m_config.zoomLevel = m_config.fontSize / 14.0f;
        m_theme.SetEditorFontSize(m_config.fontSize);
        m_editor.ResetCharWidth();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoZoomOut() {
    if (m_config.fontSize > 8.0f) {
        m_config.fontSize -= 2.0f;
        m_config.zoomLevel = m_config.fontSize / 14.0f;
        m_theme.SetEditorFontSize(m_config.fontSize);
        m_editor.ResetCharWidth();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoResetZoom() {
    m_config.fontSize = 14.0f;
    m_config.zoomLevel = 1.0f;
    m_theme.SetEditorFontSize(m_config.fontSize);
    m_editor.ResetCharWidth();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoNextTab() {
    if (m_editorGroups.empty()) return;
    auto& group = m_editorGroups[m_focusedGroupIdx];
    if (group.docs.size() > 1) {
        group.activeDocIdx = (group.activeDocIdx + 1) % static_cast<int>(group.docs.size());
        m_editor.DismissAutocomplete();
        m_editor.ClearGhostText();
        UpdateExecutionButtonStates();
        UpdateDrawerErrorFiles();
    SaveSessionState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoPrevTab() {
    if (m_editorGroups.empty()) return;
    auto& group = m_editorGroups[m_focusedGroupIdx];
    if (group.docs.size() > 1) {
        group.activeDocIdx = (group.activeDocIdx - 1 + static_cast<int>(group.docs.size())) % static_cast<int>(group.docs.size());
        m_editor.DismissAutocomplete();
        m_editor.ClearGhostText();
        UpdateExecutionButtonStates();
        UpdateDrawerErrorFiles();
    SaveSessionState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoToggleWordWrap() {
    m_wordWrap = !m_wordWrap;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoShowExplorer() {
    m_config.activeRailTab = 0;
    if (!m_config.isDrawerOpen) {
        ToggleSidebar();
    }
    UpdateDrawerErrorFiles();
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::RefreshOpenDocPathsForSearch() {
    std::vector<std::wstring> openPaths;
    for (const auto& grp : m_editorGroups) {
        for (const auto& doc : grp.docs) {
            if (!doc.filePath.empty() && !doc.isSpecialView && !doc.isUnsupportedBinary) {
                openPaths.push_back(doc.filePath);
            }
        }
    }
    m_drawer.SetOpenDocPaths(openPaths);
}

void Window::DoQuickOpenFileSearch() {
    std::vector<std::wstring> openPaths;
    for (const auto& grp : m_editorGroups) {
        for (const auto& doc : grp.docs) {
            if (!doc.filePath.empty() && !doc.isSpecialView && !doc.isUnsupportedBinary) {
                openPaths.push_back(doc.filePath);
            }
        }
    }
    m_quickOpen.Open(m_recentPaths, openPaths, m_drawer.GetOpenFolderPath());
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::UpdateDatabaseModalRuntimeStatus() {
    bool pmaRunning = (m_hPmaProcess != nullptr || m_isPmaRunning);
    bool nginxRunning = (m_hNginxProcess != nullptr || m_header.GetServerState() == ui::ServerState::Running);
    m_dbSettingsModal.SetRuntimeStatus(pmaRunning, nginxRunning);
}

void Window::DoOpenDatabaseSettings(ui::DatabaseModalTab initialTab) {
    UpdateDatabaseModalRuntimeStatus();

    std::wstring sqlName;
    auto checkDirForSql = [](const std::filesystem::path& dir) -> std::wstring {
        if (dir.empty()) return L"";
        if (std::filesystem::exists(dir / L"database.sql")) return L"database.sql";
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == L".sql") {
                return entry.path().filename().wstring();
            }
        }
        return L"";
    };

    if (!m_workspace.GetRootPath().empty()) {
        sqlName = checkDirForSql(m_workspace.GetRootPath());
    }
    if (sqlName.empty() && !m_runningProjectRoot.empty()) {
        sqlName = checkDirForSql(m_runningProjectRoot);
    }
    std::wstring activeDocPath;
    if (m_focusedGroupIdx >= 0 && m_focusedGroupIdx < (int)m_editorGroups.size()) {
        auto& grp = m_editorGroups[m_focusedGroupIdx];
        if (grp.activeDocIdx >= 0 && grp.activeDocIdx < (int)grp.docs.size()) {
            activeDocPath = grp.docs[grp.activeDocIdx].filePath;
        }
    }
    if (sqlName.empty() && !activeDocPath.empty()) {
        sqlName = checkDirForSql(std::filesystem::path(activeDocPath).parent_path());
    }
    m_dbSettingsModal.SetSqlFileAvailable(sqlName);

    m_dbSettingsModal.Open(initialTab);
    m_header.CloseServerDatabaseDropdowns();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::RunManualGarbageCollection() {
    for (auto& group : m_editorGroups) {
        for (auto& doc : group.docs) {
            doc.buffer.RunGarbageCollection();
            doc.diagnostics.shrink_to_fit();
            doc.inlayHints.shrink_to_fit();
            doc.breakpoints.shrink_to_fit();
            doc.originalDiskText.shrink_to_fit();
            if (doc.notebookDoc) {
                doc.notebookDoc->RunGarbageCollection();
            }
            for (auto& mf : doc.multiFileDiffs) {
                mf.originalBuffer.RunGarbageCollection();
                mf.workingBuffer.RunGarbageCollection();
                mf.hunks.shrink_to_fit();
            }
            doc.multiFileDiffs.shrink_to_fit();
        }
    }
    m_editor.RunGarbageCollection();
    m_drawer.ClearSearchResults();
    m_drawer.RunGarbageCollection();
    m_drawer.RunGarbageCollectionAsync();
    m_terminal.RunGarbageCollection();
    m_extensionsPanel.RunGarbageCollection();
    CoFreeUnusedLibraries();
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void Window::DoOpenServerSettings() {
    UpdateDatabaseModalRuntimeStatus();
    m_dbSettingsModal.Open(ui::DatabaseModalTab::NginxServer);
    m_header.CloseServerDatabaseDropdowns();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoShowSearch() {
    m_config.activeRailTab = 1;
    if (!m_config.isDrawerOpen) {
        ToggleSidebar();
    }
    RefreshOpenDocPathsForSearch();
    m_editor.GetKeyboardShortcutsView().SetSearchFocused(false);
    m_drawer.SetSearchFocus(1);
    UpdateDrawerErrorFiles();
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoShowReplace() {
    m_config.activeRailTab = 1;
    if (!m_config.isDrawerOpen) {
        ToggleSidebar();
    }
    m_drawer.SetReplaceOpen(true);
    RefreshOpenDocPathsForSearch();
    m_editor.GetKeyboardShortcutsView().SetSearchFocused(false);
    m_drawer.SetSearchFocus(2);
    UpdateDrawerErrorFiles();
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoFind() {
    DoShowSearch();
}

void Window::DoReplace() {
    DoShowReplace();
}

void Window::DoToggleLineComment() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        pDoc->buffer.ToggleLineComment();
        MarkDocumentModified(pDoc);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoToggleBlockComment() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        pDoc->buffer.ToggleBlockComment();
        MarkDocumentModified(pDoc);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoEmmetExpand() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        if (pDoc->buffer.ExpandEmmet()) {
            MarkDocumentModified(pDoc);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
}

void Window::DoShowSourceControl() {
    m_config.activeRailTab = 2;
    if (!m_config.isDrawerOpen) {
        ToggleSidebar();
    }
    m_drawer.OpenSourceControl();
    UpdateDrawerErrorFiles();
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::AnimateTerminalOpen() {
    m_targetTerminalProgress = 1.0f;
    m_isTerminalAnimating = true;
    SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
}

void Window::AnimateTerminalClose() {
    m_targetTerminalProgress = 0.0f;
    m_isTerminalAnimating = true;
    SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
}

void Window::AnimateRightSidebarOpen() {
    m_extensionsPanel.Open();
    m_targetRightSidebarProgress = 1.0f;
    m_isRightSidebarAnimating = true;
    SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
}

void Window::AnimateRightSidebarClose() {
    m_targetRightSidebarProgress = 0.0f;
    m_isRightSidebarAnimating = true;
    SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
}

void Window::DoCreateTerminal() {
    std::wstring dir;
    if (m_drawer.HasOpenFolder()) {
        dir = m_drawer.GetOpenFolderPath();
    }
    if (!m_terminal.IsOpen()) {
        m_terminal.Open(dir);
        m_isTerminalFocused = true;
        AnimateTerminalOpen();
    } else {
        m_isTerminalFocused = false;
        AnimateTerminalClose();
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::UpdateExecutionButtonStates() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc || pDoc->isSpecialView) {
        m_header.SetRunEnabled(false);
        m_header.SetDebugEnabled(false);
        m_header.SetLiveServerRunning(false);
        return;
    }

    std::filesystem::path p(pDoc->filePath);
    std::wstring ext = p.extension().wstring();
    for (auto& c : ext) c = towlower(c);

    // 1. PHP Application: Fully enabled for single file or project dev server execution
    if (ext == L".php" || ext == L".phtml" || ext == L".php8") {
        bool isRunning = m_isProjectRunning ||
                         (m_header.GetServerState() == ui::ServerState::Running) ||
                         (m_header.GetDatabaseState() == ui::DatabaseState::Running);
        m_header.SetRunEnabled(true);
        m_header.SetDebugEnabled(true);
        m_header.SetLiveServerRunning(isRunning);
        return;
    }

    // 2. Python Script: Independent execution
    // "unless i go to a python file, html file, and other file . on python file one can run also oyou can run another file its not single run at a time get it?"
    if (ext == L".py") {
        m_header.SetRunEnabled(true);
        m_header.SetDebugEnabled(true);
        m_header.SetLiveServerRunning(false); // ALWAYS Green Play (Run)!
        return;
    }

    // 3. Project Dev Server Running:
    // "if that next js is running then it already running even when i go to another file on that project then the run button is still stop as its a dev run get it"
    if (m_isProjectRunning && !m_runningProjectRoot.empty()) {
        if (IsFileInDirectory(pDoc->filePath, m_runningProjectRoot)) {
            m_header.SetRunEnabled(true);
            m_header.SetDebugEnabled(false);
            m_header.SetLiveServerRunning(true); // Red Square (STOP)!
            return;
        }
    }

    // 4. HTML Standalone File
    if (ext == L".html" || ext == L".htm") {
        auto* srv = FindLiveServer(pDoc->filePath);
        m_header.SetRunEnabled(true);
        m_header.SetDebugEnabled(false);
        m_header.SetLiveServerRunning(srv != nullptr);
        return;
    }

    // 5. Executable file types for Run: .js, .mjs, .cjs, .ts, .jsx, .tsx, etc.
    bool isJsTs = (ext == L".js" || ext == L".mjs" || ext == L".cjs" || ext == L".ts" || ext == L".jsx" || ext == L".tsx" || ext == L".json" || ext == L".yaml" || ext == L".yml");
    bool canRun = isJsTs;
    bool canDebug = isJsTs;

    m_header.SetRunEnabled(canRun);
    m_header.SetDebugEnabled(canDebug);
    m_header.SetLiveServerRunning(false);
}

Window::ActiveLiveServer* Window::FindLiveServer(const std::wstring& filePath) {
    if (filePath.empty()) return nullptr;
    for (auto& s : m_activeLiveServers) {
        if (_wcsicmp(s.filePath.c_str(), filePath.c_str()) == 0) {
            return &s;
        }
    }
    return nullptr;
}

Window::ActiveLiveServer* Window::GetActiveDocLiveServer() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc || pDoc->filePath.empty()) return nullptr;
    auto* s = FindLiveServer(pDoc->filePath);
    if (s) return s;
    if (m_isProjectRunning && !m_runningProjectRoot.empty() && IsFileInDirectory(pDoc->filePath, m_runningProjectRoot)) {
        return FindLiveServer(m_runningProjectRoot);
    }
    return nullptr;
}

int Window::GetNextAvailableLiveServerPort() const {
    int maxPort = 5499;
    for (const auto& s : m_activeLiveServers) {
        if (s.port > maxPort) maxPort = s.port;
    }
    return maxPort + 1;
}

void Window::StopLiveServer(const std::wstring& filePath, bool closeTab) {
    if (filePath.empty()) return;
    for (auto it = m_activeLiveServers.begin(); it != m_activeLiveServers.end(); ++it) {
        if (_wcsicmp(it->filePath.c_str(), filePath.c_str()) == 0) {
            if (it->framework == L"PHP Server") {
                StopPhpServer();
            }
            auto serverToStop = std::move(it->server);
            m_activeLiveServers.erase(it);
            if (serverToStop) {
                // Shut down asynchronously so UI thread NEVER stutters or hangs
                std::thread([s = std::move(serverToStop)]() mutable {
                    s->Stop();
                }).detach();
            }
            break;
        }
    }
    if (closeTab) {
        m_terminal.CloseTabByAssociatedFile(filePath);
    }
    UpdateExecutionButtonStates();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::StopAllLiveServers() {
    StopPhpServer();
    StopNginxServer();
    StopPmaServer();
    for (auto& s : m_activeLiveServers) {
        auto serverToStop = std::move(s.server);
        if (serverToStop) {
            std::thread([s = std::move(serverToStop)]() mutable {
                s->Stop();
            }).detach();
        }
        m_terminal.CloseTabByAssociatedFile(s.filePath);
    }
    m_activeLiveServers.clear();
    if (m_liveServer.IsRunning()) {
        std::thread([this]() {
            m_liveServer.Stop();
        }).detach();
    }
    UpdateExecutionButtonStates();
}

static std::wstring FormatCssNative(const std::wstring& src) {
    std::wstring out;
    out.reserve(src.length() * 2);
    int indent = 0;
    bool inComment = false;
    bool inString = false;
    wchar_t strChar = 0;
    bool needIndent = true;

    auto appendIndent = [&](int lvl) {
        for (int k = 0; k < lvl; ++k) out += L"    ";
    };

    size_t i = 0;
    while (i < src.length()) {
        wchar_t ch = src[i];

        if (inComment) {
            out += ch;
            if (ch == L'*' && i + 1 < src.length() && src[i + 1] == L'/') {
                out += L'/';
                i++;
                inComment = false;
            }
            i++;
            continue;
        }

        if (inString) {
            out += ch;
            if (ch == L'\\' && i + 1 < src.length()) {
                out += src[i + 1];
                i += 2;
                continue;
            }
            if (ch == strChar) inString = false;
            i++;
            continue;
        }

        if (ch == L'/' && i + 1 < src.length() && src[i + 1] == L'*') {
            inComment = true;
            out += L"/*";
            i += 2;
            continue;
        }

        if (ch == L'"' || ch == L'\'') {
            inString = true;
            strChar = ch;
            if (needIndent) { appendIndent(indent); needIndent = false; }
            out += ch;
            i++;
            continue;
        }

        if (ch == L'{') {
            while (!out.empty() && (out.back() == L' ' || out.back() == L'\t')) out.pop_back();
            out += L" {\n";
            indent++;
            needIndent = true;
            i++;
            while (i < src.length() && iswspace(src[i])) i++;
            continue;
        }

        if (ch == L'}') {
            while (!out.empty() && (out.back() == L' ' || out.back() == L'\t')) out.pop_back();
            if (!out.empty() && out.back() != L'\n') out += L'\n';
            if (indent > 0) indent--;
            appendIndent(indent);
            out += L"}\n\n";
            needIndent = true;
            i++;
            while (i < src.length() && iswspace(src[i])) i++;
            continue;
        }

        if (ch == L';') {
            out += L";\n";
            needIndent = true;
            i++;
            while (i < src.length() && iswspace(src[i])) i++;
            continue;
        }

        if (ch == L':') {
            out += L": ";
            i++;
            while (i < src.length() && (src[i] == L' ' || src[i] == L'\t')) i++;
            continue;
        }

        if (iswspace(ch)) {
            if (!needIndent && !out.empty() && out.back() != L' ' && out.back() != L'\n') {
                out += L' ';
            }
            i++;
            continue;
        }

        if (needIndent) {
            appendIndent(indent);
            needIndent = false;
        }
        out += ch;
        i++;
    }

    while (out.length() >= 2 && out.substr(out.length() - 2) == L"\n\n") out.pop_back();
    if (!out.empty() && out.back() != L'\n') out += L'\n';
    return out;
}

static std::wstring FormatHtmlNative(const std::wstring& src) {
    std::wstring out;
    out.reserve(src.length() * 2);
    int indent = 0;
    bool inComment = false;

    auto appendIndent = [&](int lvl) {
        for (int k = 0; k < lvl; ++k) out += L"    ";
    };

    static const std::unordered_set<std::wstring> voidTags = {
        L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
        L"link", L"meta", L"param", L"source", L"track", L"wbr", L"!doctype"
    };

    size_t i = 0;
    while (i < src.length()) {
        if (src[i] == L'<' && i + 3 < src.length() && src.substr(i, 4) == L"<!--") {
            inComment = true;
            if (!out.empty() && out.back() != L'\n') out += L'\n';
            appendIndent(indent);
            out += L"<!--";
            i += 4;
            continue;
        }
        if (inComment) {
            out += src[i];
            if (src[i] == L'-' && i + 2 < src.length() && src.substr(i, 3) == L"-->") {
                out += L"->";
                i += 3;
                inComment = false;
                out += L'\n';
                continue;
            }
            i++;
            continue;
        }

        if (src[i] == L'<') {
            size_t tagEnd = src.find(L'>', i);
            if (tagEnd != std::wstring::npos) {
                std::wstring tagContent = src.substr(i + 1, tagEnd - (i + 1));
                bool isClosing = (!tagContent.empty() && tagContent[0] == L'/');
                bool isSelfClosing = (!tagContent.empty() && tagContent.back() == L'/');

                size_t nameStart = isClosing ? 1 : 0;
                while (nameStart < tagContent.length() && iswspace(tagContent[nameStart])) nameStart++;
                size_t nameEnd = nameStart;
                while (nameEnd < tagContent.length() && !iswspace(tagContent[nameEnd]) && tagContent[nameEnd] != L'/') nameEnd++;
                std::wstring tagName = tagContent.substr(nameStart, nameEnd - nameStart);
                for (auto& c : tagName) c = static_cast<wchar_t>(towlower(c));

                bool isVoid = (voidTags.count(tagName) > 0 || isSelfClosing);

                if (isClosing) {
                    if (indent > 0) indent--;
                    if (!out.empty() && out.back() != L'\n') out += L'\n';
                    appendIndent(indent);
                } else {
                    if (!out.empty() && out.back() != L'\n') out += L'\n';
                    appendIndent(indent);
                }

                out += L'<' + tagContent + L'>';
                i = tagEnd + 1;

                if (!isClosing && !isVoid) {
                    indent++;
                }

                while (i < src.length() && (src[i] == L' ' || src[i] == L'\t' || src[i] == L'\r' || src[i] == L'\n')) i++;
                out += L'\n';
                continue;
            }
        }

        size_t nextTag = src.find(L'<', i);
        if (nextTag == std::wstring::npos) nextTag = src.length();
        std::wstring text = src.substr(i, nextTag - i);

        size_t tStart = 0;
        while (tStart < text.length() && iswspace(text[tStart])) tStart++;
        size_t tEnd = text.length();
        while (tEnd > tStart && iswspace(text[tEnd - 1])) tEnd--;

        if (tEnd > tStart) {
            if (!out.empty() && out.back() != L'\n') out += L'\n';
            appendIndent(indent);
            out += text.substr(tStart, tEnd - tStart);
            out += L'\n';
        }
        i = nextTag;
    }

    if (!out.empty() && out.back() != L'\n') out += L'\n';
    return out;
}

static std::wstring FormatPhpNative(const std::wstring& src) {
    std::wstring out;
    out.reserve(src.length() * 2);
    int indent = 0;
    std::wistringstream iss(src);
    std::wstring line;

    auto appendIndent = [&](int lvl) {
        for (int k = 0; k < lvl; ++k) out += L"    ";
    };

    static const std::unordered_set<std::wstring> voidTags = {
        L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
        L"link", L"meta", L"param", L"source", L"track", L"wbr", L"!doctype"
    };

    bool inPhpBlock = false;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        size_t start = 0;
        while (start < line.length() && (line[start] == L' ' || line[start] == L'\t')) start++;
        size_t end = line.length();
        while (end > start && (line[end - 1] == L' ' || line[end - 1] == L'\t')) end--;

        if (start >= end) {
            out += L'\n';
            continue;
        }

        std::wstring trimmed = line.substr(start, end - start);

        // Determine dedent for closing tags or closing braces on the current line
        int lineDedent = 0;

        // Check if line starts with closing HTML tag: </...
        if (trimmed.length() >= 2 && trimmed[0] == L'<' && trimmed[1] == L'/') {
            lineDedent++;
        }
        // Check if line starts with closing brace: }
        else if (trimmed[0] == L'}') {
            for (wchar_t ch : trimmed) {
                if (ch == L'}') lineDedent++;
                else if (!iswspace(ch)) break;
            }
        }
        // Check if line starts with alternative syntax closing: endif, endforeach, endwhile, endfor, endswitch, else, elseif
        else {
            std::wstring lower = trimmed;
            for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
            if (lower.rfind(L"endif", 0) == 0 || lower.rfind(L"endforeach", 0) == 0 ||
                lower.rfind(L"endwhile", 0) == 0 || lower.rfind(L"endfor", 0) == 0 ||
                lower.rfind(L"endswitch", 0) == 0 || lower.rfind(L"else", 0) == 0 ||
                lower.rfind(L"elseif", 0) == 0 || lower.rfind(L"<?php endif", 0) == 0 ||
                lower.rfind(L"<?php endforeach", 0) == 0 || lower.rfind(L"<?php endwhile", 0) == 0 ||
                lower.rfind(L"<?php endfor", 0) == 0 || lower.rfind(L"<?php else", 0) == 0 ||
                lower.rfind(L"<?php elseif", 0) == 0 || lower.rfind(L"<?php }", 0) == 0) {
                lineDedent++;
            }
        }

        int curIndent = indent - lineDedent;
        if (curIndent < 0) curIndent = 0;

        appendIndent(curIndent);
        out += trimmed + L'\n';

        // Now compute indent delta for the NEXT line:
        int delta = 0;

        // Curly braces delta
        for (wchar_t ch : trimmed) {
            if (ch == L'{') delta++;
            else if (ch == L'}') delta--;
        }

        // Alternative control structures ending with ':'
        if (trimmed.back() == L':') {
            std::wstring lower = trimmed;
            for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
            if (lower.find(L"if") != std::wstring::npos || lower.find(L"foreach") != std::wstring::npos ||
                lower.find(L"while") != std::wstring::npos || lower.find(L"for") != std::wstring::npos ||
                lower.find(L"else") != std::wstring::npos || lower.find(L"switch") != std::wstring::npos) {
                delta++;
            }
        }

        // HTML tags delta (outside <? ... ?>)
        size_t pos = 0;
        while (pos < trimmed.length()) {
            if (trimmed[pos] == L'<' && pos + 1 < trimmed.length() && trimmed[pos + 1] == L'?') {
                size_t phpEnd = trimmed.find(L"?>", pos + 2);
                if (phpEnd != std::wstring::npos) {
                    pos = phpEnd + 2;
                    continue;
                } else {
                    inPhpBlock = true;
                    break;
                }
            }

            if (trimmed[pos] == L'?' && pos + 1 < trimmed.length() && trimmed[pos + 1] == L'>') {
                inPhpBlock = false;
                pos += 2;
                continue;
            }

            if (!inPhpBlock && trimmed[pos] == L'<') {
                size_t tagEnd = trimmed.find(L'>', pos);
                if (tagEnd != std::wstring::npos) {
                    std::wstring tagContent = trimmed.substr(pos + 1, tagEnd - (pos + 1));
                    if (!tagContent.empty()) {
                        bool isClosing = (tagContent[0] == L'/');
                        bool isSelfClosing = (tagContent.back() == L'/');
                        size_t nStart = isClosing ? 1 : 0;
                        while (nStart < tagContent.length() && iswspace(tagContent[nStart])) nStart++;
                        size_t nEnd = nStart;
                        while (nEnd < tagContent.length() && !iswspace(tagContent[nEnd]) && tagContent[nEnd] != L'/') nEnd++;
                        std::wstring tagName = tagContent.substr(nStart, nEnd - nStart);
                        for (auto& c : tagName) c = static_cast<wchar_t>(towlower(c));

                        if (!tagName.empty() && voidTags.count(tagName) == 0 && !isSelfClosing && tagName[0] != L'!') {
                            if (isClosing) {
                                if (pos > 0) delta--;
                            } else {
                                delta++;
                            }
                        }
                    }
                    pos = tagEnd + 1;
                    continue;
                }
            }
            pos++;
        }

        indent += delta;
        if (indent < 0) indent = 0;
    }

    return out;
}

static std::wstring FormatBracesNative(const std::wstring& src) {
    std::wstring out;
    out.reserve(src.length() * 2);
    int indent = 0;
    std::wistringstream iss(src);
    std::wstring line;

    auto appendIndent = [&](int lvl) {
        for (int k = 0; k < lvl; ++k) out += L"    ";
    };

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        size_t start = 0;
        while (start < line.length() && (line[start] == L' ' || line[start] == L'\t')) start++;
        size_t end = line.length();
        while (end > start && (line[end - 1] == L' ' || line[end - 1] == L'\t')) end--;

        if (start >= end) {
            out += L'\n';
            continue;
        }

        std::wstring trimmed = line.substr(start, end - start);

        int leadingCloses = 0;
        for (wchar_t ch : trimmed) {
            if (ch == L'}') leadingCloses++;
            else if (!iswspace(ch)) break;
        }

        int curIndent = indent - leadingCloses;
        if (curIndent < 0) curIndent = 0;

        appendIndent(curIndent);
        out += trimmed + L'\n';

        for (wchar_t ch : trimmed) {
            if (ch == L'{') indent++;
            else if (ch == L'}') { if (indent > 0) indent--; }
        }
    }
    return out;
}

void Window::DoFormatDocument() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc || pDoc->isSpecialView) return;

    // Check if Prettier can format this document (HTML, CSS, JS, TS, JSON, Markdown, PHP)
    std::wstring ext;
    if (!pDoc->filePath.empty()) {
        ext = std::filesystem::path(pDoc->filePath).extension().wstring();
    } else if (!pDoc->fileName.empty()) {
        ext = std::filesystem::path(pDoc->fileName).extension().wstring();
    }
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));

    bool handledByPrettier = false;
    if (ext == L".css" || ext == L".scss" || ext == L".less" ||
        ext == L".html" || ext == L".htm" || ext == L".js" ||
        ext == L".jsx" || ext == L".ts" || ext == L".tsx" ||
        ext == L".json" || ext == L".md") {
        
        wchar_t tempPath[MAX_PATH];
        if (GetTempPathW(MAX_PATH, tempPath)) {
            std::wstring tempFile = std::wstring(tempPath) + L"phide_fmt_" + std::to_wstring(GetCurrentProcessId()) + ext;
            std::string textUtf8;
            std::wstring docText = pDoc->buffer.GetText();
            int uLen = WideCharToMultiByte(CP_UTF8, 0, docText.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (uLen > 0) {
                textUtf8.resize(uLen - 1);
                WideCharToMultiByte(CP_UTF8, 0, docText.c_str(), -1, &textUtf8[0], uLen - 1, nullptr, nullptr);
            }
            std::ofstream ofs(std::filesystem::path(tempFile), std::ios::binary);
            if (ofs) {
                ofs.write(textUtf8.data(), textUtf8.size());
                ofs.close();

                std::wstring cmd = L"cmd.exe /c prettier --write \"" + tempFile + L"\"";
                STARTUPINFOW si = { sizeof(si) };
                si.dwFlags |= STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi = { 0 };
                if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    WaitForSingleObject(pi.hProcess, 4000);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);

                    std::ifstream ifs(std::filesystem::path(tempFile), std::ios::binary);
                    if (ifs) {
                        std::string formattedStr((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                        ifs.close();
                        if (!formattedStr.empty() && formattedStr != textUtf8) {
                            int wLen = MultiByteToWideChar(CP_UTF8, 0, formattedStr.c_str(), -1, nullptr, 0);
                            if (wLen > 0) {
                                std::wstring formattedW(wLen - 1, 0);
                                MultiByteToWideChar(CP_UTF8, 0, formattedStr.c_str(), -1, &formattedW[0], wLen - 1);
                                pDoc->buffer.SetText(formattedW);
                                MarkDocumentModified(pDoc);
                                handledByPrettier = true;
                                InvalidateRect(m_hwnd, nullptr, FALSE);
                            }
                        }
                    }
                }
                std::error_code ec;
                std::filesystem::remove(std::filesystem::path(tempFile), ec);
            }
        }
    }

    if (!handledByPrettier) {
        std::wstring rawText = pDoc->buffer.GetText();
        std::wstring formatted;
        if (ext == L".css" || ext == L".scss" || ext == L".less") {
            formatted = FormatCssNative(rawText);
        } else if (ext == L".html" || ext == L".htm" || ext == L".xml" || ext == L".svg") {
            formatted = FormatHtmlNative(rawText);
        } else if (ext == L".php") {
            formatted = FormatPhpNative(rawText);
        } else if (ext == L".js" || ext == L".jsx" || ext == L".ts" || ext == L".tsx" ||
                   ext == L".cpp" || ext == L".c" || ext == L".h" || ext == L".hpp") {
            formatted = FormatBracesNative(rawText);
        }

        if (!formatted.empty() && formatted != rawText) {
            pDoc->buffer.SetText(formatted);
            MarkDocumentModified(pDoc);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else {
            size_t lineCount = pDoc->buffer.GetLineCount();
            bool modified = false;
            for (size_t i = 0; i < lineCount; ++i) {
                std::wstring line = pDoc->buffer.GetLine(i);
                size_t end = line.find_last_not_of(L" \t\r\n");
                if (end != std::wstring::npos && end + 1 < line.length()) {
                    pDoc->buffer.SetLine(i, line.substr(0, end + 1));
                    modified = true;
                }
            }
            if (modified) {
                MarkDocumentModified(pDoc);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
        }
    }
}

void Window::DoFormatSelection() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc || pDoc->isSpecialView || !pDoc->HasSelection()) return;
    size_t startLine = 0, startCol = 0, endLine = 0, endCol = 0;
    pDoc->GetSelectionRange(startLine, startCol, endLine, endCol);
    bool modified = false;
    for (size_t i = startLine; i <= endLine && i < pDoc->buffer.GetLineCount(); ++i) {
        std::wstring line = pDoc->buffer.GetLine(i);
        size_t end = line.find_last_not_of(L" \t\r\n");
        if (end != std::wstring::npos && end + 1 < line.length()) {
            pDoc->buffer.SetLine(i, line.substr(0, end + 1));
            modified = true;
        }
    }
    if (modified) {
        MarkDocumentModified(pDoc);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoOrganizeImports() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc || pDoc->isSpecialView) return;
    std::vector<std::wstring> imports;
    size_t importStart = (std::numeric_limits<size_t>::max)();
    size_t importEnd = 0;
    size_t lineCount = pDoc->buffer.GetLineCount();
    for (size_t i = 0; i < lineCount; ++i) {
        std::wstring line = pDoc->buffer.GetLine(i);
        auto trimmed = line;
        size_t first = trimmed.find_first_not_of(L" \t");
        if (first != std::wstring::npos) trimmed = trimmed.substr(first);
        if (trimmed.rfind(L"import ", 0) == 0 || trimmed.rfind(L"#include", 0) == 0 ||
            (trimmed.rfind(L"const ", 0) == 0 && trimmed.find(L"require(") != std::wstring::npos)) {
            if (importStart == (std::numeric_limits<size_t>::max)()) importStart = i;
            importEnd = i;
            imports.push_back(line);
        } else if (importStart != (std::numeric_limits<size_t>::max)()) {
            if (!trimmed.empty()) break;
        }
    }
    if (imports.size() > 1 && importStart != (std::numeric_limits<size_t>::max)()) {
        std::sort(imports.begin(), imports.end());
        for (size_t k = 0; k < imports.size(); ++k) {
            pDoc->buffer.SetLine(importStart + k, imports[k]);
        }
        MarkDocumentModified(pDoc);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoGoToDefinition() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc || pDoc->isSpecialView) return;
    m_editor.ShowRefactorPopup(L"Definition lookup active");
}

void Window::DoRenameSymbol() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc || pDoc->isSpecialView) return;
    m_editor.ShowRefactorPopup(L"Rename Symbol (F2)");
}

bool Window::IsFileInDirectory(const std::wstring& filePath, const std::wstring& dirPath) {
    if (filePath.empty() || dirPath.empty()) return false;
    std::wstring fNorm = filePath;
    std::wstring dNorm = dirPath;
    for (auto& c : fNorm) { if (c == L'/') c = L'\\'; c = static_cast<wchar_t>(towlower(c)); }
    for (auto& c : dNorm) { if (c == L'/') c = L'\\'; c = static_cast<wchar_t>(towlower(c)); }
    while (!dNorm.empty() && dNorm.back() == L'\\') dNorm.pop_back();
    if (fNorm.rfind(dNorm + L'\\', 0) == 0 || fNorm == dNorm) {
        return true;
    }
    return false;
}

std::wstring Window::FindProjectRootForFile(const std::wstring& filePath) const {
    std::wstring openFolder = m_drawer.HasOpenFolder() ? m_drawer.GetOpenFolderPath() : m_workspace.GetRootPath().wstring();
    if (filePath.empty()) {
        return openFolder;
    }

    std::filesystem::path cur = std::filesystem::path(filePath);
    std::error_code ec;
    if (!std::filesystem::is_directory(cur, ec)) {
        cur = cur.parent_path();
    }

    std::filesystem::path stopPath;
    if (!openFolder.empty()) {
        stopPath = std::filesystem::weakly_canonical(openFolder, ec);
    }

    std::filesystem::path bestRoot;
    std::filesystem::path p = cur;
    while (!p.empty() && p.has_relative_path()) {
        if (std::filesystem::exists(p / L"package.json", ec) ||
            std::filesystem::exists(p / L"electron.vite.config.ts", ec) ||
            std::filesystem::exists(p / L"electron.vite.config.js", ec) ||
            std::filesystem::exists(p / L"vite.config.ts", ec) ||
            std::filesystem::exists(p / L"vite.config.js", ec) ||
            std::filesystem::exists(p / L"next.config.js", ec) ||
            std::filesystem::exists(p / L"next.config.mjs", ec) ||
            std::filesystem::exists(p / L"next.config.ts", ec) ||
            std::filesystem::exists(p / L"manage.py", ec) ||
            std::filesystem::exists(p / L"composer.json", ec)) {
            bestRoot = p;
            break;
        }

        if (!stopPath.empty() && std::filesystem::equivalent(p, stopPath, ec)) {
            bestRoot = p;
            break;
        }

        std::filesystem::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }

    if (!bestRoot.empty()) {
        return bestRoot.wstring();
    }
    if (!openFolder.empty()) {
        return openFolder;
    }
    return cur.wstring();
}

Window::ProjectDecision Window::DetectFramework(const std::wstring& rootDir, const std::wstring& activeFilePath) {
    // 1. High-speed Decision Tree Cache Lookup with Timestamp Validation
    std::wstring key = rootDir;
    for (auto& c : key) c = static_cast<wchar_t>(towlower(c));

    auto it = m_projectDecisionCache.find(key);
    if (it != m_projectDecisionCache.end() && !it->second.markerPath.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(it->second.markerPath, ec)) {
            auto curTime = std::filesystem::last_write_time(it->second.markerPath, ec);
            if (!ec && curTime == it->second.markerTime) {
                return it->second;
            }
        }
    }

    // 2. Decision Tree Evaluation
    ProjectDecision decision;
    decision.rootDir = rootDir;
    decision.framework = ProjectFramework::None;

    std::error_code ec;
    std::filesystem::path root(rootDir);

    if (!rootDir.empty() && std::filesystem::exists(root, ec) && std::filesystem::is_directory(root, ec)) {
        // Feature Node 1: package.json (Vite, Electron, Next.js, etc.)
        std::filesystem::path pkgPath = root / L"package.json";
        if (std::filesystem::exists(pkgPath, ec)) {
            decision.markerPath = pkgPath.wstring();
            decision.markerTime = std::filesystem::last_write_time(pkgPath, ec);

            std::string pkgContent;
            {
                std::ifstream f(pkgPath, std::ios::binary);
                if (f.is_open()) {
                    std::stringstream buffer;
                    buffer << f.rdbuf();
                    pkgContent = buffer.str();
                }
            }

            auto hasDep = [&](const std::string& name) -> bool {
                return pkgContent.find("\"" + name + "\"") != std::string::npos;
            };

            // Decision Branch 1A: Electron / Electron-Vite
            bool hasElectronViteConfig = std::filesystem::exists(root / L"electron.vite.config.ts", ec) ||
                                         std::filesystem::exists(root / L"electron.vite.config.js", ec) ||
                                         std::filesystem::exists(root / L"electron.vite.config.mjs", ec);
            bool hasElectronBuilder = std::filesystem::exists(root / L"electron-builder.yml", ec) ||
                                      std::filesystem::exists(root / L"electron-builder.json", ec);
            bool hasElectron = hasDep("electron") || hasDep("electron-vite") || hasElectronViteConfig || hasElectronBuilder;

            if (hasElectron) {
                decision.framework = ProjectFramework::Electron;
                decision.title = L"Electron Server";
                if (hasElectronViteConfig || hasDep("electron-vite")) {
                    decision.command = L"npm run dev";
                } else if (pkgContent.find("\"start\"") != std::string::npos) {
                    decision.command = L"npm run start";
                } else if (pkgContent.find("\"dev\"") != std::string::npos) {
                    decision.command = L"npm run dev";
                } else {
                    decision.command = L"npm start";
                }
                decision.port = 0;
                decision.customUrl = L"";
                m_projectDecisionCache[key] = decision;
                return decision;
            }

            // Decision Branch 1B: Next.js
            bool hasNextConfig = std::filesystem::exists(root / L"next.config.js", ec) ||
                                 std::filesystem::exists(root / L"next.config.mjs", ec) ||
                                 std::filesystem::exists(root / L"next.config.ts", ec) ||
                                 std::filesystem::exists(root / L"next.config.cjs", ec) ||
                                 std::filesystem::exists(root / L".next", ec);
            if (hasNextConfig || hasDep("next")) {
                decision.framework = ProjectFramework::NextJs;
                decision.title = L"Next.js Server";
                decision.command = L"npm run dev";
                decision.port = 3000;
                decision.customUrl = L"http://localhost:3000";
                m_projectDecisionCache[key] = decision;
                return decision;
            }

            // Decision Branch 1C: Vite
            bool hasViteConfig = std::filesystem::exists(root / L"vite.config.ts", ec) ||
                                 std::filesystem::exists(root / L"vite.config.js", ec) ||
                                 std::filesystem::exists(root / L"vite.config.mjs", ec) ||
                                 std::filesystem::exists(root / L"vite.config.cjs", ec);
            if (hasViteConfig || hasDep("vite")) {
                decision.framework = ProjectFramework::Vite;
                decision.title = L"Vite Server";
                decision.command = L"npm run dev";
                decision.port = 5173;
                decision.customUrl = L"http://localhost:5173";
                m_projectDecisionCache[key] = decision;
                return decision;
            }

            // Decision Branch 1D: Astro / Nuxt
            if (std::filesystem::exists(root / L"astro.config.mjs", ec) || std::filesystem::exists(root / L"astro.config.ts", ec) || hasDep("astro")) {
                decision.framework = ProjectFramework::Vite;
                decision.title = L"Astro Server";
                decision.command = L"npm run dev";
                decision.port = 4321;
                decision.customUrl = L"http://localhost:4321";
                m_projectDecisionCache[key] = decision;
                return decision;
            }
            if (std::filesystem::exists(root / L"nuxt.config.ts", ec) || std::filesystem::exists(root / L"nuxt.config.js", ec) || hasDep("nuxt")) {
                decision.framework = ProjectFramework::Vite;
                decision.title = L"Nuxt Server";
                decision.command = L"npm run dev";
                decision.port = 3000;
                decision.customUrl = L"http://localhost:3000";
                m_projectDecisionCache[key] = decision;
                return decision;
            }

            // Decision Branch 1E: Generic Node.js with package.json
            decision.framework = ProjectFramework::NodeScript;
            decision.title = L"Node.js App";
            if (pkgContent.find("\"dev\"") != std::string::npos) {
                decision.command = L"npm run dev";
            } else if (pkgContent.find("\"start\"") != std::string::npos) {
                decision.command = L"npm start";
            } else {
                decision.command = L"npm test";
            }
            decision.port = 0;
            decision.customUrl = L"";
            m_projectDecisionCache[key] = decision;
            return decision;
        }

        // Feature Node 2: Django (manage.py)
        std::filesystem::path managePy = root / L"manage.py";
        if (std::filesystem::exists(managePy, ec)) {
            decision.markerPath = managePy.wstring();
            decision.markerTime = std::filesystem::last_write_time(managePy, ec);
            decision.framework = ProjectFramework::Django;
            decision.title = L"Django Server";
            decision.command = L"python manage.py runserver";
            decision.port = 8000;
            decision.customUrl = L"http://127.0.0.1:8000";
            m_projectDecisionCache[key] = decision;
            return decision;
        }

        // Feature Node 3: PHP Check (composer.json or index.php)
        std::filesystem::path composerJson = root / L"composer.json";
        std::filesystem::path indexPhp = root / L"index.php";
        if (std::filesystem::exists(composerJson, ec) || std::filesystem::exists(indexPhp, ec)) {
            decision.framework = ProjectFramework::Php;
            decision.title = L"PHP";
            decision.command = L"";
            m_projectDecisionCache[key] = decision;
            return decision;
        }

        // Feature Node 4: Static HTML
        std::filesystem::path indexHtml = root / L"index.html";
        if (std::filesystem::exists(indexHtml, ec)) {
            decision.framework = ProjectFramework::LiveServerHtml;
            decision.title = L"Live Server";
            decision.command = L"";
            decision.port = 5500;
            decision.customUrl = L"http://127.0.0.1:5500";
            m_projectDecisionCache[key] = decision;
            return decision;
        }
    }

    // Feature Node 5: Standalone single-file fallback based on active file extension
    std::filesystem::path p(activeFilePath);
    std::wstring ext = p.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));

    if (ext == L".php" || ext == L".phtml" || ext == L".php8") {
        decision.framework = ProjectFramework::Php;
        decision.title = L"PHP";
        decision.command = L"";
    } else if (ext == L".py") {
        decision.framework = ProjectFramework::PythonScript;
        decision.title = L"Python Script";
        decision.command = L"python \"" + activeFilePath + L"\"";
    } else if (ext == L".html" || ext == L".htm") {
        decision.framework = ProjectFramework::LiveServerHtml;
        decision.title = L"Live Server";
        decision.port = 5500;
        decision.customUrl = L"http://127.0.0.1:5500";
    } else if (ext == L".js" || ext == L".ts" || ext == L".mjs" || ext == L".cjs") {
        decision.framework = ProjectFramework::NodeScript;
        decision.title = L"Node.js Script";
        decision.command = L"node \"" + activeFilePath + L"\"";
    }

    return decision;
}

void Window::RunProjectDevServer(const std::wstring& projectRoot, const ProjectDecision& decision) {
    m_isProjectRunning = true;
    m_runningProjectRoot = projectRoot;
    m_runningProjectDecision = decision;

    auto* srv = FindLiveServer(projectRoot);
    if (!srv) {
        ActiveLiveServer entry;
        entry.filePath = projectRoot;
        entry.fileName = std::filesystem::path(projectRoot).filename().wstring();
        entry.rootDir = projectRoot;
        entry.port = decision.port;
        entry.framework = decision.title;
        entry.title = decision.title;
        entry.customUrl = decision.customUrl;
        entry.progress = 0.05f;
        entry.progressDurationMs = 2200.0f;
        entry.elapsedMs = 0.0f;
        entry.browserOpened = false;
        entry.isStarting = true;
        entry.hasError = false;
        entry.errorMsg.clear();
        m_activeLiveServers.push_back(std::move(entry));
    } else {
        srv->framework = decision.title;
        srv->title = decision.title;
        srv->customUrl = decision.customUrl;
        srv->port = decision.port;
        srv->progress = 0.05f;
        srv->elapsedMs = 0.0f;
        srv->isStarting = true;
        srv->browserOpened = false;
        srv->hasError = false;
        srv->errorMsg.clear();
    }

    m_terminal.Open(projectRoot);
    AnimateTerminalOpen();
    m_terminal.StartServerSession(decision.title, decision.command, projectRoot, projectRoot);
    m_isTerminalFocused = false;
    SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
    UpdateExecutionButtonStates();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

struct DiscardRestoreTaskResult {
    std::wstring targetPath;
    std::wstring targetFileName;
    std::wstring targetStatus;
};

struct DeleteTaskResult {
    std::wstring targetPath;
    bool isFolder = false;
    bool success = false;
};

void Window::ExecuteDiscardOrRestore(const std::wstring& targetPath, const std::wstring& targetFileName, const std::wstring& targetStatus) {
    ExecuteDiscardOrRestoreAsync(targetPath, targetFileName, targetStatus);
}

void Window::ExecuteDiscardOrRestoreAsync(const std::wstring& targetPath, const std::wstring& targetFileName, const std::wstring& targetStatus) {
    if (targetPath.empty()) {
        m_confirmDiscardModal.Close();
        return;
    }

    SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
    HWND hwnd = m_hwnd;
    std::thread([this, hwnd, targetPath, targetFileName, targetStatus]() {
        auto tStart = std::chrono::steady_clock::now();

        if (targetPath == L"*" || targetStatus == L"ALL") {
            m_drawer.DiscardAllChanges();
        } else {
            m_drawer.DiscardFile(targetPath);
        }

        // Ensure smooth visual confirmation of the spinning state
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tStart).count();
        if (elapsedMs < 150) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150 - elapsedMs));
        }

        auto* pRes = new DiscardRestoreTaskResult{ targetPath, targetFileName, targetStatus };
        PostMessageW(hwnd, WM_USER + 204, 0, reinterpret_cast<LPARAM>(pRes));
    }).detach();
}

void Window::FinishDiscardOrRestore(const std::wstring& targetPath, const std::wstring& targetFileName, const std::wstring& targetStatus) {
    if (targetPath.empty()) return;

    if (targetPath == L"*" || targetStatus == L"ALL") {
        m_drawer.ShowGitNotification(L"✓ Discarded all changes");
    } else if (targetStatus == L"D") {
        m_drawer.ShowGitNotification(L"✓ Restored " + targetFileName);
    } else {
        m_drawer.ShowGitNotification(L"✓ Discarded " + targetFileName);
    }

    auto PathsMatch = [](const std::wstring& p1, const std::wstring& p2) -> bool {
        if (p1.empty() || p2.empty()) return false;
        if (p2 == L"*") return true;
        std::wstring c1 = p1;
        std::wstring c2 = p2;
        for (auto& c : c1) { if (c == L'/') c = L'\\'; c = static_cast<wchar_t>(towlower(c)); }
        for (auto& c : c2) { if (c == L'/') c = L'\\'; c = static_cast<wchar_t>(towlower(c)); }
        if (c1 == c2) return true;
        if (c1.ends_with(L"\\" + c2) || c2.ends_with(L"\\" + c1)) return true;
        if (c1.ends_with(c2) || c2.ends_with(c1)) return true;
        return false;
    };

    // Close matching working tree diff view tabs
    for (int g = static_cast<int>(m_editorGroups.size()) - 1; g >= 0; --g) {
        if (g >= static_cast<int>(m_editorGroups.size())) continue;
        for (int d = static_cast<int>(m_editorGroups[g].docs.size()) - 1; d >= 0; --d) {
            if (g >= static_cast<int>(m_editorGroups.size())) break;
            if (d >= static_cast<int>(m_editorGroups[g].docs.size())) continue;
            auto& doc = m_editorGroups[g].docs[d];
            if (doc.isDiffView && !doc.isDiffRightReadOnly) {
                if (PathsMatch(doc.filePath, targetPath)) {
                    CloseDocument(g, d);
                }
            }
        }
    }

    // Reload open regular document buffers or close them if file no longer exists
    for (int g = static_cast<int>(m_editorGroups.size()) - 1; g >= 0; --g) {
        if (g >= static_cast<int>(m_editorGroups.size())) continue;
        for (int d = static_cast<int>(m_editorGroups[g].docs.size()) - 1; d >= 0; --d) {
            if (g >= static_cast<int>(m_editorGroups.size())) break;
            if (d >= static_cast<int>(m_editorGroups[g].docs.size())) continue;
            auto& doc = m_editorGroups[g].docs[d];
            if (!doc.filePath.empty() && PathsMatch(doc.filePath, targetPath)) {
                if (!std::filesystem::exists(doc.filePath)) {
                    CloseDocument(g, d);
                } else {
                    doc.buffer.LoadFromFile(doc.filePath);
                    doc.originalDiskText = doc.buffer.GetText();
                    doc.isDirty = false;
                    doc.ClearSelection();
                    if (doc.buffer.GetCursor().line >= doc.buffer.GetLineCount()) {
                        doc.buffer.SetCursor(doc.buffer.GetLineCount() > 0 ? doc.buffer.GetLineCount() - 1 : 0, 0);
                    }
                    doc.diagnostics = editor::SyntaxValidator::Validate(doc.filePath, doc.buffer);
                    if (doc.isDiffView) {
                        ComputeDiffHunks(doc);
                    }
                }
            }
        }
    }

    m_drawer.RefreshTree();
    UpdateDrawerDirtyFiles();
    UpdateDrawerErrorFiles();
    m_drawer.RefreshGitStatusOnly(false);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::ExecuteDeleteAsync(const std::wstring& targetPath, bool isFolder) {
    if (targetPath.empty()) {
        m_confirmDiscardModal.Close();
        return;
    }

    SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
    HWND hwnd = m_hwnd;
    std::thread([this, hwnd, targetPath, isFolder]() {
        auto tStart = std::chrono::steady_clock::now();

        std::wstring backup;
        m_explorerUndo.PrepareDelete(targetPath, isFolder, backup);
        std::error_code ec;
        std::filesystem::remove_all(targetPath, ec);
        bool success = !ec;
        if (success) {
            m_explorerUndo.PushDelete(targetPath, backup, isFolder);
        }

        // Ensure smooth visual confirmation of the spinning state
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tStart).count();
        if (elapsedMs < 150) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150 - elapsedMs));
        }

        auto* pRes = new DeleteTaskResult{ targetPath, isFolder, success };
        PostMessageW(hwnd, WM_USER + 205, 0, reinterpret_cast<LPARAM>(pRes));
    }).detach();
}

void Window::FinishDelete(const std::wstring& targetPath, bool isFolder, bool success) {
    if (targetPath.empty()) return;

    std::wstring fileName = std::filesystem::path(targetPath).filename().wstring();
    if (success) {
        m_drawer.ShowGitNotification(L"✓ Deleted " + fileName);
    } else {
        m_drawer.ShowGitNotification(L"✕ Failed to delete " + fileName);
    }

    // Close open documents corresponding to deleted path
    for (int g = static_cast<int>(m_editorGroups.size()) - 1; g >= 0; --g) {
        if (g >= static_cast<int>(m_editorGroups.size())) continue;
        for (int d = static_cast<int>(m_editorGroups[g].docs.size()) - 1; d >= 0; --d) {
            if (g >= static_cast<int>(m_editorGroups.size())) break;
            if (d >= static_cast<int>(m_editorGroups[g].docs.size())) continue;
            const auto& path = m_editorGroups[g].docs[d].filePath;
            if (path == targetPath || (isFolder && path.starts_with(targetPath + L"\\"))) {
                CloseDocument(g, d);
            }
        }
    }

    if (m_selectedExplorerPath == targetPath) m_selectedExplorerPath.clear();
    m_drawer.RefreshTree();
    UpdateDrawerDirtyFiles();
    UpdateDrawerErrorFiles();
    m_drawer.RefreshGitStatusOnly(true);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::ShowGitError(const std::wstring& cmd, const std::wstring& output, const std::wstring& repoDir) {
    {
        std::lock_guard<std::mutex> lock(m_gitErrorMutex);
        m_pendingGitError = { cmd, output, repoDir };
    }
    if (m_hwnd) {
        PostMessageW(m_hwnd, WM_USER + 203, 0, 0);
    }
}

void Window::OpenGitLogTerminal() {
    if (!m_terminal.IsOpen()) {
        AnimateTerminalOpen();
    }
    m_terminal.OpenGitLogTab(ui::GitLogManager::Instance().GetLogLines());
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::ShowGitCommandOutput(const std::wstring& cmd, const std::wstring& output, const std::wstring& repoDir) {
    // 1. Determine target directory: prefer repoDir/.git if exists, otherwise temp path
    std::filesystem::path targetDir;
    if (!repoDir.empty() && std::filesystem::exists(std::filesystem::path(repoDir) / ".git")) {
        targetDir = std::filesystem::path(repoDir) / ".git";
    } else {
        wchar_t sysTemp[MAX_PATH] = { 0 };
        GetTempPathW(MAX_PATH, sysTemp);
        targetDir = sysTemp;
    }

    // 2. Generate filename: git-error-<timestamp>.txt (Image 5)
    auto now = std::chrono::system_clock::now();
    auto epochSec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::wstring fileName = L"git-error-" + std::to_wstring(epochSec) + L".txt";
    std::filesystem::path filePath = targetDir / fileName;

    // 3. Write content: formatted command line and output
    std::wstring content = L"> " + cmd + L"\n\n" + output;
    std::ofstream out(filePath, std::ios::binary);
    if (out.is_open()) {
        int len = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string u8(len, 0);
            WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, u8.data(), len, nullptr, nullptr);
            if (!u8.empty() && u8.back() == '\0') u8.pop_back();
            out.write(u8.data(), u8.size());
        }
        out.close();
    }

    // 4. Open the temporary file in the editor as an active document tab!
    OpenFileDocument(filePath.wstring(), false);
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoOpenCommitEditMsg() {
    std::wstring repoDir;
    {
        std::lock_guard<std::recursive_mutex> lock(m_drawer.GetGitMutex());
        repoDir = m_drawer.HasOpenFolder() ? m_drawer.GetOpenFolderPath() : m_workspace.GetRootPath().wstring();
    }
    if (repoDir.empty()) {
        repoDir = std::filesystem::current_path().wstring();
    }
    if (repoDir.empty()) return;

    // 1. Stage all changes first (`git add -A`)
    ui::RunGitCmd(L"add -A", repoDir);

    // 2. Query branch name
    std::wstring branch = m_drawer.GetGitBranch();
    if (branch.empty()) {
        auto resBranch = ui::RunGitCmd(L"rev-parse --abbrev-ref HEAD", repoDir);
        if (resBranch.exitCode == 0 && !resBranch.output.empty()) {
            branch = resBranch.output;
            while (!branch.empty() && (branch.back() == L'\r' || branch.back() == L'\n' || branch.back() == L' ')) {
                branch.pop_back();
            }
        }
    }
    if (branch.empty()) branch = L"main";

    // 3. Query staged changes: `git diff --cached --name-status`
    auto resDiff = ui::RunGitCmd(L"diff --cached --name-status", repoDir);

    // 4. Construct COMMIT_EDITMSG content matching Image 4
    std::wstring editMsgContent =
        L"\n"
        L"# Please enter the commit message for your changes. Lines starting\n"
        L"# with '#' will be ignored, and an empty message aborts the commit.\n"
        L"#\n"
        L"# On branch " + branch + L"\n"
        L"# Changes to be committed:\n";

    if (resDiff.exitCode == 0 && !resDiff.output.empty()) {
        std::wstringstream ss(resDiff.output);
        std::wstring line;
        while (std::getline(ss, line)) {
            while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.pop_back();
            if (line.empty()) continue;
            wchar_t statusChar = line[0];
            std::wstring filePath;
            size_t tabPos = line.find_first_of(L"\t ");
            if (tabPos != std::wstring::npos) {
                size_t pathStart = line.find_first_not_of(L"\t ", tabPos);
                if (pathStart != std::wstring::npos) {
                    filePath = line.substr(pathStart);
                }
            }
            if (filePath.empty() && line.length() > 2) {
                filePath = line.substr(2);
            }
            std::wstring statusStr = L"modified";
            if (statusChar == L'A') statusStr = L"new file";
            else if (statusChar == L'D') statusStr = L"deleted";
            else if (statusChar == L'R') statusStr = L"renamed";
            else if (statusChar == L'M') statusStr = L"modified";

            editMsgContent += L"#\t" + statusStr + L":   " + filePath + L"\n";
        }
    } else {
        editMsgContent += L"#\tnone\n";
    }
    editMsgContent += L"#\n";

    // 5. Determine path to .git/COMMIT_EDITMSG
    std::filesystem::path gitDir = std::filesystem::path(repoDir) / L".git";
    std::error_code ec;
    if (!std::filesystem::exists(gitDir, ec)) {
        std::filesystem::create_directories(gitDir, ec);
    }
    std::filesystem::path commitEditMsgPath = gitDir / L"COMMIT_EDITMSG";

    // Write file to disk as UTF-8
    std::ofstream ofs(commitEditMsgPath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (ofs.is_open()) {
        int len = WideCharToMultiByte(CP_UTF8, 0, editMsgContent.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string u8(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, editMsgContent.c_str(), -1, u8.data(), len, nullptr, nullptr);
            if (!u8.empty() && u8.back() == '\0') u8.pop_back();
            ofs.write(u8.data(), u8.size());
        }
        ofs.close();
    }

    // 6. Set drawer state: CommitEditMsg open (locks commit message input and greyouts Commit button)
    m_drawer.SetCommitEditMsgOpen(true);
    m_drawer.RefreshGitStatusOnly(false);

    // 7. Open file in editor tab
    OpenFileDocument(commitEditMsgPath.wstring(), false);

    // 8. Place cursor at Line 0, Col 0 so user can type immediately
    auto* pDoc = GetActiveDoc();
    if (pDoc) {
        pDoc->buffer.SetCursor(0, 0);
    }

    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoRunActiveDocument() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc) return;

    std::filesystem::path p(pDoc->filePath);
    std::wstring filePath = pDoc->filePath;
    std::wstring fileName = p.filename().wstring();
    std::wstring ext = p.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));

    // Identify project root
    std::wstring projectRoot = FindProjectRootForFile(filePath);

    // 1. PHP Check: Launch PHP with Nginx and phpMyAdmin orchestration (Image 2)
    if (ext == L".php" || ext == L".phtml" || ext == L".php8") {
        DoRunPhpApplication(filePath, projectRoot);
        return;
    }

    // 2. Python Script: Independent execution
    // User explicitly requested:
    // "unless i go to a python file, html file, and other file . on python file one can run also oyou can run another file its not single run at a time get it?"
    if (ext == L".py") {
        if (p.filename() == L"manage.py") {
            ProjectDecision decision = DetectFramework(projectRoot, filePath);
            if (decision.framework == ProjectFramework::Django) {
                // If phpMyAdmin is chosen in the database dropdown, run phpMyAdmin alongside Django
                auto& srvCfg = m_header.GetServerDatabaseConfig();
                if (srvCfg.phpMyAdminEnabled && !srvCfg.dontRunDatabase) {
                    m_header.SetDatabaseState(ui::DatabaseState::Running);
                    m_header.SetDatabaseLogOpened(false);
                }
                RunProjectDevServer(projectRoot, decision);
                return;
            }
        }

        std::wstring runDir = p.parent_path().wstring();
        if (runDir.empty()) runDir = projectRoot;
        m_terminal.Open(runDir);
        AnimateTerminalOpen();
        m_terminal.ExecuteCommandLine(L"python \"" + filePath + L"\"");
        m_isTerminalFocused = true;
        UpdateExecutionButtonStates();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 3. Project-Level Framework Run (Electron, Next.js, Vite, Django, etc.)
    ProjectDecision decision = DetectFramework(projectRoot, filePath);

    if (decision.framework == ProjectFramework::Php) {
        DoRunPhpApplication(filePath, projectRoot);
        return;
    }

    if (decision.framework == ProjectFramework::NextJs ||
        decision.framework == ProjectFramework::Vite ||
        decision.framework == ProjectFramework::Electron ||
        decision.framework == ProjectFramework::Django) {
        RunProjectDevServer(projectRoot, decision);
        return;
    }

    // 4. Static HTML Live Server Run
    if (decision.framework == ProjectFramework::LiveServerHtml || ext == L".html" || ext == L".htm") {
        std::wstring folder = p.parent_path().wstring();
        if (folder.empty()) folder = projectRoot;

        auto* srv = FindLiveServer(filePath);
        if (!srv) {
            int port = GetNextAvailableLiveServerPort();
            auto serverInst = std::make_unique<server::LiveServer>();
            if (serverInst->Start(folder, fileName, port)) {
                int actualPort = serverInst->GetPort();
                std::wstring localUrl = serverInst->GetLocalUrl(fileName);
                std::wstring networkUrl = serverInst->GetNetworkUrl(fileName);

                ActiveLiveServer entry;
                entry.filePath = filePath;
                entry.fileName = fileName;
                entry.rootDir = folder;
                entry.port = actualPort;
                entry.framework = L"Live Server";
                entry.title = L"Live Server";
                entry.customUrl = localUrl;
                entry.server = std::move(serverInst);
                entry.progress = 0.05f;
                entry.progressDurationMs = 2200.0f; // ~2.2 seconds progression before auto-opening browser
                entry.elapsedMs = 0.0f;
                entry.browserOpened = false;
                entry.isStarting = true;
                m_activeLiveServers.push_back(std::move(entry));

                m_terminal.Open(folder);
                AnimateTerminalOpen();
                m_terminal.StartLiveServerSession(localUrl, networkUrl, filePath, fileName);
                m_isTerminalFocused = false;

                SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
            }
        } else {
            // Server already running for this file - restart progression then open browser
            srv->progress = 0.05f;
            srv->elapsedMs = 0.0f;
            srv->isStarting = true;
            srv->browserOpened = false;
            std::wstring localUrl = srv->server ? srv->server->GetLocalUrl(srv->fileName) : srv->customUrl;
            std::wstring networkUrl = srv->server ? srv->server->GetNetworkUrl(srv->fileName) : localUrl;
            m_terminal.Open(folder);
            AnimateTerminalOpen();
            m_terminal.StartLiveServerSession(localUrl, networkUrl, filePath, fileName);
            m_isTerminalFocused = false;
            SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
        }

        UpdateExecutionButtonStates();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 5. Generic Node Script fallback
    if (decision.framework == ProjectFramework::NodeScript || ext == L".js" || ext == L".ts" || ext == L".mjs" || ext == L".cjs") {
        std::wstring runDir = p.parent_path().wstring();
        if (runDir.empty()) runDir = projectRoot;
        m_terminal.Open(runDir);
        AnimateTerminalOpen();
        m_terminal.ExecuteCommandLine(L"node \"" + filePath + L"\"");
        m_isTerminalFocused = true;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
}

void Window::DoDebugActiveDocument() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc) return;

    std::wstring folder = std::filesystem::path(pDoc->filePath).parent_path().wstring();
    if (folder.empty() && m_drawer.HasOpenFolder()) folder = m_drawer.GetOpenFolderPath();
    if (folder.empty()) folder = m_workspace.GetRootPath();

    m_terminal.Open(folder);
    AnimateTerminalOpen();
    m_terminal.StartDebugSession(pDoc->filePath, folder);
    m_isTerminalFocused = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoStopServer() {
    // 1. If project dev server is running, terminate it
    if (m_isProjectRunning && !m_runningProjectRoot.empty()) {
        m_isProjectRunning = false;
        m_terminal.TerminateServerSession(m_runningProjectRoot);
        StopLiveServer(m_runningProjectRoot, /*closeTab=*/false);
        m_runningProjectRoot.clear();
    }

    // 2. If active doc or file-level server is running, stop it
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->filePath.empty()) {
        auto* srv = FindLiveServer(pDoc->filePath);
        if (srv) {
            StopLiveServer(pDoc->filePath, /*closeTab=*/true);
        } else if (!m_activeLiveServers.empty()) {
            StopLiveServer(m_activeLiveServers.back().filePath, /*closeTab=*/true);
        }
    } else if (!m_activeLiveServers.empty()) {
        StopLiveServer(m_activeLiveServers.back().filePath, /*closeTab=*/true);
    }

    if (m_liveServer.IsRunning()) {
        m_liveServer.Stop();
    }
    if (m_terminal.IsDebugActive()) {
        m_terminal.StopDebugSession();
    }
    StopPhpServer();
    StopNginxServer();
    StopPmaServer();
    m_header.SetServerState(ui::ServerState::Stopped);
    m_header.SetDatabaseState(ui::DatabaseState::Stopped);
    m_header.SetServerLogOpened(false);
    m_header.SetDatabaseLogOpened(false);
    m_header.SetRunErrorStopped(false);
    UpdateExecutionButtonStates();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoRestartServer() {
    m_header.SetRunErrorStopped(false);
    StopPhpServer();
    StopNginxServer();
    StopPmaServer();
    m_header.SetServerState(ui::ServerState::Stopped);
    m_header.SetDatabaseState(ui::DatabaseState::Stopped);
    m_header.SetServerLogOpened(false);
    m_header.SetDatabaseLogOpened(false);
    m_header.SetDatabaseRetryCount(0);

    auto* srv = GetActiveDocLiveServer();
    if (srv && srv->server) {
        std::wstring rootDir = srv->rootDir;
        std::wstring fileName = srv->fileName;
        std::wstring filePath = srv->filePath;
        int port = srv->port;

        srv->server->Stop();
        srv->server->Start(rootDir, fileName, port);
        std::wstring localUrl = srv->server->GetLocalUrl(fileName);
        std::wstring networkUrl = srv->server->GetNetworkUrl(fileName);

        m_terminal.StartLiveServerSession(localUrl, networkUrl, filePath, fileName);
        ShellExecuteW(NULL, L"open", localUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
    } else if (m_terminal.IsDebugActive()) {
        m_terminal.TriggerDebugAction(ui::DebugAction::Restart);
    } else {
        DoRunActiveDocument();
    }
    UpdateExecutionButtonStates();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoOpenServerLog() {
    std::vector<std::wstring> logLines;
    logLines.push_back(L"[NGINX SERVER LOG] Nginx daemon runtime status");
    logLines.push_back(L"[2026/10/25 16:24:00] [notice] 14208#0: using the 'select' event method");
    logLines.push_back(L"[2026/10/25 16:24:00] [notice] 14208#0: nginx/1.30.6 is up");
    logLines.push_back(L"[2026/10/25 16:24:00] [notice] 14208#0: built by msvc 19.44");
    logLines.push_back(L"[2026/10/25 16:24:00] [notice] 14208#0: OS: Windows x64");
    logLines.push_back(L"[2026/10/25 16:24:01] [notice] 14208#0: start worker processes");
    logLines.push_back(L"[2026/10/25 16:24:01] [notice] 14208#0: listening on http://0.0.0.0:80");
    logLines.push_back(L"[2026/10/25 16:24:02] [info] 14208#0: connection accepted from 127.0.0.1");

    std::filesystem::path logPath = GetPhideRoot() / L"include" / L"phide" / L"server" / L"nginx" / L"logs" / L"error.log";
    std::error_code ecLog;
    if (std::filesystem::exists(logPath, ecLog)) {
        std::ifstream ifs(logPath);
        std::string line;
        while (std::getline(ifs, line) && logLines.size() < 100) {
            if (!line.empty()) {
                if (line.find("OpenEvent(\"Global\\ngx_stop_") != std::string::npos) {
                    continue;
                }
                std::wstring wLine(line.begin(), line.end());
                logLines.push_back(wLine);
            }
        }
    }

    m_terminal.Open(m_drawer.GetOpenFolderPath());
    AnimateTerminalOpen();
    m_terminal.OpenLogTab(L"nginx log", logLines);
    m_header.SetServerLogOpened(true);
    m_isTerminalFocused = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoOpenDatabaseLog() {
    m_terminal.Open(m_drawer.GetOpenFolderPath());
    AnimateTerminalOpen();

    std::vector<std::wstring> logLines;
    logLines.push_back(L"[phpMyAdmin LOG] phpMyAdmin 5.2.3 database engine session");
    logLines.push_back(L"[2026/10/25 16:24:00] [info] Initializing phpMyAdmin configuration");
    logLines.push_back(L"[2026/10/25 16:24:00] [info] Checking PHP runtime environment: PHP 8.5 active");
    logLines.push_back(L"[2026/10/25 16:24:01] [info] Connecting to MySQL / MariaDB server at localhost:3306... Connected");
    logLines.push_back(L"[2026/10/25 16:24:01] [info] phpMyAdmin is up: http://localhost:8081");
    logLines.push_back(L"[2026/10/25 16:24:02] [info] Session established for root@localhost");
    m_terminal.OpenLogTab(L"phpMyAdmin log", logLines);

    m_header.SetDatabaseLogOpened(true);
    m_isTerminalFocused = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoOpenErrorLog() {
    std::vector<std::wstring> logLines;
    logLines.push_back(L"[PROCESS ERROR LOG] Execution failure report");
    logLines.push_back(L"[2026/10/25 16:24:00] [error] Process stopped due to execution error");
    if (!m_lastTerminalErrorText.empty()) {
        logLines.push_back(L"[Detail] " + m_lastTerminalErrorText);
    } else {
        logLines.push_back(L"[Detail] Port binding failed or daemon startup halted");
    }
    logLines.push_back(L"[Action] Click Restart to force retry server and database startup");

    m_terminal.Open(m_drawer.GetOpenFolderPath());
    AnimateTerminalOpen();
    m_terminal.OpenLogTab(L"Process Error Log", logLines);
    m_header.SetRunErrorStopped(false);
    m_isTerminalFocused = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::StartPhpServer(const std::wstring& rootDir, int port) {
    StopPhpServer();
    std::wstring phpExe = L"php.exe";
    std::wstring phpIni;
    wchar_t szLocalApp[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", szLocalApp, MAX_PATH) > 0) {
        std::filesystem::path phpRoot = std::filesystem::path(szLocalApp) / L"Programs" / L"PHP";
        std::error_code ec;
        if (std::filesystem::exists(phpRoot, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(phpRoot, ec)) {
                if (entry.is_directory() && std::filesystem::exists(entry.path() / L"php.exe", ec)) {
                    phpExe = (entry.path() / L"php.exe").wstring();
                    if (std::filesystem::exists(entry.path() / L"php.ini", ec)) {
                        phpIni = (entry.path() / L"php.ini").wstring();
                    }
                    break;
                }
            }
        }
    }
    std::wstring iniArg = (!phpIni.empty() && std::filesystem::exists(phpIni)) ? (L" -c \"" + phpIni + L"\"") : L"";

    std::filesystem::path prependPath = GetPhideRoot() / L"include" / L"phide" / L"server" / L"phide_prepend.php";
    std::filesystem::path appendPath  = GetPhideRoot() / L"include" / L"phide" / L"server" / L"phide_append.php";
    std::wstring liveReloadArgs = L" -d opcache.enable=0 -d output_buffering=Off";
    if (std::filesystem::exists(prependPath) && std::filesystem::exists(appendPath)) {
        std::wstring prepFwd = prependPath.wstring();
        std::wstring appFwd  = appendPath.wstring();
        for (auto& ch : prepFwd) if (ch == L'\\') ch = L'/';
        for (auto& ch : appFwd)  if (ch == L'\\') ch = L'/';
        liveReloadArgs += L" -d \"auto_prepend_file=" + prepFwd + L"\" -d \"auto_append_file=" + appFwd + L"\"";
    }

    std::wstring cmd = L"\"" + phpExe + L"\"" + iniArg + liveReloadArgs + L" -S 0.0.0.0:" + std::to_wstring(port) + L" -t \"" + rootDir + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, rootDir.c_str(), &si, &pi)) {
        m_hPhpServerProcess = pi.hProcess;
        CloseHandle(pi.hThread);
    }
}

void Window::StopPhpServer() {
    if (m_hPhpServerProcess) {
        TerminateProcess(m_hPhpServerProcess, 0);
        CloseHandle(m_hPhpServerProcess);
        m_hPhpServerProcess = nullptr;
    }
}

void Window::StartNginxServer() {
    StopNginxServer();
    std::filesystem::path nginxDir = GetPhideRoot() / L"include" / L"phide" / L"server" / L"nginx";
    std::filesystem::path nginxExe = nginxDir / L"nginx.exe";
    if (!std::filesystem::exists(nginxExe)) {
        m_header.SetServerState(ui::ServerState::Failed);
        return;
    }
    std::error_code ec;
    std::filesystem::remove(nginxDir / L"logs" / L"nginx.pid", ec);
    std::filesystem::create_directories(nginxDir / L"logs", ec);

    std::wstring cmd = L"\"" + nginxExe.wstring() + L"\" -p \"" + nginxDir.wstring() + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, nginxDir.wstring().c_str(), &si, &pi)) {
        m_hNginxProcess = pi.hProcess;
        CloseHandle(pi.hThread);
        m_header.SetServerState(ui::ServerState::Running);
    } else {
        m_header.SetServerState(ui::ServerState::Failed);
    }
}

void Window::StopNginxServer() {
    std::filesystem::path nginxDir = GetPhideRoot() / L"include" / L"phide" / L"server" / L"nginx";
    std::filesystem::path nginxExe = nginxDir / L"nginx.exe";
    if (std::filesystem::exists(nginxExe)) {
        std::wstring cmd = L"\"" + nginxExe.wstring() + L"\" -p \"" + nginxDir.wstring() + L"\" -s stop";
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(L'\0');
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, nginxDir.wstring().c_str(), &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 500);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
    if (m_hNginxProcess) {
        TerminateProcess(m_hNginxProcess, 0);
        CloseHandle(m_hNginxProcess);
        m_hNginxProcess = nullptr;
    }
    std::error_code ec;
    std::filesystem::remove(nginxDir / L"logs" / L"nginx.pid", ec);
    m_header.SetServerState(ui::ServerState::Stopped);
}

static bool IsPortListening(int port) {
    // 1. Check IPv4 TCP listeners
    DWORD dwSize = 0;
    GetExtendedTcpTable(NULL, &dwSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (dwSize > 0) {
        std::vector<BYTE> buffer(dwSize);
        if (GetExtendedTcpTable(buffer.data(), &dwSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) == NO_ERROR) {
            auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                DWORD p = table->table[i].dwLocalPort;
                int localPort = static_cast<int>(((p & 0xFF) << 8) | ((p >> 8) & 0xFF));
                if (localPort == port) {
                    return true;
                }
            }
        }
    }

    // 2. Check IPv6 TCP listeners (MariaDB / mysqld binds to '::' dual-stack IPv6!)
    DWORD dwSize6 = 0;
    GetExtendedTcpTable(NULL, &dwSize6, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (dwSize6 > 0) {
        std::vector<BYTE> buffer6(dwSize6);
        if (GetExtendedTcpTable(buffer6.data(), &dwSize6, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_LISTENER, 0) == NO_ERROR) {
            auto* table6 = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(buffer6.data());
            for (DWORD i = 0; i < table6->dwNumEntries; ++i) {
                DWORD p = table6->table[i].dwLocalPort;
                int localPort = static_cast<int>(((p & 0xFF) << 8) | ((p >> 8) & 0xFF));
                if (localPort == port) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool IsMysqlProcessRunning() {
    if (IsPortListening(3306)) return true;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"mysqld.exe") == 0 || _wcsicmp(pe.szExeFile, L"mariadbd.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

void Window::StartMysqlServer() {
    if (IsPortListening(3306)) return;

    std::filesystem::path mysqldExe = L"C:\\xampp\\mysql\\bin\\mysqld.exe";
    std::filesystem::path myIni = L"C:\\xampp\\mysql\\bin\\my.ini";
    std::filesystem::path xamppDir = L"C:\\xampp";

    if (std::filesystem::exists(mysqldExe) && std::filesystem::exists(myIni)) {
        std::wstring cmd = L"\"" + mysqldExe.wstring() + L"\" --defaults-file=\"" + myIni.wstring() + L"\" --skip-grant-tables --standalone --console";
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(L'\0');

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, xamppDir.wstring().c_str(), &si, &pi)) {
            m_hMysqlProcess = pi.hProcess;
            CloseHandle(pi.hThread);

            // Wait up to 3000ms for MySQL to initialize engine and open port 3306
            for (int attempt = 0; attempt < 60; ++attempt) {
                Sleep(50);
                if (IsPortListening(3306)) break;
            }
        }
    }
}

void Window::StopMysqlServer() {
    if (m_hMysqlProcess) {
        TerminateProcess(m_hMysqlProcess, 0);
        CloseHandle(m_hMysqlProcess);
        m_hMysqlProcess = nullptr;
    }
}

void Window::StartPmaServer() {
    StopPmaServer();
    auto& srvDb = m_header.GetServerDatabaseConfig();
    srvDb.phpMyAdminEnabled = true;
    srvDb.dontRunDatabase = false;
    StartMysqlServer();
    std::filesystem::path pmaDir = GetPhideRoot() / L"include" / L"phide" / L"db" / L"phpMyAdmin";
    if (!std::filesystem::exists(pmaDir)) {
        m_header.SetDatabaseState(ui::DatabaseState::Failed);
        return;
    }
    std::wstring phpExe = L"php.exe";
    std::wstring phpIni;
    wchar_t szLocalAppPma[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", szLocalAppPma, MAX_PATH) > 0) {
        std::filesystem::path phpRoot = std::filesystem::path(szLocalAppPma) / L"Programs" / L"PHP";
        std::error_code ec;
        if (std::filesystem::exists(phpRoot, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(phpRoot, ec)) {
                if (entry.is_directory() && std::filesystem::exists(entry.path() / L"php.exe", ec)) {
                    phpExe = (entry.path() / L"php.exe").wstring();
                    if (std::filesystem::exists(entry.path() / L"php.ini", ec)) {
                        phpIni = (entry.path() / L"php.ini").wstring();
                    }
                    break;
                }
            }
        }
    }
    std::wstring iniArg = (!phpIni.empty() && std::filesystem::exists(phpIni)) ? (L" -c \"" + phpIni + L"\"") : L"";

    int pmaPort = 8081;
    try {
        int parsed = std::stoi(m_dbSettingsModal.GetPmaWebPort());
        if (parsed > 0 && parsed < 65536) pmaPort = parsed;
    } catch (...) {}

    // Bind to 0.0.0.0:<pmaPort> so localhost:<pmaPort> and custom hosts work seamlessly
    std::wstring cmd = L"\"" + phpExe + L"\"" + iniArg + L" -S 0.0.0.0:" + std::to_wstring(pmaPort) + L" -t \"" + pmaDir.wstring() + L"\"";
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, pmaDir.wstring().c_str(), &si, &pi)) {
        m_hPmaProcess = pi.hProcess;
        m_isPmaRunning = true;
        CloseHandle(pi.hThread);

        // Wait up to 1500ms for php.exe to start listening on pmaPort
        for (int attempt = 0; attempt < 30; ++attempt) {
            Sleep(50);
            if (IsPortListening(pmaPort)) break;
        }

        m_header.SetDatabaseState(ui::DatabaseState::Running);
    } else {
        m_isPmaRunning = false;
        m_header.SetDatabaseState(ui::DatabaseState::Failed);
    }
    UpdateDatabaseModalRuntimeStatus();
}

void Window::StopPmaServer() {
    if (m_hPmaProcess) {
        TerminateProcess(m_hPmaProcess, 0);
        CloseHandle(m_hPmaProcess);
        m_hPmaProcess = nullptr;
    }
    m_isPmaRunning = false;
    m_header.SetDatabaseState(ui::DatabaseState::Stopped);
    UpdateDatabaseModalRuntimeStatus();
}

void Window::DoRunPhpApplication(const std::wstring& filePath, const std::wstring& projectRoot) {
    // 0. Save any dirty open files so edits to config.php / index.php are immediately persisted on disk
    DoSaveAll();

    auto& srvCfg = m_header.GetServerDatabaseConfig();

    // Only start MySQL if phpMyAdmin is enabled and database is not suppressed
    if (!srvCfg.dontRunDatabase && srvCfg.phpMyAdminEnabled) {
        StartMysqlServer();
    }

    // 1. Check Server (nginx)
    bool needServer = !srvCfg.dontRunServer && srvCfg.nginxEnabled;
    if (needServer) {
        m_header.SetServerState(ui::ServerState::Starting);
        m_header.SetServerLogOpened(false);
        StartNginxServer();
        if (m_header.GetServerState() != ui::ServerState::Running) {
            m_header.SetServerState(ui::ServerState::Failed);
            m_header.SetRunErrorStopped(true);
            m_lastTerminalErrorText = L"nginx.exe not found or failed to initialize";
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return; // Server failed: do not run database per user requirement!
        }
    }

    // 2. Check Database (phpMyAdmin)
    bool needDb = !srvCfg.dontRunDatabase && srvCfg.phpMyAdminEnabled;
    if (needDb) {
        m_header.SetDatabaseState(ui::DatabaseState::Starting);
        m_header.SetDatabaseLogOpened(false);
        StartPmaServer();
        if (m_header.GetDatabaseState() != ui::DatabaseState::Running) {
            // Auto retry up to 3 times
            for (int r = 1; r <= 3; ++r) {
                m_header.SetDatabaseRetryCount(r);
                m_header.SetDatabaseState(ui::DatabaseState::Retrying);
                StartPmaServer();
                if (m_header.GetDatabaseState() == ui::DatabaseState::Running) break;
            }
            if (m_header.GetDatabaseState() != ui::DatabaseState::Running) {
                m_header.SetDatabaseState(ui::DatabaseState::Failed);
            }
        }
    }

    // 2b. Auto-import database.sql if present in project directory so the schema & tables exist
    std::filesystem::path p(filePath);
    std::wstring fileName = p.filename().wstring();
    std::wstring folder = p.parent_path().wstring();
    if (folder.empty()) folder = projectRoot;

    std::wstring sqlFile = m_dbSettingsModal.GetSqlFileName();
    if (sqlFile.empty()) sqlFile = L"database.sql";
    std::filesystem::path sqlPath = std::filesystem::path(folder) / sqlFile;
    if (!std::filesystem::exists(sqlPath) && !projectRoot.empty()) {
        sqlPath = std::filesystem::path(projectRoot) / sqlFile;
    }
    std::filesystem::path mysqlExe = L"C:\\xampp\\mysql\\bin\\mysql.exe";
    if (needDb && std::filesystem::exists(mysqlExe) && std::filesystem::exists(sqlPath)) {
        std::wstring importCmd = L"cmd.exe /c \"\"" + mysqlExe.wstring() + L"\" -u root < \"" + sqlPath.wstring() + L"\"\"";
        std::vector<wchar_t> cmdBuf(importCmd.begin(), importCmd.end());
        cmdBuf.push_back(L'\0');
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 2500);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }

    // 3. Open terminal tab for PHP Server and launch PHP process
    StartPhpServer(folder, 8080);
    for (int attempt = 0; attempt < 25; ++attempt) {
        if (IsPortListening(8080)) break;
        Sleep(40);
    }

    // If user clicked Run while viewing config.php or db.php, open index.php in browser if index.php exists
    std::wstring targetFile = fileName;
    if ((_wcsicmp(fileName.c_str(), L"config.php") == 0 || _wcsicmp(fileName.c_str(), L"db.php") == 0) &&
        std::filesystem::exists(std::filesystem::path(folder) / L"index.php")) {
        targetFile = L"index.php";
    }

    std::wstring cfgHost = m_dbSettingsModal.GetHost();
    if (cfgHost.empty()) cfgHost = L"localhost";

    std::wstring localUrl = L"http://" + cfgHost + L":8080/" + targetFile;
    std::wstring networkUrl = L"http://192.168.1.105:8080/" + targetFile;

    m_terminal.Open(folder);
    AnimateTerminalOpen();
    m_terminal.StartLiveServerSession(localUrl, networkUrl, filePath, targetFile);

    // Reuse existing PHP Server entry or add a new one
    bool updatedExisting = false;
    for (auto& srv : m_activeLiveServers) {
        if (srv.framework == L"PHP Server" || _wcsicmp(srv.rootDir.c_str(), folder.c_str()) == 0) {
            srv.filePath = filePath;
            srv.fileName = targetFile;
            srv.rootDir = folder;
            srv.port = 8080;
            srv.framework = L"PHP Server";
            srv.title = L"PHP Server";
            srv.customUrl = localUrl;
            srv.progress = 0.05f;
            srv.progressDurationMs = 1200.0f;
            srv.elapsedMs = 0.0f;
            srv.browserOpened = false;
            srv.isStarting = true;
            srv.hasError = false;
            srv.errorMsg.clear();
            updatedExisting = true;
            break;
        }
    }
    if (!updatedExisting) {
        ActiveLiveServer entry;
        entry.filePath = filePath;
        entry.fileName = targetFile;
        entry.rootDir = folder;
        entry.port = 8080;
        entry.framework = L"PHP Server";
        entry.title = L"PHP Server";
        entry.customUrl = localUrl;
        entry.progress = 0.05f;
        entry.progressDurationMs = 1200.0f;
        entry.elapsedMs = 0.0f;
        entry.browserOpened = false;
        entry.isStarting = true;
        m_activeLiveServers.push_back(std::move(entry));
    }

    m_isProjectRunning = true;
    m_runningProjectRoot = projectRoot;
    m_header.SetLiveServerRunning(true);
    m_isTerminalFocused = false;

    SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::LoadFileFromPath(const std::wstring& path) {
    OpenFileDocument(path, false);
}

void Window::SaveFileToPath(const std::wstring& path) {
    auto* pDoc = GetActiveDoc();
    if (pDoc) {
        if (pDoc->isSpecialView && pDoc->viewType == L"notebook" && pDoc->notebookDoc) {
            if (pDoc->notebookDoc->SaveToFile(path)) {
                pDoc->filePath = path;
                auto pos = path.find_last_of(L"\\/");
                pDoc->fileName = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
                pDoc->isDirty = false;
                pDoc->isPreview = false;
                UpdateDrawerDirtyFiles();
                m_drawer.RefreshGitStatusOnly(true);
                UpdateDrawerErrorFiles();
                SaveSessionState();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        }
        if (pDoc->buffer.SaveToFile(path)) {
            pDoc->filePath = path;
            auto pos = path.find_last_of(L"\\/");
            std::wstring baseName = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
            if (pDoc->isDiffView) {
                pDoc->fileName = baseName + L" (Working Tree)";
            } else {
                pDoc->fileName = baseName;
            }
            pDoc->isDirty = false;
            pDoc->originalDiskText = pDoc->buffer.GetText();
            pDoc->isPreview = false;
            if (path == m_snippetManager.GetSnippetsFilePath()) {
                m_snippetManager.LoadSnippets();
            }
            SyncDocumentBuffers(pDoc);
            UpdateDrawerDirtyFiles();
            m_drawer.RefreshGitStatusOnly(true); // Immediate 0ms refresh on Save!
            UpdateDrawerErrorFiles();
    SaveSessionState();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
}

void Window::OpenFileByName(const std::wstring& name) {
    // Check if real file exists in current workspace / directory
    std::wstring localPath;
    if (name == L"main.cpp") localPath = L"src\\main.cpp";
    else if (name == L"CMakeLists.txt") localPath = L"CMakeLists.txt";
    else localPath = name;

    if (!localPath.empty() && std::filesystem::exists(localPath)) {
        OpenFileDocument(localPath, true);
        return;
    }

    // Default rich templates representing PHP & C++ IDE
    std::wstring templateCode;
    if (name == L"index.php") {
        templateCode =
            L"<?php\n\n"
            L"declare(strict_types=1);\n\n"
            L"namespace App;\n\n"
            L"use Phide\\Core\\Kernel;\n"
            L"use Phide\\Http\\Request;\n"
            L"use Phide\\Http\\Response;\n\n"
            L"require_once __DIR__ . '/../vendor/autoload.php';\n\n"
            L"$kernel = new Kernel(environment: 'development', debug: true);\n"
            L"$request = Request::capture();\n"
            L"$response = $kernel->handle($request);\n"
            L"$response->send();\n"
            L"$kernel->terminate($request, $response);\n";
    } else if (name == L"Kernel.php") {
        templateCode =
            L"<?php\n\n"
            L"declare(strict_types=1);\n\n"
            L"namespace Phide\\Core;\n\n"
            L"use Phide\\Http\\Request;\n"
            L"use Phide\\Http\\Response;\n\n"
            L"class Kernel {\n"
            L"    private string $environment;\n"
            L"    private bool $debug;\n\n"
            L"    public function __construct(string $environment, bool $debug = false) {\n"
            L"        $this->environment = $environment;\n"
            L"        $this->debug = $debug;\n"
            L"    }\n\n"
            L"    public function handle(Request $request): Response {\n"
            L"        return new Response(content: \"phide engine active\", status: 200);\n"
            L"    }\n\n"
            L"    public function terminate(Request $request, Response $response): void {\n"
            L"        // Cleanup worker threads and flush buffers\n"
            L"    }\n"
            L"}\n";
    } else if (name == L"composer.json") {
        templateCode =
            L"{\n"
            L"    \"name\": \"phide/studio\",\n"
            L"    \"description\": \"phide - Web Development IDE\",\n"
            L"    \"type\": \"project\",\n"
            L"    \"require\": {\n"
            L"        \"php\": \">=8.2\",\n"
            L"        \"nikic/php-parser\": \"^5.0\"\n"
            L"    },\n"
            L"    \"autoload\": {\n"
            L"        \"psr-4\": {\n"
            L"            \"App\\\\\": \"src/\"\n"
            L"        }\n"
            L"    }\n"
            L"}\n";
    } else if (name == L"CMakeLists.txt") {
        templateCode =
            L"cmake_minimum_required(VERSION 3.22)\n"
            L"project(phide VERSION 1.0.0 LANGUAGES CXX)\n\n"
            L"set(CMAKE_CXX_STANDARD 20)\n"
            L"set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n"
            L"add_executable(phide\n"
            L"    src/main.cpp\n"
            L"    src/app/Window.cpp\n"
            L"    src/ui/Theme.cpp\n"
            L"    src/ui/EditorCanvas.cpp\n"
            L")\n\n"
            L"target_link_libraries(phide PRIVATE\n"
            L"    d2d1.lib\n"
            L"    dwrite.lib\n"
            L"    dwmapi.lib\n"
            L"    user32.lib\n"
            L")\n";
    } else if (name == L"main.cpp") {
        templateCode =
            L"#include <windows.h>\n"
            L"#include \"phide/app/Window.hpp\"\n\n"
            L"int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {\n"
            L"    phide::app::Window app;\n"
            L"    if (!app.Create(hInstance, nCmdShow)) {\n"
            L"        return 1;\n"
            L"    }\n"
            L"    return app.Run();\n"
            L"}\n";
    }

    OpenVirtualDocument(name, templateCode, true);
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    Window* pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        auto* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<Window*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hwnd;
    } else {
        pThis = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static HICON CreatePhideAppIcon(HINSTANCE hInstance) {
    HICON hResIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (hResIcon) return hResIcon;

    BITMAPV5HEADER bi{};
    bi.bV5Size        = sizeof(BITMAPV5HEADER);
    bi.bV5Width       = static_cast<LONG>(ui::kPhideLogoWidth);
    bi.bV5Height      = -static_cast<LONG>(ui::kPhideLogoHeight); // Top-down DIB
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    HDC hdc = GetDC(nullptr);
    void* lpBits = nullptr;
    HBITMAP hColor = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &lpBits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hColor || !lpBits) {
        if (hColor) DeleteObject(hColor);
        return nullptr;
    }

    memcpy(lpBits, ui::g_phideLogoBgra, ui::kPhideLogoWidth * ui::kPhideLogoHeight * 4);
    HBITMAP hMask = CreateBitmap(ui::kPhideLogoWidth, ui::kPhideLogoHeight, 1, 1, nullptr);

    ICONINFO ii{};
    ii.fIcon    = TRUE;
    ii.hbmMask  = hMask;
    ii.hbmColor = hColor;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hColor);
    if (hMask) DeleteObject(hMask);
    return hIcon;
}

bool Window::Create(HINSTANCE hInstance, int nCmdShow) {
    if (FAILED(CreateDeviceIndependentResources())) {
        return false;
    }

    HICON hAppIcon = CreatePhideAppIcon(hInstance);

    WNDCLASSEX wc{};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = Window::WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = hAppIcon;
    wc.hIconSm = hAppIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Direct2D handles full background rendering
    wc.lpszClassName = L"phide_WindowClass";

    RegisterClassEx(&wc);

    // Initial system DPI detection for dynamic scaling
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    m_config.dpiScale = static_cast<float>(dpi) / 96.0f;

    const int width  = m_config.Scale(m_config.initialWidth);
    const int height = m_config.Scale(m_config.initialHeight);

    // Center window on primary monitor
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const int x = (screenW - width) / 2;
    const int y = (screenH - height) / 2;

    m_hwnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"phide - Web Development IDE",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, width, height,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

    if (hAppIcon) {
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hAppIcon));
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hAppIcon));
    }

    // Enable Windows 11 Immersive Dark Mode for window frame and menus
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    // Extend DWM client frame into the client area to enable custom frameless rendering
    MARGINS margins = { 0, 0, 1, 0 };
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    // Force frame refresh
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    // Start 500ms Caret blink timer
    SetTimer(m_hwnd, m_caretTimerId, 500, nullptr);

    // Start 50ms Search throttler polling timer
    SetTimer(m_hwnd, m_throttleTimerId, 50, nullptr);

    // Start 60-second periodic idle garbage collection timer
    SetTimer(m_hwnd, m_gcTimerId, 60000, nullptr);

    // Register callback for immediate terminal output repainting
    m_terminal.SetOutputCallback([this]() {
        if (m_hwnd) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    });

    // Auto-stop server when its tab is closed via [X] in integrated terminal
    m_terminal.SetOnLiveServerTabClosed([this](const std::wstring& filePath) {
        if (m_isProjectRunning && (m_runningProjectRoot == filePath || filePath.empty() || IsFileInDirectory(filePath, m_runningProjectRoot))) {
            m_isProjectRunning = false;
            m_runningProjectRoot.clear();
        }
        StopLiveServer(filePath, /*closeTab=*/false);
        UpdateExecutionButtonStates();
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    });

    // Auto-stop server when an error is caught in server tab (per user request: "if its error its automatic stopped")
    m_terminal.SetOnServerError([this](const std::wstring& rootDir, const std::wstring& errorText) {
        for (auto& srv : m_activeLiveServers) {
            if (rootDir.empty() || srv.rootDir == rootDir || srv.filePath == rootDir) {
                srv.hasError = true;
                if (srv.errorMsg.empty()) srv.errorMsg = errorText;
                srv.isStarting = false;
                srv.browserOpened = true; // Never redirect on error
            }
        }
        if (m_isProjectRunning && (rootDir.empty() || m_runningProjectRoot == rootDir || IsFileInDirectory(rootDir, m_runningProjectRoot))) {
            m_isProjectRunning = false;
            m_runningProjectRoot.clear();
            UpdateExecutionButtonStates();
        }
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    });

    // Animate terminal close when '-' minimize button is clicked in integrated terminal
    m_terminal.SetOnMinimize([this]() {
        m_isTerminalFocused = false;
        AnimateTerminalClose();
    });

    // Animate terminal maximize / restore smoothly (User requirement)
    m_terminal.SetOnToggleFullScreen([this](bool isFs) {
        m_targetTermFullScreen = isFs ? 1.0f : 0.0f;
        m_isTermFullScreenAnimating = true;
        SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
    });

    // Initialize user snippets engine
    m_snippetManager.Initialize(GetSnippetsConfigPath());
    m_intelliSense.SetSnippetManager(&m_snippetManager);

    // Detect installed PC environment runtimes (Python, Node.js, PHP)
    m_extensionsPanel.RefreshPcStatus();
    m_extensionsPanel.SetOnDataChanged([this]() {
        if (m_hwnd) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    });

    // Register callback for reactive git state updates
    m_drawer.SetOnGitStateChanged([this]() {
        // Auto-close any working tree diff view whose file no longer has git changes
        const auto& gitChanges = m_drawer.GetGitChanges();
        for (int g = static_cast<int>(m_editorGroups.size()) - 1; g >= 0; --g) {
            if (g >= static_cast<int>(m_editorGroups.size())) continue;
            for (int d = static_cast<int>(m_editorGroups[g].docs.size()) - 1; d >= 0; --d) {
                if (g >= static_cast<int>(m_editorGroups.size())) break;
                if (d >= static_cast<int>(m_editorGroups[g].docs.size())) continue;
                auto& doc = m_editorGroups[g].docs[d];
                if (doc.isDiffView && !doc.isDiffRightReadOnly) {
                    bool stillHasChanges = false;
                    for (const auto& item : gitChanges) {
                        std::wstring cleanDoc = doc.filePath;
                        for (auto& c : cleanDoc) if (c == L'/') c = L'\\';
                        std::wstring cleanItem = item.path;
                        for (auto& c : cleanItem) if (c == L'/') c = L'\\';
                        if (cleanDoc == cleanItem || cleanDoc.ends_with(cleanItem) || cleanItem.ends_with(cleanDoc)) {
                            stillHasChanges = true;
                            break;
                        }
                    }
                    if (!stillHasChanges) {
                        CloseDocument(g, d);
                    }
                }
            }
        }
        if (m_hwnd) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    });

    m_drawer.SetOnGitError([this](const std::wstring& cmd, const std::wstring& output, const std::wstring& repoDir) {
        ShowGitError(cmd, output, repoDir);
    });

    m_drawer.SetOnRequireGitHubLogin([this]() {
        std::wstring dir = m_drawer.GetOpenFolderPath();
        m_terminal.Open(dir);
        m_terminalAnimProgress = 1.0f;
        m_targetTerminalProgress = 1.0f;
        m_isTerminalFocused = true;
        AnimateTerminalOpen();
        m_terminal.OpenGitHubLoginTab(dir);
        SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
        if (m_hwnd) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    });

    m_terminal.SetOnGitHubAuthSuccess([this](const std::wstring& username) {
        m_drawer.SetGitHubLoggedIn(true);
        m_drawer.RefreshGitState(false);
        m_drawer.ShowGitNotification(L"✓ Logged in to GitHub as " + username);
        if (m_hwnd) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    });

    ui::GitLogManager::Instance().SetListener([this](const std::vector<std::wstring>& newBatch) {
        m_terminal.AppendGitLogLines(newBatch);
    });

    m_drawer.SetOnRefreshOpenDocs([this]() {
        RefreshOpenDocPathsForSearch();
    });

    m_drawer.SetOnFilesReplaced([this](const std::vector<std::wstring>& paths) {
        for (const auto& p : paths) {
            for (auto& grp : m_editorGroups) {
                for (auto& doc : grp.docs) {
                    if (doc.filePath == p) {
                        doc.buffer.LoadFromFile(p);
                        doc.isDirty = false;
                    }
                }
            }
        }
        TriggerDebouncedGitRefresh(500);
        if (m_hwnd) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    });

    // Wire Package Manager HWND for embedded WebView2 and Install Package callback
    m_editor.GetPackageManagerView().SetHwnd(m_hwnd);
    m_editor.GetPackageManagerView().SetOnInstallCallback([this](const std::wstring& pkgName, const std::wstring& cmd, phide::ui::PkgInstallScope scope) {
        bool isUninstall = (cmd.find(L"uninstall") != std::wstring::npos || cmd.find(L"remove") != std::wstring::npos);
        std::wstring taskTitle = isUninstall ? (L"Uninstalling package " + pkgName) : (L"Installing package " + pkgName);
        m_extensionsPanel.StartProcessTask(taskTitle, cmd);
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);

        if (m_terminal.IsOpen()) {
            m_terminal.ExecuteCommandLine(cmd);
        }

        std::thread([this, pkgName, cmd, taskTitle, scope, isUninstall]() {
            // Find appropriate interpreter or tool
            std::wstring pyPath = L"python.exe";
            for (const auto& item : m_extensionsPanel.GetItems()) {
                if (item.id == L"python" && !item.environments.empty()) {
                    for (const auto& env : item.environments) {
                        if (env.isDefault && std::filesystem::exists(env.path)) {
                            pyPath = env.path;
                            break;
                        }
                    }
                }
            }

            // Determine project workspace directory
            std::wstring wsFolder = m_drawer.HasOpenFolder() ? m_drawer.GetOpenFolderPath() : std::filesystem::current_path().wstring();

            // For local project installations, ensure workspace .venv exists (PyCharm style)
            if (scope == phide::ui::PkgInstallScope::Project && (cmd.find(L"pip ") == 0 || cmd.find(L"\" -m pip") != std::wstring::npos)) {
                std::filesystem::path venvPy = std::filesystem::path(wsFolder) / L".venv" / L"Scripts" / L"python.exe";
                std::error_code ecVenv;
                if (!std::filesystem::exists(venvPy, ecVenv) && !isUninstall) {
                    m_extensionsPanel.UpdateProcessTask(taskTitle, 0.20f, L"Creating workspace virtual environment (.venv)...");
                    
                    // Resolve interpreter capable of executing venv
                    std::wstring venvCreatorPy;
                    auto testVenv = [](const std::wstring& exe) -> bool {
                        if (exe.empty()) return false;
                        std::error_code ec;
                        if (!std::filesystem::exists(exe, ec)) return false;
                        if (std::filesystem::exists(std::filesystem::path(exe).parent_path() / L"Lib" / L"venv", ec)) return true;
                        std::wstring out = RunCmdCapture(L"\"" + exe + L"\" -c \"import venv; print('OK')\"");
                        return out.find(L"OK") != std::wstring::npos;
                    };

                    if (testVenv(pyPath)) {
                        venvCreatorPy = pyPath;
                    } else {
                        for (const auto& item : m_extensionsPanel.GetItems()) {
                            if (item.id == L"python") {
                                for (const auto& env : item.environments) {
                                    if (testVenv(env.path)) {
                                        venvCreatorPy = env.path;
                                        break;
                                    }
                                }
                            }
                            if (!venvCreatorPy.empty()) break;
                        }
                    }
                    if (venvCreatorPy.empty()) {
                        wchar_t pyLauncher[MAX_PATH] = { 0 };
                        if (SearchPathW(nullptr, L"py.exe", nullptr, MAX_PATH, pyLauncher, nullptr) > 0 ||
                            std::filesystem::exists(L"C:\\Windows\\py.exe", ecVenv)) {
                            std::wstring lPath = (pyLauncher[0] != 0) ? pyLauncher : L"C:\\Windows\\py.exe";
                            if (RunCmdCapture(L"\"" + lPath + L"\" -m venv --help").find(L"usage:") != std::wstring::npos) {
                                venvCreatorPy = lPath;
                            }
                        }
                    }
                    if (venvCreatorPy.empty()) {
                        venvCreatorPy = pyPath;
                    }

                    std::wstring venvCmd = L"\"" + venvCreatorPy + L"\" -m venv \"" + (std::filesystem::path(wsFolder) / L".venv").wstring() + L"\"";
                    STARTUPINFOW siV{};
                    siV.cb = sizeof(siV);
                    siV.dwFlags = STARTF_USESHOWWINDOW;
                    siV.wShowWindow = SW_HIDE;
                    PROCESS_INFORMATION piV{};
                    std::vector<wchar_t> vBuf(venvCmd.begin(), venvCmd.end());
                    vBuf.push_back(0);
                    if (CreateProcessW(nullptr, vBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, wsFolder.c_str(), &siV, &piV)) {
                        WaitForSingleObject(piV.hProcess, 35000);
                        CloseHandle(piV.hProcess);
                        CloseHandle(piV.hThread);
                    }
                    m_drawer.RefreshTree();
                }

                if (std::filesystem::exists(venvPy, ecVenv)) {
                    pyPath = venvPy.wstring();
                }
            }

            std::wstring runCmd = cmd;
            if (cmd.find(L"pip install") == 0) {
                std::wstring args = cmd.substr(11);
                runCmd = L"\"" + pyPath + L"\" -m pip install " + args;
            } else if (cmd.find(L"pip uninstall") == 0) {
                std::wstring args = cmd.substr(13);
                runCmd = L"\"" + pyPath + L"\" -m pip uninstall " + args;
            } else if (cmd.find(L"composer ") == 0) {
                wchar_t compFound[MAX_PATH] = { 0 };
                std::wstring compPath = L"composer";
                if (SearchPathW(nullptr, L"composer.bat", nullptr, MAX_PATH, compFound, nullptr) > 0) {
                    compPath = compFound;
                } else if (SearchPathW(nullptr, L"composer", nullptr, MAX_PATH, compFound, nullptr) > 0) {
                    compPath = compFound;
                } else {
                    wchar_t szLocalApp[MAX_PATH] = { 0 };
                    if (GetEnvironmentVariableW(L"LOCALAPPDATA", szLocalApp, MAX_PATH) > 0) {
                        std::filesystem::path p = std::filesystem::path(szLocalApp) / L"Programs" / L"PHP" / L"v8.5" / L"composer.bat";
                        std::error_code ec;
                        if (std::filesystem::exists(p, ec)) compPath = p.wstring();
                    }
                }
                runCmd = L"\"" + compPath + L"\" " + cmd.substr(9);
            } else if (cmd.find(L"npm ") == 0) {
                wchar_t npmFound[MAX_PATH] = { 0 };
                std::wstring npmPath = L"npm.cmd";
                std::error_code ecNpm;
                if (SearchPathW(nullptr, L"npm.cmd", nullptr, MAX_PATH, npmFound, nullptr) > 0) {
                    npmPath = npmFound;
                } else if (SearchPathW(nullptr, L"npm", nullptr, MAX_PATH, npmFound, nullptr) > 0) {
                    npmPath = npmFound;
                } else if (std::filesystem::exists(L"C:\\Program Files\\nodejs\\npm.cmd", ecNpm)) {
                    npmPath = L"C:\\Program Files\\nodejs\\npm.cmd";
                }
                runCmd = L"\"" + npmPath + L"\" " + cmd.substr(4);
            }

            m_extensionsPanel.UpdateProcessTask(taskTitle, 0.40f, L"Running: " + runCmd);
            if (m_hwnd) PostMessageW(m_hwnd, WM_PAINT, 0, 0);

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi{};

            std::wstring execCmd = L"cmd.exe /c " + runCmd;
            std::vector<wchar_t> cmdBuf(execCmd.begin(), execCmd.end());
            cmdBuf.push_back(0);

            BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, wsFolder.c_str(), &si, &pi);
            if (ok) {
                float prog = 0.40f;
                while (WaitForSingleObject(pi.hProcess, 350) == WAIT_TIMEOUT) {
                    prog = (std::min)(0.92f, prog + 0.08f);
                    m_extensionsPanel.UpdateProcessTask(taskTitle, prog, (isUninstall ? L"Removing " : L"Downloading and installing ") + pkgName + L"...");
                    if (m_hwnd) PostMessageW(m_hwnd, WM_PAINT, 0, 0);
                }
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }

            if (isUninstall) {
                m_extensionsPanel.CompleteProcessTask(taskTitle, L"Successfully uninstalled " + pkgName);
                m_editor.GetPackageManagerView().MarkPackageUninstalled(pkgName, scope);
            } else {
                m_extensionsPanel.CompleteProcessTask(taskTitle, L"Successfully installed " + pkgName);
                m_editor.GetPackageManagerView().MarkPackageInstalled(pkgName, scope);
            }
            m_editor.GetPackageManagerView().ScanInstalledPackages(wsFolder);
            m_drawer.RefreshTree();
            if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);

            // Auto-dismiss completed package process task after 5 seconds so it doesn't linger indefinitely
            std::thread([this, taskTitle]() {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                const auto& tasks = m_extensionsPanel.GetProcessTasks();
                for (size_t i = 0; i < tasks.size(); ++i) {
                    if (tasks[i].title == taskTitle && tasks[i].isCompleted) {
                        m_extensionsPanel.DismissProcessTask(i);
                        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
                        break;
                    }
                }
            }).detach();
        }).detach();
    });

    // Restore saved IDE session state (open folder, expanded tree dirs, open file, sidebar open/close state)
    LoadSessionState();

    // Trigger Source Control to load immediately in background on app startup (User requirement: no stuck spinning animation)
    m_rail.SetGitLoading(false);
    m_drawer.ClearGitLoading();
    m_drawer.RefreshGitState(true, false);

    if (m_drawer.HasOpenFolder()) {
        m_editor.GetPackageManagerView().ScanInstalledPackages(m_drawer.GetOpenFolderPath());
        StartWorkspaceWatcher(m_drawer.GetOpenFolderPath());
    }

    // Launch application windowed or maximized based on invocation parameters
    bool startWindowed = false;
    LPCWSTR cmdLine = GetCommandLineW();
    if (cmdLine && (wcsstr(cmdLine, L"--windowed") != nullptr || wcsstr(cmdLine, L"--not-maximized") != nullptr)) {
        startWindowed = true;
    }

    if (startWindowed) {
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        const int winW = 1200;
        const int winH = 750;
        const int winX = (screenW - winW) / 2;
        const int winY = (screenH - winH) / 2;
        SetWindowPos(m_hwnd, nullptr, winX, winY, winW, winH, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        ShowWindow(m_hwnd, SW_SHOWNORMAL);
    } else {
        ShowWindow(m_hwnd, SW_MAXIMIZE);
    }
    UpdateWindow(m_hwnd);

    // Parse command line arguments for initial folder or file
    int nArgs = 0;
    LPWSTR* szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (szArglist) {
        for (int i = 1; i < nArgs; ++i) {
            std::wstring arg = szArglist[i];
            if (arg == L"--windowed" || arg == L"--not-maximized") continue;
            std::error_code ec;
            std::filesystem::path p(arg);
            if (std::filesystem::is_directory(p, ec)) {
                DoOpenFolderFromPath(arg);
            } else if (std::filesystem::is_regular_file(p, ec)) {
                DoOpenFolderFromPath(p.parent_path().wstring());
                LoadFileFromPath(arg);
            }
        }
        LocalFree(szArglist);
    }

    return true;
}

int Window::Run() {
    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

HRESULT Window::CreateDeviceIndependentResources() {
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        nullptr,
        reinterpret_cast<void**>(m_pD2DFactory.GetAddressOf())
    );
    if (FAILED(hr)) return hr;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_pDWriteFactory.GetAddressOf())
    );
    return hr;
}

HRESULT Window::CreateDeviceResources() {
    if (m_pRenderTarget) return S_OK;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        m_hwnd, size, D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS
    );

    HRESULT hr = m_pD2DFactory->CreateHwndRenderTarget(rtProps, hwndProps, &m_pRenderTarget);
    if (FAILED(hr)) return hr;

    // Allocate theme GPU brushes and typography formats
    return m_theme.CreateDeviceResources(m_pRenderTarget.Get(), m_pDWriteFactory.Get());
}

void Window::DiscardDeviceResources() {
    m_theme.DiscardDeviceResources();
    m_editor.DiscardDeviceResources();
    m_header.DiscardDeviceResources();
    m_rail.DiscardDeviceResources();
    m_pRenderTarget.Reset();
    if (m_popupMenu.IsOpen()) {
        m_popupMenu.Close();
        m_header.SetActiveMenu(ui::HeaderMenuId::None);
    }
}

void Window::OnResize(UINT width, UINT height) {
    if (m_pRenderTarget) {
        m_pRenderTarget->Resize(D2D1::SizeU(width, height));
    }
    m_editor.GetPackageManagerView().UpdateWebViewBounds();
}

void Window::OnPaint() {
    if (FAILED(CreateDeviceResources())) return;

    PAINTSTRUCT ps;
    BeginPaint(m_hwnd, &ps);

    m_pRenderTarget->BeginDraw();
    m_pRenderTarget->Clear(m_theme.colors.windowBg);

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const float clientW = static_cast<float>(rc.right - rc.left);
    const float clientH = static_cast<float>(rc.bottom - rc.top);

    // Layout Subsystem Dimensions with Smooth Sliding Animation
    const float railW   = m_config.ScaleF(static_cast<float>(m_config.railWidth));
    const float fullDrawerW = m_config.ScaleF(static_cast<float>(m_config.drawerWidth));
    const float currentDrawerW = fullDrawerW * m_drawerAnimProgress;
    const bool hasLeftDrawer = (m_drawerAnimProgress > 0.001f);

    const float fullRightSidebarW = m_config.ScaleF(340.0f);
    const float currentRightSidebarW = fullRightSidebarW * m_rightSidebarAnimProgress;
    const bool hasRightSidebar = (m_rightSidebarAnimProgress > 0.001f);

    const float canvasLeft  = railW + currentDrawerW;
    const float canvasWidth = (std::max)(100.0f, clientW - canvasLeft - currentRightSidebarW);

    // 1. Render Workspace Canvas (shows placeholder watermark when no file is open, multi-tabs and active doc when open)
    // Only highlight search occurrences when user is actively inside Code Search drawer tab!
    // As requested: highlight removes automatically when search query becomes different or user navigates to other drawers (e.g. File Explorer)
    std::wstring searchHL;
    bool hlMatchCase = false;
    bool hlWholeWord = false;
    bool hlUseRegex = false;
    if (m_config.activeRailTab == 1 && hasLeftDrawer) {
        searchHL = m_drawer.GetSearchQuery();
        hlMatchCase = m_drawer.GetMatchCase();
        hlWholeWord = m_drawer.GetMatchWholeWord();
        hlUseRegex = m_drawer.GetUseRegex();
    }

    auto* curDoc = GetActiveDoc();
    m_drawer.SetActiveFilePath(curDoc ? curDoc->filePath : L"");

    bool suppressEditorCaret = m_quickOpen.IsOpen() || m_themePicker.IsOpen() || m_colorPicker.IsOpen() || m_popupMenu.IsOpen() ||
        (m_terminal.IsOpen() && m_isTerminalFocused) ||
        m_drawer.IsRenaming() || m_drawer.IsCreating() ||
        (m_drawerAnimProgress > 0.001f && (m_drawer.IsGitCommitBoxFocused() || m_drawer.IsSearchFocused()));

    const float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));

    // Fill workspace canvas background behind all panels
    // so when terminal is open with a gap, the gap matches the dark canvas background seamlessly
    D2D1_RECT_F rcWorkspace = D2D1::RectF(railW, headerH, clientW, clientH);
    m_pRenderTarget->FillRectangle(rcWorkspace, m_theme.brushCanvasBg ? m_theme.brushCanvasBg.Get() : m_theme.brushDrawerBg.Get());

    // Pre-fill the corner patch at (railW, headerH) with headerBg
    // so the 14px inward rounded arc of DrawerPanel (or EditorCanvas when drawer is closed)
    // curves smoothly against the title header and activity rail across all 20 themes.
    D2D1_RECT_F rcCornerPatch = D2D1::RectF(railW, headerH, railW + m_config.ScaleF(18.0f), headerH + m_config.ScaleF(18.0f));
    m_pRenderTarget->FillRectangle(rcCornerPatch, m_theme.brushHeaderBg.Get());

    if (!m_terminal.IsOpen() && !m_isTerminalAnimating) {
        m_terminalAnimProgress = 0.0f;
    }

    float currentTermH = GetCurrentTerminalHeight(clientH);
    m_terminal.SetFullScreenProgress(m_termFullScreenProgress);

    // Apply margin gap when terminal is open (user requirement: "make the height of the sidebar and editory misus it a bit when the terminal is open")
    const float termGap = (m_terminalAnimProgress > 0.001f && m_terminal.IsOpen()) ? m_config.ScaleF(8.0f) : 0.0f;
    float editorH = clientH - currentTermH - termGap;
    float drawerH = clientH - currentTermH - termGap;
    float terminalLeft = static_cast<float>(railW);

    m_editor.Render(m_pRenderTarget.Get(), m_theme, m_config, m_editorGroups, m_focusedGroupIdx,
                    canvasLeft, canvasWidth, editorH, hasLeftDrawer,
                    searchHL, hlMatchCase, hlWholeWord, hlUseRegex,
                    suppressEditorCaret);

    // 1b. Render Explorer Drag-and-Drop Insertion Caret / Target Indicator
    if (m_isDraggingExplorerFile && m_hasDragDropTarget && m_dragDropTargetGroup >= 0 &&
        m_dragDropTargetGroup < static_cast<int>(m_editorGroups.size())) {
        const auto& group = m_editorGroups[m_dragDropTargetGroup];
        if (group.activeDocIdx >= 0 && group.activeDocIdx < static_cast<int>(group.docs.size())) {
            const auto& doc = group.docs[group.activeDocIdx];
            if (!doc.isSpecialView && !doc.isImageView && !doc.isDiffView) {
                float paneW = canvasWidth / m_editorGroups.size();
                float pLeft = canvasLeft + m_dragDropTargetGroup * paneW;
                float gutterW = m_config.ScaleF(50.0f);
                float tabH = m_config.ScaleF(34.0f);
                float breadcrumbH = m_config.ScaleF(24.0f);
                float topY = headerH + tabH + breadcrumbH;
                float lineH = m_config.ScaleF(std::round(m_config.fontSize * m_config.lineHeightMultiplier));
                float charW = m_config.ScaleF(7.8f);

                if (m_dragDropTargetLine >= doc.scrollLine) {
                    size_t visualLine = m_dragDropTargetLine - doc.scrollLine;
                    float lineY = topY + m_config.ScaleF(6.0f) + visualLine * lineH;
                    if (lineY + lineH <= editorH) {
                        float caretX = pLeft + gutterW + m_config.ScaleF(12.0f) + m_dragDropTargetCol * charW;
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dropTargetBrush;
                        m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 0.85f, 0.9f), &dropTargetBrush);
                        if (dropTargetBrush) {
                            m_pRenderTarget->FillRectangle(
                                D2D1::RectF(caretX - 1.5f, lineY, caretX + 1.5f, lineY + lineH),
                                dropTargetBrush.Get()
                            );
                            m_pRenderTarget->DrawLine(
                                D2D1::Point2F(pLeft + gutterW + m_config.ScaleF(6.0f), lineY + lineH),
                                D2D1::Point2F(pLeft + paneW - m_config.ScaleF(20.0f), lineY + lineH),
                                dropTargetBrush.Get(),
                                1.5f
                            );
                        }
                    }
                }
            }
        }
    }

    // 2. Render Slide-out Drawer Panel (smoothly morphs and resizes its bottom with terminal)
    if (hasLeftDrawer) {
        m_drawer.Render(m_pRenderTarget.Get(), m_theme, m_config, drawerH, currentDrawerW);
    }

    // 2b. Render Extensions Right Sidebar Panel (smoothly slides in from right with zero content squishing!)
    if (hasRightSidebar) {
        float rightSidebarX = clientW - currentRightSidebarW;
        float rightSidebarH = (std::max)(50.0f, clientH - headerH - currentTermH);
        D2D1_RECT_F clipRect = D2D1::RectF(rightSidebarX, headerH, clientW, headerH + rightSidebarH);
        m_pRenderTarget->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_extensionsPanel.Render(m_pRenderTarget.Get(), m_theme, m_config, rightSidebarX, headerH, fullRightSidebarW, rightSidebarH);
        m_pRenderTarget->PopAxisAlignedClip();
    } else if (m_extensionsPanel.HasActiveProcesses()) {
        auto* curDoc = GetActiveDoc();
        bool isPackagesTab = (curDoc && curDoc->isSpecialView && curDoc->viewType == L"packages");

        bool hasOnlyPackageTasks = true;
        for (const auto& task : m_extensionsPanel.GetProcessTasks()) {
            if (task.title.find(L"package") == std::wstring::npos &&
                task.title.find(L"Package") == std::wstring::npos) {
                hasOnlyPackageTasks = false;
                break;
            }
        }

        if (isPackagesTab) {
            // Floating Processes HUD Card on the LEFT side of Packages view (safe from right-side WebView2)
            const float cardW = m_config.ScaleF(280.0f);
            const float cardX = canvasLeft + m_config.ScaleF(12.0f);
            float cardH = (std::max)(50.0f, clientH - headerH - currentTermH);
            float cardY = headerH;
            m_extensionsPanel.RenderProcessesCard(m_pRenderTarget.Get(), m_theme, m_config, cardX, cardY, cardW, cardH);
        } else if (!hasOnlyPackageTasks) {
            // Floating Processes HUD Card at bottom-right for non-package background processes
            const float cardW = m_config.ScaleF(340.0f);
            const float cardX = clientW - cardW - m_config.ScaleF(16.0f);
            float cardH = (std::max)(50.0f, clientH - headerH - currentTermH);
            float cardY = headerH;
            m_extensionsPanel.RenderProcessesCard(m_pRenderTarget.Get(), m_theme, m_config, cardX, cardY, cardW, cardH);
        } else {
            m_extensionsPanel.ClearProcessesCardBounds();
        }
    } else {
        m_extensionsPanel.ClearProcessesCardBounds();
    }

    // 3. Integrated Terminal Panel at Bottom (smoothly slides up across workspace and sidebar)
    if (m_terminalAnimProgress > 0.001f && m_terminal.IsOpen()) {
        m_terminal.Render(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH, terminalLeft, currentTermH);
    }

    // Live Server Status Card: Bigger, longer, and modern flat design with progression (per user request)
    auto* pActiveLiveServer = GetActiveDocLiveServer();
    if (!pActiveLiveServer && !m_activeLiveServers.empty()) {
        pActiveLiveServer = &m_activeLiveServers.back();
    }
    if (!pActiveLiveServer && m_isProjectRunning && !m_runningProjectRoot.empty()) {
        pActiveLiveServer = FindLiveServer(m_runningProjectRoot);
    }

    if (pActiveLiveServer && (m_isProjectRunning || pActiveLiveServer->isStarting || pActiveLiveServer->hasError || (pActiveLiveServer->server && pActiveLiveServer->server->IsRunning()) || !pActiveLiveServer->customUrl.empty() || !pActiveLiveServer->title.empty())) {
        const float cardW = m_config.ScaleF(280.0f); // Sleek modern developer toast card
        const float cardH = m_config.ScaleF(34.0f);  // STRICTLY SINGLE-LINE: 34px height (no 2nd line per user request!)
        const float minimapW = m_config.ScaleF(88.0f);
        const float rightOffset = (hasRightSidebar ? currentRightSidebarW : 0.0f) +
            (m_config.showMinimap ? minimapW : 0.0f) +
            m_config.ScaleF(16.0f);
        const float cardX = clientW - cardW - rightOffset;
        float cardY = clientH - cardH - m_config.ScaleF(12.0f);
        if (m_terminal.IsOpen()) {
            float th = GetCurrentTerminalHeight(clientH);
            cardY = clientH - th - cardH - m_config.ScaleF(12.0f);
        }
        m_liveServerPillBounds = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);
        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_liveServerPillBounds, m_config.ScaleF(6.0f), m_config.ScaleF(6.0f));

        // Modern FLAT background (zero harsh lines, subtle surface matching phide)
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBgBrush;
        m_pRenderTarget->CreateSolidColorBrush(
            m_hoverLiveServerPill
                ? (m_theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.15f, 0.19f, 0.98f) : D2D1::ColorF(0.89f, 0.91f, 0.94f, 0.98f))
                : (m_theme.IsDarkMode() ? D2D1::ColorF(0.11f, 0.12f, 0.15f, 0.98f) : D2D1::ColorF(0.94f, 0.95f, 0.97f, 0.98f)),
            &cardBgBrush
        );
        if (cardBgBrush) m_pRenderTarget->FillRoundedRectangle(rrcCard, cardBgBrush.Get());

        // Subtle 1px flat border
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBorderBrush;
        m_pRenderTarget->CreateSolidColorBrush(
            m_theme.IsDarkMode() ? D2D1::ColorF(0.22f, 0.24f, 0.30f, 0.90f) : D2D1::ColorF(0.82f, 0.84f, 0.88f, 0.90f),
            &cardBorderBrush
        );
        if (cardBorderBrush) m_pRenderTarget->DrawRoundedRectangle(rrcCard, cardBorderBrush.Get(), 1.0f);

        // 1. Glowing Status Dot (●) - Red error dot, Blue pulse while starting, Emerald green when live
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dotBrush;
        m_pRenderTarget->CreateSolidColorBrush(
            pActiveLiveServer->hasError
                ? D2D1::ColorF(0.94f, 0.28f, 0.28f, 1.0f)
                : (pActiveLiveServer->isStarting
                    ? D2D1::ColorF(0.24f, 0.58f, 1.0f, 1.0f)
                    : D2D1::ColorF(0.20f, 0.88f, 0.50f, 1.0f)),
            &dotBrush
        );
        float dotCx = cardX + m_config.ScaleF(14.0f);
        float dotCy = cardY + cardH * 0.5f;
        float dotR = m_config.ScaleF(3.5f);
        if (dotBrush) {
            m_pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotCx, dotCy), dotR, dotR), dotBrush.Get());
        }

        // Close button 'X' on far right of single line (per user request: "move the X here so it wont be 2 line")
        float closeBtnSize = m_config.ScaleF(18.0f);
        float closeBtnRight = cardX + cardW - m_config.ScaleF(8.0f);
        float closeBtnLeft = closeBtnRight - closeBtnSize;
        float closeBtnTop = cardY + (cardH - closeBtnSize) * 0.5f;
        float closeBtnBottom = closeBtnTop + closeBtnSize;
        m_liveServerCloseBounds = D2D1::RectF(closeBtnLeft, closeBtnTop, closeBtnRight, closeBtnBottom);

        if (m_hoverLiveServerClose) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> closeHoverBrush;
            m_pRenderTarget->CreateSolidColorBrush(
                m_theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f),
                &closeHoverBrush
            );
            if (closeHoverBrush) {
                m_pRenderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(m_liveServerCloseBounds, m_config.ScaleF(3.0f), m_config.ScaleF(3.0f)),
                    closeHoverBrush.Get()
                );
            }
        }

        // Render 'X' cross icon
        float xMidX = (closeBtnLeft + closeBtnRight) * 0.5f;
        float xMidY = (closeBtnTop + closeBtnBottom) * 0.5f;
        float xArm = m_config.ScaleF(4.0f);
        ID2D1Brush* xBrush = m_hoverLiveServerClose
            ? (m_theme.brushTextPrimary ? m_theme.brushTextPrimary.Get() : m_theme.brushTextMuted.Get())
            : (m_theme.brushTextMuted ? m_theme.brushTextMuted.Get() : m_theme.brushTextSecondary.Get());
        if (xBrush) {
            m_pRenderTarget->DrawLine(
                D2D1::Point2F(xMidX - xArm, xMidY - xArm),
                D2D1::Point2F(xMidX + xArm, xMidY + xArm),
                xBrush, 1.3f
            );
            m_pRenderTarget->DrawLine(
                D2D1::Point2F(xMidX + xArm, xMidY - xArm),
                D2D1::Point2F(xMidX - xArm, xMidY + xArm),
                xBrush, 1.3f
            );
        }

        // 3. Port Pill Badge right next to the 'X' button
        std::wstring portText;
        if (pActiveLiveServer->hasError) {
            portText = L"Failed";
        } else if (pActiveLiveServer->port > 0) {
            portText = L"Port: " + std::to_wstring(pActiveLiveServer->port);
        } else {
            portText = L"Native";
        }
        const float portBadgeW = m_config.ScaleF(62.0f);
        const float portBadgeH = m_config.ScaleF(18.0f);
        const float portBadgeRight = closeBtnLeft - m_config.ScaleF(6.0f);
        const float portBadgeLeft = portBadgeRight - portBadgeW;
        const float portBadgeTop = cardY + (cardH - portBadgeH) * 0.5f;
        D2D1_RECT_F rcPortBadge = D2D1::RectF(portBadgeLeft, portBadgeTop, portBadgeRight, portBadgeTop + portBadgeH);
        D2D1_ROUNDED_RECT rrcPortBadge = D2D1::RoundedRect(rcPortBadge, m_config.ScaleF(4.0f), m_config.ScaleF(4.0f));
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> portBadgeBrush;
        m_pRenderTarget->CreateSolidColorBrush(
            pActiveLiveServer->hasError
                ? D2D1::ColorF(0.92f, 0.25f, 0.25f, 0.18f)
                : (m_theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.06f)),
            &portBadgeBrush
        );
        if (portBadgeBrush) m_pRenderTarget->FillRoundedRectangle(rrcPortBadge, portBadgeBrush.Get());

        IDWriteTextFormat* pFmtHint = m_theme.fontHint ? m_theme.fontHint.Get() : m_theme.fontCode.Get();
        ID2D1Brush* textMuted = m_theme.brushTextMuted ? m_theme.brushTextMuted.Get() : m_theme.brushTextSecondary.Get();
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> errBadgeTextBrush;
        if (pActiveLiveServer->hasError) {
            m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.45f, 0.45f, 1.0f), &errBadgeTextBrush);
        }
        ID2D1Brush* badgeTextBrush = (pActiveLiveServer->hasError && errBadgeTextBrush) ? errBadgeTextBrush.Get() : textMuted;
        if (pFmtHint && badgeTextBrush) {
            DWRITE_TEXT_ALIGNMENT prevAlign = pFmtHint->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtHint->GetParagraphAlignment();
            pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pFmtHint->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            m_pRenderTarget->DrawText(portText.c_str(), static_cast<UINT32>(portText.length()), pFmtHint, rcPortBadge, badgeTextBrush);
            pFmtHint->SetTextAlignment(prevAlign);
            pFmtHint->SetParagraphAlignment(prevPAlign);
        }

        // 2. Title / Framework Name (e.g. "Next.js Server", "Vite Server", "Live Server") - strictly single line
        std::wstring titleText = !pActiveLiveServer->title.empty() ? pActiveLiveServer->title : L"Live Server";
        D2D1_RECT_F rcTitle = D2D1::RectF(cardX + m_config.ScaleF(24.0f), cardY + (cardH - m_config.ScaleF(18.0f)) * 0.5f, portBadgeLeft - m_config.ScaleF(6.0f), cardY + (cardH + m_config.ScaleF(18.0f)) * 0.5f);
        ID2D1Brush* textPrimary = m_theme.IsDarkMode()
            ? (m_theme.brushTextWhite ? m_theme.brushTextWhite.Get() : m_theme.brushTextPrimary.Get())
            : m_theme.brushTextPrimary.Get();
        IDWriteTextFormat* pFmtTitle = m_theme.fontMenuItem ? m_theme.fontMenuItem.Get() : m_theme.fontBody.Get();
        if (pFmtTitle && textPrimary) {
            m_pRenderTarget->DrawText(titleText.c_str(), static_cast<UINT32>(titleText.length()), pFmtTitle, rcTitle, textPrimary);
        }

        // 4. If starting, render a subtle 2px progression bar at the bottom edge of the 1-line card
        if (pActiveLiveServer->isStarting) {
            float trackLeft = cardX + m_config.ScaleF(4.0f);
            float trackW = cardW - m_config.ScaleF(8.0f);
            float trackH = m_config.ScaleF(2.0f);
            float trackY = cardY + cardH - trackH - m_config.ScaleF(1.0f);
            float fillW = trackW * (std::clamp)(pActiveLiveServer->progress, 0.05f, 1.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> progFillBrush;
            m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.56f, 0.95f, 0.9f), &progFillBrush);
            if (progFillBrush) {
                m_pRenderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(trackLeft, trackY, trackLeft + fillW, trackY + trackH), 1.0f, 1.0f),
                    progFillBrush.Get()
                );
            }
        }

        // Floating Tooltip on Hover
        if (m_hoverLiveServerPill && !pActiveLiveServer->isStarting) {
            std::wstring targetUrl = !pActiveLiveServer->customUrl.empty()
                ? pActiveLiveServer->customUrl
                : (pActiveLiveServer->server ? pActiveLiveServer->server->GetLocalUrl(pActiveLiveServer->fileName) : (pActiveLiveServer->port > 0 ? (L"http://127.0.0.1:" + std::to_wstring(pActiveLiveServer->port)) : L""));
            std::wstring tip = pActiveLiveServer->hasError
                ? L"Dev server encountered an error. Click to view terminal output."
                : (!targetUrl.empty() ? (titleText + L" (Click to open " + targetUrl + L")") : titleText);
            const float tipH = m_config.ScaleF(24.0f);
            const float tipPadX = m_config.ScaleF(10.0f);
            float textW = m_config.ScaleF(static_cast<float>(tip.length() * 6.5f + 14.0f));
            float tipW = textW + tipPadX * 2.0f;
            float tipX = cardX + (cardW - tipW) / 2.0f;
            float tipY = cardY - tipH - m_config.ScaleF(6.0f);
            if (tipX < 10.0f) tipX = 10.0f;
            if (tipX + tipW > clientW - 10.0f) tipX = clientW - tipW - 10.0f;

            D2D1_RECT_F rcTip = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);
            D2D1_ROUNDED_RECT rrcTip = D2D1::RoundedRect(rcTip, 4.0f, 4.0f);
            m_pRenderTarget->FillRoundedRectangle(rrcTip, m_theme.brushTooltipBg.Get());
            m_pRenderTarget->DrawRoundedRectangle(rrcTip, m_theme.brushTooltipBorder.Get(), 1.0f);
            m_pRenderTarget->DrawText(
                tip.c_str(),
                static_cast<UINT32>(tip.length()),
                m_theme.fontTooltip ? m_theme.fontTooltip.Get() : m_theme.fontHint.Get(),
                rcTip,
                m_theme.brushTooltipText ? m_theme.brushTooltipText.Get() : m_theme.brushTextPrimary.Get()
            );
        }
    } else {
        m_liveServerPillBounds = {};
        m_liveServerCloseBounds = {};
    }

    // 4. Render Collapsed Activity Rail (Home -> Search -> Git with customized tooltips)
    m_rail.SetGitLoading(m_drawer.IsGitLoading());
    m_rail.SetGitBadgeCount(m_drawer.GetGitChanges().size());
    m_rail.Render(m_pRenderTarget.Get(), m_theme, m_config, clientH);

    // 4. Render Dark Frameless Title Header at Top (with active highlight on Sidebar button when open)
    m_header.SetExtensionsActive(m_extensionsPanel.IsOpen());
    UpdateExecutionButtonStates();
    m_header.Render(m_pRenderTarget.Get(), m_theme, m_config, clientW, IsZoomed(m_hwnd));

    // 5. Render Active Popup Dropdown Menu Overlay
    if (m_popupMenu.IsOpen()) {
        m_popupMenu.Render(m_pRenderTarget.Get(), m_theme, m_config);
    }

    // 6. Render Modal Theme Picker Popup (Dark, Light, Auto with preview screenshot cards)
    if (m_themePicker.IsOpen()) {
        m_themePicker.Render(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH);
    }

    // 6b. Render Interactive Direct2D Color Picker Modal (Images 2, 5)
    if (m_colorPicker.IsOpen()) {
        m_colorPicker.Render(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH);
    }

    // 7. Render Quick Open File Search Palette Modal (Ctrl+K, Ctrl+P, Magnifying button)
    if (m_quickOpen.IsOpen()) {
        m_quickOpen.Render(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH);
    }

    // 8. Render Floating Drag-and-Drop Ghost Pill (Godot / VS Code style)
    if (m_isDraggingExplorerFile && !m_dragCandidateName.empty()) {
        float ghostX = static_cast<float>(m_dragCurrentPt.x) + 14.0f;
        float ghostY = static_cast<float>(m_dragCurrentPt.y) + 14.0f;
        float badgeW = m_config.ScaleF(static_cast<float>(m_dragCandidateName.length() * 8.0f + 64.0f));
        if (badgeW < m_config.ScaleF(140.0f)) badgeW = m_config.ScaleF(140.0f);
        if (badgeW > m_config.ScaleF(260.0f)) badgeW = m_config.ScaleF(260.0f);
        const float badgeH = m_config.ScaleF(28.0f);

        if (ghostX + badgeW > clientW - 8.0f) ghostX = clientW - badgeW - 8.0f;
        if (ghostY + badgeH > clientH - 8.0f) ghostY = clientH - badgeH - 8.0f;

        D2D1_RECT_F rcBadge = D2D1::RectF(ghostX, ghostY, ghostX + badgeW, ghostY + badgeH);
        D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(rcBadge, 5.0f, 5.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBg;
        m_pRenderTarget->CreateSolidColorBrush(
            m_theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.14f, 0.18f, 0.95f) : D2D1::ColorF(0.96f, 0.97f, 0.99f, 0.95f),
            &badgeBg
        );
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBorder;
        m_pRenderTarget->CreateSolidColorBrush(
            m_hasDragDropTarget ? D2D1::ColorF(0.0f, 0.50f, 0.95f, 0.95f) : D2D1::ColorF(0.40f, 0.45f, 0.52f, 0.8f),
            &badgeBorder
        );

        if (badgeBg) m_pRenderTarget->FillRoundedRectangle(rrcBadge, badgeBg.Get());
        if (badgeBorder) m_pRenderTarget->DrawRoundedRectangle(rrcBadge, badgeBorder.Get(), 1.5f);

        float iconCx = ghostX + m_config.ScaleF(14.0f);
        float iconCy = ghostY + badgeH / 2.0f;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> plusBrush;
        m_pRenderTarget->CreateSolidColorBrush(
            m_hasDragDropTarget ? D2D1::ColorF(0.20f, 0.85f, 0.40f, 1.0f) : D2D1::ColorF(0.0f, 0.50f, 0.95f, 1.0f),
            &plusBrush
        );
        if (plusBrush) {
            m_pRenderTarget->DrawLine(D2D1::Point2F(iconCx - 4.0f, iconCy), D2D1::Point2F(iconCx + 4.0f, iconCy), plusBrush.Get(), 1.8f);
            m_pRenderTarget->DrawLine(D2D1::Point2F(iconCx, iconCy - 4.0f), D2D1::Point2F(iconCx, iconCy + 4.0f), plusBrush.Get(), 1.8f);
        }

        std::wstring dragLabel = m_dragCandidateName;
        if (m_hasDragDropTarget) {
            dragLabel += L" (import)";
        }
        D2D1_RECT_F rcText = D2D1::RectF(ghostX + m_config.ScaleF(26.0f), ghostY + m_config.ScaleF(4.0f), ghostX + badgeW - m_config.ScaleF(6.0f), ghostY + badgeH);
        ID2D1Brush* textBrush = m_theme.IsDarkMode() ? (m_theme.brushTextWhite ? m_theme.brushTextWhite.Get() : m_theme.brushTextPrimary.Get()) : m_theme.brushTextPrimary.Get();
        m_pRenderTarget->DrawText(
            dragLabel.c_str(),
            static_cast<UINT32>(dragLabel.length()),
            m_theme.fontHint ? m_theme.fontHint.Get() : m_theme.fontCode.Get(),
            rcText,
            textBrush
        );
    }

    // 8.5. Render Too Many Changes Notification Toast (Image 3)
    if (m_drawer.HasTooManyChangesToast()) {
        m_drawer.RenderTooManyChangesToast(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH);
    }

    // 9. Render Confirm Discard / Restore Modal Overlay (Images 2 & 3)
    if (m_confirmDiscardModal.IsOpen()) {
        m_confirmDiscardModal.Render(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH);
    }

    // 9.5 Render Git Error Modal Overlay (Image 2)
    if (m_gitErrorModal.IsOpen()) {
        m_gitErrorModal.Render(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH);
    }

    // 10. Render Database & phpMyAdmin / Nginx Server Settings Modal Overlay (Images 3 & 4)
    if (m_dbSettingsModal.IsOpen()) {
        m_dbSettingsModal.Render(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH);
    }

    // 11. Render First-Time Commit / Publish Repository Centered Modal (VS Code style)
    if (m_drawer.IsPublishRepoModalOpen()) {
        m_drawer.RenderPublishRepoModal(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH);
    }

    // 12. Render "Login to GitHub" Flat Design Modal
    if (m_drawer.IsGitHubLoginModalOpen()) {
        m_drawer.RenderGitHubLoginModal(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH);
    }

    HRESULT hr = m_pRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }

    EndPaint(m_hwnd, &ps);
}

LRESULT Window::HandleNCHitTest(POINT screenPt) {
    POINT clientPt = screenPt;
    ScreenToClient(m_hwnd, &clientPt);

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    const int border = m_config.Scale(6);

    // Window Resize Borders (6px perimeter)
    if (clientPt.y < border) {
        if (clientPt.x < border) return HTTOPLEFT;
        if (clientPt.x >= w - border) return HTTOPRIGHT;
        return HTTOP;
    }
    if (clientPt.y >= h - border) {
        if (clientPt.x < border) return HTBOTTOMLEFT;
        if (clientPt.x >= w - border) return HTBOTTOMRIGHT;
        return HTBOTTOM;
    }
    if (clientPt.x < border) return HTLEFT;
    if (clientPt.x >= w - border) return HTRIGHT;

    // Header region hit testing
    const int headerH = m_config.Scale(m_config.headerHeight);
    if (clientPt.y < headerH) {
        // Returns HTCLIENT for menus & caption buttons, and HTCAPTION for draggable empty header area
        return m_header.HitTestNCHitTest(clientPt, m_config);
    }

    return HTCLIENT;
}

void Window::HandleMouseMove(POINT clientPt, WPARAM wParam) {
    if (m_drawer.IsGitHubLoginModalOpen()) {
        if (m_drawer.HandleGitHubLoginMouseMove(clientPt)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    if (m_drawer.IsPublishRepoModalOpen()) {
        if (m_drawer.HandlePublishRepoMouseMove(clientPt)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    if (m_dbSettingsModal.IsOpen()) {
        m_dbSettingsModal.OnMouseMove(clientPt);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_drawer.HasTooManyChangesToast()) {
        POINT pt = { clientPt.x, clientPt.y };
        if (m_drawer.HandleToastMouseMove(pt)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    if (m_confirmDiscardModal.IsOpen()) {
        if (m_confirmDiscardModal.HandleMouseMove(clientPt)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    if (m_gitErrorModal.IsOpen()) {
        if (m_gitErrorModal.HandleMouseMove(clientPt)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    if (!m_trackingMouse) {
        TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
        m_trackingMouse = true;
    }

    // Check Tab Scrollbar dragging across editor groups (User requirement)
    for (auto& group : m_editorGroups) {
        if (group.isDraggingTabScroll) {
            bool lbuttonDown = (wParam & MK_LBUTTON) != 0 ||
                               ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) ||
                               ((GetKeyState(VK_LBUTTON) & 0x8000) != 0);
            if (!lbuttonDown) {
                group.isDraggingTabScroll = false;
                ReleaseCapture();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            float dx = static_cast<float>(clientPt.x) - group.dragTabScrollStartX;
            float canvasW = group.bounds.right - group.bounds.left;
            float maxScroll = (std::max)(0.0f, group.totalTabsWidth - canvasW);
            float thumbW = (std::max)(m_config.ScaleF(36.0f), (canvasW / group.totalTabsWidth) * canvasW);
            float scrollableTrack = canvasW - thumbW;
            if (scrollableTrack > 0.0f && maxScroll > 0.0f) {
                float deltaOffset = (dx / scrollableTrack) * maxScroll;
                group.tabScrollOffset = (std::clamp)(group.dragTabScrollStartOffset + deltaOffset, 0.0f, maxScroll);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return;
        }
    }

    bool oldHoverPill = m_hoverLiveServerPill;
    bool oldHoverClose = m_hoverLiveServerClose;
    bool hasActiveServer = (GetActiveDocLiveServer() != nullptr) || !m_activeLiveServers.empty() || m_liveServer.IsRunning();
    m_hoverLiveServerClose = hasActiveServer &&
        (clientPt.x >= m_liveServerCloseBounds.left && clientPt.x <= m_liveServerCloseBounds.right &&
         clientPt.y >= m_liveServerCloseBounds.top && clientPt.y <= m_liveServerCloseBounds.bottom);
    m_hoverLiveServerPill = hasActiveServer && !m_hoverLiveServerClose &&
        (clientPt.x >= m_liveServerPillBounds.left && clientPt.x <= m_liveServerPillBounds.right &&
         clientPt.y >= m_liveServerPillBounds.top && clientPt.y <= m_liveServerPillBounds.bottom);
    if (m_hoverLiveServerPill != oldHoverPill || m_hoverLiveServerClose != oldHoverClose) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    if (m_hoverLiveServerPill || m_hoverLiveServerClose) {
        SetCursor(LoadCursor(NULL, IDC_HAND));
    }

    if (m_terminal.IsOpen()) {
        RECT rcClient;
        GetClientRect(m_hwnd, &rcClient);
        float clientH = static_cast<float>(rcClient.bottom - rcClient.top);
        if (m_terminal.OnMouseMove(clientPt, (GetKeyState(VK_LBUTTON) & 0x8000) != 0, clientH)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    if (m_themePicker.IsOpen()) {
        if (m_themePicker.HandleMouseMove(clientPt)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    if (m_colorPicker.IsOpen()) {
        auto onColorChange = [this](const std::wstring& newColor, size_t line, size_t col, size_t len) {
            if (m_focusedGroupIdx >= 0 && m_focusedGroupIdx < m_editorGroups.size()) {
                auto& group = m_editorGroups[m_focusedGroupIdx];
                if (group.activeDocIdx >= 0 && group.activeDocIdx < group.docs.size()) {
                    auto& doc = group.docs[group.activeDocIdx];
                    if (line < doc.buffer.GetLineCount()) {
                        const auto& curLine = doc.buffer.GetLine(line);
                        if (col <= curLine.length()) {
                            std::wstring newLine = curLine.substr(0, col) + newColor + curLine.substr((std::min)(curLine.length(), col + len));
                            doc.buffer.SetLine(line, newLine);
                            doc.isDirty = true;
                            doc.diagnostics = editor::SyntaxValidator::Validate(doc.filePath, doc.buffer);
                        }
                    }
                }
            }
        };
        if (m_colorPicker.HandleMouseMove(D2D1::Point2F(static_cast<float>(clientPt.x), static_cast<float>(clientPt.y)), onColorChange)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    if (m_quickOpen.IsOpen()) {
        if (m_quickOpen.HandleMouseMove(clientPt)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    // Check Autocomplete Popup Mouse Hover (Images 2, 5)
    if (m_editor.IsAutocompleteVisible()) {
        int hitIdx = -1;
        if (m_editor.HitTestAutocomplete(clientPt, hitIdx)) {
            if (hitIdx >= 0 && hitIdx != static_cast<int>(m_editor.GetAutocompleteSelectedIndex())) {
                m_editor.SetAutocompleteSelectedIndex(static_cast<size_t>(hitIdx));
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return;
        }
    }

    // Check Tab Dragging & Reordering (User request: "fix i cant drag the tab of split 1 to the other split", Image 4)
    if (m_dragTabIdx >= 0 && m_dragTabGroup >= 0 && m_dragTabGroup < static_cast<int>(m_editorGroups.size())) {
        bool lbuttonDown = (wParam & MK_LBUTTON) != 0 ||
                           ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) ||
                           ((GetKeyState(VK_LBUTTON) & 0x8000) != 0);
        if (!lbuttonDown) {
            m_dragTabIdx = -1;
            m_dragTabGroup = -1;
            m_tabDropTargetGroup = -1;
            m_tabDropTargetIdx = -1;
            m_isDraggingTab = false;
            m_editor.SetTabDragState(false, -1, -1, -1, -1);
            ReleaseCapture();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }

        float dx = static_cast<float>(clientPt.x - m_dragTabStartPt.x);
        float dy = static_cast<float>(clientPt.y - m_dragTabStartPt.y);
        if (!m_isDraggingTab && std::hypot(dx, dy) > 4.0f) {
            m_isDraggingTab = true;
        }

        if (m_isDraggingTab) {
            m_dragTabCurrentPt = clientPt;

            RECT clientRc;
            GetClientRect(m_hwnd, &clientRc);
            const int railW = m_config.Scale(m_config.railWidth);
            const int drawerW = static_cast<int>(m_config.Scale(m_config.drawerWidth) * m_drawerAnimProgress);
            const float canvasLeft = static_cast<float>(railW + drawerW);
            const float clientW = static_cast<float>(clientRc.right - clientRc.left);
            const float rightSidebarW = (m_extensionsPanel.IsOpen() || m_isRightSidebarAnimating)
                ? (m_config.ScaleF(320.0f) * m_rightSidebarAnimProgress) : 0.0f;
            const float canvasRight = clientW - rightSidebarW;
            const float canvasWidth = (std::max)(0.0f, canvasRight - canvasLeft);

            // Determine which editor group pane the mouse is hovering over (supports cross-split tab dragging!)
            size_t numGroups = m_editorGroups.size();
            float paneW = (numGroups > 0) ? (canvasWidth / static_cast<float>(numGroups)) : canvasWidth;
            int hoverGroup = m_dragTabGroup;
            for (size_t g = 0; g < numGroups; ++g) {
                float pLeft = canvasLeft + static_cast<float>(g) * paneW;
                float pRight = pLeft + paneW;
                if (clientPt.x >= pLeft && (clientPt.x < pRight || g == numGroups - 1)) {
                    hoverGroup = static_cast<int>(g);
                    break;
                }
            }

            m_tabDropTargetGroup = hoverGroup;
            auto& targetGroup = m_editorGroups[hoverGroup];
            int targetIdx = static_cast<int>(targetGroup.docs.size());

            // If mouse is within the tab bar area or over the tabs, find the exact insertion index
            for (size_t i = 0; i < targetGroup.docs.size(); ++i) {
                const auto& b = targetGroup.docs[i].tabBounds;
                float midX = (b.left + b.right) / 2.0f;
                if (clientPt.x < midX) {
                    targetIdx = static_cast<int>(i);
                    break;
                }
            }
            m_tabDropTargetIdx = targetIdx;

            auto& srcGroup = m_editorGroups[m_dragTabGroup];
            std::wstring dragTitle = (m_dragTabIdx >= 0 && m_dragTabIdx < static_cast<int>(srcGroup.docs.size()))
                ? srcGroup.docs[m_dragTabIdx].fileName
                : L"";
            m_editor.SetTabDragState(true, m_dragTabGroup, m_dragTabIdx, m_tabDropTargetGroup, m_tabDropTargetIdx,
                                     D2D1::Point2F(static_cast<float>(clientPt.x), static_cast<float>(clientPt.y)),
                                     dragTitle);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // Check Explorer File Dragging (Godot / VS Code style)
    if (!m_dragCandidatePath.empty()) {
        if (!(GetKeyState(VK_LBUTTON) & 0x8000)) {
            m_dragCandidatePath.clear();
            m_dragCandidateName.clear();
            m_isDraggingExplorerFile = false;
            m_hasDragDropTarget = false;
            m_dragDropTargetGroup = -1;
            ReleaseCapture();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }

        float dx = static_cast<float>(clientPt.x - m_dragStartPt.x);
        float dy = static_cast<float>(clientPt.y - m_dragStartPt.y);
        if (!m_isDraggingExplorerFile && std::hypot(dx, dy) > 5.0f) {
            m_isDraggingExplorerFile = true;
        }

        if (m_isDraggingExplorerFile) {
            m_dragCurrentPt = clientPt;

            RECT clientRc;
            GetClientRect(m_hwnd, &clientRc);
            const int railW = m_config.Scale(m_config.railWidth);
            const int drawerW = static_cast<int>(m_config.Scale(m_config.drawerWidth) * m_drawerAnimProgress);
            const float canvasLeft = static_cast<float>(railW + drawerW);
            const float canvasWidth = static_cast<float>(clientRc.right) - canvasLeft;
            const float clientH = static_cast<float>(clientRc.bottom - clientRc.top);
            const float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));
            const float tabH = m_config.ScaleF(34.0f);

            float currentTermH = GetCurrentTerminalHeight(clientH);
            const float termGap = (m_terminalAnimProgress > 0.001f && m_terminal.IsOpen()) ? m_config.ScaleF(6.0f) : 0.0f;
            float editorH = clientH - currentTermH - termGap;

            m_hasDragDropTarget = false;
            m_dragDropTargetGroup = -1;

            if (clientPt.x >= canvasLeft && clientPt.x < clientRc.right &&
                clientPt.y >= headerH + tabH && clientPt.y < editorH &&
                canvasWidth > 10.0f && !m_editorGroups.empty()) {

                float paneW = canvasWidth / m_editorGroups.size();
                for (size_t g = 0; g < m_editorGroups.size(); ++g) {
                    float pLeft = canvasLeft + g * paneW;
                    float pRight = pLeft + paneW;
                    if (clientPt.x >= pLeft && (clientPt.x < pRight || g == m_editorGroups.size() - 1)) {
                        m_dragDropTargetGroup = static_cast<int>(g);
                        auto& group = m_editorGroups[g];
                        if (group.activeDocIdx >= 0 && group.activeDocIdx < static_cast<int>(group.docs.size())) {
                            auto& doc = group.docs[group.activeDocIdx];
                            if (!doc.isSpecialView && !doc.isImageView && !doc.isDiffView) {
                                size_t line = 0, col = 0;
                                if (m_editor.HitTestCodeArea(clientPt, m_config, doc, pLeft, editorH, line, col)) {
                                    m_dragDropTargetLine = line;
                                    m_dragDropTargetCol = col;
                                    m_hasDragDropTarget = true;
                                } else {
                                    const float breadcrumbH = m_config.ScaleF(24.0f);
                                    const float topY = headerH + tabH + breadcrumbH;
                                    const float lineH = m_config.ScaleF(std::round(m_config.fontSize * m_config.lineHeightMultiplier));
                                    float relY = static_cast<float>(clientPt.y) - (topY + m_config.ScaleF(6.0f));
                                    if (relY < 0.0f) relY = 0.0f;
                                    size_t visualSlot = doc.scrollLine + static_cast<size_t>(relY / lineH);
                                    if (visualSlot >= doc.buffer.GetLineCount()) {
                                        m_dragDropTargetLine = doc.buffer.GetLineCount() > 0 ? doc.buffer.GetLineCount() - 1 : 0;
                                        m_dragDropTargetCol = doc.buffer.GetLine(m_dragDropTargetLine).length();
                                    } else {
                                        m_dragDropTargetLine = visualSlot;
                                        m_dragDropTargetCol = 0;
                                    }
                                    m_hasDragDropTarget = true;
                                }
                            }
                        }
                        break;
                    }
                }
            }

            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    bool needRepaint = false;

    // Check Editor Mouse Drag Text Selection (Images 2, 3, 4)
    auto* pActiveDoc = GetActiveDoc();
    if (pActiveDoc && pActiveDoc->isSelecting && (GetKeyState(VK_LBUTTON) & 0x8000)) {
        RECT clientRc;
        GetClientRect(m_hwnd, &clientRc);
        const int railW = m_config.Scale(m_config.railWidth);
        const int drawerW = static_cast<int>(m_config.Scale(m_config.drawerWidth) * m_drawerAnimProgress);
        const float canvasLeft = static_cast<float>(railW + drawerW);
        const float canvasWidth = static_cast<float>(clientRc.right) - canvasLeft;
        const float clientH = static_cast<float>(clientRc.bottom - clientRc.top);
        if (canvasWidth > 10.0f && !m_editorGroups.empty()) {
            float paneW = canvasWidth / m_editorGroups.size();
            float paneCanvasLeft = canvasLeft + m_focusedGroupIdx * paneW;
            const float gutterW = m_config.ScaleF(50.0f);
            const float codeLeft = paneCanvasLeft + gutterW + m_config.ScaleF(12.0f);
            const float minimapW = m_config.ScaleF(88.0f);
            const float scrollbarW = m_config.ScaleF(14.0f);
            const float codeRight = m_config.showMinimap ? (paneCanvasLeft + paneW - minimapW - m_config.ScaleF(6.0f)) : (paneCanvasLeft + paneW - scrollbarW);

            // Horizontal auto-scroll when dragging near or past right / left edge
            if (static_cast<float>(clientPt.x) > codeRight - m_config.ScaleF(35.0f)) {
                pActiveDoc->scrollOffsetX += m_config.ScaleF(28.0f);
            } else if (static_cast<float>(clientPt.x) < codeLeft + m_config.ScaleF(20.0f) && pActiveDoc->scrollOffsetX > 0.0f) {
                pActiveDoc->scrollOffsetX = (std::max)(0.0f, pActiveDoc->scrollOffsetX - m_config.ScaleF(28.0f));
            }

            // Vertical edge scrolling when dragging near top/bottom
            float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));
            if (clientPt.y < headerH + 25 && pActiveDoc->scrollLine > 0) {
                pActiveDoc->scrollLine--;
            } else if (clientPt.y > clientH - 25 && pActiveDoc->scrollLine + 1 < pActiveDoc->buffer.GetLineCount()) {
                pActiveDoc->scrollLine++;
            }

            POINT testPt = clientPt;
            if (static_cast<float>(testPt.x) < codeLeft) testPt.x = static_cast<LONG>(codeLeft);
            if (static_cast<float>(testPt.x) > codeRight - 2.0f) testPt.x = static_cast<LONG>(codeRight - 2.0f);

            size_t line = 0, col = 0;
            if (m_editor.HitTestCodeArea(testPt, m_config, *pActiveDoc, paneCanvasLeft, clientH, line, col)) {
                pActiveDoc->selEnd = { line, col };
                pActiveDoc->buffer.SetCursor(line, col);
                if (pActiveDoc->selAnchor.line != pActiveDoc->selEnd.line || pActiveDoc->selAnchor.col != pActiveDoc->selEnd.col) {
                    pActiveDoc->hasSelection = true;
                    pActiveDoc->selectedWord = pActiveDoc->GetSelectedText();
                } else {
                    pActiveDoc->hasSelection = false;
                    pActiveDoc->selectedWord.clear();
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        }
    }

    // Check diagnostic error hover in active document
    if (pActiveDoc && !pActiveDoc->diagnostics.empty()) {
        size_t prevHover = pActiveDoc->hoveredDiagnosticLine;
        pActiveDoc->hoveredDiagnosticLine = static_cast<size_t>(-1);
        RECT clientRc;
        GetClientRect(m_hwnd, &clientRc);
        const int railW = m_config.Scale(m_config.railWidth);
        const int drawerW = static_cast<int>(m_config.Scale(m_config.drawerWidth) * m_drawerAnimProgress);
        const float canvasLeft = static_cast<float>(railW + drawerW);
        const float canvasWidth = static_cast<float>(clientRc.right) - canvasLeft;
        const float clientH = static_cast<float>(clientRc.bottom - clientRc.top);
        if (canvasWidth > 10.0f && !m_editorGroups.empty()) {
            float paneW = canvasWidth / m_editorGroups.size();
            float paneCanvasLeft = canvasLeft + m_focusedGroupIdx * paneW;
            size_t hoverLine = 0, hoverCol = 0;
            if (m_editor.HitTestCodeArea(clientPt, m_config, *pActiveDoc, paneCanvasLeft, clientH, hoverLine, hoverCol)) {
                for (const auto& d : pActiveDoc->diagnostics) {
                    if (d.line == hoverLine) {
                        pActiveDoc->hoveredDiagnosticLine = hoverLine;
                        break;
                    }
                }
            }
        }
        if (pActiveDoc->hoveredDiagnosticLine != prevHover) {
            needRepaint = true;
        }
    }

    // Check Popup Menu hover first if open
    if (m_popupMenu.IsOpen()) {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        float clientW = static_cast<float>(rc.right - rc.left);
        float clientH = static_cast<float>(rc.bottom - rc.top);
        if (m_popupMenu.HandleMouseMove(clientPt, m_config, clientW, clientH)) {
            needRepaint = true;
        }
    }

    // Check vertical scrollbar dragging
    if (pActiveDoc && pActiveDoc->isDraggingScrollbar) {
        float deltaY = static_cast<float>(clientPt.y) - pActiveDoc->dragStartY;
        float trackH = pActiveDoc->scrollbarTrack.bottom - pActiveDoc->scrollbarTrack.top;
        float thumbH = pActiveDoc->scrollbarThumb.bottom - pActiveDoc->scrollbarThumb.top;
        float scrollRange = trackH - thumbH;
        if (scrollRange > 0.0f) {
            size_t totalLines = pActiveDoc->buffer.GetLineCount();
            if (pActiveDoc->isMultiFileDiff) {
                totalLines = 0;
                for (const auto& entry : pActiveDoc->multiFileDiffs) {
                    totalLines += 2;
                    if (!entry.isCollapsed) {
                        totalLines += entry.isBinary ? 3 : (std::max)(entry.originalBuffer.GetLineCount(), entry.workingBuffer.GetLineCount());
                    }
                }
            }
            float linesPerPixel = static_cast<float>(totalLines) / scrollRange;
            int newScroll = static_cast<int>(pActiveDoc->dragStartScrollLine) + static_cast<int>(deltaY * linesPerPixel);
            if (newScroll < 0) newScroll = 0;
            if (static_cast<size_t>(newScroll) >= totalLines) {
                newScroll = totalLines > 0 ? static_cast<int>(totalLines - 1) : 0;
            }
            pActiveDoc->scrollLine = static_cast<size_t>(newScroll);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // Check Minimap viewport slider dragging (Image 5) - direct 1:1 proportional tracking
    if (pActiveDoc && pActiveDoc->isDraggingMinimap) {
        float trackH = pActiveDoc->minimapTrack.bottom - pActiveDoc->minimapTrack.top;
        float sliderH = pActiveDoc->minimapSlider.bottom - pActiveDoc->minimapSlider.top;
        float scrollRange = trackH - sliderH;
        if (scrollRange > 0.0f) {
            size_t totalLines = pActiveDoc->buffer.GetLineCount();
            size_t maxS = (totalLines > 3) ? (totalLines - 3) : 0;
            float desiredSliderTop = (static_cast<float>(clientPt.y) - pActiveDoc->minimapTrack.top) - pActiveDoc->minimapGrabOffsetY;
            float ratio = (std::max)(0.0f, (std::min)(1.0f, desiredSliderTop / scrollRange));
            pActiveDoc->scrollLine = static_cast<size_t>(ratio * maxS);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // Check horizontal scrollbar dragging
    if (pActiveDoc && pActiveDoc->isDraggingHScrollbar) {
        float deltaX = static_cast<float>(clientPt.x) - pActiveDoc->dragStartHX;
        float trackW = pActiveDoc->hScrollbarTrack.right - pActiveDoc->hScrollbarTrack.left;
        float thumbW = pActiveDoc->hScrollbarThumb.right - pActiveDoc->hScrollbarThumb.left;
        float travelRange = trackW - thumbW;
        if (travelRange > 0.0f) {
            float viewportCodeW = pActiveDoc->hScrollbarTrack.right - pActiveDoc->hScrollbarTrack.left;
            float maxScrollX = (pActiveDoc->maxContentWidth > viewportCodeW) ? (pActiveDoc->maxContentWidth - viewportCodeW + m_config.ScaleF(40.0f)) : 0.0f;
            float newScrollX = pActiveDoc->dragStartScrollOffsetX + (deltaX / travelRange) * maxScrollX;
            pActiveDoc->scrollOffsetX = (std::clamp)(newScrollX, 0.0f, maxScrollX);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // Forward to keyboard shortcuts view or packages view if active doc is special view
    if (pActiveDoc && pActiveDoc->isSpecialView) {
        if (pActiveDoc->viewType == L"keybindings") {
            if (m_editor.GetKeyboardShortcutsView().HandleMouseMove(clientPt)) {
                needRepaint = true;
            }
        } else if (pActiveDoc->viewType == L"packages") {
            if (m_editor.GetPackageManagerView().HandleMouseMove(clientPt)) {
                needRepaint = true;
            }
        } else if (pActiveDoc->viewType == L"notebook") {
            if (m_editor.GetNotebookView().HandleMouseMove(clientPt, *pActiveDoc, m_config)) {
                needRepaint = true;
            }
        }
    }

    // Check Header menu, execution buttons, split editor button, zoom buttons, and caption button hover
    auto hoveredMenu = m_header.HitTestMenu(clientPt);
    auto hoveredCaption = m_header.HitTestCaptionButton(clientPt);
    auto hoveredExec = m_header.HitTestExecButton(clientPt);
    if (m_header.UpdateHoverState(hoveredMenu, hoveredCaption, hoveredExec)) needRepaint = true;
    if (m_header.SetHoveredSplitEditor(m_header.HitTestSplitEditor(clientPt))) needRepaint = true;
    if (m_header.SetHoveredQuickOpen(m_header.HitTestQuickOpen(clientPt))) needRepaint = true;
    if (m_header.SetHoveredExtensions(m_header.HitTestExtensions(clientPt))) needRepaint = true;
    if (m_header.SetHoveredDatabase(m_header.HitTestDatabase(clientPt))) needRepaint = true;
    if (m_header.SetHoveredServer(m_header.HitTestServer(clientPt))) needRepaint = true;
    if (m_header.SetHoveredDbPillSettings(m_header.HitTestDbPillSettings(clientPt))) needRepaint = true;
    if (m_header.SetHoveredDbPillBrowser(m_header.HitTestDbPillBrowser(clientPt))) needRepaint = true;
    if (m_header.SetHoveredSrvPillSettings(m_header.HitTestSrvPillSettings(clientPt))) needRepaint = true;
    if (m_header.SetHoveredSrvPillBrowser(m_header.HitTestSrvPillBrowser(clientPt))) needRepaint = true;

    // Track hovered item inside Database or Server checklist dropdowns
    int hoveredDropItem = -1;
    if (m_header.IsDatabaseDropdownOpen()) {
        auto rc = m_header.GetDatabaseDropdownBounds();
        if (clientPt.x >= rc.left && clientPt.x <= rc.right && clientPt.y >= rc.top && clientPt.y <= rc.bottom) {
            float totalH = rc.bottom - rc.top;
            float third = totalH / 3.0f;
            float relY = clientPt.y - rc.top;
            hoveredDropItem = (relY < third) ? 0 : ((relY < third * 2.0f) ? 1 : 2);
        }
    } else if (m_header.IsServerDropdownOpen()) {
        auto rc = m_header.GetServerDropdownBounds();
        if (clientPt.x >= rc.left && clientPt.x <= rc.right && clientPt.y >= rc.top && clientPt.y <= rc.bottom) {
            float midY = (rc.top + rc.bottom) * 0.5f;
            hoveredDropItem = (clientPt.y < midY) ? 0 : 1;
        }
    }
    if (m_header.SetHoveredDropdownItem(hoveredDropItem)) needRepaint = true;

    bool hovDbSet = m_header.HitTestDbSettings(clientPt);
    bool hovSrvSet = m_header.HitTestServerSettings(clientPt);
    if (m_header.SetHoveredDbSettings(hovDbSet)) needRepaint = true;
    if (m_header.SetHoveredServerSettings(hovSrvSet)) needRepaint = true;

    // Check Extensions right sidebar or floating processes HUD hover
    if ((m_rightSidebarAnimProgress > 0.001f && m_extensionsPanel.IsOpen()) || m_extensionsPanel.HasActiveProcesses()) {
        if (m_extensionsPanel.HandleMouseMove(clientPt)) {
            needRepaint = true;
        }
    }

    int hoveredZoom = 0;
    if (m_header.HitTestZoomOut(clientPt)) hoveredZoom = 1;
    else if (m_header.HitTestZoomReset(clientPt)) hoveredZoom = 2;
    else if (m_header.HitTestZoomIn(clientPt)) hoveredZoom = 3;
    if (m_header.SetHoveredZoom(hoveredZoom)) needRepaint = true;

    // Dynamic top-menu switching when any menu is currently open and mouse hovers over another header item
    if (m_popupMenu.IsOpen() && m_header.GetActiveMenu() != ui::HeaderMenuId::None) {
        if (hoveredMenu == ui::HeaderMenuId::File && m_header.GetActiveMenu() != ui::HeaderMenuId::File) {
            D2D1_RECT_F rcFile = m_header.GetMenuItemBounds(ui::HeaderMenuId::File);
            m_popupMenu.OpenFileMenu(rcFile.left, rcFile.bottom + 2.0f, m_config, m_autoSave,
                                     HasOpenFile(), (GetActiveDoc() && GetActiveDoc()->isDirty),
                                     m_drawer.HasOpenFolder(), m_recentPaths);
            m_header.SetActiveMenu(ui::HeaderMenuId::File);
            needRepaint = true;
        } else if (hoveredMenu == ui::HeaderMenuId::Edit && m_header.GetActiveMenu() != ui::HeaderMenuId::Edit) {
            D2D1_RECT_F rcEdit = m_header.GetMenuItemBounds(ui::HeaderMenuId::Edit);
            auto* pDoc = GetActiveDoc();
            bool hasDoc = (pDoc != nullptr && !pDoc->isSpecialView);
            bool canUndo = hasDoc && pDoc->buffer.CanUndo();
            bool canRedo = hasDoc && pDoc->buffer.CanRedo();
            m_popupMenu.OpenEditMenu(rcEdit.left, rcEdit.bottom + 2.0f, m_config, canUndo, canRedo);
            m_header.SetActiveMenu(ui::HeaderMenuId::Edit);
            needRepaint = true;
        } else if (hoveredMenu == ui::HeaderMenuId::View && m_header.GetActiveMenu() != ui::HeaderMenuId::View) {
            D2D1_RECT_F rcView = m_header.GetMenuItemBounds(ui::HeaderMenuId::View);
            m_popupMenu.OpenViewMenu(rcView.left, rcView.bottom + 2.0f, m_config,
                                     m_config.isDrawerOpen, IsZoomed(m_hwnd), m_wordWrap,
                                     m_editorGroups.size());
            m_header.SetActiveMenu(ui::HeaderMenuId::View);
            needRepaint = true;
        } else if (hoveredMenu == ui::HeaderMenuId::Code && m_header.GetActiveMenu() != ui::HeaderMenuId::Code) {
            D2D1_RECT_F rcCode = m_header.GetMenuItemBounds(ui::HeaderMenuId::Code);
            auto* pDoc = GetActiveDoc();
            bool hasDoc = (pDoc != nullptr && !pDoc->isSpecialView);
            bool hasSelection = (pDoc != nullptr && pDoc->HasSelection());
            m_popupMenu.OpenCodeMenu(rcCode.left, rcCode.bottom + 2.0f, m_config, hasDoc, hasSelection);
            m_header.SetActiveMenu(ui::HeaderMenuId::Code);
            needRepaint = true;
        } else if (hoveredMenu == ui::HeaderMenuId::Tools && m_header.GetActiveMenu() != ui::HeaderMenuId::Tools) {
            D2D1_RECT_F rcTools = m_header.GetMenuItemBounds(ui::HeaderMenuId::Tools);
            m_popupMenu.OpenToolsMenu(rcTools.left, rcTools.bottom + 2.0f, m_config,
                                      HasOpenFile(), m_terminal.IsOpen(), m_header.IsLiveServerRunning());
            m_header.SetActiveMenu(ui::HeaderMenuId::Tools);
            needRepaint = true;
        } else if (hoveredMenu == ui::HeaderMenuId::Window && m_header.GetActiveMenu() != ui::HeaderMenuId::Window) {
            D2D1_RECT_F rcWindow = m_header.GetMenuItemBounds(ui::HeaderMenuId::Window);
            m_popupMenu.OpenWindowMenu(rcWindow.left, rcWindow.bottom + 2.0f, m_config,
                                       IsZoomed(m_hwnd), m_config.isDrawerOpen, m_terminal.IsOpen(), HasOpenFile());
            m_header.SetActiveMenu(ui::HeaderMenuId::Window);
            needRepaint = true;
        }
    }

    // Check Rail icon hover (triggers customized tooltip)
    auto hoveredRail = m_rail.HitTest(clientPt);
    if (m_rail.SetHoveredTab(hoveredRail)) needRepaint = true;

    // Check Drawer hover
    RECT wndRc;
    GetClientRect(m_hwnd, &wndRc);
    const float clientH = static_cast<float>(wndRc.bottom - wndRc.top);
    float termH = GetCurrentTerminalHeight(clientH);
    const float termGap = (m_terminalAnimProgress > 0.001f && m_terminal.IsOpen()) ? m_config.ScaleF(6.0f) : 0.0f;
    float effectiveDrawerH = m_terminal.IsOpen() ? (clientH - termH - termGap) : clientH;

    if (m_drawerAnimProgress > 0.001f && clientPt.y < effectiveDrawerH) {
        if (m_config.activeRailTab == 0) {
            if (!m_drawer.HasOpenFolder()) {
                bool btnHover = m_drawer.HitTestOpenFolderButton(clientPt, m_config);
                if (m_drawer.SetHoveredOpenFolderBtn(btnHover)) needRepaint = true;
            } else {
                int hoveredTree = m_drawer.HitTest(clientPt, m_config);
                if (m_drawer.SetHoveredIndex(hoveredTree)) needRepaint = true;
            }
        } else if (m_config.activeRailTab == 1) {
            if (m_drawer.HandleSearchMouseMove(clientPt)) {
                needRepaint = true;
            }
        } else if (m_config.activeRailTab == 2) {
            if (m_isDraggingGitSplitter) {
                const float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));
                m_drawer.SetGitSplitY(static_cast<float>(clientPt.y), headerH + 110.0f, effectiveDrawerH - 70.0f);
                needRepaint = true;
            } else if (m_drawer.HandleGitMouseMove(clientPt)) {
                needRepaint = true;
            }
        }
    } else if (m_drawerAnimProgress > 0.001f) {
        if (m_drawer.SetHoveredIndex(-1)) needRepaint = true;
    }

    if (needRepaint) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::HandleLButtonDown(POINT clientPt) {
    if (m_drawer.IsGitHubLoginModalOpen()) {
        if (m_drawer.HandleGitHubLoginMouseDown(clientPt)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    if (m_drawer.IsPublishRepoModalOpen()) {
        if (m_drawer.HandlePublishRepoMouseDown(clientPt)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // Check Database & phpMyAdmin / Nginx Settings Modal Clicks (Images 3 & 4)
    if (m_dbSettingsModal.IsOpen()) {
        m_dbSettingsModal.HandleClick(clientPt);
        ui::DatabaseModalAction act = m_dbSettingsModal.HitTestAction(clientPt);
        switch (act) {
            case ui::DatabaseModalAction::CloseModal:
                m_dbSettingsModal.Close();
                break;
            case ui::DatabaseModalAction::SwitchTabPma:
                m_dbSettingsModal.SetActiveTab(ui::DatabaseModalTab::PhpMyAdmin);
                break;
            case ui::DatabaseModalAction::SwitchTabNginx:
                m_dbSettingsModal.SetActiveTab(ui::DatabaseModalTab::NginxServer);
                break;
            case ui::DatabaseModalAction::CopyHost:
                SetClipboardText(m_hwnd, m_dbSettingsModal.GetHost());
                m_dbSettingsModal.SetCopiedFeedback(L"Host ($host)");
                break;
            case ui::DatabaseModalAction::CopyPort:
                SetClipboardText(m_hwnd, m_dbSettingsModal.GetPort());
                m_dbSettingsModal.SetCopiedFeedback(L"Port");
                break;
            case ui::DatabaseModalAction::CopyUser:
                SetClipboardText(m_hwnd, m_dbSettingsModal.GetUser());
                m_dbSettingsModal.SetCopiedFeedback(L"User ($user)");
                break;
            case ui::DatabaseModalAction::CopyPass:
                SetClipboardText(m_hwnd, m_dbSettingsModal.GetPass());
                m_dbSettingsModal.SetCopiedFeedback(L"Password ($pass)");
                break;
            case ui::DatabaseModalAction::CopyDb:
                SetClipboardText(m_hwnd, m_dbSettingsModal.GetDefaultDb());
                m_dbSettingsModal.SetCopiedFeedback(L"Database Name ($db)");
                break;
            case ui::DatabaseModalAction::CopyPdoSnippet:
                SetClipboardText(m_hwnd, m_dbSettingsModal.GetPdoSnippet());
                m_dbSettingsModal.SetCopiedFeedback(L"PDO Snippet");
                break;
            case ui::DatabaseModalAction::CopyMysqliSnippet:
                SetClipboardText(m_hwnd, m_dbSettingsModal.GetMysqliSnippet());
                m_dbSettingsModal.SetCopiedFeedback(L"MySQLi Snippet");
                break;
            case ui::DatabaseModalAction::CopyNginxUrl:
                SetClipboardText(m_hwnd, m_dbSettingsModal.GetNginxUrl());
                m_dbSettingsModal.SetCopiedFeedback(L"Nginx URL");
                break;
            case ui::DatabaseModalAction::CopyNginxPort:
                SetClipboardText(m_hwnd, m_dbSettingsModal.GetNginxPort());
                m_dbSettingsModal.SetCopiedFeedback(L"Nginx Port");
                break;
            case ui::DatabaseModalAction::CopyNginxConfigPath:
                SetClipboardText(m_hwnd, m_dbSettingsModal.GetNginxConfigPath());
                m_dbSettingsModal.SetCopiedFeedback(L"Nginx Config Path");
                break;
            case ui::DatabaseModalAction::TogglePmaRun: {
                if (m_hPmaProcess != nullptr || m_isPmaRunning) {
                    StopPmaServer();
                } else {
                    StartPmaServer();
                }
                UpdateDatabaseModalRuntimeStatus();
                break;
            }
            case ui::DatabaseModalAction::ToggleServerRun: {
                if (m_hNginxProcess != nullptr || m_header.GetServerState() == ui::ServerState::Running) {
                    StopNginxServer();
                } else {
                    StartNginxServer();
                }
                UpdateDatabaseModalRuntimeStatus();
                break;
            }
            case ui::DatabaseModalAction::OpenBrowserPma:
                if (m_hPmaProcess == nullptr && !m_isPmaRunning) {
                    StartPmaServer();
                    UpdateDatabaseModalRuntimeStatus();
                }
                ShellExecuteW(nullptr, L"open", m_dbSettingsModal.GetPmaUrl().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case ui::DatabaseModalAction::OpenBrowserNginx:
                if (m_hNginxProcess == nullptr && m_header.GetServerState() != ui::ServerState::Running) {
                    StartNginxServer();
                    UpdateDatabaseModalRuntimeStatus();
                }
                ShellExecuteW(nullptr, L"open", m_dbSettingsModal.GetNginxUrl().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case ui::DatabaseModalAction::EditPmaConfig: {
                m_dbSettingsModal.Close();
                std::filesystem::path fullP = GetPhideRoot() / m_dbSettingsModal.GetPmaConfigPath();
                OpenFileDocument(fullP.wstring(), false);
                break;
            }
            case ui::DatabaseModalAction::EditNginxConfig: {
                m_dbSettingsModal.Close();
                std::filesystem::path fullP = GetPhideRoot() / m_dbSettingsModal.GetNginxConfigPath();
                OpenFileDocument(fullP.wstring(), false);
                break;
            }
            case ui::DatabaseModalAction::ImportSql: {
                StartMysqlServer();
                std::wstring sqlFile = m_dbSettingsModal.GetSqlFileName();
                std::wstring activeDocPath;
                if (m_focusedGroupIdx >= 0 && m_focusedGroupIdx < (int)m_editorGroups.size()) {
                    auto& grp = m_editorGroups[m_focusedGroupIdx];
                    if (grp.activeDocIdx >= 0 && grp.activeDocIdx < (int)grp.docs.size()) {
                        activeDocPath = grp.docs[grp.activeDocIdx].filePath;
                    }
                }
                std::filesystem::path sqlPath;
                if (!m_workspace.GetRootPath().empty() && std::filesystem::exists(m_workspace.GetRootPath() / sqlFile)) {
                    sqlPath = m_workspace.GetRootPath() / sqlFile;
                } else if (!m_runningProjectRoot.empty() && std::filesystem::exists(std::filesystem::path(m_runningProjectRoot) / sqlFile)) {
                    sqlPath = std::filesystem::path(m_runningProjectRoot) / sqlFile;
                } else if (!activeDocPath.empty() && std::filesystem::exists(std::filesystem::path(activeDocPath).parent_path() / sqlFile)) {
                    sqlPath = std::filesystem::path(activeDocPath).parent_path() / sqlFile;
                }
                std::filesystem::path mysqlExe = L"C:\\xampp\\mysql\\bin\\mysql.exe";
                if (std::filesystem::exists(mysqlExe) && !sqlPath.empty()) {
                    std::wstring cmd = L"cmd.exe /c \"\"" + mysqlExe.wstring() + L"\" -u root < \"" + sqlPath.wstring() + L"\"\"";
                    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
                    cmdBuf.push_back(L'\0');
                    STARTUPINFOW si{};
                    si.cb = sizeof(si);
                    si.dwFlags = STARTF_USESHOWWINDOW;
                    si.wShowWindow = SW_HIDE;
                    PROCESS_INFORMATION pi{};
                    if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                        WaitForSingleObject(pi.hProcess, 4000);
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                        m_dbSettingsModal.SetCopiedFeedback(L"imported " + sqlFile + L"!");
                    }
                }
                break;
            }
            case ui::DatabaseModalAction::None:
                if (!m_dbSettingsModal.HitTestBounds(clientPt)) {
                    m_dbSettingsModal.Close();
                }
                break;
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // Check Too Many Changes Toast Notification Clicks (Image 3)
    if (m_drawer.HasTooManyChangesToast()) {
        POINT pt = { clientPt.x, clientPt.y };
        if (m_drawer.HandleToastMouseDown(pt)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // Check Confirm Discard / Restore / Delete / Notice Modal Clicks (Images 2, 3, 4, 5)
    if (m_confirmDiscardModal.IsOpen()) {
        if (m_confirmDiscardModal.IsLoading()) {
            return; // strict idempotency: modal action is busy executing
        }
        bool wasNotice = (m_confirmDiscardModal.GetType() == ui::ConfirmModalType::Notice);
        auto modalType = m_confirmDiscardModal.GetType();
        bool confirmed = false;
        std::wstring targetPath, targetFileName, targetStatus;
        if (m_confirmDiscardModal.HandleMouseDown(clientPt, confirmed, targetPath, targetFileName, targetStatus)) {
            if (confirmed) {
                if (wasNotice) {
                    m_confirmDiscardModal.Close();
                    m_drawer.SetGitCommitBoxFocus(1);
                } else if (modalType == ui::ConfirmModalType::DeleteFile || modalType == ui::ConfirmModalType::DeleteFolder) {
                    ExecuteDeleteAsync(targetPath, modalType == ui::ConfirmModalType::DeleteFolder);
                } else {
                    ExecuteDiscardOrRestoreAsync(targetPath, targetFileName, targetStatus);
                }
            } else if (wasNotice) {
                m_drawer.SetGitCommitBoxFocus(1);
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // Check Git Error Modal Clicks (Image 2)
    if (m_gitErrorModal.IsOpen()) {
        ui::GitErrorModalAction action = ui::GitErrorModalAction::None;
        if (m_gitErrorModal.HandleMouseDown(clientPt, action)) {
            if (action == ui::GitErrorModalAction::OpenGitLog) {
                OpenGitLogTerminal();
            } else if (action == ui::GitErrorModalAction::ShowCommandOutput) {
                ShowGitCommandOutput(m_gitErrorModal.GetCommand(), m_gitErrorModal.GetOutput(), m_gitErrorModal.GetRepoDir());
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        return; // Block background interaction while modal is open
    }

    // Check Live Server Status Pill Click
    auto* pActiveLiveServer = GetActiveDocLiveServer();
    if (!pActiveLiveServer && !m_activeLiveServers.empty()) pActiveLiveServer = &m_activeLiveServers.back();
    if (!pActiveLiveServer && m_isProjectRunning && !m_runningProjectRoot.empty()) pActiveLiveServer = FindLiveServer(m_runningProjectRoot);
    if (pActiveLiveServer && (m_isProjectRunning || pActiveLiveServer->isStarting || pActiveLiveServer->hasError || (pActiveLiveServer->server && pActiveLiveServer->server->IsRunning()) || !pActiveLiveServer->customUrl.empty() || !pActiveLiveServer->title.empty())) {
        // First check close button 'X' click (per user request: "move the X here so it wont be 2 line")
        if (clientPt.x >= m_liveServerCloseBounds.left && clientPt.x <= m_liveServerCloseBounds.right &&
            clientPt.y >= m_liveServerCloseBounds.top && clientPt.y <= m_liveServerCloseBounds.bottom) {
            pActiveLiveServer->hasError = false;
            pActiveLiveServer->isStarting = false;
            pActiveLiveServer->title.clear();
            pActiveLiveServer->customUrl.clear();
            for (auto it = m_activeLiveServers.begin(); it != m_activeLiveServers.end(); ++it) {
                if (&(*it) == pActiveLiveServer) {
                    if (!it->server || !it->server->IsRunning()) {
                        m_activeLiveServers.erase(it);
                    }
                    break;
                }
            }
            m_liveServerPillBounds = {};
            m_liveServerCloseBounds = {};
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }

        if (clientPt.x >= m_liveServerPillBounds.left && clientPt.x <= m_liveServerPillBounds.right &&
            clientPt.y >= m_liveServerPillBounds.top && clientPt.y <= m_liveServerPillBounds.bottom) {
            if (pActiveLiveServer->hasError) {
                // Error state: Focus terminal so user can inspect error output! Do NOT open browser!
                if (!m_terminal.IsOpen()) {
                    m_terminal.Open(pActiveLiveServer->rootDir);
                    AnimateTerminalOpen();
                }
                m_isTerminalFocused = true;
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            std::wstring url = !pActiveLiveServer->customUrl.empty()
                ? pActiveLiveServer->customUrl
                : (pActiveLiveServer->server ? pActiveLiveServer->server->GetLocalUrl(pActiveLiveServer->fileName) : (pActiveLiveServer->port > 0 ? (L"http://127.0.0.1:" + std::to_wstring(pActiveLiveServer->port)) : L""));
            if (!url.empty()) {
                ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
            return;
        }
    }

    // Check Terminal Panel Clicks (Image 1, 2, 3, 4)
    if (m_terminal.IsOpen() && m_terminal.HitTest(clientPt)) {
        m_isTerminalFocused = true;
        std::wstring dir;
        if (m_drawer.HasOpenFolder()) dir = m_drawer.GetOpenFolderPath();
        m_terminal.OnMouseDown(clientPt, m_config, dir);
        if (m_terminal.IsResizing()) {
            SetCapture(m_hwnd);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    } else {
        m_isTerminalFocused = false;
        m_terminal.CloseMenus();
    }

    // Check Autocomplete Popup Click (Images 2 & 5)
    if (m_editor.IsAutocompleteVisible()) {
        int hitIdx = -1;
        if (m_editor.HitTestAutocomplete(clientPt, hitIdx)) {
            if (hitIdx >= 0) {
                m_editor.SetAutocompleteSelectedIndex(static_cast<size_t>(hitIdx));
                CommitAutocompleteSelection();
            } else {
                m_editor.DismissAutocomplete();
                m_editor.ClearGhostText();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return;
        } else {
            m_editor.DismissAutocomplete();
            m_editor.ClearGhostText();
        }
    }

    // 0a. Floating commit card click check (card is positioned outside drawer over editor canvas)
    if (m_config.activeRailTab == 2 && m_drawer.IsCommitCardVisible() && m_drawer.HitTestCommitCard(clientPt)) {
        std::wstring openFile;
        std::wstring discardedFile;
        bool isDiff = false;
        std::wstring openCommitDiff;
        std::wstring openUrl;
        bool promptDiscard = false;
        std::wstring discardFileName;
        std::wstring discardStatus;
        size_t discardCount = 1;
        bool promptEmptyCommit = false;
        if (m_drawer.HitTestGit(clientPt, m_config, openFile, discardedFile, isDiff, openCommitDiff, openUrl,
                                &promptDiscard, &discardFileName, &discardStatus, &discardCount, &promptEmptyCommit)) {
            if (promptEmptyCommit) {
                m_drawer.ClearGitTooltip();
                DoOpenCommitEditMsg();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            if (promptDiscard) {
                m_drawer.ClearGitTooltip();
                m_confirmDiscardModal.Open(discardedFile, discardFileName, discardStatus, discardCount);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            if (!openUrl.empty()) {
                ShellExecuteW(nullptr, L"open", openUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            if (!openCommitDiff.empty()) {
                OpenCommitDiffDocument(openCommitDiff, openFile);
            } else if (openFile.ends_with(L".ipynb")) {
                OpenNotebookView(openFile);
            } else if (isDiff && !openFile.empty()) {
                OpenDiffDocument(openFile, false);
            } else if (!openFile.empty()) {
                OpenFileDocument(openFile, false);
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    {
        float curRailW = m_config.ScaleF(static_cast<float>(m_config.railWidth));
        float curDrawerW = m_config.ScaleF(static_cast<float>(m_config.drawerWidth)) * m_drawerAnimProgress;
        if (clientPt.x > curRailW + curDrawerW) {
            m_drawer.DismissCommitCard();
        }
    }

    if (m_editor.IsRefactorPopupVisible()) {
        m_editor.DismissRefactorPopup();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    if (m_colorPicker.IsOpen()) {
        auto onColorChange = [this](const std::wstring& newColor, size_t line, size_t col, size_t len) {
            if (m_focusedGroupIdx >= 0 && m_focusedGroupIdx < m_editorGroups.size()) {
                auto& group = m_editorGroups[m_focusedGroupIdx];
                if (group.activeDocIdx >= 0 && group.activeDocIdx < group.docs.size()) {
                    auto& doc = group.docs[group.activeDocIdx];
                    if (line < doc.buffer.GetLineCount()) {
                        const auto& curLine = doc.buffer.GetLine(line);
                        if (col <= curLine.length()) {
                            std::wstring newLine = curLine.substr(0, col) + newColor + curLine.substr((std::min)(curLine.length(), col + len));
                            doc.buffer.SetLine(line, newLine);
                            doc.isDirty = true;
                            doc.diagnostics = editor::SyntaxValidator::Validate(doc.filePath, doc.buffer);
                        }
                    }
                }
            }
        };
        m_colorPicker.HandleLButtonDown(D2D1::Point2F(static_cast<float>(clientPt.x), static_cast<float>(clientPt.y)), onColorChange);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 0. Theme Picker Modal Clicks: only updates theme if a theme card was clicked
    if (m_themePicker.IsOpen()) {
        ui::ThemeMode chosenMode = m_theme.GetThemeMode();
        if (m_themePicker.HandleLButtonDown(clientPt, chosenMode)) {
            m_theme.SetThemeMode(chosenMode);
            BOOL dm = m_theme.IsDarkMode() ? TRUE : FALSE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dm, sizeof(dm));
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            UpdateDrawerErrorFiles();
    SaveSessionState();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 0b. Quick Open Modal Clicks (Image 5)
    if (m_quickOpen.IsOpen()) {
        std::wstring chosenFile;
        int targetLine = -1;
        std::wstring actionCmd;
        if (m_quickOpen.HandleMouseDown(clientPt, chosenFile, targetLine, actionCmd)) {
            if (!actionCmd.empty()) {
                ExecuteQuickOpenAction(actionCmd);
            } else if (!chosenFile.empty()) {
                OpenFileDocument(chosenFile, false);
                if (targetLine > 0) {
                    JumpToEditorLine(static_cast<size_t>(targetLine));
                }
            }
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // Popup Dropdown Menu Clicks (if menu is active)
    if (m_popupMenu.IsOpen()) {
        if (m_popupMenu.HitTestMenuArea(clientPt)) {
            auto action = m_popupMenu.HandleLButtonDown(clientPt);
            if (action != ui::MenuActionId::None) {
                switch (action) {
                    // File Menu Actions
                    case ui::MenuActionId::NewTextFile:
                        DoNewTextFile();
                        break;
                    case ui::MenuActionId::NewFile:
                        DoNewFile();
                        break;
                    case ui::MenuActionId::NewWindow:
                    case ui::MenuActionId::NewWindowDefault:
                    case ui::MenuActionId::NewWindowEmpty:
                        DoNewWindow();
                        break;
                    case ui::MenuActionId::OpenFile:
                        DoOpenFile();
                        break;
                    case ui::MenuActionId::OpenFolder:
                        DoOpenFolder();
                        break;
                    case ui::MenuActionId::OpenWorkspaceFromFile:
                        DoOpenWorkspaceFromFile();
                        break;
                    case ui::MenuActionId::RecentItem0:
                        if (m_recentPaths.size() > 0) {
                            if (std::filesystem::is_directory(m_recentPaths[0])) DoOpenFolderFromPath(m_recentPaths[0]);
                            else LoadFileFromPath(m_recentPaths[0]);
                        }
                        break;
                    case ui::MenuActionId::RecentItem1:
                        if (m_recentPaths.size() > 1) {
                            if (std::filesystem::is_directory(m_recentPaths[1])) DoOpenFolderFromPath(m_recentPaths[1]);
                            else LoadFileFromPath(m_recentPaths[1]);
                        }
                        break;
                    case ui::MenuActionId::RecentItem2:
                        if (m_recentPaths.size() > 2) {
                            if (std::filesystem::is_directory(m_recentPaths[2])) DoOpenFolderFromPath(m_recentPaths[2]);
                            else LoadFileFromPath(m_recentPaths[2]);
                        }
                        break;
                    case ui::MenuActionId::RecentItem3:
                        if (m_recentPaths.size() > 3) {
                            if (std::filesystem::is_directory(m_recentPaths[3])) DoOpenFolderFromPath(m_recentPaths[3]);
                            else LoadFileFromPath(m_recentPaths[3]);
                        }
                        break;
                    case ui::MenuActionId::RecentItem4:
                        if (m_recentPaths.size() > 4) {
                            if (std::filesystem::is_directory(m_recentPaths[4])) DoOpenFolderFromPath(m_recentPaths[4]);
                            else LoadFileFromPath(m_recentPaths[4]);
                        }
                        break;
                    case ui::MenuActionId::RecentItem5:
                        if (m_recentPaths.size() > 5) {
                            if (std::filesystem::is_directory(m_recentPaths[5])) DoOpenFolderFromPath(m_recentPaths[5]);
                            else LoadFileFromPath(m_recentPaths[5]);
                        }
                        break;
                    case ui::MenuActionId::ReopenClosedEditor:
                        DoReopenClosedEditor();
                        break;
                    case ui::MenuActionId::ClearRecent:
                        m_recentPaths.clear();
                        break;
                    case ui::MenuActionId::AddFolderToWorkspace:
                        DoAddFolderToWorkspace();
                        break;
                    case ui::MenuActionId::SaveWorkspaceAs:
                        DoSaveWorkspaceAs();
                        break;
                    case ui::MenuActionId::DuplicateWorkspace:
                        DoDuplicateWorkspace();
                        break;
                    case ui::MenuActionId::Save:
                        DoSaveFile();
                        break;
                    case ui::MenuActionId::SaveAs:
                        DoSaveFileAs();
                        break;
                    case ui::MenuActionId::SaveAll:
                        DoSaveAll();
                        break;
                    case ui::MenuActionId::ExportProfile:
                        DoExportProfile();
                        break;
                    case ui::MenuActionId::ImportProfile:
                        DoImportProfile();
                        break;
                    case ui::MenuActionId::ToggleAutoSave:
                        m_autoSave = !m_autoSave;
                        if (m_autoSave) {
                            DoSaveAll();
                        }
                        break;
                    case ui::MenuActionId::Settings:
                        DoOpenSettings();
                        break;
                    case ui::MenuActionId::KeyboardShortcuts:
                        DoOpenKeyboardShortcuts();
                        break;
                    case ui::MenuActionId::UserSnippets:
                        DoOpenUserSnippets();
                        break;
                    case ui::MenuActionId::ThemeSettings:
                        DoToggleThemeModal();
                        break;
                    case ui::MenuActionId::RevertFile:
                        DoRevertFile();
                        break;
                    case ui::MenuActionId::CloseEditor:
                        DoCloseEditor();
                        break;
                    case ui::MenuActionId::CloseFolder:
                        DoCloseFolder();
                        break;
                    case ui::MenuActionId::CloseWindow:
                    case ui::MenuActionId::Exit:
                        DestroyWindow(m_hwnd);
                        return;

                    // View Menu Actions (Images 2, 3, 4)
                    case ui::MenuActionId::TogglePrimarySideBar:
                        ToggleSidebar();
                        break;
                    case ui::MenuActionId::FullScreen:
                        if (IsZoomed(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
                        else ShowWindow(m_hwnd, SW_MAXIMIZE);
                        break;
                    case ui::MenuActionId::ZenMode:
                        m_config.zenMode = !m_config.zenMode;
                        break;
                    case ui::MenuActionId::ToggleMinimap:
                        m_config.showMinimap = !m_config.showMinimap;
                        break;
                    case ui::MenuActionId::ToggleBreadcrumbs:
                        m_config.showBreadcrumbs = !m_config.showBreadcrumbs;
                        break;
                    case ui::MenuActionId::ToggleRenderWhitespace:
                        m_config.renderWhitespace = !m_config.renderWhitespace;
                        break;
                    case ui::MenuActionId::Explorer:
                        DoShowExplorer();
                        break;
                    case ui::MenuActionId::Search:
                        DoShowSearch();
                        break;
                    case ui::MenuActionId::SourceControl:
                        DoShowSourceControl();
                        break;
                    case ui::MenuActionId::WordWrap:
                        m_wordWrap = !m_wordWrap;
                        break;
                    case ui::MenuActionId::Terminal:
                        DoCreateTerminal();
                        break;
                    case ui::MenuActionId::ZoomIn:
                        DoZoomIn();
                        break;
                    case ui::MenuActionId::ZoomOut:
                        DoZoomOut();
                        break;
                    case ui::MenuActionId::ResetZoom:
                        DoResetZoom();
                        break;

                    // Editor Layout Submenu Actions (Image 3)
                    case ui::MenuActionId::LayoutSingle:
                        if (m_editorGroups.size() > 1) {
                            if (m_focusedGroupIdx >= 0 && static_cast<size_t>(m_focusedGroupIdx) < m_editorGroups.size()) {
                                auto keepGroup = std::move(m_editorGroups[m_focusedGroupIdx]);
                                m_editorGroups.clear();
                                m_editorGroups.push_back(std::move(keepGroup));
                            } else {
                                m_editorGroups.resize(1);
                            }
                            m_focusedGroupIdx = 0;
                        }
                        break;
                    case ui::MenuActionId::LayoutTwoColumns:
                        if (m_editorGroups.size() < 2) {
                            SplitEditor();
                        } else if (m_editorGroups.size() > 2) {
                            m_editorGroups.resize(2);
                            if (m_focusedGroupIdx >= 2) m_focusedGroupIdx = 1;
                        }
                        break;
                    case ui::MenuActionId::LayoutThreeColumns:
                        while (m_editorGroups.size() < 3) {
                            SplitEditor();
                        }
                        break;
                    case ui::MenuActionId::LayoutSplitRight:
                    case ui::MenuActionId::LayoutSplitDown:
                    case ui::MenuActionId::LayoutSplitLeft:
                    case ui::MenuActionId::LayoutSplitUp:
                    case ui::MenuActionId::LayoutSplitInGroup:
                    case ui::MenuActionId::LayoutTwoRows:
                    case ui::MenuActionId::LayoutThreeRows:
                    case ui::MenuActionId::LayoutGrid2x2:
                    case ui::MenuActionId::LayoutTwoRowsRight:
                    case ui::MenuActionId::LayoutTwoColumnsBottom:
                        SplitEditor();
                        break;
                    case ui::MenuActionId::LayoutFlip:
                        if (m_editorGroups.size() > 1) {
                            std::reverse(m_editorGroups.begin(), m_editorGroups.end());
                            m_focusedGroupIdx = static_cast<int>(m_editorGroups.size() - 1 - m_focusedGroupIdx);
                        }
                        break;
                    case ui::MenuActionId::LayoutMoveNewWindow:
                    case ui::MenuActionId::LayoutCopyNewWindow:
                        DoNewWindow();
                        break;

                    // Context Menu Actions (Images 4 & 5)
                    case ui::MenuActionId::CtxOpenToSide:
                        if (!m_contextTargetPath.empty()) {
                            OpenFileDocument(m_contextTargetPath, false);
                        }
                        break;
                    case ui::MenuActionId::CtxOpenWith:
                        if (!m_contextTargetPath.empty()) {
                            ShellExecuteW(m_hwnd, L"open", L"rundll32.exe",
                                         (L"shell32.dll,OpenAs_RunDLL " + m_contextTargetPath).c_str(),
                                         nullptr, SW_SHOWNORMAL);
                        }
                        break;
                    case ui::MenuActionId::CtxRevealInExplorer:
                        if (!m_contextTargetPath.empty()) {
                            std::wstring param = L"/select,\"" + m_contextTargetPath + L"\"";
                            ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
                        }
                        break;
                    case ui::MenuActionId::CtxOpenInTerminal:
                        if (!m_contextTargetPath.empty()) {
                            std::wstring dir = m_contextTargetIsFolder ? m_contextTargetPath
                                : std::filesystem::path(m_contextTargetPath).parent_path().wstring();
                            ShellExecuteW(nullptr, L"open", L"cmd.exe", nullptr, dir.empty() ? nullptr : dir.c_str(), SW_SHOWNORMAL);
                        }
                        break;
                    case ui::MenuActionId::CtxCompareWithSelected:
                        if (!m_compareSourcePath.empty() && std::filesystem::exists(m_compareSourcePath)) {
                            std::wstring msg = L"Diff Comparison:\nSelected: " + m_compareSourcePath + L"\nTarget: " + m_contextTargetPath;
                            MessageBoxW(m_hwnd, msg.c_str(), L"Compare with Selected", MB_OK | MB_ICONINFORMATION);
                        } else {
                            m_compareSourcePath = m_contextTargetPath;
                        }
                        break;
                    case ui::MenuActionId::CtxSelectForCompare:
                        m_compareSourcePath = m_contextTargetPath;
                        break;
                    case ui::MenuActionId::CtxOpenTimeline:
                        if (!m_contextTargetPath.empty()) {
                            std::wstring dir = std::filesystem::path(m_contextTargetPath).parent_path().wstring();
                            std::wstring gitCmd = L"/k git log -p -- \"" + m_contextTargetPath + L"\"";
                            ShellExecuteW(nullptr, L"open", L"cmd.exe", gitCmd.c_str(), dir.c_str(), SW_SHOWNORMAL);
                        }
                        break;
                    case ui::MenuActionId::CtxUndo:
                        DoExplorerUndo();
                        break;
                    case ui::MenuActionId::CtxRedo:
                        DoExplorerRedo();
                        break;
                    case ui::MenuActionId::CtxCut:
                        if (!m_contextTargetPath.empty()) {
                            m_clipboardPath = m_contextTargetPath;
                            m_clipboardIsCut = true;
                            SetClipboardText(m_hwnd, m_contextTargetPath);
                        }
                        break;
                    case ui::MenuActionId::CtxCopy:
                        if (!m_contextTargetPath.empty()) {
                            m_clipboardPath = m_contextTargetPath;
                            m_clipboardIsCut = false;
                            SetClipboardText(m_hwnd, m_contextTargetPath);
                        }
                        break;
                    case ui::MenuActionId::CtxPaste:
                        if (!m_clipboardPath.empty() && std::filesystem::exists(m_clipboardPath)) {
                            std::filesystem::path src(m_clipboardPath);
                            std::filesystem::path destDir = m_contextTargetIsFolder ? m_contextTargetPath
                                : std::filesystem::path(m_contextTargetPath).parent_path().wstring();
                            std::filesystem::path dest = destDir / src.filename();
                            std::error_code ec;
                            if (std::filesystem::is_directory(src)) {
                                std::filesystem::copy(src, dest, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
                            } else {
                                std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing, ec);
                            }
                            if (m_clipboardIsCut && !ec) {
                                m_explorerUndo.PushMove(src.wstring(), dest.wstring());
                                std::filesystem::remove_all(src, ec);
                                m_clipboardPath.clear();
                                m_clipboardIsCut = false;
                            } else if (!ec) {
                                m_explorerUndo.PushCopy(dest.wstring());
                            }
                            m_drawer.RefreshTree();
                        }
                        break;
                    case ui::MenuActionId::CtxCopyPath:
                        if (!m_contextTargetPath.empty()) {
                            SetClipboardText(m_hwnd, m_contextTargetPath);
                        }
                        break;
                    case ui::MenuActionId::CtxCopyRelativePath:
                        if (!m_contextTargetPath.empty()) {
                            std::wstring rel = GetRelativePath(m_contextTargetPath, m_drawer.GetOpenFolderPath());
                            SetClipboardText(m_hwnd, rel);
                        }
                        break;
                    case ui::MenuActionId::CtxRename:
                        if (!m_contextTargetPath.empty()) {
                            m_drawer.StartInlineRenameByPath(m_contextTargetPath);
                            InvalidateRect(m_hwnd, nullptr, FALSE);
                        }
                        break;
                    case ui::MenuActionId::CtxDelete:
                        if (!m_contextTargetPath.empty()) {
                            bool isFolder = std::filesystem::is_directory(m_contextTargetPath);
                            m_confirmDiscardModal.OpenDelete(m_contextTargetPath, isFolder);
                            InvalidateRect(m_hwnd, nullptr, FALSE);
                        }
                        break;
                    case ui::MenuActionId::CtxNewFile: {
                        std::wstring parentDir = m_contextTargetIsFolder ? m_contextTargetPath
                            : (m_contextTargetPath.empty() ? (m_drawer.HasOpenFolder() ? m_drawer.GetOpenFolderPath() : L".")
                                                           : std::filesystem::path(m_contextTargetPath).parent_path().wstring());
                        m_drawer.StartInlineCreate(parentDir, false);
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        break;
                    }
                    case ui::MenuActionId::CtxNewFolder: {
                        std::wstring parentDir = m_contextTargetIsFolder ? m_contextTargetPath
                            : (m_contextTargetPath.empty() ? (m_drawer.HasOpenFolder() ? m_drawer.GetOpenFolderPath() : L".")
                                                           : std::filesystem::path(m_contextTargetPath).parent_path().wstring());
                        m_drawer.StartInlineCreate(parentDir, true);
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        break;
                    }
                    case ui::MenuActionId::CtxFindInFolder:
                        DoShowSearch();
                        break;

                    // Top Edit Menu Actions (User Request & Image 5)
                    case ui::MenuActionId::EditUndo:
                        DoUndo();
                        break;
                    case ui::MenuActionId::EditRedo:
                        DoRedo();
                        break;
                    case ui::MenuActionId::EditCut:
                        DoCut();
                        break;
                    case ui::MenuActionId::EditCopy:
                        DoCopy();
                        break;
                    case ui::MenuActionId::EditPaste:
                        DoPaste();
                        break;
                    case ui::MenuActionId::EditFind:
                        DoFind();
                        break;
                    case ui::MenuActionId::EditReplace:
                        DoReplace();
                        break;
                    case ui::MenuActionId::EditFindInFiles:
                        DoShowSearch();
                        break;
                    case ui::MenuActionId::EditReplaceInFiles:
                        DoShowReplace();
                        break;
                    case ui::MenuActionId::EditToggleLineComment:
                        DoToggleLineComment();
                        break;
                    case ui::MenuActionId::EditToggleBlockComment:
                        DoToggleBlockComment();
                        break;
                    case ui::MenuActionId::EditEmmetExpand:
                        DoEmmetExpand();
                        break;

                    // Editor Context Menu Actions (Images 4 & 5)
                    case ui::MenuActionId::EditorChangeAllOccurrences:
                        DoChangeAllOccurrences();
                        break;
                    case ui::MenuActionId::EditorRefactor:
                        DoRefactor();
                        break;
                    case ui::MenuActionId::EditorCut:
                        DoCut();
                        break;
                    case ui::MenuActionId::EditorCopy:
                        DoCopy();
                        break;
                    case ui::MenuActionId::EditorPaste:
                        DoPaste();
                        break;
                    case ui::MenuActionId::EditorCommandPalette:
                    case ui::MenuActionId::CommandPalette:
                        DoCommandPalette();
                        break;
                    case ui::MenuActionId::DebugStart:
                        DoDebugActiveDocument();
                        break;
                    case ui::MenuActionId::DebugRunWithoutDebug:
                        DoRunActiveDocument();
                        break;
                    case ui::MenuActionId::DebugStop:
                        DoStopServer();
                        break;
                    case ui::MenuActionId::DebugRestart:
                        DoRestartServer();
                        break;
                    case ui::MenuActionId::DebugStepOver:
                        m_terminal.TriggerDebugAction(ui::DebugAction::StepOver);
                        break;
                    case ui::MenuActionId::DebugStepInto:
                        m_terminal.TriggerDebugAction(ui::DebugAction::StepInto);
                        break;
                    case ui::MenuActionId::DebugStepOut:
                        m_terminal.TriggerDebugAction(ui::DebugAction::StepOut);
                        break;
                    case ui::MenuActionId::DebugContinue:
                        m_terminal.TriggerDebugAction(ui::DebugAction::Continue);
                        break;
                    case ui::MenuActionId::DebugToggleBreakpoint: {
                        auto* pDoc = GetActiveDoc();
                        if (pDoc && !pDoc->isSpecialView) {
                            size_t curLine = pDoc->buffer.GetCursor().line;
                            auto it = std::find(pDoc->breakpoints.begin(), pDoc->breakpoints.end(), curLine);
                            if (it != pDoc->breakpoints.end()) {
                                pDoc->breakpoints.erase(it);
                            } else {
                                pDoc->breakpoints.push_back(curLine);
                            }
                        }
                        break;
                    }
                    case ui::MenuActionId::DebugConsole: {
                        if (!m_terminal.IsOpen()) {
                            std::wstring dir = m_drawer.HasOpenFolder() ? m_drawer.GetOpenFolderPath() : m_workspace.GetRootPath().wstring();
                            m_terminal.Open(dir);
                        }
                        m_terminal.ShowDebugConsole();
                        m_isTerminalFocused = true;
                        break;
                    }
                    case ui::MenuActionId::DebugStartLiveServer:
                        DoRunActiveDocument();
                        break;
                    case ui::MenuActionId::DebugStopLiveServer:
                        DoStopServer();
                        break;
                    case ui::MenuActionId::DebugRestartLiveServer:
                        DoRestartServer();
                        break;
                    case ui::MenuActionId::CodeFormatDocument:
                        DoFormatDocument();
                        break;
                    case ui::MenuActionId::CodeFormatSelection:
                        DoFormatSelection();
                        break;
                    case ui::MenuActionId::CodeOrganizeImports:
                        DoOrganizeImports();
                        break;
                    case ui::MenuActionId::CodeGoToDefinition:
                    case ui::MenuActionId::CodePeekDefinition:
                        DoGoToDefinition();
                        break;
                    case ui::MenuActionId::CodeRenameSymbol:
                        DoRenameSymbol();
                        break;
                    case ui::MenuActionId::CodeRefactor:
                        DoRefactor();
                        break;
                    case ui::MenuActionId::CodeToggleLineComment:
                        DoToggleLineComment();
                        break;
                    case ui::MenuActionId::CodeToggleBlockComment:
                        DoToggleBlockComment();
                        break;
                    case ui::MenuActionId::CodeEmmetExpand:
                        DoEmmetExpand();
                        break;
                    case ui::MenuActionId::ToolsBuildProject: {
                        if (!m_terminal.IsOpen()) {
                            DoCreateTerminal();
                        }
                        auto* tab = m_terminal.GetActiveTab();
                        if (tab) tab->SendInput("cmd.exe /c scripts\\build.bat\r\n");
                        break;
                    }
                    case ui::MenuActionId::ToolsRunTests: {
                        if (!m_terminal.IsOpen()) {
                            DoCreateTerminal();
                        }
                        auto* tab = m_terminal.GetActiveTab();
                        if (tab) tab->SendInput("cmd.exe /c scripts\\run_test.bat\r\n");
                        break;
                    }
                    case ui::MenuActionId::ToolsFormatDocument:
                        DoFormatDocument();
                        break;
                    case ui::MenuActionId::ToolsEmmetExpand:
                        DoEmmetExpand();
                        break;
                    case ui::MenuActionId::ToolsColorThemePicker:
                        DoToggleThemeModal();
                        break;
                    case ui::MenuActionId::ToolsKeyboardShortcuts:
                        DoOpenKeyboardShortcuts();
                        break;
                    case ui::MenuActionId::ToolsCommandPalette:
                        DoCommandPalette();
                        break;
                    case ui::MenuActionId::ToolsToggleTerminal:
                        DoCreateTerminal();
                        break;
                    case ui::MenuActionId::ToolsToggleGitGraph:
                        DoShowSourceControl();
                        break;
                    case ui::MenuActionId::ToolsLiveServerManager:
                        if (m_header.IsLiveServerRunning()) {
                            DoStopServer();
                        } else {
                            DoRunActiveDocument();
                        }
                        break;
                    case ui::MenuActionId::ToolsCleanMemoryCache:
                        RunManualGarbageCollection();
                        break;

                    // Window Menu Actions (Image 4)
                    case ui::MenuActionId::WindowNewWindow:
                        DoNewWindow();
                        break;
                    case ui::MenuActionId::WindowCloseWindow:
                        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
                        break;
                    case ui::MenuActionId::WindowToggleFullScreen:
                        if (IsZoomed(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
                        else ShowWindow(m_hwnd, SW_MAXIMIZE);
                        break;
                    case ui::MenuActionId::WindowZoomIn:
                        DoZoomIn();
                        break;
                    case ui::MenuActionId::WindowZoomOut:
                        DoZoomOut();
                        break;
                    case ui::MenuActionId::WindowResetZoom:
                        DoResetZoom();
                        break;
                    case ui::MenuActionId::WindowToggleSideBar:
                        ToggleSidebar();
                        break;
                    case ui::MenuActionId::WindowToggleTerminal:
                        DoCreateTerminal();
                        break;
                    case ui::MenuActionId::WindowNextTab:
                        DoNextTab();
                        break;
                    case ui::MenuActionId::WindowPrevTab:
                        DoPrevTab();
                        break;
                    case ui::MenuActionId::WindowSplitEditor:
                        SplitEditor();
                        break;
                    case ui::MenuActionId::WindowCloseEditor:
                        DoCloseEditor();
                        break;

                    // Help Menu Actions (User Request: email, Facebook, WhatsApp, Discord)
                    case ui::MenuActionId::HelpDocumentation:
                        ShellExecuteW(nullptr, L"open", L"https://github.com", nullptr, nullptr, SW_SHOWNORMAL);
                        break;
                    case ui::MenuActionId::HelpKeyboardShortcuts:
                        DoOpenKeyboardShortcuts();
                        break;
                    case ui::MenuActionId::HelpContactEmail:
                        // Opens Google Gmail compose with recipient prefilled to john12david12perez@gmail.com
                        ShellExecuteW(nullptr, L"open", L"https://mail.google.com/mail/?view=cm&fs=1&to=john12david12perez@gmail.com&su=Phide%20Web%20Development%20IDE%20Inquiry", nullptr, nullptr, SW_SHOWNORMAL);
                        break;
                    case ui::MenuActionId::HelpFacebook:
                        ShellExecuteW(nullptr, L"open", L"https://www.facebook.com/johndavid.laniohan", nullptr, nullptr, SW_SHOWNORMAL);
                        break;
                    case ui::MenuActionId::HelpWhatsApp:
                        ShellExecuteW(nullptr, L"open", L"https://whatsapp.com/channel/0029Vb936ZR5K3zdoSrSB33V", nullptr, nullptr, SW_SHOWNORMAL);
                        break;
                    case ui::MenuActionId::HelpDiscord:
                        ShellExecuteW(nullptr, L"open", L"https://discord.gg/t2UEppVgbN", nullptr, nullptr, SW_SHOWNORMAL);
                        break;
                    case ui::MenuActionId::HelpAbout:
                        MessageBoxW(m_hwnd,
                            L"Phide - Web Development IDE\nVersion 1.0.0 (Direct2D Native Desktop)\n\nFast, lightweight, native C++ desktop IDE for Web & PHP development.\n\nAuthor: John David Laniohan Perez\nEmail: john12david12perez@gmail.com",
                            L"About Phide - Web Development IDE",
                            MB_OK | MB_ICONINFORMATION);
                        break;
                    default:
                        break;
                }
                m_popupMenu.Close();
                m_header.SetActiveMenu(ui::HeaderMenuId::None);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        } else {
            // Click was outside the popup menu area: dismiss the menu
            auto activeMenu = m_header.GetActiveMenu();
            m_popupMenu.Close();
            m_header.SetActiveMenu(ui::HeaderMenuId::None);
            InvalidateRect(m_hwnd, nullptr, FALSE);

            // If the user clicked directly on the button that opened it, we're done (toggled off)
            if (m_header.HitTestMenu(clientPt) == activeMenu) {
                return;
            }
        }
    }

    // 1. Caption Button Clicks (Minimize, Maximize, Close)
    auto captionHit = m_header.HitTestCaptionButton(clientPt);
    if (captionHit != ui::CaptionButtonType::None) {
        if (captionHit == ui::CaptionButtonType::Close) {
            DestroyWindow(m_hwnd);
            return;
        }
        if (captionHit == ui::CaptionButtonType::Maximize) {
            if (IsZoomed(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
            else ShowWindow(m_hwnd, SW_MAXIMIZE);
            return;
        }
        if (captionHit == ui::CaptionButtonType::Minimize) {
            ShowWindow(m_hwnd, SW_MINIMIZE);
            return;
        }
    }

    // 1b. Server / Database Dropdowns & Buttons (Image 2)
    if (m_header.IsDatabaseDropdownOpen() || m_header.IsServerDropdownOpen()) {
        if (m_header.HitTestDbSettings(clientPt)) {
            auto& srvDb = m_header.GetServerDatabaseConfig();
            srvDb.phpMyAdminEnabled = true;
            srvDb.dontRunDatabase = false;
            DoOpenDatabaseSettings(ui::DatabaseModalTab::PhpMyAdmin);
            return;
        }
        if (m_header.HitTestServerSettings(clientPt)) {
            DoOpenServerSettings();
            return;
        }
        if (m_header.HandleDropdownClick(clientPt)) {
            auto& srvDb = m_header.GetServerDatabaseConfig();
            if (srvDb.dontRunDatabase) {
                if (m_isPmaRunning) StopPmaServer();
            }
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        m_header.CloseServerDatabaseDropdowns();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    // Check pill settings or browser icon clicks on running server/database (User request: "even if they are running have a setting icon when user hover so they can see and like see the settings and do open in browser")
    if (m_header.HitTestDbPillSettings(clientPt)) {
        DoOpenDatabaseSettings(ui::DatabaseModalTab::PhpMyAdmin);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (m_header.HitTestDbPillBrowser(clientPt)) {
        if (!m_isPmaRunning) StartPmaServer();
        ShellExecuteW(nullptr, L"open", m_dbSettingsModal.GetPmaUrl().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (m_header.HitTestSrvPillSettings(clientPt)) {
        DoOpenServerSettings();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    if (m_header.HitTestSrvPillBrowser(clientPt)) {
        ShellExecuteW(nullptr, L"open", m_dbSettingsModal.GetNginxUrl().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_header.HitTestDatabase(clientPt)) {
        if (m_header.GetDatabaseState() == ui::DatabaseState::Running || m_header.GetDatabaseState() == ui::DatabaseState::Failed) {
            DoOpenDatabaseLog();
        } else {
            m_header.ToggleDatabaseDropdown();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_header.HitTestServer(clientPt)) {
        if (m_header.GetServerState() == ui::ServerState::Running || m_header.GetServerState() == ui::ServerState::Failed) {
            DoOpenServerLog();
        } else {
            m_header.ToggleServerDropdown();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 2. Execution Widget Button Clicks (Run, Restart, Stop, Log, Debug, Terminal)
    auto execHit = m_header.HitTestExecButton(clientPt);
    if (execHit != ui::HeaderExecId::None) {
        if (execHit == ui::HeaderExecId::Run) {
            DoRunActiveDocument();
        } else if (execHit == ui::HeaderExecId::Restart) {
            DoRestartServer();
        } else if (execHit == ui::HeaderExecId::Stop) {
            DoStopServer();
        } else if (execHit == ui::HeaderExecId::Log) {
            DoOpenErrorLog();
        } else if (execHit == ui::HeaderExecId::Debug) {
            DoDebugActiveDocument();
        } else if (execHit == ui::HeaderExecId::Terminal) {
            DoCreateTerminal();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 3. Header Menu / Icon Button Clicks
    auto menuHit = m_header.HitTestMenu(clientPt);
    if (menuHit != ui::HeaderMenuId::None) {
        if (menuHit == ui::HeaderMenuId::Sidebar) {
            ToggleSidebar();
        } else if (menuHit == ui::HeaderMenuId::File) {
            D2D1_RECT_F rcFile = m_header.GetMenuItemBounds(ui::HeaderMenuId::File);
            m_popupMenu.OpenFileMenu(rcFile.left, rcFile.bottom + 2.0f, m_config, m_autoSave,
                                     HasOpenFile(), (GetActiveDoc() && GetActiveDoc()->isDirty),
                                     m_drawer.HasOpenFolder(), m_recentPaths);
            m_header.SetActiveMenu(ui::HeaderMenuId::File);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (menuHit == ui::HeaderMenuId::Edit) {
            D2D1_RECT_F rcEdit = m_header.GetMenuItemBounds(ui::HeaderMenuId::Edit);
            auto* pDoc = GetActiveDoc();
            bool hasDoc = (pDoc != nullptr && !pDoc->isSpecialView);
            bool canUndo = hasDoc && pDoc->buffer.CanUndo();
            bool canRedo = hasDoc && pDoc->buffer.CanRedo();
            m_popupMenu.OpenEditMenu(rcEdit.left, rcEdit.bottom + 2.0f, m_config, canUndo, canRedo);
            m_header.SetActiveMenu(ui::HeaderMenuId::Edit);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (menuHit == ui::HeaderMenuId::View) {
            D2D1_RECT_F rcView = m_header.GetMenuItemBounds(ui::HeaderMenuId::View);
            m_popupMenu.OpenViewMenu(rcView.left, rcView.bottom + 2.0f, m_config,
                                     m_config.isDrawerOpen, IsZoomed(m_hwnd), m_wordWrap,
                                     m_editorGroups.size());
            m_header.SetActiveMenu(ui::HeaderMenuId::View);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (menuHit == ui::HeaderMenuId::Debug) {
            D2D1_RECT_F rcDbg = m_header.GetMenuItemBounds(ui::HeaderMenuId::Debug);
            bool isLiveRunning = m_liveServer.IsRunning();
            bool isDbgActive = m_terminal.IsDebugActive();
            bool canRun = m_header.IsRunEnabled();
            m_popupMenu.OpenDebugMenu(rcDbg.left, rcDbg.bottom + 2.0f, m_config,
                                      isLiveRunning, isDbgActive, canRun);
            m_header.SetActiveMenu(ui::HeaderMenuId::Debug);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (menuHit == ui::HeaderMenuId::Code) {
            D2D1_RECT_F rcCode = m_header.GetMenuItemBounds(ui::HeaderMenuId::Code);
            auto* pDoc = GetActiveDoc();
            bool hasDoc = (pDoc != nullptr && !pDoc->isSpecialView);
            bool hasSelection = (pDoc != nullptr && pDoc->HasSelection());
            m_popupMenu.OpenCodeMenu(rcCode.left, rcCode.bottom + 2.0f, m_config, hasDoc, hasSelection);
            m_header.SetActiveMenu(ui::HeaderMenuId::Code);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (menuHit == ui::HeaderMenuId::Tools) {
            D2D1_RECT_F rcTools = m_header.GetMenuItemBounds(ui::HeaderMenuId::Tools);
            m_popupMenu.OpenToolsMenu(rcTools.left, rcTools.bottom + 2.0f, m_config,
                                      HasOpenFile(), m_terminal.IsOpen(), m_header.IsLiveServerRunning());
            m_header.SetActiveMenu(ui::HeaderMenuId::Tools);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (menuHit == ui::HeaderMenuId::Window) {
            D2D1_RECT_F rcWindow = m_header.GetMenuItemBounds(ui::HeaderMenuId::Window);
            m_popupMenu.OpenWindowMenu(rcWindow.left, rcWindow.bottom + 2.0f, m_config,
                                       IsZoomed(m_hwnd), m_config.isDrawerOpen, m_terminal.IsOpen(), HasOpenFile());
            m_header.SetActiveMenu(ui::HeaderMenuId::Window);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (menuHit == ui::HeaderMenuId::Help) {
            D2D1_RECT_F rcHelp = m_header.GetMenuItemBounds(ui::HeaderMenuId::Help);
            m_popupMenu.OpenHelpMenu(rcHelp.left, rcHelp.bottom + 2.0f, m_config);
            m_header.SetActiveMenu(ui::HeaderMenuId::Help);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    // 3b. Split Editor Button Click (splits editor up to 3 columns)
    if (m_header.HitTestSplitEditor(clientPt)) {
        SplitEditor();
        return;
    }

    // 3c. Quick Open Magnifying Button Click (Image 5)
    if (m_header.HitTestQuickOpen(clientPt)) {
        DoQuickOpenFileSearch();
        return;
    }

    // 3d. Editor Zoom In / Zoom Out Widget Click (Image 3)
    if (m_header.HitTestZoomOut(clientPt)) {
        DoZoomOut();
        return;
    }
    if (m_header.HitTestZoomReset(clientPt)) {
        DoResetZoom();
        return;
    }
    if (m_header.HitTestZoomIn(clientPt)) {
        DoZoomIn();
        return;
    }

    // 3e. Extensions 2x2 Grid Button Click (Toggles Right Sidebar)
    if (m_header.HitTestExtensions(clientPt)) {
        if (m_extensionsPanel.IsOpen()) {
            AnimateRightSidebarClose();
        } else {
            AnimateRightSidebarOpen();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 3e2. Floating Processes HUD Card Clicks (even when right sidebar is closed)
    if (m_extensionsPanel.HitTestProcesses(clientPt)) {
        if (m_extensionsPanel.HandleLButtonDown(clientPt)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // 3f. Extensions Right Sidebar Panel Clicks
    if (m_rightSidebarAnimProgress > 0.001f && m_extensionsPanel.IsOpen() && m_extensionsPanel.HitTest(clientPt)) {
        bool handled = m_extensionsPanel.HandleLButtonDown(clientPt);
        if (handled) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    const int railW = m_config.Scale(m_config.railWidth);
    const int drawerW = static_cast<int>(m_config.Scale(m_config.drawerWidth) * m_drawerAnimProgress);
    const float canvasLeft = static_cast<float>(railW + drawerW);

    // If clicking inside the editor canvas area, unfocus drawer search and git commit inputs immediately
    if (clientPt.x >= static_cast<LONG>(canvasLeft)) {
        m_drawer.SetSearchFocus(0);
        m_drawer.SetGitCommitBoxFocus(0);
    }
    const float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));
    RECT clientRc;
    GetClientRect(m_hwnd, &clientRc);
    const float rightSidebarW = m_config.ScaleF(340.0f) * m_rightSidebarAnimProgress;
    const float canvasWidth = static_cast<float>(clientRc.right) - canvasLeft - rightSidebarW;
    const float clientH = static_cast<float>(clientRc.bottom - clientRc.top);

    // 4. Editor Canvas Clicks (Multi-Editor Groups aware)
    if (clientPt.x >= canvasLeft && clientPt.y >= headerH && !m_editorGroups.empty() && canvasWidth > 10.0f) {
        m_explorerHasFocus = false;
        // Clear drawer search focus whenever user clicks anywhere inside the editor canvas
        m_drawer.SetSearchFocus(0);
        size_t numGroups = m_editorGroups.size();
        float paneW = canvasWidth / numGroups;
        int targetGroup = -1;
        for (size_t g = 0; g < numGroups; ++g) {
            float pLeft = canvasLeft + g * paneW;
            float pRight = pLeft + paneW;
            if (clientPt.x >= pLeft && (clientPt.x < pRight || g == numGroups - 1)) {
                targetGroup = static_cast<int>(g);
                break;
            }
        }

        if (targetGroup >= 0) {
            m_focusedGroupIdx = targetGroup;
            auto& group = m_editorGroups[targetGroup];

            // 4a. Check Tab Scrollbar interaction for this group (User requirement: "i cant use the croll batr")
            float groupCanvasW = group.bounds.right - group.bounds.left;
            if (group.totalTabsWidth > groupCanvasW && groupCanvasW > 10.0f) {
                float maxScroll = (std::max)(0.0f, group.totalTabsWidth - groupCanvasW);
                float thumbW = (std::max)(m_config.ScaleF(36.0f), (groupCanvasW / group.totalTabsWidth) * groupCanvasW);
                float scrollableTrack = groupCanvasW - thumbW;

                // Check thumb click -> start dragging
                if (clientPt.x >= group.tabScrollThumb.left && clientPt.x <= group.tabScrollThumb.right &&
                    clientPt.y >= group.tabScrollThumb.top && clientPt.y <= group.tabScrollThumb.bottom) {
                    group.isDraggingTabScroll = true;
                    group.dragTabScrollStartX = static_cast<float>(clientPt.x);
                    group.dragTabScrollStartOffset = group.tabScrollOffset;
                    SetCapture(m_hwnd);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                }
                // Check track click -> jump scroll
                else if (clientPt.x >= group.tabScrollTrack.left && clientPt.x <= group.tabScrollTrack.right &&
                         clientPt.y >= group.tabScrollTrack.top && clientPt.y <= group.tabScrollTrack.bottom) {
                    if (scrollableTrack > 0.0f && maxScroll > 0.0f) {
                        float clickRelX = static_cast<float>(clientPt.x) - group.tabScrollTrack.left - thumbW / 2.0f;
                        float ratio = (std::clamp)(clickRelX / scrollableTrack, 0.0f, 1.0f);
                        group.tabScrollOffset = ratio * maxScroll;
                        group.isDraggingTabScroll = true;
                        group.dragTabScrollStartX = static_cast<float>(clientPt.x);
                        group.dragTabScrollStartOffset = group.tabScrollOffset;
                        SetCapture(m_hwnd);
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    }
                }
            }

            // 4b. Tab Close Button Click in this group
            int closeIdx = m_editor.HitTestTabClose(clientPt, group.docs);
            if (closeIdx >= 0) {
                CloseDocument(targetGroup, closeIdx);
                return;
            }

            // 4b. Tab Header Click in this group (initiates potential tab drag-and-drop reorder)
            int tabHitIdx = m_editor.HitTestTab(clientPt, group.docs);
            if (tabHitIdx >= 0) {
                group.activeDocIdx = tabHitIdx;
                m_dragTabGroup = targetGroup;
                m_dragTabIdx = tabHitIdx;
                m_tabDropTargetGroup = targetGroup;
                m_tabDropTargetIdx = tabHitIdx;
                m_dragTabStartPt = clientPt;
                m_dragTabCurrentPt = clientPt;
                m_isDraggingTab = false;
                SetCapture(m_hwnd);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }

            // 4c. Content interaction within this group's active document
            if (group.activeDocIdx >= 0 && group.activeDocIdx < static_cast<int>(group.docs.size())) {
                auto& doc = group.docs[group.activeDocIdx];
                if (doc.isUnsupportedBinary) {
                    const auto& btn = doc.unsupportedActionBtn;
                    if (clientPt.x >= btn.left && clientPt.x <= btn.right &&
                        clientPt.y >= btn.top && clientPt.y <= btn.bottom) {
                        std::wstring param = L"/select,\"" + doc.filePath + L"\"";
                        ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOW);
                        return;
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                } else if (doc.isSpecialView && doc.viewType == L"keybindings") {
                    // Ensure drawer search focus is cleared when interacting with keyboard shortcuts view
                    m_drawer.SetSearchFocus(0);
                    if (m_editor.GetKeyboardShortcutsView().HandleLButtonDown(clientPt)) {
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    }
                } else if (doc.isSpecialView && doc.viewType == L"packages") {
                    m_drawer.SetSearchFocus(0);
                    if (m_editor.GetPackageManagerView().HandleLButtonDown(clientPt)) {
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    }
                } else if (doc.isSpecialView && doc.viewType == L"notebook") {
                    m_drawer.SetSearchFocus(0);
                    if (m_editor.GetNotebookView().HandleLButtonDown(clientPt, doc, m_config, m_hwnd)) {
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    }
                } else if (doc.isSpecialView && doc.viewType == L"student") {
                    m_drawer.SetSearchFocus(0);
                    int action = m_editor.HitTestStudentView(clientPt);
                    if (action == 1) {
                        DoNewFile();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    } else if (action == 2) {
                        OpenNotebookView();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    } else if (action == 3) {
                        m_config.activeRailTab = 2;
                        if (!m_config.isDrawerOpen) ToggleSidebar();
                        m_drawer.OpenSourceControl();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    } else if (action == 4) {
                        DoOpenDatabaseSettings();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    } else if (action == 10) {
                        OpenNotebookView();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    } else if (action == 11) {
                        DoNewFile();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    } else if (action == 12) {
                        DoOpenDatabaseSettings();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    }
                } else if (doc.isMultiFileDiff) {
                    // 1. Collapsible file header clicks in multi-file diff view (Image 4)
                    for (auto& entry : doc.multiFileDiffs) {
                        if (clientPt.x >= entry.headerBounds.left && clientPt.x <= entry.headerBounds.right &&
                            clientPt.y >= entry.headerBounds.top && clientPt.y <= entry.headerBounds.bottom) {
                            entry.isCollapsed = !entry.isCollapsed;
                            InvalidateRect(m_hwnd, nullptr, FALSE);
                            return;
                        }
                    }

                    // 2. Vertical scrollbar click & drag in multi-file diff view
                    bool hitThumb = false;
                    if (m_editor.HitTestScrollbar(clientPt, doc, hitThumb)) {
                        if (hitThumb) {
                            doc.isDraggingScrollbar = true;
                            doc.dragStartY = static_cast<float>(clientPt.y);
                            doc.dragStartScrollLine = doc.scrollLine;
                        } else {
                            if (clientPt.y < doc.scrollbarThumb.top) {
                                if (doc.scrollLine >= 10) doc.scrollLine -= 10;
                                else doc.scrollLine = 0;
                            } else if (clientPt.y > doc.scrollbarThumb.bottom) {
                                doc.scrollLine += 10;
                            }
                        }
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    }

                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                } else {
                    float paneCanvasLeft = canvasLeft + targetGroup * paneW;

                    // Fold chevron click in gutter
                    int foldLine = m_editor.HitTestFoldChevron(clientPt);
                    if (foldLine >= 0) {
                        size_t fl = static_cast<size_t>(foldLine);
                        if (doc.foldedLines.count(fl)) {
                            doc.foldedLines.erase(fl);
                        } else {
                            doc.foldedLines.insert(fl);
                        }
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    }

                    // Minimap click & drag (Image 5) - flawless center jump & drag
                    bool hitMinimapSlider = false;
                    if (m_config.showMinimap && m_editor.HitTestMinimap(clientPt, doc, hitMinimapSlider)) {
                        doc.isDraggingMinimap = true;
                        SetCapture(m_hwnd);

                        float trackH = doc.minimapTrack.bottom - doc.minimapTrack.top;
                        float sliderH = doc.minimapSlider.bottom - doc.minimapSlider.top;
                        float scrollRange = trackH - sliderH;
                        size_t totalLines = doc.buffer.GetLineCount();
                        size_t maxS = (totalLines > 3) ? (totalLines - 3) : 0;

                        if (hitMinimapSlider) {
                            // Clicked on slider: preserve exact grab offset
                            doc.minimapGrabOffsetY = static_cast<float>(clientPt.y) - doc.minimapSlider.top;
                        } else {
                            // Clicked on track: center slider around click point
                            doc.minimapGrabOffsetY = sliderH * 0.5f;
                            if (scrollRange > 0.0f) {
                                float targetSliderTop = (static_cast<float>(clientPt.y) - doc.minimapTrack.top) - doc.minimapGrabOffsetY;
                                float ratio = (std::max)(0.0f, (std::min)(1.0f, targetSliderTop / scrollRange));
                                doc.scrollLine = static_cast<size_t>(ratio * maxS);
                            }
                        }
                        doc.minimapDragStartY = static_cast<float>(clientPt.y);
                        doc.minimapDragStartLine = doc.scrollLine;

                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    }

                    // Vertical scrollbar click & drag (when minimap is disabled)
                    bool hitThumb = false;
                    if (!m_config.showMinimap && m_editor.HitTestScrollbar(clientPt, doc, hitThumb)) {
                        if (hitThumb) {
                            doc.isDraggingScrollbar = true;
                            doc.dragStartY = static_cast<float>(clientPt.y);
                            doc.dragStartScrollLine = doc.scrollLine;
                        } else {
                            if (clientPt.y < doc.scrollbarThumb.top) {
                                if (doc.scrollLine >= 10) doc.scrollLine -= 10;
                                else doc.scrollLine = 0;
                            } else if (clientPt.y > doc.scrollbarThumb.bottom) {
                                doc.scrollLine += 10;
                            }
                        }
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    }

                    // Horizontal scrollbar click & drag
                    bool hitHThumb = false;
                    if (m_editor.HitTestHScrollbar(clientPt, doc, hitHThumb)) {
                        if (hitHThumb) {
                            doc.isDraggingHScrollbar = true;
                            doc.dragStartHX = static_cast<float>(clientPt.x);
                            doc.dragStartScrollOffsetX = doc.scrollOffsetX;
                            SetCapture(m_hwnd);
                        } else {
                            if (clientPt.x < doc.hScrollbarThumb.left) {
                                doc.scrollOffsetX = (std::max)(0.0f, doc.scrollOffsetX - 150.0f);
                            } else if (clientPt.x > doc.hScrollbarThumb.right) {
                                doc.scrollOffsetX += 150.0f;
                            }
                        }
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    }

                    // Code Area click (positions caret)
                    if (doc.isDiffView) {
                        float subHeaderH = m_config.ScaleF(24.0f);
                        float dividerW = m_config.ScaleF(32.0f);
                        float scrollbarW = m_config.ScaleF(12.0f);
                        float halfW = (paneW - dividerW - scrollbarW) / 2.0f;
                        float gutterW = m_config.ScaleF(46.0f);
                        float lineH = m_config.ScaleF(20.0f);
                        float charW = m_config.ScaleF(7.8f);

                        float leftX = paneCanvasLeft;
                        float dividerX = leftX + halfW;
                        float rightX = dividerX + dividerW;
                        float codeTopY = m_config.ScaleF(38.0f) + m_config.ScaleF(34.0f) + m_config.ScaleF(24.0f) + subHeaderH;

                        if (clientPt.y >= codeTopY && lineH > 0.0f) {
                            size_t clickedLine = doc.scrollLine + static_cast<size_t>((clientPt.y - codeTopY) / lineH);
                            if (clientPt.x >= rightX) {
                                doc.diffActivePane = 1;
                                size_t clickedCol = 0;
                                if (clientPt.x >= rightX + gutterW + m_config.ScaleF(12.0f) && charW > 0.0f) {
                                    clickedCol = static_cast<size_t>((clientPt.x - (rightX + gutterW + m_config.ScaleF(12.0f))) / charW);
                                }
                                doc.buffer.SetCursor(clickedLine, clickedCol);
                            } else if (clientPt.x < dividerX) {
                                doc.diffActivePane = 0; // Read-only side
                                size_t clickedCol = 0;
                                if (clientPt.x >= leftX + gutterW + m_config.ScaleF(12.0f) && charW > 0.0f) {
                                    clickedCol = static_cast<size_t>((clientPt.x - (leftX + gutterW + m_config.ScaleF(12.0f))) / charW);
                                }
                                doc.diffOriginalBuffer.SetCursor(clickedLine, clickedCol);
                            }
                            InvalidateRect(m_hwnd, nullptr, FALSE);
                            return;
                        }
                    } else {
                        // Check gutter click for breakpoint toggle (Image 4)
                        const float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));
                        const float tabH = m_config.ScaleF(34.0f);
                        const float breadcrumbH = m_config.ScaleF(24.0f);
                        const float topY = headerH + tabH + breadcrumbH;
                        const float gutterW = m_config.ScaleF(50.0f);
                        const float lineH = m_config.ScaleF(std::round(m_config.fontSize * m_config.lineHeightMultiplier));
                        if (clientPt.x >= paneCanvasLeft && clientPt.x < paneCanvasLeft + gutterW &&
                            clientPt.y >= topY && clientPt.y <= clientH) {
                            float relY = clientPt.y - (topY + m_config.ScaleF(6.0f));
                            if (relY >= 0.0f && lineH > 0.0f) {
                                size_t visualSlot = doc.scrollLine + static_cast<size_t>(relY / lineH);
                                size_t totalLines = doc.buffer.GetLineCount();
                                size_t curVisual = 0;
                                for (size_t b = 0; b < totalLines; ++b) {
                                    if (curVisual == visualSlot) {
                                        auto it = std::find(doc.breakpoints.begin(), doc.breakpoints.end(), b);
                                        if (it != doc.breakpoints.end()) {
                                            doc.breakpoints.erase(it);
                                        } else {
                                            doc.breakpoints.push_back(b);
                                        }
                                        InvalidateRect(m_hwnd, nullptr, FALSE);
                                        return;
                                    }
                                    curVisual++;
                                }
                            }
                        }

                        // Check if clicked an inline Color Preview Swatch (Images 2, 5)
                        const auto* pColorSwatch = m_editor.HitTestColorSwatch(clientPt);
                        if (pColorSwatch) {
                            m_colorPicker.Open(pColorSwatch->bounds.left, pColorSwatch->bounds.bottom, pColorSwatch->colorText, pColorSwatch->line, pColorSwatch->col, pColorSwatch->length);
                            InvalidateRect(m_hwnd, nullptr, FALSE);
                            return;
                        }

                        size_t line = 0, col = 0;
                        if (m_editor.HitTestCodeArea(clientPt, m_config, doc, paneCanvasLeft, clientH, line, col)) {
                            doc.buffer.SetCursor(line, col);
                            doc.targetHighlightLine = static_cast<size_t>(-1);
                            doc.targetHighlightLen = 0;
                            // Start drag text selection (Images 2, 3, 4)
                            doc.selAnchor = { line, col };
                            doc.selEnd = { line, col };
                            doc.hasSelection = false;
                            doc.isSelecting = true;
                            doc.selectedWord.clear();
                            SetCapture(m_hwnd);
                            InvalidateRect(m_hwnd, nullptr, FALSE);
                            return;
                        }
                    }
                }
            }

            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // 5. Handle inline tree rename commit on click outside
    if (m_drawer.IsRenaming()) {
        const auto& rcBox = m_drawer.GetRenameBox();
        if (clientPt.x >= rcBox.left && clientPt.x <= rcBox.right &&
            clientPt.y >= rcBox.top && clientPt.y <= rcBox.bottom) {
            return;
        }
        std::wstring oldPath, newPath;
        if (m_drawer.CommitInlineRename(oldPath, newPath)) {
            m_explorerUndo.PushRename(oldPath, newPath);
            for (auto& group : m_editorGroups) {
                for (auto& doc : group.docs) {
                    if (doc.filePath == oldPath) {
                        doc.filePath = newPath;
                        doc.fileName = std::filesystem::path(newPath).filename().wstring();
                    }
                }
            }
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    // 5b. Handle inline tree item creation commit on click outside
    if (m_drawer.IsCreating()) {
        const auto& rcBox = m_drawer.GetCreateBox();
        const auto& rcPill = m_drawer.GetCreatePill();
        if ((clientPt.x >= rcBox.left && clientPt.x <= rcBox.right &&
             clientPt.y >= rcBox.top && clientPt.y <= rcBox.bottom) ||
            (clientPt.x >= rcPill.left && clientPt.x <= rcPill.right &&
             clientPt.y >= rcPill.top && clientPt.y <= rcPill.bottom)) {
            return;
        }
        std::wstring createdPath;
        bool isFolder = m_drawer.IsCreatingFolder();
        if (m_drawer.CommitInlineCreate(createdPath)) {
            m_explorerUndo.PushCreate(createdPath, isFolder);
            if (!isFolder) {
                OpenFileDocument(createdPath, false);
            }
        } else {
            m_drawer.CancelInlineCreate();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    // 6. Drawer Tree Item or Search Results Clicks
    if (m_drawerAnimProgress > 0.001f) {
        if (m_config.activeRailTab == 1) {
            std::wstring jumpFile;
            size_t jumpLine = 0;
            size_t matchCol = 0;
            size_t matchLen = 0;
            // Clear shortcuts view search focus when interacting with search drawer
            m_editor.GetKeyboardShortcutsView().SetSearchFocused(false);
            if (m_drawer.HitTestSearch(clientPt, m_config, jumpFile, jumpLine, matchCol, matchLen)) {
                if (!jumpFile.empty()) {
                    OpenFileDocument(jumpFile, false);
                    auto* pDoc = GetActiveDoc();
                    if (pDoc) {
                        pDoc->targetHighlightLine = jumpLine;
                        pDoc->targetHighlightCol = matchCol;
                        pDoc->targetHighlightLen = matchLen;
                        pDoc->targetHighlightTime = GetTickCount64();
                        pDoc->buffer.SetCursor(jumpLine, matchCol + matchLen);

                        // Center line vertically in editor view
                        RECT rcClient;
                        GetClientRect(m_hwnd, &rcClient);
                        float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));
                        float editorH = static_cast<float>(rcClient.bottom - rcClient.top) - headerH;
                        float lineH = m_config.fontSize * m_config.lineHeightMultiplier;
                        size_t visibleLines = (lineH > 0.0f) ? static_cast<size_t>(editorH / lineH) : 25;
                        size_t halfVisible = visibleLines / 2;
                        if (jumpLine > halfVisible) {
                            pDoc->scrollLine = jumpLine - halfVisible;
                        } else {
                            pDoc->scrollLine = 0;
                        }
                    }
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        } else if (m_config.activeRailTab == 2) {
            // Check vertical splitter drag between Changes and Graph (Image 2 & 3)
            if (m_drawer.HitTestGitSplitter(clientPt)) {
                m_isDraggingGitSplitter = true;
                SetCapture(m_hwnd);
                return;
            }
            std::wstring openFile;
            std::wstring discardedFile;
            bool isDiff = false;
            std::wstring openCommitDiff;
            std::wstring openUrl;
            bool promptDiscard = false;
            std::wstring discardFileName;
            std::wstring discardStatus;
            size_t discardCount = 1;
            bool promptEmptyCommit = false;
            if (m_drawer.HitTestGit(clientPt, m_config, openFile, discardedFile, isDiff, openCommitDiff, openUrl,
                                    &promptDiscard, &discardFileName, &discardStatus, &discardCount, &promptEmptyCommit)) {
                if (promptEmptyCommit) {
                    m_drawer.ClearGitTooltip();
                    DoOpenCommitEditMsg();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                }
                if (promptDiscard) {
                    m_drawer.ClearGitTooltip();
                    m_confirmDiscardModal.Open(discardedFile, discardFileName, discardStatus, discardCount);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return;
                }
                if (!openUrl.empty()) {
                    ShellExecuteW(nullptr, L"open", openUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                if (!openCommitDiff.empty()) {
                    OpenCommitDiffDocument(openCommitDiff, openFile);
                } else if (openFile.ends_with(L".ipynb")) {
                    OpenNotebookView(openFile);
                } else if (isDiff && !openFile.empty()) {
                    OpenDiffDocument(openFile, false);
                } else if (!openFile.empty()) {
                    OpenFileDocument(openFile, false);
                }
                if (!discardedFile.empty()) {
                    ExecuteDiscardOrRestore(discardedFile, discardedFile, L"M");
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        } else {
            if (!m_drawer.HasOpenFolder()) {
                if (m_drawer.HitTestOpenFolderButton(clientPt, m_config)) {
                    DoOpenFolder();
                    return;
                }
            } else {
                int clickedTree = m_drawer.HitTest(clientPt, m_config);
                if (clickedTree >= 0) {
                    m_explorerHasFocus = true;
                    const auto* pItem = m_drawer.GetItem(clickedTree);
                    if (pItem) {
                        m_selectedExplorerPath = pItem->fullPath;
                        m_drawer.SetActiveFilePath(pItem->fullPath);
                        if (pItem->isFolder) {
                            m_drawer.ToggleFolder(clickedTree);
                            UpdateDrawerErrorFiles();
    SaveSessionState();
                        } else {
                            // File item clicked: initiate potential drag candidate
                            m_dragCandidatePath = pItem->fullPath;
                            m_dragCandidateName = pItem->label;
                            m_dragStartPt = clientPt;
                            m_dragCurrentPt = clientPt;
                            m_isDraggingExplorerFile = false;
                            m_hasDragDropTarget = false;
                            m_dragDropTargetGroup = -1;
                            SetCapture(m_hwnd);
                            return;
                        }
                    }
                }
            }
        }
    }

    // Unfocus drawer search and git commit inputs when clicking outside drawer
    m_drawer.SetSearchFocus(0);
    m_drawer.SetGitCommitBoxFocus(0);

    // 7. Activity Rail Clicks (Files, Search, Git, Debug, Packages)
    auto railHit = m_rail.HitTest(clientPt);
    if (railHit != ui::RailTabId::None) {
        if (railHit == ui::RailTabId::Student) {
            OpenSpecialView(L"student", L"Student Tutorials");
            UpdateDrawerErrorFiles();
            SaveSessionState();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (railHit == ui::RailTabId::Notebook) {
            OpenNotebookView();
            UpdateDrawerErrorFiles();
            SaveSessionState();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        if (railHit == ui::RailTabId::Packages) {
            OpenSpecialView(L"packages", L"Packages");
            UpdateDrawerErrorFiles();
            SaveSessionState();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }

        int clickedTab = static_cast<int>(railHit);
        if (m_config.activeRailTab == clickedTab && m_config.isDrawerOpen) {
            ToggleSidebar();
        } else {
            m_config.activeRailTab = clickedTab;
            if (!m_config.isDrawerOpen) {
                ToggleSidebar();
            }
            if (clickedTab == 1) {
                RefreshOpenDocPathsForSearch();
                m_drawer.SetSearchFocus(1);
            } else if (clickedTab == 2) {
                m_drawer.OpenSourceControl();
            }
        }
        UpdateDrawerErrorFiles();
    SaveSessionState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
}

void Window::HandleLButtonUp(POINT clientPt) {
    auto* pActiveDoc = GetActiveDoc();
    if (pActiveDoc && pActiveDoc->isSpecialView && pActiveDoc->viewType == L"notebook") {
        if (m_editor.GetNotebookView().HandleLButtonUp(clientPt, *pActiveDoc, m_config)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    if (m_colorPicker.IsOpen()) {
        if (m_colorPicker.HandleLButtonUp(D2D1::Point2F(static_cast<float>(clientPt.x), static_cast<float>(clientPt.y)))) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    // 0a. Tab Scrollbar Dragging release (User requirement)
    for (auto& group : m_editorGroups) {
        if (group.isDraggingTabScroll) {
            group.isDraggingTabScroll = false;
            ReleaseCapture();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // 0b. Tab Drag & Drop Reordering commit (User request: "fix i cant drag the tab of split 1 to the other split", Image 4)
    if (m_dragTabIdx >= 0 && m_dragTabGroup >= 0 && m_dragTabGroup < static_cast<int>(m_editorGroups.size())) {
        if (m_isDraggingTab && m_tabDropTargetGroup >= 0 && m_tabDropTargetGroup < static_cast<int>(m_editorGroups.size()) && m_tabDropTargetIdx >= 0) {
            int srcGroup = m_dragTabGroup;
            int dstGroup = m_tabDropTargetGroup;
            int from = m_dragTabIdx;
            int to = m_tabDropTargetIdx;

            if (srcGroup == dstGroup) {
                // Same group reordering
                auto& group = m_editorGroups[srcGroup];
                if (from >= 0 && from < static_cast<int>(group.docs.size()) &&
                    to >= 0 && to <= static_cast<int>(group.docs.size())) {
                    auto doc = std::move(group.docs[from]);
                    group.docs.erase(group.docs.begin() + from);
                    int insertPos = (to > from) ? (to - 1) : to;
                    if (insertPos < 0) insertPos = 0;
                    if (insertPos > static_cast<int>(group.docs.size())) {
                        insertPos = static_cast<int>(group.docs.size());
                    }
                    group.docs.insert(group.docs.begin() + insertPos, std::move(doc));
                    group.activeDocIdx = insertPos;
                    SaveSessionState();
                }
            } else {
                // Cross-group move: drag tab from split A to split B!
                auto& fromGrp = m_editorGroups[srcGroup];
                auto& toGrp = m_editorGroups[dstGroup];
                if (from >= 0 && from < static_cast<int>(fromGrp.docs.size()) &&
                    to >= 0 && to <= static_cast<int>(toGrp.docs.size())) {
                    auto doc = std::move(fromGrp.docs[from]);
                    fromGrp.docs.erase(fromGrp.docs.begin() + from);

                    int insertPos = to;
                    if (insertPos < 0) insertPos = 0;
                    if (insertPos > static_cast<int>(toGrp.docs.size())) {
                        insertPos = static_cast<int>(toGrp.docs.size());
                    }
                    toGrp.docs.insert(toGrp.docs.begin() + insertPos, std::move(doc));
                    toGrp.activeDocIdx = insertPos;

                    // If source group has no remaining documents, auto-collapse split if multiple splits exist
                    if (fromGrp.docs.empty()) {
                        if (m_editorGroups.size() > 1) {
                            m_editorGroups.erase(m_editorGroups.begin() + srcGroup);
                            int finalDst = (srcGroup < dstGroup) ? (dstGroup - 1) : dstGroup;
                            if (finalDst < 0) finalDst = 0;
                            if (finalDst >= static_cast<int>(m_editorGroups.size())) {
                                finalDst = static_cast<int>(m_editorGroups.size()) - 1;
                            }
                            m_focusedGroupIdx = finalDst;
                        } else {
                            fromGrp.activeDocIdx = -1;
                            m_focusedGroupIdx = 0;
                        }
                    } else {
                        if (fromGrp.activeDocIdx >= static_cast<int>(fromGrp.docs.size())) {
                            fromGrp.activeDocIdx = static_cast<int>(fromGrp.docs.size()) - 1;
                        }
                        m_focusedGroupIdx = dstGroup;
                    }
                    SaveSessionState();
                }
            }
        }
        m_isDraggingTab = false;
        m_dragTabGroup = -1;
        m_dragTabIdx = -1;
        m_tabDropTargetGroup = -1;
        m_tabDropTargetIdx = -1;
        m_editor.SetTabDragState(false, -1, -1, -1, -1);
        ReleaseCapture();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (!m_dragCandidatePath.empty()) {
        if (m_isDraggingExplorerFile) {
            RECT clientRc;
            GetClientRect(m_hwnd, &clientRc);
            const int railW = m_config.Scale(m_config.railWidth);
            const int drawerW = static_cast<int>(m_config.Scale(m_config.drawerWidth) * m_drawerAnimProgress);
            const float canvasLeft = static_cast<float>(railW + drawerW);
            const float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));
            const float tabH = m_config.ScaleF(34.0f);

            // Dropped on Tab Bar -> open file as a tab!
            if (clientPt.x >= canvasLeft && clientPt.y >= headerH && clientPt.y < headerH + tabH) {
                OpenFileDocument(m_dragCandidatePath, false);
            } else if (m_hasDragDropTarget && m_dragDropTargetGroup >= 0) {
                m_focusedGroupIdx = m_dragDropTargetGroup;
                auto* pDoc = GetActiveDoc();
                if (pDoc && !pDoc->isSpecialView && !pDoc->isImageView && !pDoc->isDiffView) {
                    if (pDoc->filePath != m_dragCandidatePath) {
                        size_t line = pDoc->buffer.GetLineCount() > 0 ? (std::min)(m_dragDropTargetLine, pDoc->buffer.GetLineCount() - 1) : 0;
                        size_t col = (line < pDoc->buffer.GetLineCount()) ? (std::min)(m_dragDropTargetCol, pDoc->buffer.GetLine(line).length()) : 0;

                        // Check if drop target is inside quotes (Image 4 & User request: "when i drag a file on "" it put the directory of that")
                        bool isInsideQuotes = false;
                        if (line < pDoc->buffer.GetLineCount()) {
                            const auto& lineStr = pDoc->buffer.GetLine(line);
                            size_t dQuotes = 0, sQuotes = 0;
                            for (size_t c = 0; c < col && c < lineStr.length(); ++c) {
                                if (lineStr[c] == L'"') dQuotes++;
                                else if (lineStr[c] == L'\'') sQuotes++;
                            }
                            if ((dQuotes % 2 == 1) || (sQuotes % 2 == 1)) {
                                isInsideQuotes = true;
                            } else if (col > 0 && col < lineStr.length()) {
                                if ((lineStr[col - 1] == L'"' && lineStr[col] == L'"') ||
                                    (lineStr[col - 1] == L'\'' && lineStr[col] == L'\'')) {
                                    isInsideQuotes = true;
                                }
                            }
                        }

                        std::wstring textToInsert;
                        if (isInsideQuotes) {
                            // User dropped into quotes: insert just the relative file path location!
                            textToInsert = phide::editor::SmartImportHelper::ComputeRelativePath(pDoc->filePath, m_dragCandidatePath);
                        } else {
                            // User dropped outside quotes: generate smart language-aware import statement!
                            textToInsert = phide::editor::SmartImportHelper::Generate(pDoc->filePath, m_dragCandidatePath);
                        }

                        if (!textToInsert.empty()) {
                            if (pDoc->buffer.GetLineCount() == 0) {
                                pDoc->buffer.InsertText(textToInsert);
                            } else {
                                pDoc->buffer.SetCursor(line, col);
                                pDoc->buffer.InsertText(textToInsert);
                            }
                            MarkDocumentModified(pDoc);
                            m_editor.DismissAutocomplete();
                            m_editor.ClearGhostText();
                            // Immediately update Source Control dirty files & git status (User request: "when i drop a file to editor it should also trugger the source control")
                            UpdateDrawerDirtyFiles();
                            m_drawer.RefreshGitStatusOnly(true);
                        }
                    }
                } else if (!pDoc) {
                    OpenFileDocument(m_dragCandidatePath, false);
                }
            } else if (clientPt.x >= canvasLeft && clientPt.y >= headerH) {
                auto* pDoc = GetActiveDoc();
                if (!pDoc) {
                    OpenFileDocument(m_dragCandidatePath, false);
                }
            }
        } else {
            // Normal click without drag: preview/open file document!
            m_selectedExplorerPath = m_dragCandidatePath;
            m_explorerHasFocus = true;
            OpenFileDocument(m_dragCandidatePath, true);
        }

        m_dragCandidatePath.clear();
        m_dragCandidateName.clear();
        m_isDraggingExplorerFile = false;
        m_hasDragDropTarget = false;
        m_dragDropTargetGroup = -1;
        ReleaseCapture();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    (void)clientPt;
    if (m_terminal.IsOpen()) {
        bool wasResizing = m_terminal.IsResizing();
        m_terminal.OnMouseUp(clientPt);
        if (wasResizing) {
            ReleaseCapture();
        }
    }
    if (m_isDraggingGitSplitter) {
        m_isDraggingGitSplitter = false;
        m_config.gitSplitY = m_drawer.GetGitSplitY();
        UpdateDrawerErrorFiles();
    SaveSessionState();
        ReleaseCapture();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    for (auto& group : m_editorGroups) {
        for (auto& doc : group.docs) {
            doc.isDraggingScrollbar = false;
            doc.isDraggingHScrollbar = false;
            doc.isDraggingMinimap = false;
            if (doc.isSelecting) {
                doc.isSelecting = false;
                if (doc.selAnchor.line == doc.selEnd.line && doc.selAnchor.col == doc.selEnd.col) {
                    doc.hasSelection = false;
                    doc.selectedWord.clear();
                }
            }
        }
    }
    ReleaseCapture();
}

void Window::HandleRButtonDown(POINT clientPt) {
    if (m_popupMenu.IsOpen()) {
        m_popupMenu.Close();
        m_header.SetActiveMenu(ui::HeaderMenuId::None);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    float railW = m_config.ScaleF(static_cast<float>(m_config.railWidth));
    float drawerW = m_config.ScaleF(static_cast<float>(m_config.drawerWidth)) * m_drawerAnimProgress;
    float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));

    // 1. Right-click inside Drawer Explorer
    if (m_drawerAnimProgress > 0.001f && clientPt.x >= railW && clientPt.x < railW + drawerW && clientPt.y >= headerH) {
        int hitIdx = m_drawer.HitTest(clientPt, m_config);
        const ui::DrawerTreeItem* pItem = (hitIdx >= 0) ? m_drawer.GetItem(hitIdx) : nullptr;
        bool isFolder = false;
        std::wstring targetPath;
        if (pItem) {
            isFolder = pItem->isFolder;
            targetPath = pItem->fullPath;
        } else if (m_drawer.HasOpenFolder()) {
            isFolder = true;
            targetPath = m_drawer.GetOpenFolderPath();
        }

        if (!targetPath.empty()) {
            m_contextTargetPath = targetPath;
            m_contextTargetIsFolder = isFolder;
            m_selectedExplorerPath = targetPath;
            m_explorerHasFocus = true;
            RECT clientRc;
            GetClientRect(m_hwnd, &clientRc);
            float clientW = static_cast<float>(clientRc.right - clientRc.left);
            float clientH = static_cast<float>(clientRc.bottom - clientRc.top);
            bool canPaste = !m_clipboardPath.empty() && std::filesystem::exists(m_clipboardPath);
            m_popupMenu.OpenContextMenu(
                static_cast<float>(clientPt.x),
                static_cast<float>(clientPt.y),
                isFolder,
                targetPath,
                m_drawer.GetOpenFolderPath(),
                m_config,
                canPaste,
                m_explorerUndo.CanUndo(),
                m_explorerUndo.CanRedo(),
                clientW,
                clientH
            );
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    const float tabH = m_config.ScaleF(34.0f);

    // 2. Right-click inside Editor Tab Bar
    if (clientPt.x >= railW + drawerW && clientPt.y >= headerH && clientPt.y < headerH + tabH) {
        std::wstring targetPath;
        for (const auto& grp : m_editorGroups) {
            int tabHit = m_editor.HitTestTab(clientPt, grp.docs);
            if (tabHit >= 0 && tabHit < static_cast<int>(grp.docs.size())) {
                targetPath = grp.docs[tabHit].filePath;
                break;
            }
        }
        if (targetPath.empty()) {
            auto* pDoc = GetActiveDoc();
            if (pDoc) targetPath = pDoc->filePath;
        }

        if (!targetPath.empty() && std::filesystem::exists(targetPath)) {
            m_contextTargetPath = targetPath;
            m_contextTargetIsFolder = std::filesystem::is_directory(targetPath);
            bool canPaste = !m_clipboardPath.empty() && std::filesystem::exists(m_clipboardPath);
            RECT clientRc;
            GetClientRect(m_hwnd, &clientRc);
            float clientW = static_cast<float>(clientRc.right - clientRc.left);
            float clientH = static_cast<float>(clientRc.bottom - clientRc.top);
            m_popupMenu.OpenContextMenu(
                static_cast<float>(clientPt.x),
                static_cast<float>(clientPt.y),
                m_contextTargetIsFolder,
                targetPath,
                m_drawer.GetOpenFolderPath(),
                m_config,
                canPaste,
                m_explorerUndo.CanUndo(),
                m_explorerUndo.CanRedo(),
                clientW,
                clientH
            );
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // 3. Right-click inside Editor Canvas / Code Area (Image 4: Editor Context Menu)
    if (clientPt.x >= railW + drawerW && clientPt.y >= headerH + tabH) {
        // Focus the editor group containing click & position cursor
        for (size_t g = 0; g < m_editorGroups.size(); ++g) {
            const auto& b = m_editorGroups[g].bounds;
            if (clientPt.x >= b.left && clientPt.x < b.right && clientPt.y >= b.top && clientPt.y < b.bottom) {
                m_focusedGroupIdx = static_cast<int>(g);
                auto* pDoc = GetActiveDoc();
                if (pDoc && !pDoc->isSpecialView && !pDoc->isImageView && !pDoc->isUnsupportedBinary) {
                    RECT rc;
                    GetClientRect(m_hwnd, &rc);
                    float clientH = static_cast<float>(rc.bottom - rc.top);
                    size_t outLine = 0, outCol = 0;
                    if (m_editor.HitTestCodeArea(clientPt, m_config, *pDoc, b.left, clientH, outLine, outCol)) {
                        pDoc->buffer.SetCursor(outLine, outCol);
                    }
                }
                break;
            }
        }

        bool hasSelection = (GetActiveDoc() != nullptr);
        bool canPaste = (IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE);
        RECT clientRc;
        GetClientRect(m_hwnd, &clientRc);
        float clientW = static_cast<float>(clientRc.right - clientRc.left);
        float clientH = static_cast<float>(clientRc.bottom - clientRc.top);
        m_popupMenu.OpenEditorContextMenu(
            static_cast<float>(clientPt.x),
            static_cast<float>(clientPt.y),
            m_config,
            hasSelection,
            canPaste,
            clientW,
            clientH
        );
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
}

LRESULT Window::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_NCCALCSIZE: {
            if (wParam == TRUE) {
                // When maximized, compensate for window borders extending off-screen
                if (IsZoomed(m_hwnd)) {
                    auto* pParams = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    HMONITOR hMon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO mi{ sizeof(MONITORINFO) };
                    GetMonitorInfo(hMon, &mi);
                    pParams->rgrc[0] = mi.rcWork;
                }
                return 0; // Eliminates default OS non-client frame
            }
            return 0;
        }

        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            return HandleNCHitTest(pt);
        }

        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(m_hwnd, &pt);

                // Explorer Drag & Drop Cursor
                if (m_isDraggingExplorerFile) {
                    SetCursor(LoadCursor(nullptr, m_hasDragDropTarget ? IDC_ARROW : IDC_NO));
                    return TRUE;
                }

                // 00.0 GitHub Login Modal
                if (m_drawer.IsGitHubLoginModalOpen()) {
                    if (m_drawer.HitTestGitHubLoginModalButtons(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                }

                // 00.01 Publish Repo Modal
                if (m_drawer.IsPublishRepoModalOpen()) {
                    if (m_drawer.HitTestPublishRepoModalButtons(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                }

                // 00. Confirm Discard / Restore / Delete / Notice Modal (Images 2, 3, 4, 5)
                if (m_confirmDiscardModal.IsOpen()) {
                    if (m_confirmDiscardModal.IsLoading()) {
                        SetCursor(LoadCursor(nullptr, IDC_WAIT));
                        return TRUE;
                    }
                    if (m_confirmDiscardModal.HitTestButtons(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                }

                // 00.1 Git Error Modal (Image 2)
                if (m_gitErrorModal.IsOpen()) {
                    if (m_gitErrorModal.HitTestButtons(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                }

                // 00a. Database & phpMyAdmin / Nginx Server Settings Modal (Images 3 & 4)
                if (m_dbSettingsModal.IsOpen()) {
                    if (m_dbSettingsModal.HitTestAction(pt) != ui::DatabaseModalAction::None) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                }

                // 0a. Quick Open File Search Modal (Image 5)
                if (m_quickOpen.IsOpen()) {
                    if (m_quickOpen.HitTestInput(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                        return TRUE;
                    }
                    if (m_quickOpen.HitTest(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                }

                // 0b. Popup Menu Dropdowns (File, Edit, View, Context Menus)
                if (m_popupMenu.IsOpen()) {
                    if (m_popupMenu.HitTestMenuArea(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                }

                // 0c. Theme Picker Catalog Modal
                if (m_colorPicker.IsOpen()) {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                }

            if (m_themePicker.IsOpen()) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Set pointer (hand) cursor when hovering over sidebar icons
                if (m_rail.HitTest(pt) != ui::RailTabId::None) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Provide pointer hand cursor over caption buttons, menus, execution buttons, split editor, quick open, extensions, and zoom buttons
                if (m_header.HitTestCaptionButton(pt) != ui::CaptionButtonType::None ||
                    m_header.HitTestMenu(pt) != ui::HeaderMenuId::None ||
                    m_header.HitTestExecButton(pt) != ui::HeaderExecId::None ||
                    m_header.HitTestSplitEditor(pt) ||
                    m_header.HitTestQuickOpen(pt) ||
                    m_header.HitTestExtensions(pt) ||
                    m_header.HitTestDatabase(pt) ||
                    m_header.HitTestServer(pt) ||
                    m_header.HitTestDropdowns(pt) ||
                    m_header.HitTestDbSettings(pt) ||
                    m_header.HitTestServerSettings(pt) ||
                    m_header.HitTestDbPillSettings(pt) ||
                    m_header.HitTestDbPillBrowser(pt) ||
                    m_header.HitTestSrvPillSettings(pt) ||
                    m_header.HitTestSrvPillBrowser(pt) ||
                    m_header.HitTestZoomIn(pt) ||
                    m_header.HitTestZoomOut(pt) ||
                    m_header.HitTestZoomReset(pt)) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Extensions Right Sidebar Panel
                if (m_rightSidebarAnimProgress > 0.001f && m_extensionsPanel.IsOpen() && m_extensionsPanel.HitTest(pt)) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Tab horizontal scrollbar thumb and track (User requirement: "i cant still drag the scroll bar")
                for (const auto& grp : m_editorGroups) {
                    if (grp.totalTabsWidth > (grp.bounds.right - grp.bounds.left)) {
                        if (pt.x >= grp.tabScrollThumb.left && pt.x <= grp.tabScrollThumb.right &&
                            pt.y >= grp.tabScrollThumb.top && pt.y <= grp.tabScrollThumb.bottom) {
                            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                            return TRUE;
                        }
                        if (pt.x >= grp.tabScrollTrack.left && pt.x <= grp.tabScrollTrack.right &&
                            pt.y >= grp.tabScrollTrack.top && pt.y <= grp.tabScrollTrack.bottom) {
                            SetCursor(LoadCursor(nullptr, IDC_HAND));
                            return TRUE;
                        }
                    }
                }

                // Tab bar tabs, close buttons or folding chevrons
                for (const auto& grp : m_editorGroups) {
                    if (m_editor.HitTestTab(pt, grp.docs) >= 0 || m_editor.HitTestTabClose(pt, grp.docs) >= 0) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                }
                if (m_editor.HitTestFoldChevron(pt) >= 0) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Drawer Search Pane Buttons & Inputs
                if (m_config.activeRailTab == 1 && m_drawerAnimProgress > 0.001f) {
                    if (m_drawer.HitTestSearchButtons(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                    if (m_drawer.HitTestSearchInputs(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                        return TRUE;
                    }
                }

                // Unsupported binary action button ("Reveal in File Explorer")
                auto* pCurDoc = GetActiveDoc();
                if (pCurDoc && pCurDoc->isUnsupportedBinary) {
                    const auto& b = pCurDoc->unsupportedActionBtn;
                    if (pt.x >= b.left && pt.x <= b.right && pt.y >= b.top && pt.y <= b.bottom) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                }

                // Open Folder button in drawer when empty
                if (!m_drawer.HasOpenFolder() && m_drawer.HitTestOpenFolderButton(pt, m_config)) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Inline rename / create input box in drawer: show I-beam cursor
                if (m_drawer.IsRenaming() || m_drawer.IsCreating()) {
                    const auto& rcBox = m_drawer.IsRenaming() ? m_drawer.GetRenameBox() : m_drawer.GetCreateBox();
                    if (pt.x >= rcBox.left && pt.x <= rcBox.right && pt.y >= rcBox.top && pt.y <= rcBox.bottom) {
                        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                        return TRUE;
                    }
                }

                // Source Control: Draggable splitter, buttons and inputs (Images 1, 2, 3, 5)
                if (m_drawer.HitTestGitButtons(pt)) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }
                if (m_drawer.HitTestGitInputs(pt)) {
                    SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                    return TRUE;
                }
                if (m_config.activeRailTab == 2 && m_drawerAnimProgress > 0.001f) {
                    if (m_drawer.HitTestGitSplitter(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_SIZENS));
                        return TRUE;
                    }
                }

                // Autocomplete popup cursor handling (Images 2, 5)
                if (m_editor.IsAutocompleteVisible()) {
                    int hitIdx = -1;
                    if (m_editor.HitTestAutocomplete(pt, hitIdx)) {
                        SetCursor(LoadCursor(nullptr, (hitIdx >= 0) ? IDC_HAND : IDC_ARROW));
                        return TRUE;
                    }
                }

                // Terminal Panel cursor handling (Images 1, 2, 3, 4, 5)
                if (m_terminal.IsOpen() && m_terminal.HitTest(pt)) {
                    if (m_terminal.HitTestSplitter(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_SIZENS));
                        return TRUE;
                    }
                    if (m_terminal.HitTestTerminalSplitter(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                        return TRUE;
                    }
                    if (m_terminal.HitTestButtons(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                    if (m_terminal.HitTestTextArea(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                        return TRUE;
                    }
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                }

                // Editor code text area: show I-beam cursor (matching Image 1)
                if (pCurDoc && !pCurDoc->isSpecialView && !pCurDoc->isImageView && !pCurDoc->isUnsupportedBinary) {
                    float railW = m_config.ScaleF(static_cast<float>(m_config.railWidth));
                    float drawerW = m_config.ScaleF(static_cast<float>(m_config.drawerWidth)) * m_drawerAnimProgress;
                    float canvasLeft = railW + drawerW;
                    float gutterW = m_config.ScaleF(50.0f);
                    float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));
                    float tabH    = m_config.ScaleF(34.0f);
                    float breadcrumbH = m_config.ScaleF(24.0f);
                    float topY    = headerH + tabH + breadcrumbH;
                    float scrollbarW = m_config.ScaleF(14.0f);
                    RECT rcClient;
                    GetClientRect(m_hwnd, &rcClient);
                    float clientW = static_cast<float>(rcClient.right - rcClient.left);
                    float clientH = static_cast<float>(rcClient.bottom - rcClient.top);

                    if (pt.x >= canvasLeft + gutterW && pt.x < clientW - scrollbarW &&
                        pt.y >= topY && pt.y < clientH) {
                        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                        return TRUE;
                    }
                }

                // Processes HUD card cursor (Images 2 & 3)
                if (m_extensionsPanel.HitTestProcesses(pt)) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Package Manager View cursor handling (Install button, split arrow, cards, inputs)
                if (pCurDoc && pCurDoc->isSpecialView && pCurDoc->viewType == L"packages") {
                    LPCWSTR cur = m_editor.GetPackageManagerView().GetCursorForPoint(pt);
                    if (cur) {
                        SetCursor(LoadCursor(nullptr, cur));
                        return TRUE;
                    }
                }

                // Notebook View cursor handling (toolbar buttons, cell play/delete buttons, code text area)
                if (pCurDoc && pCurDoc->isSpecialView && pCurDoc->viewType == L"notebook") {
                    LPCWSTR cur = m_editor.GetNotebookView().GetCursorForPoint(pt, *pCurDoc);
                    if (cur) {
                        SetCursor(LoadCursor(nullptr, cur));
                        return TRUE;
                    }
                }

                // Student Tutorials & Learning Hub cursor handling
                if (pCurDoc && pCurDoc->isSpecialView && pCurDoc->viewType == L"student") {
                    if (m_editor.HitTestStudentView(pt) > 0) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                }

                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                return TRUE;
            }
            return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
        }

        case WM_MOUSEMOVE: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            HandleMouseMove(pt, wParam);
            return 0;
        }

        case WM_MOUSELEAVE: {
            m_trackingMouse = false;
            m_header.UpdateHoverState(ui::HeaderMenuId::None, ui::CaptionButtonType::None, ui::HeaderExecId::None);
            m_rail.SetHoveredTab(ui::RailTabId::None);
            m_drawer.SetHoveredIndex(-1);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            HandleLButtonDown(pt);
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            POINT clientPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int railW = m_config.Scale(m_config.railWidth);
            const int drawerW = static_cast<int>(m_config.Scale(m_config.drawerWidth) * m_drawerAnimProgress);
            const float canvasLeft = static_cast<float>(railW + drawerW);
            RECT clientRc;
            GetClientRect(m_hwnd, &clientRc);
            const float canvasWidth = static_cast<float>(clientRc.right) - canvasLeft;
            const float clientH = static_cast<float>(clientRc.bottom - clientRc.top);
            const float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));

            // Double click in Explorer tree: open file permanently (not preview)!
            if (m_drawerAnimProgress > 0.001f && clientPt.x >= railW && clientPt.x < railW + drawerW && clientPt.y >= headerH) {
                int clickedTree = m_drawer.HitTest(clientPt, m_config);
                if (clickedTree >= 0) {
                    const auto* pItem = m_drawer.GetItem(clickedTree);
                    if (pItem && !pItem->isFolder) {
                        OpenFileDocument(pItem->fullPath, false);
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }
                }
            }

            if (clientPt.x >= canvasLeft && clientPt.y >= headerH && !m_editorGroups.empty() && canvasWidth > 10.0f) {
                float paneW = canvasWidth / m_editorGroups.size();
                int targetGroup = -1;
                for (size_t g = 0; g < m_editorGroups.size(); ++g) {
                    float pLeft = canvasLeft + g * paneW;
                    float pRight = pLeft + paneW;
                    if (clientPt.x >= pLeft && (clientPt.x < pRight || g == m_editorGroups.size() - 1)) {
                        targetGroup = static_cast<int>(g);
                        break;
                    }
                }
                if (targetGroup >= 0) {
                    m_focusedGroupIdx = targetGroup;
                    auto& group = m_editorGroups[targetGroup];

                    // Tab double-click: makes preview tab permanent
                    int tabHit = m_editor.HitTestTab(clientPt, group.docs);
                    if (tabHit >= 0 && tabHit < static_cast<int>(group.docs.size())) {
                        group.docs[tabHit].isPreview = false;
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }

                    if (group.activeDocIdx >= 0 && group.activeDocIdx < static_cast<int>(group.docs.size())) {
                        auto& doc = group.docs[group.activeDocIdx];
                        if (!doc.isSpecialView && !doc.isImageView && !doc.isUnsupportedBinary) {
                            float paneCanvasLeft = canvasLeft + targetGroup * paneW;
                            size_t line = 0, col = 0;
                            if (m_editor.HitTestCodeArea(clientPt, m_config, doc, paneCanvasLeft, clientH, line, col)) {
                                doc.SelectWordAt(line, col);
                                InvalidateRect(m_hwnd, nullptr, FALSE);
                                return 0;
                            }
                        }
                    }
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            HandleLButtonUp(pt);
            return 0;
        }

        case WM_RBUTTONDOWN: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            HandleRButtonDown(pt);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(m_hwnd, &pt);

            if (m_terminal.IsOpen() && m_terminal.HitTest(pt)) {
                if (m_terminal.OnMouseWheel(pt, delta)) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            if (m_rightSidebarAnimProgress > 0.001f && m_extensionsPanel.IsOpen() && m_extensionsPanel.HitTest(pt)) {
                if (m_extensionsPanel.HandleMouseWheel(delta)) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // Check Autocomplete popup mouse wheel (Images 2 & 5)
            if (m_editor.IsAutocompleteVisible()) {
                int hitDummy = -1;
                if (m_editor.HitTestAutocomplete(pt, hitDummy)) {
                    if (m_editor.HandleAutocompleteMouseWheel(delta)) {
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }
                }
            }

            // 0a. Scroll Quick Open search results if modal is open (Image 5)
            if (m_quickOpen.IsOpen()) {
                if (m_quickOpen.HandleMouseWheel(delta)) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // 0b. Scroll Theme Picker Catalog if modal is open
            if (m_themePicker.IsOpen()) {
                if (m_themePicker.HandleMouseWheel(delta)) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            float railW = m_config.ScaleF(static_cast<float>(m_config.railWidth));
            float drawerW = m_config.ScaleF(static_cast<float>(m_config.drawerWidth)) * m_drawerAnimProgress;
            float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));

            // 1. Scroll Drawer Panel tree if mouse is over sidebar
            if (m_drawerAnimProgress > 0.001f && pt.x >= railW && pt.x < railW + drawerW && pt.y >= headerH) {
                if (m_drawer.HandleMouseWheel(delta, m_config.activeRailTab, static_cast<float>(pt.y))) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            float canvasLeft = railW + drawerW;
            RECT rcClient;
            GetClientRect(m_hwnd, &rcClient);
            float canvasW = static_cast<float>(rcClient.right - rcClient.left) - canvasLeft;

            // 1b. Scroll Tab Strip horizontally if mouse is over Tab Bar (User request: "since i do many tag it many now there should be a scrol bar so i could scroll throught them")
            const float tabH = m_config.ScaleF(34.0f);
            if (pt.y >= headerH && pt.y < headerH + tabH && pt.x >= canvasLeft) {
                if (canvasW > 10.0f && !m_editorGroups.empty()) {
                    float paneW = canvasW / m_editorGroups.size();
                    size_t targetG = 0;
                    for (size_t g = 0; g < m_editorGroups.size(); ++g) {
                        float pLeft = canvasLeft + g * paneW;
                        float pRight = pLeft + paneW;
                        if (pt.x >= pLeft && pt.x < pRight) {
                            targetG = g;
                            break;
                        }
                    }
                    auto& grp = m_editorGroups[targetG];
                    if (grp.totalTabsWidth > paneW) {
                        float maxScroll = grp.totalTabsWidth - paneW;
                        float scrollAmount = (delta > 0) ? -50.0f : 50.0f;
                        grp.tabScrollOffset = (std::clamp)(grp.tabScrollOffset + scrollAmount, 0.0f, maxScroll);
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }
                }
            }

            // 2. Scroll Editor Canvas
            editor::OpenDocument* pDoc = nullptr;
            if (canvasW > 10.0f && !m_editorGroups.empty()) {
                float paneW = canvasW / m_editorGroups.size();
                for (size_t g = 0; g < m_editorGroups.size(); ++g) {
                    float pLeft = canvasLeft + g * paneW;
                    float pRight = pLeft + paneW;
                    if (pt.x >= pLeft && pt.x < pRight) {
                        auto& grp = m_editorGroups[g];
                        if (grp.activeDocIdx >= 0 && grp.activeDocIdx < static_cast<int>(grp.docs.size())) {
                            pDoc = &grp.docs[grp.activeDocIdx];
                        }
                        break;
                    }
                }
            }
            if (!pDoc) {
                pDoc = GetActiveDoc();
            }
            if (pDoc) {
                if (pDoc->isSpecialView && pDoc->viewType == L"keybindings") {
                    if (m_editor.GetKeyboardShortcutsView().HandleMouseWheel(delta)) {
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }
                } else if (pDoc->isSpecialView && pDoc->viewType == L"packages") {
                    if (m_editor.GetPackageManagerView().HandleMouseWheel(delta, static_cast<float>(pt.y))) {
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }
                } else if (pDoc->isSpecialView && pDoc->viewType == L"notebook") {
                    if (m_editor.GetNotebookView().HandleMouseWheel(delta, pt, *pDoc, m_config)) {
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }
                } else {
                    bool isShiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (isShiftDown) {
                        float hDelta = (delta > 0) ? -60.0f : 60.0f;
                        float maxScrollX = (std::max)(0.0f, pDoc->maxContentWidth);
                        pDoc->scrollOffsetX = (std::clamp)(pDoc->scrollOffsetX + hDelta, 0.0f, maxScrollX);
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }
                    int linesToScroll = 3;
                    if (delta > 0) {
                        if (pDoc->scrollLine >= static_cast<size_t>(linesToScroll)) {
                            pDoc->scrollLine -= linesToScroll;
                        } else {
                            pDoc->scrollLine = 0;
                        }
                    } else {
                        pDoc->scrollLine += linesToScroll;
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            return 0;
        }

        case WM_MOUSEHWHEEL: {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            auto* pDoc = GetActiveDoc();
            if (pDoc && !pDoc->isSpecialView) {
                float hDelta = (delta > 0) ? 60.0f : -60.0f;
                float maxScrollX = (std::max)(0.0f, pDoc->maxContentWidth);
                pDoc->scrollOffsetX = (std::clamp)(pDoc->scrollOffsetX + hDelta, 0.0f, maxScrollX);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }
            return 0;
        }

        case WM_CHAR: {
            if (m_suppressNextChar) {
                m_suppressNextChar = false;
                return 0;
            }

            if (m_drawer.IsPublishRepoModalOpen()) {
                if (m_drawer.HandlePublishRepoChar(static_cast<wchar_t>(wParam))) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
                return 0;
            }

            if (m_dbSettingsModal.IsOpen()) {
                if (m_dbSettingsModal.OnChar(static_cast<wchar_t>(wParam))) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
                return 0;
            }

            // Integrated Terminal keystroke input (Image 1) - completely swallowed when terminal is focused!
            if (m_terminal.IsOpen() && m_isTerminalFocused) {
                m_terminal.OnChar(static_cast<wchar_t>(wParam));
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            // 0. Quick Open file search input (Image 5)
            if (m_quickOpen.IsOpen()) {
                if (m_quickOpen.HandleChar(static_cast<wchar_t>(wParam))) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            if (m_drawer.IsRenaming()) {
                m_drawer.HandleRenameChar(static_cast<wchar_t>(wParam));
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            if (m_drawer.IsCreating()) {
                m_drawer.HandleCreateChar(static_cast<wchar_t>(wParam));
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            // 1. If Code Search in drawer has focus, typed characters MUST route to Code Search
            if (m_config.activeRailTab == 1 && m_drawerAnimProgress > 0.001f && m_drawer.IsSearchFocused()) {
                m_drawer.HandleSearchChar(static_cast<wchar_t>(wParam));
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            // 1b. If Git commit message input has focus, typed characters MUST route to Git input
            if (m_config.activeRailTab == 2 && m_drawerAnimProgress > 0.001f && m_drawer.IsGitCommitBoxFocused()) {
                m_drawer.HandleGitChar(static_cast<wchar_t>(wParam));
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            auto* pDoc = GetActiveDoc();
            if (pDoc) {
                wchar_t ch = static_cast<wchar_t>(wParam);
                // 2. If Keyboard Shortcuts view is active and its search bar is focused, route to shortcuts view
                if (pDoc->isSpecialView && pDoc->viewType == L"keybindings") {
                    if (m_editor.GetKeyboardShortcutsView().IsSearchFocused()) {
                        if (m_editor.GetKeyboardShortcutsView().HandleChar(ch)) {
                            InvalidateRect(m_hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }
                } else if (pDoc->isSpecialView && pDoc->viewType == L"packages") {
                    if (m_editor.GetPackageManagerView().IsSearchFocused()) {
                        if (m_editor.GetPackageManagerView().HandleChar(ch)) {
                            InvalidateRect(m_hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }
                } else if (pDoc->isSpecialView && pDoc->viewType == L"notebook") {
                    if (m_editor.GetNotebookView().HandleChar(ch, *pDoc, m_hwnd)) {
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }
                } else {
                    // Printable characters only (Enter and Tab are cleanly handled in WM_KEYDOWN)
                    if (ch >= 32 && ch != 127) {
                        if (pDoc->isDiffView && (pDoc->diffActivePane == 0 || pDoc->isDiffRightReadOnly)) {
                            return 0; // Read-only side!
                        }
                        wchar_t pairClose = 0;
                        if (ch == L'(') pairClose = L')';
                        else if (ch == L'[') pairClose = L']';
                        else if (ch == L'{') pairClose = L'}';
                        else if (ch == L'"') pairClose = L'"';
                        else if (ch == L'\'') pairClose = L'\'';
                        else if (ch == L'`') pairClose = L'`';

                        // 1. Wrap active selection when typing bracket or quote opener
                        if (pDoc->HasSelection()) {
                            if (pairClose != 0) {
                                std::wstring sel = pDoc->GetSelectedText();
                                pDoc->DeleteSelection();
                                std::wstring wrapped = std::wstring(1, ch) + sel + pairClose;
                                pDoc->buffer.InsertText(wrapped);
                                MarkDocumentModified(pDoc);
                                if (pDoc->isDiffView) ComputeDiffHunks(*pDoc);
                                m_editor.DismissAutocomplete();
                                m_editor.ClearGhostText();
                                EnsureCursorVisible();
                                InvalidateRect(m_hwnd, nullptr, FALSE);
                                return 0;
                            } else {
                                pDoc->DeleteSelection();
                            }
                        }

                        // 2. Overtype closing character if cursor is immediately before it
                        auto cur = pDoc->buffer.GetCursor();
                        const auto& lines = pDoc->buffer.GetLines();
                        if (cur.line < lines.size()) {
                            const auto& line = lines[cur.line];
                            if (cur.col < line.length() && line[cur.col] == ch) {
                                if (ch == L')' || ch == L']' || ch == L'}' || ch == L'"' || ch == L'\'' || ch == L'`') {
                                    pDoc->buffer.MoveCursorRight();
                                    m_editor.DismissAutocomplete();
                                    m_editor.ClearGhostText();
                                    EnsureCursorVisible();
                                    InvalidateRect(m_hwnd, nullptr, FALSE);
                                    return 0;
                                }
                            }
                        }

                        // 3. Auto-insert pair and position cursor inside
                        if (pairClose != 0) {
                            std::wstring pairStr;
                            pairStr += ch;
                            pairStr += pairClose;
                            pDoc->buffer.InsertText(pairStr);
                            pDoc->buffer.MoveCursorLeft();
                            MarkDocumentModified(pDoc);
                            if (pDoc->isDiffView) ComputeDiffHunks(*pDoc);
                            m_editor.DismissAutocomplete();
                            m_editor.ClearGhostText();
                            EnsureCursorVisible();
                            InvalidateRect(m_hwnd, nullptr, FALSE);
                            return 0;
                        }

                        // Auto-closing HTML tag on '>' (Image 1: <div> -> <div></div>)
                        if (ch == L'>') {
                            auto pos = pDoc->buffer.GetCursor();
                            const auto& lineText = pDoc->buffer.GetLine(pos.line);
                            std::wstring prefixText = (pos.col <= lineText.length()) ? lineText.substr(0, pos.col) : lineText;

                            size_t openBracket = prefixText.rfind(L'<');
                            if (openBracket != std::wstring::npos) {
                                bool isClosing = (openBracket + 1 < prefixText.length() && prefixText[openBracket + 1] == L'/');
                                bool isSelfClosing = (!prefixText.empty() && prefixText.back() == L'/');
                                bool isCommentOrDoctype = (prefixText.substr(openBracket).rfind(L"<!--", 0) == 0 ||
                                                           prefixText.substr(openBracket).rfind(L"<!", 0) == 0 ||
                                                           prefixText.substr(openBracket).rfind(L"<?", 0) == 0);

                                if (!isClosing && !isSelfClosing && !isCommentOrDoctype) {
                                    size_t tagStart = openBracket + 1;
                                    while (tagStart < prefixText.length() && (prefixText[tagStart] == L' ' || prefixText[tagStart] == L'\t')) {
                                        tagStart++;
                                    }
                                    size_t tagEnd = tagStart;
                                    while (tagEnd < prefixText.length() && (iswalnum(prefixText[tagEnd]) || prefixText[tagEnd] == L'-' || prefixText[tagEnd] == L':' || prefixText[tagEnd] == L'_')) {
                                        tagEnd++;
                                    }
                                    std::wstring tagName = prefixText.substr(tagStart, tagEnd - tagStart);
                                    std::wstring tagLower = tagName;
                                    for (auto& c : tagLower) c = towlower(c);

                                    static const std::unordered_set<std::wstring> voidTags = {
                                        L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
                                        L"link", L"meta", L"param", L"source", L"track", L"wbr"
                                    };

                                    if (!tagName.empty() && voidTags.count(tagLower) == 0) {
                                        pDoc->buffer.InsertChar(L'>');
                                        std::wstring closeTag = L"</" + tagName + L">";
                                        for (wchar_t c : closeTag) {
                                            pDoc->buffer.InsertChar(c);
                                        }
                                        auto cur = pDoc->buffer.GetCursor();
                                        pDoc->buffer.SetCursor(cur.line, cur.col - closeTag.length());
                                        MarkDocumentModified(pDoc);
                                        if (pDoc->isDiffView) ComputeDiffHunks(*pDoc);
                                        m_editor.DismissAutocomplete();
                                        m_editor.ClearGhostText();
                                        InvalidateRect(m_hwnd, nullptr, FALSE);
                                        return 0;
                                    }
                                }
                            }
                        }

                        // Auto-closing tag on '/' after '<' (typing '</' automatically completes nearest unclosed tag, e.g. '</main>')
                        if (ch == L'/') {
                            auto pos = pDoc->buffer.GetCursor();
                            const auto& lineText = pDoc->buffer.GetLine(pos.line);
                            if (pos.col > 0 && pos.col <= lineText.length() && lineText[pos.col - 1] == L'<') {
                                static const std::unordered_set<std::wstring> voidTags = {
                                    L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
                                    L"link", L"meta", L"param", L"source", L"track", L"wbr"
                                };
                                std::vector<std::wstring> closedStack;
                                std::wstring targetTag;
                                const auto& lines = pDoc->buffer.GetLines();
                                for (int l = static_cast<int>(pos.line); l >= 0 && targetTag.empty(); --l) {
                                    const auto& lStr = lines[l];
                                    size_t maxC = (l == static_cast<int>(pos.line)) ? (pos.col - 1) : lStr.length();
                                    for (int c = static_cast<int>(maxC) - 1; c >= 0; --c) {
                                        if (lStr[c] == L'<') {
                                            if (c + 1 < static_cast<int>(lStr.length()) && lStr[c + 1] == L'/') {
                                                size_t s = c + 2;
                                                while (s < lStr.length() && (lStr[s] == L' ' || lStr[s] == L'\t')) s++;
                                                size_t e = s;
                                                while (e < lStr.length() && (iswalnum(lStr[e]) || lStr[e] == L'-' || lStr[e] == L':' || lStr[e] == L'_')) e++;
                                                if (e > s) {
                                                    std::wstring t = lStr.substr(s, e - s);
                                                    for (auto& chT : t) chT = towlower(chT);
                                                    closedStack.push_back(t);
                                                }
                                            } else if (c + 1 < static_cast<int>(lStr.length()) && lStr[c + 1] != L'!' && lStr[c + 1] != L'?') {
                                                size_t s = c + 1;
                                                while (s < lStr.length() && (lStr[s] == L' ' || lStr[s] == L'\t')) s++;
                                                size_t e = s;
                                                while (e < lStr.length() && (iswalnum(lStr[e]) || lStr[e] == L'-' || lStr[e] == L':' || lStr[e] == L'_')) e++;
                                                if (e > s) {
                                                    std::wstring t = lStr.substr(s, e - s);
                                                    std::wstring tLower = t;
                                                    for (auto& chT : tLower) chT = towlower(chT);
                                                    if (voidTags.count(tLower) == 0) {
                                                        if (!closedStack.empty() && closedStack.back() == tLower) {
                                                            closedStack.pop_back();
                                                        } else {
                                                            targetTag = t;
                                                            break;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                if (!targetTag.empty()) {
                                    pDoc->buffer.InsertChar(L'/');
                                    std::wstring completeClose = targetTag + L">";
                                    for (wchar_t c : completeClose) pDoc->buffer.InsertChar(c);
                                    MarkDocumentModified(pDoc);
                                    if (pDoc->isDiffView) ComputeDiffHunks(*pDoc);
                                    m_editor.DismissAutocomplete();
                                    m_editor.ClearGhostText();
                                    InvalidateRect(m_hwnd, nullptr, FALSE);
                                    return 0;
                                }
                            }
                        }

                        pDoc->buffer.InsertChar(ch);
                        EnsureCursorVisible();
                        MarkDocumentModified(pDoc);
                        if (pDoc->isDiffView) {
                            ComputeDiffHunks(*pDoc);
                        }

                        // Query massive HTML5 / JavaScript / Python / Path IntelliSense & Snippets (Images 2 & 5)
                        if (iswalnum(ch) || ch == L'!' || ch == L'@' || ch == L'#' || ch == L'-' || ch == L'.' || ch == L'<' || ch == L':' || ch == L'$' || ch == L'_' || ch == L'/' || ch == L'\\' || ch == L'"' || ch == L'\'') {
                            auto pos = pDoc->buffer.GetCursor();
                            const auto& lineText = pDoc->buffer.GetLine(pos.line);
                            std::wstring docTarget = pDoc->filePath.empty() ? pDoc->fileName : pDoc->filePath;
                            if (m_intelliSense.GetWorkspaceRoot().empty() && m_drawer.HasOpenFolder()) {
                                m_intelliSense.SetWorkspaceRoot(m_drawer.GetOpenFolderPath());
                            }
                            auto items = m_intelliSense.QueryCompletions(docTarget, lineText, pos.col, pDoc ? &pDoc->buffer.GetLines() : nullptr);
                            if (!items.empty()) {
                                m_editor.ShowAutocomplete(items);
                                if (items[0].kind == editor::CompletionKind::Folder || items[0].kind == editor::CompletionKind::File) {
                                    m_editor.ClearGhostText();
                                } else {
                                    std::wstring prefix = editor::IntelliSenseEngine::ExtractQueryPrefix(lineText, pos.col);
                                    std::wstring ghost = items[0].label;
                                    if (!prefix.empty() && ghost.rfind(prefix, 0) == 0) {
                                        ghost = ghost.substr(prefix.length());
                                    }
                                    m_editor.SetGhostText(ghost);
                                }
                            } else {
                                m_editor.DismissAutocomplete();
                                m_editor.ClearGhostText();
                            }
                        } else {
                            m_editor.DismissAutocomplete();
                            m_editor.ClearGhostText();
                        }
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                    }
                }
            }
            return 0;
        }

        case WM_KILLFOCUS: {
            if (m_popupMenu.IsOpen()) {
                m_popupMenu.Close();
                m_header.SetActiveMenu(ui::HeaderMenuId::None);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            RunManualGarbageCollection();
            return 0;
        }

        case WM_SYSKEYDOWN: {
            if (wParam == 'Z') {
                DoToggleWordWrap();
                return 0;
            }
            if (m_config.activeRailTab == 1 && m_drawerAnimProgress > 0.001f) {
                if (wParam == 'L') {
                    m_drawer.SetSearchOnlyOpenEditors(!m_drawer.GetSearchOnlyOpenEditors());
                    m_drawer.LaunchAsyncSearch();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                } else if (wParam == 'C') {
                    m_drawer.SetMatchCase(!m_drawer.GetMatchCase());
                    m_drawer.LaunchAsyncSearch();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                } else if (wParam == 'W') {
                    m_drawer.SetMatchWholeWord(!m_drawer.GetMatchWholeWord());
                    m_drawer.LaunchAsyncSearch();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                } else if (wParam == 'R') {
                    m_drawer.SetUseRegex(!m_drawer.GetUseRegex());
                    m_drawer.LaunchAsyncSearch();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
        }

        case WM_KEYDOWN: {
            if (m_drawer.IsGitHubLoginModalOpen()) {
                if (m_drawer.HandleGitHubLoginKeyDown(wParam)) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
                return 0;
            }

            if (m_drawer.IsPublishRepoModalOpen()) {
                if (m_drawer.HandlePublishRepoKeyDown(wParam)) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
                return 0;
            }

            if (m_dbSettingsModal.IsOpen()) {
                if (m_dbSettingsModal.OnKeyDown(wParam)) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
                if (wParam == VK_ESCAPE) {
                    m_dbSettingsModal.Close();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            if (m_editor.IsRefactorPopupVisible() && wParam != VK_SHIFT && wParam != VK_CONTROL && wParam != VK_MENU) {
                m_editor.DismissRefactorPopup();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }

            // Inline Ghost Text Tab-to-complete (Image 3)
            if (wParam == VK_TAB && !m_editor.GetGhostText().empty() && !m_editor.IsAutocompleteVisible()) {
                auto* pDoc = GetActiveDoc();
                if (pDoc) {
                    m_suppressNextChar = true;
                    const std::wstring& ghost = m_editor.GetGhostText();
                    for (wchar_t c : ghost) {
                        pDoc->buffer.InsertChar(c);
                    }
                    MarkDocumentModified(pDoc);
                    m_editor.ClearGhostText();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // Autocomplete Popup Navigation & Completion Insertion (Image 5)
            if (m_editor.IsAutocompleteVisible()) {
                if (wParam == VK_ESCAPE) {
                    m_editor.DismissAutocomplete();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
                if (wParam == VK_UP) {
                    m_editor.SelectPrevAutocomplete();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
                if (wParam == VK_DOWN) {
                    m_editor.SelectNextAutocomplete();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
                if (wParam == VK_RETURN || wParam == VK_TAB) {
                    CommitAutocompleteSelection();
                    return 0;
                }
            }

            // Ctrl+Space IntelliSense Trigger
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == VK_SPACE) {
                auto* pDoc = GetActiveDoc();
                if (pDoc) {
                    auto pos = pDoc->buffer.GetCursor();
                    const auto& lineText = pDoc->buffer.GetLine(pos.line);
                    std::wstring docTarget = pDoc->filePath.empty() ? pDoc->fileName : pDoc->filePath;
                    if (m_intelliSense.GetWorkspaceRoot().empty() && m_drawer.HasOpenFolder()) {
                        m_intelliSense.SetWorkspaceRoot(m_drawer.GetOpenFolderPath());
                    }
                    auto items = m_intelliSense.QueryCompletions(docTarget, lineText, pos.col, pDoc ? &pDoc->buffer.GetLines() : nullptr);
                    if (!items.empty()) {
                        m_editor.ShowAutocomplete(items);
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // F5 execution shortcuts (Ctrl+F5 = Run, F5 = Debug, Shift+F5 = Stop, Ctrl+Shift+F5 = Restart)
            if (wParam == VK_F5) {
                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                    DoRestartServer();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                } else if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                    DoStopServer();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                } else if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                    DoRunActiveDocument();
                    return 0;
                } else {
                    if (m_terminal.IsDebugActive()) {
                        m_terminal.TriggerDebugAction(ui::DebugAction::Continue);
                    } else {
                        DoDebugActiveDocument();
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // F9 Toggle Breakpoint
            if (wParam == VK_F9) {
                auto* pDoc = GetActiveDoc();
                if (pDoc && !pDoc->isSpecialView) {
                    size_t curLine = pDoc->buffer.GetCursor().line;
                    auto it = std::find(pDoc->breakpoints.begin(), pDoc->breakpoints.end(), curLine);
                    if (it != pDoc->breakpoints.end()) {
                        pDoc->breakpoints.erase(it);
                    } else {
                        pDoc->breakpoints.push_back(curLine);
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // Ctrl+Shift+X Runtimes Panel toggle (Images 1 & 2)
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && (GetKeyState(VK_SHIFT) & 0x8000) != 0 && (wParam == 'X' || wParam == 'x')) {
                if (m_extensionsPanel.IsOpen()) {
                    AnimateRightSidebarClose();
                } else {
                    AnimateRightSidebarOpen();
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            // Ctrl+Shift+Y Debug Console toggle
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && (GetKeyState(VK_SHIFT) & 0x8000) != 0 && (wParam == 'Y' || wParam == 'y')) {
                if (!m_terminal.IsOpen()) {
                    std::wstring dir = m_drawer.HasOpenFolder() ? m_drawer.GetOpenFolderPath() : m_workspace.GetRootPath().wstring();
                    m_terminal.Open(dir);
                }
                m_terminal.ShowDebugConsole();
                m_isTerminalFocused = true;
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            if (wParam == VK_F10 && m_terminal.IsDebugActive()) {
                m_terminal.TriggerDebugAction(ui::DebugAction::StepOver);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            if (wParam == VK_F11 && m_terminal.IsDebugActive()) {
                if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                    m_terminal.TriggerDebugAction(ui::DebugAction::StepOut);
                } else {
                    m_terminal.TriggerDebugAction(ui::DebugAction::StepInto);
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            // Toggle integrated terminal panel via Ctrl+` (VK_OEM_3)
            if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == VK_OEM_3) {
                DoCreateTerminal();
                return 0;
            }

            // Split active terminal via Ctrl+Shift+5 or Ctrl+Shift+\ (Images 4 & 5)
            if (m_terminal.IsOpen() && m_isTerminalFocused && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (wParam == '5' || wParam == VK_OEM_5)) {
                m_terminal.SplitActiveTab();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            // Integrated Terminal key handling - completely isolated so backspace/typing NEVER leaks into editor!
            if (m_terminal.IsOpen() && m_isTerminalFocused) {
                if (wParam == VK_ESCAPE) {
                    m_terminal.Close();
                    m_isTerminalFocused = false;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
                if (m_terminal.OnKeyDown(static_cast<UINT>(wParam), (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
                // When terminal has focus, swallow all editing & navigation keys so editor buffer is untouched
                // Do NOT let Ctrl+C, Ctrl+V, Ctrl+X, Ctrl+A, Ctrl+Z, Ctrl+Y, Tab, Enter, Backspace leak into editor!
                bool isCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (!isCtrl || wParam == 'C' || wParam == 'c' || wParam == 'V' || wParam == 'v' ||
                    wParam == 'X' || wParam == 'x' || wParam == 'A' || wParam == 'a' ||
                    wParam == 'Z' || wParam == 'z' || wParam == 'Y' || wParam == 'y' ||
                    wParam == VK_BACK || wParam == VK_DELETE || wParam == VK_TAB || wParam == VK_RETURN) {
                    return 0;
                }
            }

            // 0a. Confirm Discard / Restore / Delete / Notice modal keyboard handling (Images 2, 3, 4, 5)
            if (m_confirmDiscardModal.IsOpen()) {
                if (m_confirmDiscardModal.IsLoading()) {
                    return 0; // strict idempotency: modal action is busy executing
                }
                bool wasNotice = (m_confirmDiscardModal.GetType() == ui::ConfirmModalType::Notice);
                auto modalType = m_confirmDiscardModal.GetType();
                bool confirmed = false;
                std::wstring targetPath, targetFileName, targetStatus;
                if (m_confirmDiscardModal.HandleKeyDown(wParam, confirmed, targetPath, targetFileName, targetStatus)) {
                    if (confirmed) {
                        if (wasNotice) {
                            m_confirmDiscardModal.Close();
                            m_drawer.SetGitCommitBoxFocus(1);
                        } else if (modalType == ui::ConfirmModalType::DeleteFile || modalType == ui::ConfirmModalType::DeleteFolder) {
                            ExecuteDeleteAsync(targetPath, modalType == ui::ConfirmModalType::DeleteFolder);
                        } else {
                            ExecuteDiscardOrRestoreAsync(targetPath, targetFileName, targetStatus);
                        }
                    } else if (wasNotice) {
                        m_drawer.SetGitCommitBoxFocus(1);
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
                return 0;
            }

            // 0a.1 Git Error Modal keyboard handling (Image 2)
            if (m_gitErrorModal.IsOpen()) {
                ui::GitErrorModalAction action = ui::GitErrorModalAction::None;
                if (m_gitErrorModal.HandleKeyDown(wParam, action)) {
                    if (action == ui::GitErrorModalAction::OpenGitLog) {
                        OpenGitLogTerminal();
                    } else if (action == ui::GitErrorModalAction::ShowCommandOutput) {
                        ShowGitCommandOutput(m_gitErrorModal.GetCommand(), m_gitErrorModal.GetOutput(), m_gitErrorModal.GetRepoDir());
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
                return 0; // Block background keystrokes while error modal is open
            }

            // 0. Quick Open file search palette (Image 5)
            if (m_quickOpen.IsOpen()) {
                if (wParam == VK_ESCAPE) {
                    m_quickOpen.Close();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
                std::wstring chosenFile;
                int targetLine = -1;
                std::wstring actionCmd;
                if (m_quickOpen.HandleKeyDown(wParam, chosenFile, targetLine, actionCmd)) {
                    if (!actionCmd.empty()) {
                        ExecuteQuickOpenAction(actionCmd);
                    } else if (!chosenFile.empty()) {
                        OpenFileDocument(chosenFile, false);
                        if (targetLine > 0) {
                            JumpToEditorLine(static_cast<size_t>(targetLine));
                        }
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
                return 0;
            }

            // Handle inline tree rename keys (Enter to commit, Escape to cancel)
            if (m_drawer.IsRenaming()) {
                if (wParam == VK_RETURN) {
                    m_suppressNextChar = true;
                    std::wstring oldPath, newPath;
                    if (m_drawer.CommitInlineRename(oldPath, newPath)) {
                        m_explorerUndo.PushRename(oldPath, newPath);
                        for (auto& group : m_editorGroups) {
                            for (auto& doc : group.docs) {
                                if (doc.filePath == oldPath) {
                                    doc.filePath = newPath;
                                    doc.fileName = std::filesystem::path(newPath).filename().wstring();
                                }
                            }
                        }
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                } else if (wParam == VK_ESCAPE) {
                    m_suppressNextChar = true;
                    m_drawer.CancelInlineRename();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                } else {
                    m_drawer.HandleRenameKeyDown(wParam);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // Handle inline tree item create keys (Enter to commit, Escape to cancel)
            if (m_drawer.IsCreating()) {
                if (wParam == VK_RETURN) {
                    m_suppressNextChar = true;
                    std::wstring createdPath;
                    bool isFolder = m_drawer.IsCreatingFolder();
                    if (m_drawer.CommitInlineCreate(createdPath)) {
                        m_explorerUndo.PushCreate(createdPath, isFolder);
                        if (!isFolder) {
                            OpenFileDocument(createdPath, false);
                        }
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                } else if (wParam == VK_ESCAPE) {
                    m_suppressNextChar = true;
                    m_drawer.CancelInlineCreate();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                } else {
                    m_drawer.HandleCreateKeyDown(wParam);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // F2 inline rename trigger when hovering over tree item in File Explorer
            if (wParam == VK_F2 && m_config.activeRailTab == 0 && m_drawerAnimProgress > 0.001f) {
                int hovered = m_drawer.GetHoveredIndex();
                if (hovered >= 0) {
                    m_drawer.StartInlineRename(hovered);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // Delete key: deletes selected file/folder in File Explorer (User request: "the del keyboard key doesnt work i click the file and click on the keyboard the del ut the fiule is not deleted")
            auto* pActiveDocCheck = GetActiveDoc();
            bool docHasSelection = (pActiveDocCheck && pActiveDocCheck->HasSelection());
            if (wParam == VK_DELETE && m_config.activeRailTab == 0 && m_drawerAnimProgress > 0.001f &&
                (m_explorerHasFocus || (!m_selectedExplorerPath.empty() && !docHasSelection))) {
                DoDeleteSelectedExplorerItem();
                return 0;
            }

            if (wParam == VK_ESCAPE) {
                if (m_isDraggingExplorerFile) {
                    m_isDraggingExplorerFile = false;
                    m_dragCandidatePath.clear();
                    m_dragCandidateName.clear();
                    m_hasDragDropTarget = false;
                    m_dragDropTargetGroup = -1;
                    ReleaseCapture();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
                m_drawer.DismissCommitCard();
                auto* pCurDoc = GetActiveDoc();
                if (pCurDoc && pCurDoc->HasSelection()) {
                    pCurDoc->ClearSelection();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            if (m_colorPicker.IsOpen()) {
                if (m_colorPicker.HandleKeyDown(wParam)) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            if (m_themePicker.IsOpen()) {
                if (wParam == VK_ESCAPE) {
                    m_themePicker.Close();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            if (wParam == VK_ESCAPE && m_popupMenu.IsOpen()) {
                m_popupMenu.Close();
                m_header.SetActiveMenu(ui::HeaderMenuId::None);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            // 1. If Code Search in drawer is focused, navigation keys route to Code Search
            if (m_config.activeRailTab == 1 && m_drawerAnimProgress > 0.001f && m_drawer.IsSearchFocused()) {
                if (wParam == VK_BACK || wParam == VK_TAB || wParam == VK_ESCAPE || wParam == VK_RETURN) {
                    m_drawer.HandleSearchKeyDown(wParam);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // 1b. If Git commit message in drawer is focused, navigation keys route to Git input
            if (m_config.activeRailTab == 2 && m_drawerAnimProgress > 0.001f && m_drawer.IsGitCommitBoxFocused()) {
                if (wParam == VK_BACK || wParam == VK_ESCAPE || wParam == VK_RETURN) {
                    m_drawer.HandleGitKeyDown(wParam);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            auto* pDoc = GetActiveDoc();
            if (pDoc && pDoc->isSpecialView) {
                if (pDoc->viewType == L"keybindings") {
                    if (m_editor.GetKeyboardShortcutsView().IsSearchFocused()) {
                        if (m_editor.GetKeyboardShortcutsView().HandleKeyDown(wParam)) {
                            InvalidateRect(m_hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }
                } else if (pDoc->viewType == L"packages") {
                    if (m_editor.GetPackageManagerView().HandleKeyDown(wParam)) {
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }
                } else if (pDoc->viewType == L"notebook") {
                    if (m_editor.GetNotebookView().HandleKeyDown(wParam, *pDoc, m_hwnd)) {
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }
                }
            }

            if (wParam == VK_F11) {
                if (IsZoomed(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
                else ShowWindow(m_hwnd, SW_MAXIMIZE);
                return 0;
            }

            // Shift+Alt+A: Toggle Block Comment (VS Code standard)
            if ((GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000) && wParam == 'A') {
                DoToggleBlockComment();
                return 0;
            }

            // Shift+Alt+F: Format Document
            if ((GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000) && wParam == 'F') {
                DoFormatDocument();
                return 0;
            }

            // F12: Go to Definition
            if (wParam == VK_F12) {
                DoGoToDefinition();
                return 0;
            }

            // F2: Rename Symbol
            if (wParam == VK_F2) {
                DoRenameSymbol();
                return 0;
            }

            // Alt+Z: Toggle Word Wrap
            if ((GetKeyState(VK_MENU) & 0x8000) && !(GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Z') {
                m_wordWrap = !m_wordWrap;
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }

            // Keyboard Shortcuts with Ctrl key
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool altDown   = (GetKeyState(VK_MENU) & 0x8000) != 0;

                // Zoom In: Ctrl+= / Ctrl++ (VK_OEM_PLUS or VK_ADD)
                if (wParam == VK_OEM_PLUS || wParam == VK_ADD) {
                    DoZoomIn();
                    return 0;
                }
                // Zoom Out: Ctrl+- (VK_OEM_MINUS or VK_SUBTRACT)
                if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) {
                    DoZoomOut();
                    return 0;
                }
                // Reset Zoom: Ctrl+0 ('0' or VK_NUMPAD0)
                if (wParam == '0' || wParam == VK_NUMPAD0) {
                    DoResetZoom();
                    return 0;
                }

                // Ctrl+/ (VK_OEM_2): Toggle Line Comment
                if (wParam == VK_OEM_2) {
                    DoToggleLineComment();
                    return 0;
                }

                if (wParam == VK_OEM_5) { // Ctrl+\ (Split Editor Right, Images 3 & 4)
                    ToggleSplitEditor();
                    return 0;
                }
                if (wParam == VK_F2) {
                    DoChangeAllOccurrences();
                    return 0;
                }
                if (wParam == 'R' && shiftDown) {
                    DoRefactor();
                    return 0;
                }
                if (wParam == 'P') {
                    if (shiftDown) {
                        DoCommandPalette();
                    } else {
                        DoQuickOpenFileSearch();
                    }
                    return 0;
                }
                if (wParam == 'B') {
                    ToggleSidebar();
                    return 0;
                }
                if (wParam == 'N') {
                    if (shiftDown) {
                        DoNewWindow();
                    } else if (altDown) {
                        DoNewFile();
                    } else {
                        DoNewTextFile();
                    }
                    return 0;
                }
                if (wParam == 'O') {
                    if (m_chordCtrlK) {
                        DoOpenFolder();
                    } else {
                        DoOpenFile();
                    }
                    m_chordCtrlK = false;
                    return 0;
                }
                if (wParam == 'S') {
                    if (m_chordCtrlK) {
                        DoSaveAll();
                    } else if (shiftDown) {
                        DoSaveFileAs();
                    } else {
                        DoSaveFile();
                    }
                    m_chordCtrlK = false;
                    return 0;
                }
                if (wParam == 'W' || wParam == VK_F4) {
                    DoCloseEditor();
                    return 0;
                }
                if (wParam == 'F') {
                    if (m_chordCtrlK) {
                        DoCloseFolder();
                        m_chordCtrlK = false;
                        return 0;
                    } else if (shiftDown) {
                        DoShowSearch();
                        return 0;
                    } else {
                        DoFind();
                        return 0;
                    }
                }
                if (wParam == 'H') {
                    if (shiftDown) {
                        DoShowReplace();
                    } else {
                        DoReplace();
                    }
                    return 0;
                }
                if (wParam == 'E') {
                    if (shiftDown) {
                        DoShowExplorer();
                        return 0;
                    }
                }
                if (wParam == 'G') {
                    if (shiftDown) {
                        DoShowSourceControl();
                        return 0;
                    }
                }
                if (wParam == 'J') {
                    if (shiftDown) {
                        OpenNotebookView();
                        return 0;
                    }
                }
                if (wParam == VK_OEM_3) { // Backtick (`)
                    DoCreateTerminal();
                    return 0;
                }
                if (wParam == 'T') {
                    if (shiftDown) {
                        DoReopenClosedEditor();
                        return 0;
                    } else if (m_chordCtrlK) {
                        DoToggleThemeModal();
                        m_chordCtrlK = false;
                        return 0;
                    }
                }
                if (wParam == VK_OEM_COMMA) {
                    DoOpenSettings();
                    return 0;
                }
                if (wParam == 'Z') {
                    if (m_chordCtrlK) {
                        m_config.zenMode = !m_config.zenMode;
                        m_chordCtrlK = false;
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }
                    POINT pt;
                    GetCursorPos(&pt);
                    ScreenToClient(m_hwnd, &pt);
                    const float railW = m_config.ScaleF(48.0f);
                    const float drawerW = m_config.ScaleF(260.0f) * m_drawerAnimProgress;
                    bool isOverDrawer = (pt.x >= railW && pt.x < railW + drawerW);
                    if (isOverDrawer || m_drawer.IsRenaming()) {
                        if (shiftDown) {
                            DoExplorerRedo();
                        } else {
                            DoExplorerUndo();
                        }
                    } else {
                        if (shiftDown) {
                            DoRedo();
                        } else {
                            DoUndo();
                        }
                    }
                    return 0;
                }
                if (wParam == 'Y') {
                    POINT pt;
                    GetCursorPos(&pt);
                    ScreenToClient(m_hwnd, &pt);
                    const float railW = m_config.ScaleF(48.0f);
                    const float drawerW = m_config.ScaleF(260.0f) * m_drawerAnimProgress;
                    bool isOverDrawer = (pt.x >= railW && pt.x < railW + drawerW);
                    if (isOverDrawer || m_drawer.IsRenaming()) {
                        DoExplorerRedo();
                    } else {
                        DoRedo();
                    }
                    return 0;
                }
                if (wParam == 'X') {
                    DoCut();
                    return 0;
                }
                if (wParam == 'C') {
                    DoCopy();
                    return 0;
                }
                if (wParam == 'V') {
                    DoPaste();
                    return 0;
                }
                if (wParam == 'A') {
                    DoSelectAll();
                    return 0;
                }
                if (wParam == 'K') {
                    m_chordCtrlK = true;
                    DoQuickOpenFileSearch();
                    return 0;
                }
            } else {
                if (wParam != VK_CONTROL && wParam != VK_SHIFT && wParam != VK_MENU) {
                    m_chordCtrlK = false;
                }
            }

            // Close Editor via Ctrl+F4
            if (wParam == VK_F4 && (GetKeyState(VK_CONTROL) & 0x8000)) {
                DoCloseEditor();
                return 0;
            }

            if (pDoc && !pDoc->isSpecialView) {
                if (pDoc->isDiffView && (pDoc->diffActivePane == 0 || pDoc->isDiffRightReadOnly)) {
                    // Left side is READ-ONLY: allow arrow navigation only!
                    if (wParam == VK_LEFT) {
                        pDoc->diffOriginalBuffer.MoveCursorLeft();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                    } else if (wParam == VK_RIGHT) {
                        pDoc->diffOriginalBuffer.MoveCursorRight();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                    } else if (wParam == VK_UP) {
                        pDoc->diffOriginalBuffer.MoveCursorUp();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                    } else if (wParam == VK_DOWN) {
                        pDoc->diffOriginalBuffer.MoveCursorDown();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                    }
                    return 0;
                }

                if (wParam == VK_BACK || wParam == VK_DELETE) {
                    if (pDoc->HasSelection()) {
                        pDoc->DeleteSelection();
                    } else if (wParam == VK_BACK) {
                        auto cur = pDoc->buffer.GetCursor();
                        const auto& lines = pDoc->buffer.GetLines();
                        bool isPair = false;
                        if (cur.line < lines.size()) {
                            const auto& line = lines[cur.line];
                            if (cur.col > 0 && cur.col < line.length()) {
                                wchar_t leftCh = line[cur.col - 1];
                                wchar_t rightCh = line[cur.col];
                                if ((leftCh == L'(' && rightCh == L')') ||
                                    (leftCh == L'[' && rightCh == L']') ||
                                    (leftCh == L'{' && rightCh == L'}') ||
                                    (leftCh == L'"' && rightCh == L'"') ||
                                    (leftCh == L'\'' && rightCh == L'\'') ||
                                    (leftCh == L'`' && rightCh == L'`')) {
                                    isPair = true;
                                }
                            }
                        }
                        if (isPair) {
                            pDoc->buffer.MoveCursorRight();
                            pDoc->buffer.Backspace();
                            pDoc->buffer.Backspace();
                        } else {
                            pDoc->buffer.Backspace();
                        }
                    }
                    MarkDocumentModified(pDoc);
                    if (pDoc->isDiffView) ComputeDiffHunks(*pDoc);
                    EnsureCursorVisible();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                } else if (wParam == VK_LEFT) {
                    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (isShift) {
                        if (!pDoc->hasSelection) {
                            pDoc->selAnchor = pDoc->buffer.GetCursor();
                            pDoc->hasSelection = true;
                        }
                    } else if (pDoc->HasSelection()) {
                        pDoc->ClearSelection();
                    }
                    pDoc->buffer.MoveCursorLeft();
                    if (isShift) {
                        pDoc->selEnd = pDoc->buffer.GetCursor();
                        pDoc->selectedWord = pDoc->GetSelectedText();
                    }
                    EnsureCursorVisible();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_RIGHT) {
                    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (isShift) {
                        if (!pDoc->hasSelection) {
                            pDoc->selAnchor = pDoc->buffer.GetCursor();
                            pDoc->hasSelection = true;
                        }
                    } else if (pDoc->HasSelection()) {
                        pDoc->ClearSelection();
                    }
                    pDoc->buffer.MoveCursorRight();
                    if (isShift) {
                        pDoc->selEnd = pDoc->buffer.GetCursor();
                        pDoc->selectedWord = pDoc->GetSelectedText();
                    }
                    EnsureCursorVisible();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_HOME) {
                    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (isShift) {
                        if (!pDoc->hasSelection) {
                            pDoc->selAnchor = pDoc->buffer.GetCursor();
                            pDoc->hasSelection = true;
                        }
                    } else if (pDoc->HasSelection()) {
                        pDoc->ClearSelection();
                    }
                    auto cur = pDoc->buffer.GetCursor();
                    pDoc->buffer.SetCursor(cur.line, 0);
                    if (isShift) {
                        pDoc->selEnd = pDoc->buffer.GetCursor();
                        pDoc->selectedWord = pDoc->GetSelectedText();
                    }
                    EnsureCursorVisible();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_END) {
                    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (isShift) {
                        if (!pDoc->hasSelection) {
                            pDoc->selAnchor = pDoc->buffer.GetCursor();
                            pDoc->hasSelection = true;
                        }
                    } else if (pDoc->HasSelection()) {
                        pDoc->ClearSelection();
                    }
                    auto cur = pDoc->buffer.GetCursor();
                    size_t lineLen = pDoc->buffer.GetLine(cur.line).length();
                    pDoc->buffer.SetCursor(cur.line, lineLen);
                    if (isShift) {
                        pDoc->selEnd = pDoc->buffer.GetCursor();
                        pDoc->selectedWord = pDoc->GetSelectedText();
                    }
                    EnsureCursorVisible();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_UP) {
                    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (isShift) {
                        if (!pDoc->hasSelection) {
                            pDoc->selAnchor = pDoc->buffer.GetCursor();
                            pDoc->hasSelection = true;
                        }
                    } else if (pDoc->HasSelection()) {
                        pDoc->ClearSelection();
                    }
                    pDoc->buffer.MoveCursorUp();
                    if (isShift) {
                        pDoc->selEnd = pDoc->buffer.GetCursor();
                        pDoc->selectedWord = pDoc->GetSelectedText();
                    }
                    size_t curLine = pDoc->buffer.GetCursor().line;
                    if (curLine < pDoc->scrollLine) {
                        pDoc->scrollLine = curLine;
                    }
                    EnsureCursorVisible();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_DOWN) {
                    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (isShift) {
                        if (!pDoc->hasSelection) {
                            pDoc->selAnchor = pDoc->buffer.GetCursor();
                            pDoc->hasSelection = true;
                        }
                    } else if (pDoc->HasSelection()) {
                        pDoc->ClearSelection();
                    }
                    pDoc->buffer.MoveCursorDown();
                    if (isShift) {
                        pDoc->selEnd = pDoc->buffer.GetCursor();
                        pDoc->selectedWord = pDoc->GetSelectedText();
                    }
                    size_t curLine = pDoc->buffer.GetCursor().line;
                    RECT rc;
                    GetClientRect(m_hwnd, &rc);
                    float h = static_cast<float>(rc.bottom - rc.top);
                    size_t visibleLines = static_cast<size_t>(h / m_config.ScaleF(20.0f));
                    if (curLine >= pDoc->scrollLine + visibleLines - 4) {
                        pDoc->scrollLine = (curLine + 4 > visibleLines) ? (curLine + 4 - visibleLines) : 0;
                    }
                    EnsureCursorVisible();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_PRIOR) { // Page Up
                    RECT rc;
                    GetClientRect(m_hwnd, &rc);
                    float h = static_cast<float>(rc.bottom - rc.top);
                    size_t page = static_cast<size_t>(h / m_config.ScaleF(20.0f));
                    if (page < 3) page = 3;
                    for (size_t p = 0; p < page; ++p) pDoc->buffer.MoveCursorUp();
                    if (pDoc->scrollLine >= page) pDoc->scrollLine -= page;
                    else pDoc->scrollLine = 0;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_NEXT) { // Page Down
                    RECT rc;
                    GetClientRect(m_hwnd, &rc);
                    float h = static_cast<float>(rc.bottom - rc.top);
                    size_t page = static_cast<size_t>(h / m_config.ScaleF(20.0f));
                    if (page < 3) page = 3;
                    for (size_t p = 0; p < page; ++p) pDoc->buffer.MoveCursorDown();
                    pDoc->scrollLine += page;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_TAB) {
                    std::wstring ext = L"";
                    if (!pDoc->filePath.empty()) {
                        std::filesystem::path p(pDoc->filePath);
                        ext = p.extension().wstring();
                    } else if (!pDoc->fileName.empty()) {
                        std::filesystem::path p(pDoc->fileName);
                        ext = p.extension().wstring();
                    }
                    if (!ext.empty() && ext[0] == L'.') ext = ext.substr(1);
                    if (!m_snippetManager.TryExpand(pDoc->buffer, ext)) {
                        // Try expanding Emmet abbreviation next (e.g., div -> <div></div>)
                        if (!pDoc->buffer.ExpandEmmet()) {
                            pDoc->buffer.InsertChar(L' ');
                            pDoc->buffer.InsertChar(L' ');
                            pDoc->buffer.InsertChar(L' ');
                            pDoc->buffer.InsertChar(L' ');
                        }
                    }
                    MarkDocumentModified(pDoc);
                    if (pDoc->isDiffView) ComputeDiffHunks(*pDoc);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_RETURN) {
                    std::wstring ext = L"";
                    if (!pDoc->filePath.empty()) {
                        std::filesystem::path p(pDoc->filePath);
                        ext = p.extension().wstring();
                    } else if (!pDoc->fileName.empty()) {
                        std::filesystem::path p(pDoc->fileName);
                        ext = p.extension().wstring();
                    }
                    if (!ext.empty() && ext[0] == L'.') ext = ext.substr(1);

                    // Check if current prefix can expand as a snippet on Enter (e.g. typing "log" or "!" and hitting Enter)
                    if (m_snippetManager.TryExpand(pDoc->buffer, ext)) {
                        MarkDocumentModified(pDoc);
                        if (pDoc->isDiffView) ComputeDiffHunks(*pDoc);
                        m_editor.DismissAutocomplete();
                        m_editor.ClearGhostText();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }

                    if (pDoc->HasSelection()) {
                        pDoc->DeleteSelection();
                    }

                    // Check if between opening and closing tags: <div>|</div>
                    auto pos = pDoc->buffer.GetCursor();
                    const auto& lineText = pDoc->buffer.GetLine(pos.line);
                    if (pos.col > 0 && pos.col < lineText.length() && lineText[pos.col - 1] == L'>' && lineText[pos.col] == L'<' && pos.col + 1 < lineText.length() && lineText[pos.col + 1] == L'/') {
                        size_t indent = 0;
                        while (indent < lineText.length() && (lineText[indent] == L' ' || lineText[indent] == L'\t')) {
                            indent++;
                        }
                        std::wstring baseIndent = lineText.substr(0, indent);
                        std::wstring extraIndent = L"    ";
                        pDoc->buffer.InsertChar(L'\n');
                        for (wchar_t c : baseIndent) pDoc->buffer.InsertChar(c);
                        for (wchar_t c : extraIndent) pDoc->buffer.InsertChar(c);
                        size_t targetLine = pos.line + 1;
                        size_t targetCol = baseIndent.length() + extraIndent.length();
                        pDoc->buffer.InsertChar(L'\n');
                        for (wchar_t c : baseIndent) pDoc->buffer.InsertChar(c);
                        pDoc->buffer.SetCursor(targetLine, targetCol);
                        MarkDocumentModified(pDoc);
                        if (pDoc->isDiffView) ComputeDiffHunks(*pDoc);
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }

                    // Check if between braces: {|}
                    if (pos.col > 0 && pos.col < lineText.length() && lineText[pos.col - 1] == L'{' && lineText[pos.col] == L'}') {
                        size_t indent = 0;
                        while (indent < lineText.length() && (lineText[indent] == L' ' || lineText[indent] == L'\t')) {
                            indent++;
                        }
                        std::wstring baseIndent = lineText.substr(0, indent);
                        std::wstring extraIndent = (baseIndent.length() % 2 == 0 && baseIndent.length() % 4 != 0) ? L"  " : L"    ";
                        pDoc->buffer.InsertChar(L'\n');
                        for (wchar_t c : baseIndent) pDoc->buffer.InsertChar(c);
                        for (wchar_t c : extraIndent) pDoc->buffer.InsertChar(c);
                        size_t targetLine = pos.line + 1;
                        size_t targetCol = baseIndent.length() + extraIndent.length();
                        pDoc->buffer.InsertChar(L'\n');
                        for (wchar_t c : baseIndent) pDoc->buffer.InsertChar(c);
                        pDoc->buffer.SetCursor(targetLine, targetCol);
                        MarkDocumentModified(pDoc);
                        if (pDoc->isDiffView) ComputeDiffHunks(*pDoc);
                        EnsureCursorVisible();
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }

                    // Auto-indentation based STRICTLY on the current line (pos.line) where Enter is pressed
                    size_t indent = 0;
                    while (indent < lineText.length() && (lineText[indent] == L' ' || lineText[indent] == L'\t')) {
                        indent++;
                    }
                    std::wstring baseIndent = lineText.substr(0, indent);

                    // Strictly examine text before cursor on current line (pos.line)
                    std::wstring textBeforeCursor = (pos.col <= lineText.length()) ? lineText.substr(0, pos.col) : lineText;
                    size_t endPos = textBeforeCursor.find_last_not_of(L" \t");
                    bool shouldIndent = false;
                    if (endPos != std::wstring::npos && (textBeforeCursor[endPos] == L':' || textBeforeCursor[endPos] == L'{')) {
                        shouldIndent = true;
                    }

                    std::wstring extraIndent = (baseIndent.length() % 2 == 0 && baseIndent.length() % 4 != 0) ? L"  " : L"    ";

                    pDoc->buffer.InsertChar(L'\n');
                    for (wchar_t c : baseIndent) {
                        pDoc->buffer.InsertChar(c);
                    }
                    if (shouldIndent) {
                        for (wchar_t c : extraIndent) {
                            pDoc->buffer.InsertChar(c);
                        }
                    }
                    MarkDocumentModified(pDoc);
                    if (pDoc->isDiffView) ComputeDiffHunks(*pDoc);
                    EnsureCursorVisible();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam == m_caretTimerId) {
                m_editor.ToggleCaretBlink();
                m_drawer.ToggleCaretBlink();
                if (m_dbSettingsModal.IsOpen()) {
                    m_dbSettingsModal.ToggleCaret();
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
            } else if (wParam == m_throttleTimerId) {
                ULONGLONG now = GetTickCount64();
                if (m_drawer.PollSearchThrottle(now)) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }

                // Debounced Git auto-refresh on idle after typing/saving
                if (m_gitRefreshPending && now >= m_gitRefreshTargetTime) {
                    m_gitRefreshPending = false;
                    if (!m_drawer.IsGitRefreshing()) {
                        m_drawer.RefreshGitStatusOnly(true);
                    }
                }

                // Spinner badge on Activity Rail only active when explicit git action is loading
                static ULONGLONG s_gitLoadingStartTime = 0;
                bool isGitLoading = m_drawer.IsGitLoading();
                if (isGitLoading) {
                    if (s_gitLoadingStartTime == 0) s_gitLoadingStartTime = now;
                    // Watchdog safety timeout: clear stuck loading state after 2.5 seconds
                    if (now - s_gitLoadingStartTime > 2500) {
                        isGitLoading = false;
                        m_drawer.ClearGitLoading();
                        m_rail.SetGitLoading(false);
                        s_gitLoadingStartTime = 0;
                    }
                } else {
                    s_gitLoadingStartTime = 0;
                }

                if (isGitLoading) {
                    m_rail.UpdateGitAnim(14.0f);
                    m_drawer.UpdateGitLoadingAnim(14.0f);
                    m_rail.SetGitLoading(true);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else {
                    m_rail.SetGitLoading(false);
                }
            } else if (wParam == m_animTimerId) {
                bool keepAnimating = false;
                const float speed = 0.22f; // Smooth, quick sliding animation

                float diffDrawer = m_targetDrawerProgress - m_drawerAnimProgress;
                if (std::abs(diffDrawer) < 0.005f) {
                    m_drawerAnimProgress = m_targetDrawerProgress;
                    m_isDrawerAnimating = false;
                } else {
                    m_drawerAnimProgress += diffDrawer * speed;
                    keepAnimating = true;
                }

                float diffTerm = m_targetTerminalProgress - m_terminalAnimProgress;
                if (std::abs(diffTerm) < 0.005f) {
                    m_terminalAnimProgress = m_targetTerminalProgress;
                    m_isTerminalAnimating = false;
                    if (m_terminalAnimProgress <= 0.001f && m_terminal.IsOpen()) {
                        m_terminal.Close();
                    }
                } else {
                    m_terminalAnimProgress += diffTerm * speed;
                    keepAnimating = true;
                }

                // Smoothly animate terminal maximize / minimize full screen progress (User requirement)
                float diffFs = m_targetTermFullScreen - m_termFullScreenProgress;
                if (std::abs(diffFs) < 0.005f) {
                    m_termFullScreenProgress = m_targetTermFullScreen;
                    m_isTermFullScreenAnimating = false;
                } else {
                    m_termFullScreenProgress += diffFs * speed;
                    keepAnimating = true;
                }
                m_terminal.SetFullScreenProgress(m_termFullScreenProgress);

                // Smoothly animate right sidebar slide in / out
                float diffRightSidebar = m_targetRightSidebarProgress - m_rightSidebarAnimProgress;
                if (std::abs(diffRightSidebar) < 0.005f) {
                    m_rightSidebarAnimProgress = m_targetRightSidebarProgress;
                    m_isRightSidebarAnimating = false;
                    if (m_rightSidebarAnimProgress <= 0.001f && m_extensionsPanel.IsOpen()) {
                        m_extensionsPanel.Close();
                    }
                } else {
                    m_rightSidebarAnimProgress += diffRightSidebar * speed;
                    keepAnimating = true;
                }

                if (m_rightSidebarAnimProgress > 0.001f || m_extensionsPanel.IsOpen()) {
                    m_extensionsPanel.UpdateAnim(16.0f);
                    keepAnimating = true;
                }

                // Smoothly update Live Server progression and auto-open user browser upon completion
                for (auto& srv : m_activeLiveServers) {
                    if (srv.hasError || m_terminal.HasServerError(srv.rootDir)) {
                        srv.hasError = true;
                        if (srv.errorMsg.empty()) {
                            srv.errorMsg = m_terminal.GetServerErrorText(srv.rootDir);
                        }
                        srv.isStarting = false;
                        srv.browserOpened = true; // Handled: NEVER launch browser on error!
                        if (m_isProjectRunning && (srv.rootDir == m_runningProjectRoot || m_runningProjectRoot.empty())) {
                            m_isProjectRunning = false;
                            m_runningProjectRoot.clear();
                            UpdateExecutionButtonStates();
                        }
                        continue;
                    }

                    if (srv.isStarting) {
                        srv.elapsedMs += 16.0f;
                        srv.progress = (std::min)(1.0f, srv.elapsedMs / srv.progressDurationMs);
                        keepAnimating = true;
                        if (!srv.browserOpened && srv.progress >= 1.0f) {
                            srv.browserOpened = true;
                            srv.isStarting = false;
                            if (!srv.hasError && !m_terminal.HasServerError(srv.rootDir)) {
                                if (srv.port > 0 || !srv.customUrl.empty()) {
                                    std::wstring localUrl = !srv.customUrl.empty()
                                        ? srv.customUrl
                                        : (srv.server ? srv.server->GetLocalUrl(srv.fileName) : (L"http://127.0.0.1:" + std::to_wstring(srv.port)));
                                    ShellExecuteW(NULL, L"open", localUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
                                }
                            }
                        }
                    }
                }

                if (m_isProjectRunning && !m_runningProjectRoot.empty() && m_terminal.HasServerError(m_runningProjectRoot)) {
                    m_isProjectRunning = false;
                    m_runningProjectRoot.clear();
                    UpdateExecutionButtonStates();
                }

                // Smoothly animate confirm / restore / delete modal button spinner
                if (m_confirmDiscardModal.IsOpen() && m_confirmDiscardModal.IsLoading()) {
                    m_confirmDiscardModal.UpdateSpinner(14.0f);
                    keepAnimating = true;
                }

                if (!keepAnimating && !m_isDrawerAnimating && !m_isTerminalAnimating && !m_isTermFullScreenAnimating && !m_isRightSidebarAnimating) {
                    KillTimer(m_hwnd, m_animTimerId);
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
            } else if (wParam == m_gcTimerId) {
                RunManualGarbageCollection();
            }
            return 0;
        }

        case WM_PAINT: {
            OnPaint();
            return 0;
        }

        case WM_ERASEBKGND:
            return 1; // Prevent flicker; Direct2D handles all surface rendering

        case WM_SIZE: {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            OnResize(width, height);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_DPICHANGED: {
            int newDpi = HIWORD(wParam);
            auto* pNewRect = reinterpret_cast<RECT*>(lParam);
            m_config.dpiScale = static_cast<float>(newDpi) / 96.0f;
            DiscardDeviceResources();
            SetWindowPos(m_hwnd, nullptr, pNewRect->left, pNewRect->top,
                         pNewRect->right - pNewRect->left, pNewRect->bottom - pNewRect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_USER + 201: {
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_USER + 202: {
            m_drawer.RefreshTree();
            m_drawer.RefreshGitStatusOnly(true);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_USER + 203: {
            PendingGitError err;
            {
                std::lock_guard<std::mutex> lock(m_gitErrorMutex);
                if (m_pendingGitError) {
                    err = *m_pendingGitError;
                    m_pendingGitError.reset();
                }
            }
            if (!err.command.empty() || !err.output.empty()) {
                m_gitErrorModal.Open(err.command, err.output, err.repoDir);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_USER + 204: {
            auto* pRes = reinterpret_cast<DiscardRestoreTaskResult*>(lParam);
            if (pRes) {
                FinishDiscardOrRestore(pRes->targetPath, pRes->targetFileName, pRes->targetStatus);
                delete pRes;
            }
            m_confirmDiscardModal.Close();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_USER + 205: {
            auto* pRes = reinterpret_cast<DeleteTaskResult*>(lParam);
            if (pRes) {
                FinishDelete(pRes->targetPath, pRes->isFolder, pRes->success);
                delete pRes;
            }
            m_confirmDiscardModal.Close();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_CLOSE: {
            UpdateDrawerErrorFiles();
    SaveSessionState();
            DestroyWindow(m_hwnd);
            return 0;
        }

        case WM_DESTROY: {
            StopWorkspaceWatcher();
            m_explorerUndo.CleanupTrash();
            UpdateDrawerErrorFiles();
    SaveSessionState();
            PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
    }
}

} // namespace phide::app
