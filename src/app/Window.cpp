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
#include <windowsx.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace phide::app {

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
                // Coalesce rapid disk events (debounced quiet period)
                Sleep(1500);
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
    float savedGitSplitY = 320.0f;
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
        m_recentPaths = recentPaths;
        if (m_recentPaths.size() > 6) m_recentPaths.resize(6);
    }

    m_config.activeRailTab = activeRailTab;
    if (savedGitSplitY > 100.0f) {
        m_config.gitSplitY = savedGitSplitY;
        m_drawer.SetGitSplitY(savedGitSplitY, 100.0f, 2000.0f);
    }

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
    SaveSessionState();
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
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoNewFile() {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0C/C++ Files (*.cpp;*.h;*.hpp)\0*.cpp;*.h;*.hpp\0PHP Files (*.php)\0*.php\0JSON Files (*.json)\0*.json\0Text Files (*.txt)\0*.txt\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    std::wstring initDir;
    if (m_drawer.HasOpenFolder()) {
        initDir = m_drawer.GetOpenFolderPath();
        ofn.lpstrInitialDir = initDir.c_str();
    }
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_ENABLESIZING;

    if (GetSaveFileNameW(&ofn)) {
        std::ofstream f(std::filesystem::path{ szFile });
        f.close();
        OpenFileDocument(szFile, false);
        if (m_drawer.HasOpenFolder()) {
            m_drawer.RefreshTree();
        }
    }
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
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0C/C++ Files (*.cpp;*.h;*.hpp)\0*.cpp;*.h;*.hpp\0PHP Files (*.php)\0*.php\0JSON Files (*.json)\0*.json\0Text Files (*.txt)\0*.txt\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ENABLESIZING;

    if (GetOpenFileNameW(&ofn)) {
        OpenFileDocument(szFile, false);
    }
}

void Window::OpenFileDocument(const std::wstring& path, bool isPreview) {
    if (path.empty()) return;

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
                SaveSessionState();
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        }
    }

    std::wstring resolvedPath = path;
    if (!std::filesystem::exists(resolvedPath) && m_drawer.HasOpenFolder()) {
        std::wstring candidate = m_drawer.GetOpenFolderPath() + L"\\" + path;
        if (std::filesystem::exists(candidate)) {
            resolvedPath = candidate;
        }
    }

    auto& group = GetActiveGroup();

    // 2. Check if current active document can be replaced
    // VS Code Rule: a tab is replaced ONLY if it is a preview tab AND has NO unsaved edits (!isDirty) AND not a special view.
    bool canReplace = (group.activeDocIdx >= 0 && group.activeDocIdx < static_cast<int>(group.docs.size()) &&
                       group.docs[group.activeDocIdx].isPreview &&
                       !group.docs[group.activeDocIdx].isDirty &&
                       !group.docs[group.activeDocIdx].isSpecialView);

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

        // Compute Git diff highlights against HEAD if inside a git repository (Image 5)
        if (m_drawer.IsGitRepo()) {
            std::wstring relPath = resolvedPath;
            if (m_drawer.HasOpenFolder()) {
                try {
                    relPath = std::filesystem::relative(resolvedPath, m_drawer.GetOpenFolderPath()).wstring();
                } catch (...) {}
            }
            m_drawer.ComputeGitDiffLines(relPath, newDoc.diffAddedLines, newDoc.diffDeletedLines);
            if (!newDoc.diffAddedLines.empty() || !newDoc.diffDeletedLines.empty()) {
                newDoc.isDiffView = true;
            }
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

    // Add to recent paths LRU list (max 6 entries)
    auto it = std::find(m_recentPaths.begin(), m_recentPaths.end(), resolvedPath);
    if (it != m_recentPaths.end()) m_recentPaths.erase(it);
    m_recentPaths.insert(m_recentPaths.begin(), resolvedPath);
    if (m_recentPaths.size() > 6) m_recentPaths.resize(6);

    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::OpenDiffDocument(const std::wstring& relPath, bool isPreview) {
    if (relPath.empty()) return;
    std::wstring dir = m_drawer.GetOpenFolderPath();
    std::wstring fullPath = dir.empty() ? relPath : (dir + L"\\" + relPath);
    std::wstring fileName = std::filesystem::path(relPath).filename().wstring();

    std::wstring headContent = m_drawer.GetGitFileContent(relPath);

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
        return; // Identical
    }

    if (midN > 0 && midM > 0 && (midN * midM <= 2500000)) {
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

void Window::CloseDocument(int groupIdx, int docIdx) {
    if (groupIdx < 0 || groupIdx >= static_cast<int>(m_editorGroups.size())) return;
    auto& group = m_editorGroups[groupIdx];
    if (docIdx < 0 || docIdx >= static_cast<int>(group.docs.size())) return;

    if (!group.docs[docIdx].filePath.empty()) {
        m_lastClosedFilePath = group.docs[docIdx].filePath;
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
    SaveSessionState();
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
    m_drawer.RefreshGitState(true);
    StartWorkspaceWatcher(path);

    // Track in recent paths LRU list (max 6 entries)
    auto it = std::find(m_recentPaths.begin(), m_recentPaths.end(), path);
    if (it != m_recentPaths.end()) m_recentPaths.erase(it);
    m_recentPaths.insert(m_recentPaths.begin(), path);
    if (m_recentPaths.size() > 6) m_recentPaths.resize(6);

    if (!m_config.isDrawerOpen) {
        ToggleSidebar();
    }
    SaveSessionState();
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
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0C/C++ Files (*.cpp;*.h;*.hpp)\0*.cpp;*.h;*.hpp\0PHP Files (*.php)\0*.php\0JSON Files (*.json)\0*.json\0Text Files (*.txt)\0*.txt\0";
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
                doc.buffer.SaveToFile(doc.filePath);
                doc.isDirty = false;
                anySaved = true;
                SyncDocumentBuffers(&doc);
                if (doc.filePath == m_snippetManager.GetSnippetsFilePath()) {
                    m_snippetManager.LoadSnippets();
                }
            }
        }
    }
    if (anySaved) {
        UpdateDrawerDirtyFiles();
        TriggerDebouncedGitRefresh(1000);
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
    SaveSessionState();
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
                dirtyPaths.push_back(doc.filePath);
            }
        }
    }
    m_drawer.SetDirtyFiles(dirtyPaths);
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

    // Check if user is completing an HTML tag (e.g. typed "<div" or "div")
    bool isTagKind = (item->kind == editor::CompletionKind::Tag);
    size_t prefixStart = (pos.col >= prefix.length()) ? (pos.col - prefix.length()) : 0;
    bool hasLeadingAngle = (prefixStart > 0 && lineText[prefixStart - 1] == L'<');

    static const std::unordered_set<std::wstring> voidTags = {
        L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
        L"link", L"meta", L"param", L"source", L"track", L"wbr"
    };

    std::wstring tagLower = item->label;
    for (auto& c : tagLower) c = towlower(c);
    bool isVoid = (voidTags.count(tagLower) > 0);

    if (isTagKind || hasLeadingAngle) {
        // Tag completion: create the whole block! (User complaint: "instead creating the whole block")
        for (size_t i = 0; i < prefix.length(); ++i) {
            pDoc->buffer.Backspace();
        }

        if (hasLeadingAngle) {
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
    if (m_autoSave && !pDoc->filePath.empty()) {
        pDoc->buffer.SaveToFile(pDoc->filePath);
        pDoc->isDirty = false;
        SyncDocumentBuffers(pDoc);
        UpdateDrawerDirtyFiles();
        TriggerDebouncedGitRefresh(1500);
    } else {
        pDoc->isDirty = true;
        SyncDocumentBuffers(pDoc);
        UpdateDrawerDirtyFiles();
        TriggerDebouncedGitRefresh(2000);
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

void Window::DoToggleWordWrap() {
    m_wordWrap = !m_wordWrap;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoShowExplorer() {
    m_config.activeRailTab = 0;
    if (!m_config.isDrawerOpen) {
        ToggleSidebar();
    }
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

void Window::DoShowSearch() {
    m_config.activeRailTab = 1;
    if (!m_config.isDrawerOpen) {
        ToggleSidebar();
    }
    RefreshOpenDocPathsForSearch();
    m_editor.GetKeyboardShortcutsView().SetSearchFocused(false);
    m_drawer.SetSearchFocus(1);
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
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoCreateTerminal() {
    std::wstring dir;
    if (m_drawer.HasOpenFolder()) {
        dir = m_drawer.GetOpenFolderPath();
    }
    m_terminal.Toggle(dir);
    m_isTerminalFocused = m_terminal.IsOpen();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::UpdateExecutionButtonStates() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc || pDoc->isSpecialView) {
        m_header.SetRunEnabled(false);
        m_header.SetDebugEnabled(false);
        return;
    }

    std::filesystem::path p(pDoc->filePath);
    std::wstring ext = p.extension().wstring();
    for (auto& c : ext) c = towlower(c);

    // Executable file types for Run: .html, .htm, .js, .mjs, .cjs, .ts, .jsx, .tsx, .py
    // CSS and unsupported files are disabled per user specification
    bool isHtml = (ext == L".html" || ext == L".htm");
    bool isJsTs = (ext == L".js" || ext == L".mjs" || ext == L".cjs" || ext == L".ts" || ext == L".jsx" || ext == L".tsx");
    bool isPython = (ext == L".py");

    bool canRun = isHtml || isJsTs || isPython;
    bool canDebug = isJsTs || isPython || isHtml;

    m_header.SetRunEnabled(canRun);
    m_header.SetDebugEnabled(canDebug);
}

void Window::DoRunActiveDocument() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc) return;

    std::filesystem::path p(pDoc->filePath);
    std::wstring ext = p.extension().wstring();
    for (auto& c : ext) c = towlower(c);

    if (ext == L".html" || ext == L".htm") {
        // Fast Live Server startup with clickable URLs
        std::wstring folder = p.parent_path().wstring();
        if (folder.empty() && m_drawer.HasOpenFolder()) {
            folder = m_drawer.GetOpenFolderPath();
        }
        if (folder.empty()) {
            folder = m_workspace.GetRootPath();
        }
        if (folder.empty()) {
            wchar_t cur[MAX_PATH];
            GetCurrentDirectoryW(MAX_PATH, cur);
            folder = cur;
        }

        if (!m_liveServer.IsRunning() || m_liveServer.GetRootDir() != folder) {
            m_liveServer.Stop();
            m_liveServer.Start(folder, p.filename().wstring(), 5500);
        }

        std::wstring fileName = p.filename().wstring();
        std::wstring localUrl = m_liveServer.GetLocalUrl(fileName);
        std::wstring networkUrl = m_liveServer.GetNetworkUrl(fileName);

        m_terminal.Open(folder);
        m_terminal.StartLiveServerSession(localUrl, networkUrl);
        m_isTerminalFocused = true;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else if (ext == L".js" || ext == L".mjs" || ext == L".cjs" || ext == L".ts" || ext == L".jsx" || ext == L".tsx") {
        std::wstring folder = p.parent_path().wstring();
        if (folder.empty() && m_drawer.HasOpenFolder()) folder = m_drawer.GetOpenFolderPath();
        if (folder.empty()) folder = m_workspace.GetRootPath();

        m_terminal.Open(folder);
        m_terminal.ExecuteCommandLine(L"node \"" + pDoc->filePath + L"\"");
        m_isTerminalFocused = true;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else if (ext == L".py") {
        std::wstring folder = p.parent_path().wstring();
        if (folder.empty() && m_drawer.HasOpenFolder()) folder = m_drawer.GetOpenFolderPath();
        if (folder.empty()) folder = m_workspace.GetRootPath();

        m_terminal.Open(folder);
        m_terminal.ExecuteCommandLine(L"python \"" + pDoc->filePath + L"\"");
        m_isTerminalFocused = true;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoDebugActiveDocument() {
    auto* pDoc = GetActiveDoc();
    if (!pDoc) return;

    std::wstring folder = std::filesystem::path(pDoc->filePath).parent_path().wstring();
    if (folder.empty() && m_drawer.HasOpenFolder()) folder = m_drawer.GetOpenFolderPath();
    if (folder.empty()) folder = m_workspace.GetRootPath();

    m_terminal.Open(folder);
    m_terminal.StartDebugSession(pDoc->filePath, folder);
    m_isTerminalFocused = true;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::LoadFileFromPath(const std::wstring& path) {
    OpenFileDocument(path, false);
}

void Window::SaveFileToPath(const std::wstring& path) {
    auto* pDoc = GetActiveDoc();
    if (pDoc) {
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
            pDoc->isPreview = false;
            if (path == m_snippetManager.GetSnippetsFilePath()) {
                m_snippetManager.LoadSnippets();
            }
            SyncDocumentBuffers(pDoc);
            UpdateDrawerDirtyFiles();
            TriggerDebouncedGitRefresh(1000);
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
            L"    \"description\": \"High-Performance Modern C++ & PHP IDE\",\n"
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

bool Window::Create(HINSTANCE hInstance, int nCmdShow) {
    if (FAILED(CreateDeviceIndependentResources())) {
        return false;
    }

    WNDCLASSEX wc{};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = Window::WndProc;
    wc.hInstance = hInstance;
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
        L"phide - Modern C++ & PHP IDE",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, width, height,
        nullptr, nullptr, hInstance, this
    );

    if (!m_hwnd) return false;

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

    // Register callback for immediate terminal output repainting
    m_terminal.SetOutputCallback([this]() {
        if (m_hwnd) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    });

    // Initialize user snippets engine
    m_snippetManager.Initialize(GetSnippetsConfigPath());

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

    // Restore saved IDE session state (open folder, expanded tree dirs, open file, sidebar open/close state)
    LoadSessionState();

    if (m_drawer.HasOpenFolder()) {
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

    const float canvasLeft  = railW + currentDrawerW;
    const float canvasWidth = (std::max)(100.0f, clientW - canvasLeft);

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

    bool suppressEditorCaret = m_quickOpen.IsOpen() || m_themePicker.IsOpen() || m_popupMenu.IsOpen() ||
        (m_terminal.IsOpen() && m_isTerminalFocused) ||
        m_drawer.IsRenaming() ||
        (m_drawerAnimProgress > 0.001f && (m_drawer.IsGitCommitBoxFocused() || m_drawer.IsSearchFocused()));

    const float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));

    // Pre-fill the corner patch at (railW, headerH) with headerBg
    // so the 14px inward rounded arc of DrawerPanel (or EditorCanvas when drawer is closed)
    // curves smoothly against the title header and activity rail across all 20 themes.
    D2D1_RECT_F rcCornerPatch = D2D1::RectF(railW, headerH, railW + m_config.ScaleF(18.0f), headerH + m_config.ScaleF(18.0f));
    m_pRenderTarget->FillRectangle(rcCornerPatch, m_theme.brushHeaderBg.Get());

    float termH = 0.0f;
    if (m_terminal.IsOpen()) {
        termH = m_terminal.IsFullScreen() ? (clientH - headerH) : m_terminal.GetHeight();
    }
    float editorH = clientH - termH;

    m_editor.Render(m_pRenderTarget.Get(), m_theme, m_config, m_editorGroups, m_focusedGroupIdx,
                    canvasLeft, canvasWidth, editorH, hasLeftDrawer,
                    searchHL, hlMatchCase, hlWholeWord, hlUseRegex,
                    suppressEditorCaret);

    // Integrated Terminal Panel at Bottom (Images 1, 2, 3, 4)
    if (m_terminal.IsOpen()) {
        m_terminal.Render(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH, canvasLeft);
    }

    // Live Server Status Badge / Pill (Image 2: ⚡ Port: 5500)
    if (m_liveServer.IsRunning()) {
        float pillW = m_config.ScaleF(110.0f);
        float pillH = m_config.ScaleF(22.0f);
        float pillX = clientW - pillW - m_config.ScaleF(12.0f);
        float pillY = clientH - pillH - m_config.ScaleF(6.0f);
        if (m_terminal.IsOpen()) {
            float th = m_terminal.IsFullScreen() ? (clientH - headerH) : m_terminal.GetHeight();
            pillY = clientH - th - pillH - m_config.ScaleF(6.0f);
        }
        m_liveServerPillBounds = D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH);
        D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(m_liveServerPillBounds, 4.0f, 4.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBgBrush;
        m_pRenderTarget->CreateSolidColorBrush(
            m_hoverLiveServerPill
                ? (m_theme.IsDarkMode() ? D2D1::ColorF(0.20f, 0.40f, 0.25f, 0.95f) : D2D1::ColorF(0.80f, 0.95f, 0.85f, 0.95f))
                : (m_theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.25f, 0.16f, 0.90f) : D2D1::ColorF(0.88f, 0.96f, 0.90f, 0.90f)),
            &pillBgBrush
        );
        if (pillBgBrush) m_pRenderTarget->FillRoundedRectangle(rrcPill, pillBgBrush.Get());
        m_pRenderTarget->DrawRoundedRectangle(rrcPill, m_theme.brushBrandAccent.Get() ? m_theme.brushBrandAccent.Get() : m_theme.brushBorderSubtle.Get(), 1.0f);

        std::wstring pillText = L"⚡ Port: " + std::to_wstring(m_liveServer.GetPort());
        D2D1_RECT_F rcPillText = D2D1::RectF(pillX + m_config.ScaleF(6.0f), pillY, pillX + pillW, pillY + pillH);
        m_pRenderTarget->DrawText(
            pillText.c_str(),
            static_cast<UINT32>(pillText.length()),
            m_theme.fontMenuItem.Get() ? m_theme.fontMenuItem.Get() : m_theme.fontCode.Get(),
            rcPillText,
            m_theme.brushTextWhite.Get() ? m_theme.brushTextWhite.Get() : m_theme.brushTextPrimary.Get()
        );
    } else {
        m_liveServerPillBounds = {};
    }

    // 2. Render Slide-out Drawer Panel (when visible or animating, with 14px rounded curve at top-left)
    if (hasLeftDrawer) {
        m_drawer.Render(m_pRenderTarget.Get(), m_theme, m_config, clientH, currentDrawerW);
    }

    // 3. Render Collapsed Activity Rail (Home -> Search -> Git with customized tooltips)
    m_rail.SetGitLoading(m_drawer.IsGitLoading());
    m_rail.SetGitBadgeCount(m_drawer.GetGitChanges().size());
    m_rail.Render(m_pRenderTarget.Get(), m_theme, m_config, clientH);

    // 4. Render Dark Frameless Title Header at Top (with active highlight on Sidebar button when open)
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

    // 7. Render Quick Open File Search Palette Modal (Ctrl+K, Ctrl+P, Magnifying button)
    if (m_quickOpen.IsOpen()) {
        m_quickOpen.Render(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH);
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

void Window::HandleMouseMove(POINT clientPt) {
    if (!m_trackingMouse) {
        TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
        m_trackingMouse = true;
    }

    bool oldHoverPill = m_hoverLiveServerPill;
    m_hoverLiveServerPill = m_liveServer.IsRunning() &&
        (clientPt.x >= m_liveServerPillBounds.left && clientPt.x <= m_liveServerPillBounds.right &&
         clientPt.y >= m_liveServerPillBounds.top && clientPt.y <= m_liveServerPillBounds.bottom);
    if (m_hoverLiveServerPill != oldHoverPill) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    if (m_hoverLiveServerPill) {
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
            size_t line = 0, col = 0;
            if (m_editor.HitTestCodeArea(clientPt, m_config, *pActiveDoc, paneCanvasLeft, clientH, line, col)) {
                pActiveDoc->selEnd = { line, col };
                pActiveDoc->buffer.SetCursor(line, col);
                if (pActiveDoc->selAnchor.line != pActiveDoc->selEnd.line || pActiveDoc->selAnchor.col != pActiveDoc->selEnd.col) {
                    pActiveDoc->hasSelection = true;
                    pActiveDoc->selectedWord = pActiveDoc->GetSelectedText();
                } else {
                    pActiveDoc->hasSelection = false;
                    pActiveDoc->selectedWord.clear();
                }

                // Edge scrolling when dragging near top/bottom
                float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));
                if (clientPt.y < headerH + 25 && pActiveDoc->scrollLine > 0) {
                    pActiveDoc->scrollLine--;
                } else if (clientPt.y > clientH - 25 && pActiveDoc->scrollLine + 1 < pActiveDoc->buffer.GetLineCount()) {
                    pActiveDoc->scrollLine++;
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

    // Check Minimap viewport slider dragging (Image 5)
    if (pActiveDoc && pActiveDoc->isDraggingMinimap) {
        float deltaY = static_cast<float>(clientPt.y) - pActiveDoc->minimapDragStartY;
        float trackH = pActiveDoc->minimapTrack.bottom - pActiveDoc->minimapTrack.top;
        float sliderH = pActiveDoc->minimapSlider.bottom - pActiveDoc->minimapSlider.top;
        float scrollRange = trackH - sliderH;
        if (scrollRange > 0.0f) {
            size_t totalLines = pActiveDoc->buffer.GetLineCount();
            float linesPerPixel = static_cast<float>(totalLines) / scrollRange;
            int newScroll = static_cast<int>(pActiveDoc->minimapDragStartLine) + static_cast<int>(deltaY * linesPerPixel);
            if (newScroll < 0) newScroll = 0;
            if (totalLines > 0 && static_cast<size_t>(newScroll) >= totalLines) {
                newScroll = static_cast<int>(totalLines - 1);
            }
            pActiveDoc->scrollLine = static_cast<size_t>(newScroll);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // Forward to keyboard shortcuts view if active doc is keybindings
    if (pActiveDoc && pActiveDoc->isSpecialView && pActiveDoc->viewType == L"keybindings") {
        if (m_editor.GetKeyboardShortcutsView().HandleMouseMove(clientPt)) {
            needRepaint = true;
        }
    }

    // Check Header menu, execution buttons, split editor button, zoom buttons, and caption button hover
    auto hoveredMenu = m_header.HitTestMenu(clientPt);
    auto hoveredCaption = m_header.HitTestCaptionButton(clientPt);
    auto hoveredExec = m_header.HitTestExecButton(clientPt);
    if (m_header.UpdateHoverState(hoveredMenu, hoveredCaption, hoveredExec)) needRepaint = true;
    if (m_header.SetHoveredSplitEditor(m_header.HitTestSplitEditor(clientPt))) needRepaint = true;
    if (m_header.SetHoveredQuickOpen(m_header.HitTestQuickOpen(clientPt))) needRepaint = true;

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
        }
    }

    // Check Rail icon hover (triggers customized tooltip)
    auto hoveredRail = m_rail.HitTest(clientPt);
    if (m_rail.SetHoveredTab(hoveredRail)) needRepaint = true;

    // Check Drawer hover
    if (m_drawerAnimProgress > 0.001f) {
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
                RECT clientRc;
                GetClientRect(m_hwnd, &clientRc);
                const float clientH = static_cast<float>(clientRc.bottom - clientRc.top);
                m_drawer.SetGitSplitY(static_cast<float>(clientPt.y), headerH + 110.0f, clientH - 70.0f);
                needRepaint = true;
            } else if (m_drawer.HandleGitMouseMove(clientPt)) {
                needRepaint = true;
            }
        }
    }

    if (needRepaint) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::HandleLButtonDown(POINT clientPt) {
    // Check Live Server Status Pill Click (Opens browser to localUrl)
    if (m_liveServer.IsRunning() && clientPt.x >= m_liveServerPillBounds.left && clientPt.x <= m_liveServerPillBounds.right &&
        clientPt.y >= m_liveServerPillBounds.top && clientPt.y <= m_liveServerPillBounds.bottom) {
        auto* pDoc = GetActiveDoc();
        std::wstring fileName = pDoc ? std::filesystem::path(pDoc->filePath).filename().wstring() : L"";
        std::wstring url = m_liveServer.GetLocalUrl(fileName);
        ShellExecuteW(NULL, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return;
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
        if (m_drawer.HitTestGit(clientPt, m_config, openFile, discardedFile, isDiff, openCommitDiff, openUrl)) {
            if (!openUrl.empty()) {
                ShellExecuteW(nullptr, L"open", openUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            if (!openCommitDiff.empty()) {
                OpenCommitDiffDocument(openCommitDiff, openFile);
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

    // 0. Theme Picker Modal Clicks: only updates theme if a theme card was clicked
    if (m_themePicker.IsOpen()) {
        ui::ThemeMode chosenMode = m_theme.GetThemeMode();
        if (m_themePicker.HandleLButtonDown(clientPt, chosenMode)) {
            m_theme.SetThemeMode(chosenMode);
            BOOL dm = m_theme.IsDarkMode() ? TRUE : FALSE;
            DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dm, sizeof(dm));
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            SaveSessionState();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 0b. Quick Open Modal Clicks (Image 5)
    if (m_quickOpen.IsOpen()) {
        std::wstring chosenFile;
        if (m_quickOpen.HandleMouseDown(clientPt, chosenFile)) {
            if (!chosenFile.empty()) {
                OpenFileDocument(chosenFile, false);
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
                            std::wstring confirmMsg = L"Are you sure you want to delete '" +
                                std::filesystem::path(m_contextTargetPath).filename().wstring() + L"'?";
                            if (MessageBoxW(m_hwnd, confirmMsg.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
                                std::wstring backup;
                                bool isFolder = std::filesystem::is_directory(m_contextTargetPath);
                                m_explorerUndo.PrepareDelete(m_contextTargetPath, isFolder, backup);
                                std::error_code ec;
                                std::filesystem::remove_all(m_contextTargetPath, ec);
                                if (!ec) {
                                    m_explorerUndo.PushDelete(m_contextTargetPath, backup, isFolder);
                                }
                                for (size_t g = 0; g < m_editorGroups.size(); ++g) {
                                    for (int i = 0; i < static_cast<int>(m_editorGroups[g].docs.size()); ++i) {
                                        if (m_editorGroups[g].docs[i].filePath == m_contextTargetPath) {
                                            CloseDocument(static_cast<int>(g), i);
                                            break;
                                        }
                                    }
                                }
                                m_drawer.RefreshTree();
                            }
                        }
                        break;
                    case ui::MenuActionId::CtxNewFile: {
                        std::wstring parentDir = m_contextTargetIsFolder ? m_contextTargetPath
                            : std::filesystem::path(m_contextTargetPath).parent_path().wstring();
                        std::wstring newName = L"Untitled.txt";
                        if (PromptInputDialog(m_hwnd, L"New File", L"File name:", newName, newName) && !newName.empty()) {
                            std::filesystem::path newPath = std::filesystem::path(parentDir) / newName;
                            if (!std::filesystem::exists(newPath)) {
                                std::ofstream ofs(newPath);
                                ofs.close();
                            }
                            m_explorerUndo.PushCreate(newPath.wstring(), false);
                            m_drawer.RefreshTree();
                            OpenFileDocument(newPath.wstring(), false);
                        }
                        break;
                    }
                    case ui::MenuActionId::CtxNewFolder: {
                        std::wstring parentDir = m_contextTargetIsFolder ? m_contextTargetPath
                            : std::filesystem::path(m_contextTargetPath).parent_path().wstring();
                        std::wstring newFolder = L"NewFolder";
                        if (PromptInputDialog(m_hwnd, L"New Folder", L"Folder name:", newFolder, newFolder) && !newFolder.empty()) {
                            std::filesystem::path newDirPath = std::filesystem::path(parentDir) / newFolder;
                            std::error_code ec;
                            std::filesystem::create_directories(newDirPath, ec);
                            if (!ec) {
                                m_explorerUndo.PushCreate(newDirPath.wstring(), true);
                            }
                            m_drawer.RefreshTree();
                        }
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

    // 2. Execution Widget Button Clicks (Run, Debug, Terminal)
    auto execHit = m_header.HitTestExecButton(clientPt);
    if (execHit != ui::HeaderExecId::None) {
        if (execHit == ui::HeaderExecId::Run) {
            DoRunActiveDocument();
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

    const int railW = m_config.Scale(m_config.railWidth);
    const int drawerW = static_cast<int>(m_config.Scale(m_config.drawerWidth) * m_drawerAnimProgress);
    const float canvasLeft = static_cast<float>(railW + drawerW);
    const float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));
    RECT clientRc;
    GetClientRect(m_hwnd, &clientRc);
    const float canvasWidth = static_cast<float>(clientRc.right) - canvasLeft;
    const float clientH = static_cast<float>(clientRc.bottom - clientRc.top);

    // 4. Editor Canvas Clicks (Multi-Editor Groups aware)
    if (clientPt.x >= canvasLeft && clientPt.y >= headerH && !m_editorGroups.empty() && canvasWidth > 10.0f) {
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

            // 4a. Tab Close Button Click in this group
            int closeIdx = m_editor.HitTestTabClose(clientPt, group.docs);
            if (closeIdx >= 0) {
                CloseDocument(targetGroup, closeIdx);
                return;
            }

            // 4b. Tab Header Click in this group
            int tabHitIdx = m_editor.HitTestTab(clientPt, group.docs);
            if (tabHitIdx >= 0) {
                group.activeDocIdx = tabHitIdx;
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

                    // Minimap click & drag (Image 5)
                    bool hitMinimapSlider = false;
                    if (m_config.showMinimap && m_editor.HitTestMinimap(clientPt, doc, hitMinimapSlider)) {
                        doc.isDraggingMinimap = true;
                        doc.minimapDragStartY = static_cast<float>(clientPt.y);
                        doc.minimapDragStartLine = doc.scrollLine;
                        SetCapture(m_hwnd);

                        if (!hitMinimapSlider) {
                            // Direct click on minimap track: jump scroll position to center clicked line
                            float trackH = doc.minimapTrack.bottom - doc.minimapTrack.top;
                            if (trackH > 0.0f) {
                                float relY = static_cast<float>(clientPt.y) - doc.minimapTrack.top;
                                float ratio = (std::max)(0.0f, (std::min)(1.0f, relY / trackH));
                                size_t totalLines = doc.buffer.GetLineCount();
                                if (totalLines > 0) {
                                    size_t targetLine = static_cast<size_t>(ratio * totalLines);
                                    if (targetLine > 10) doc.scrollLine = targetLine - 10;
                                    else doc.scrollLine = 0;
                                }
                            }
                        }
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
            if (m_drawer.HitTestGit(clientPt, m_config, openFile, discardedFile, isDiff, openCommitDiff, openUrl)) {
                if (!openUrl.empty()) {
                    ShellExecuteW(nullptr, L"open", openUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
                if (!openCommitDiff.empty()) {
                    OpenCommitDiffDocument(openCommitDiff, openFile);
                } else if (isDiff && !openFile.empty()) {
                    OpenDiffDocument(openFile, false);
                } else if (!openFile.empty()) {
                    OpenFileDocument(openFile, false);
                }
                if (!discardedFile.empty()) {
                    // 1. Auto-close any working tree diff view tabs matching the discarded file(s)
                    for (int g = static_cast<int>(m_editorGroups.size()) - 1; g >= 0; --g) {
                        if (g >= static_cast<int>(m_editorGroups.size())) continue;
                        for (int d = static_cast<int>(m_editorGroups[g].docs.size()) - 1; d >= 0; --d) {
                            if (g >= static_cast<int>(m_editorGroups.size())) break;
                            if (d >= static_cast<int>(m_editorGroups[g].docs.size())) continue;
                            auto& doc = m_editorGroups[g].docs[d];
                            if (doc.isDiffView && !doc.isDiffRightReadOnly) {
                                std::wstring cleanDoc = doc.filePath;
                                for (auto& c : cleanDoc) if (c == L'/') c = L'\\';
                                std::wstring cleanDisc = discardedFile;
                                for (auto& c : cleanDisc) if (c == L'/') c = L'\\';

                                bool matches = (discardedFile == L"*") ||
                                               (cleanDoc == cleanDisc) ||
                                               (cleanDoc.ends_with(cleanDisc)) ||
                                               (cleanDisc.ends_with(cleanDoc));
                                if (matches) {
                                    CloseDocument(g, d);
                                }
                            }
                        }
                    }

                    // 2. Reload open regular document buffers if matching discarded file
                    for (auto& group : m_editorGroups) {
                        for (auto& doc : group.docs) {
                            if (!doc.filePath.empty()) {
                                std::wstring cleanDoc = doc.filePath;
                                for (auto& c : cleanDoc) if (c == L'/') c = L'\\';
                                std::wstring cleanDisc = discardedFile;
                                for (auto& c : cleanDisc) if (c == L'/') c = L'\\';

                                bool matches = (discardedFile == L"*") ||
                                               (cleanDoc == cleanDisc) ||
                                               (cleanDoc.ends_with(cleanDisc)) ||
                                               (cleanDisc.ends_with(cleanDoc));
                                if (matches) {
                                    if (std::filesystem::exists(doc.filePath)) {
                                        doc.buffer.LoadFromFile(doc.filePath);
                                    } else {
                                        doc.buffer.SetText(L"");
                                    }
                                    doc.isDirty = false;
                                    if (doc.isDiffView) {
                                        ComputeDiffHunks(doc);
                                    }
                                }
                            }
                        }
                    }
                    TriggerDebouncedGitRefresh(500);
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
                    const auto* pItem = m_drawer.GetItem(clickedTree);
                    if (pItem) {
                        if (pItem->isFolder) {
                            m_drawer.ToggleFolder(clickedTree);
                            SaveSessionState();
                        } else {
                            // Open file in preview mode! (If current file is dirty, VS Code rule adds a tab; if clean preview, replaces it)
                            OpenFileDocument(pItem->fullPath, true);
                        }
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return;
                    }
                }
            }
        }
    }

    // Unfocus drawer search and git commit inputs when clicking outside drawer
    m_drawer.SetSearchFocus(0);
    m_drawer.SetGitCommitBoxFocus(0);

    // 7. Activity Rail Clicks (Files, Search, Git, Debug, Extensions)
    auto railHit = m_rail.HitTest(clientPt);
    if (railHit != ui::RailTabId::None) {
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
        SaveSessionState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
}

void Window::HandleLButtonUp(POINT clientPt) {
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
        SaveSessionState();
        ReleaseCapture();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    for (auto& group : m_editorGroups) {
        for (auto& doc : group.docs) {
            doc.isDraggingScrollbar = false;
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
                m_explorerUndo.CanRedo()
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
            m_popupMenu.OpenContextMenu(
                static_cast<float>(clientPt.x),
                static_cast<float>(clientPt.y),
                m_contextTargetIsFolder,
                targetPath,
                m_drawer.GetOpenFolderPath(),
                m_config,
                canPaste,
                m_explorerUndo.CanUndo(),
                m_explorerUndo.CanRedo()
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
        m_popupMenu.OpenEditorContextMenu(
            static_cast<float>(clientPt.x),
            static_cast<float>(clientPt.y),
            m_config,
            hasSelection,
            canPaste
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
                if (m_themePicker.IsOpen()) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Set pointer (hand) cursor when hovering over sidebar icons
                if (m_rail.HitTest(pt) != ui::RailTabId::None) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Provide pointer hand cursor over caption buttons, menus, execution buttons, split editor, quick open, and zoom buttons
                if (m_header.HitTestCaptionButton(pt) != ui::CaptionButtonType::None ||
                    m_header.HitTestMenu(pt) != ui::HeaderMenuId::None ||
                    m_header.HitTestExecButton(pt) != ui::HeaderExecId::None ||
                    m_header.HitTestSplitEditor(pt) ||
                    m_header.HitTestQuickOpen(pt) ||
                    m_header.HitTestZoomIn(pt) ||
                    m_header.HitTestZoomOut(pt) ||
                    m_header.HitTestZoomReset(pt)) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
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

                // Inline rename input box in drawer: show I-beam cursor
                if (m_drawer.IsRenaming()) {
                    const auto& rcBox = m_drawer.GetRenameBox();
                    if (pt.x >= rcBox.left && pt.x <= rcBox.right && pt.y >= rcBox.top && pt.y <= rcBox.bottom) {
                        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                        return TRUE;
                    }
                }

                // Source Control: Draggable splitter, buttons and inputs (Images 1, 2, 3, 5)
                if (m_config.activeRailTab == 2 && m_drawerAnimProgress > 0.5f) {
                    if (m_drawer.HitTestGitSplitter(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_SIZENS));
                        return TRUE;
                    }
                    if (m_drawer.HitTestGitInputs(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                        return TRUE;
                    }
                    if (m_drawer.HitTestGitButtons(pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
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

                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                return TRUE;
            }
            return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
        }

        case WM_MOUSEMOVE: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            HandleMouseMove(pt);
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

            // 2. Scroll Editor Canvas
            editor::OpenDocument* pDoc = nullptr;
            float canvasLeft = railW + drawerW;
            RECT rcClient;
            GetClientRect(m_hwnd, &rcClient);
            float canvasW = static_cast<float>(rcClient.right - rcClient.left) - canvasLeft;
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
                } else {
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

        case WM_CHAR: {
            if (m_suppressNextChar) {
                m_suppressNextChar = false;
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
                } else {
                    // Printable characters only (Enter and Tab are cleanly handled in WM_KEYDOWN)
                    if (ch >= 32 && ch != 127) {
                        if (pDoc->isDiffView && (pDoc->diffActivePane == 0 || pDoc->isDiffRightReadOnly)) {
                            return 0; // Read-only side!
                        }
                        if (pDoc->HasSelection()) {
                            pDoc->DeleteSelection();
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

                        pDoc->buffer.InsertChar(ch);
                        MarkDocumentModified(pDoc);
                        if (pDoc->isDiffView) {
                            ComputeDiffHunks(*pDoc);
                        }

                        // Query massive HTML5 / JavaScript / Python IntelliSense (Image 5)
                        if (iswalnum(ch) || ch == L'.' || ch == L'<' || ch == L':' || ch == L'$' || ch == L'_') {
                            auto pos = pDoc->buffer.GetCursor();
                            const auto& lineText = pDoc->buffer.GetLine(pos.line);
                            auto items = m_intelliSense.QueryCompletions(pDoc->fileName, lineText, pos.col);
                            if (!items.empty()) {
                                m_editor.ShowAutocomplete(items);
                                std::wstring prefix = editor::IntelliSenseEngine::ExtractQueryPrefix(lineText, pos.col);
                                std::wstring ghost = items[0].label;
                                if (!prefix.empty() && ghost.rfind(prefix, 0) == 0) {
                                    ghost = ghost.substr(prefix.length());
                                }
                                m_editor.SetGhostText(ghost);
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
                    auto items = m_intelliSense.QueryCompletions(pDoc->fileName, lineText, pos.col);
                    if (!items.empty()) {
                        m_editor.ShowAutocomplete(items);
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // F5 execution shortcuts (Ctrl+F5 = Run, F5 = Debug)
            if (wParam == VK_F5) {
                if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && (GetKeyState(VK_SHIFT) & 0x8000) != 0 && m_terminal.IsDebugActive()) {
                    m_terminal.TriggerDebugAction(ui::DebugAction::Restart);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                } else if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && m_terminal.IsDebugActive()) {
                    m_terminal.TriggerDebugAction(ui::DebugAction::Stop);
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
                if (!(GetKeyState(VK_CONTROL) & 0x8000)) {
                    return 0;
                }
            }

            // 0. Quick Open file search palette (Image 5)
            if (m_quickOpen.IsOpen()) {
                if (wParam == VK_ESCAPE) {
                    m_quickOpen.Close();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
                std::wstring chosenFile;
                if (m_quickOpen.HandleKeyDown(wParam, chosenFile)) {
                    if (!chosenFile.empty()) {
                        OpenFileDocument(chosenFile, false);
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

            // F2 inline rename trigger when hovering over tree item in File Explorer
            if (wParam == VK_F2 && m_config.activeRailTab == 0 && m_drawerAnimProgress > 0.001f) {
                int hovered = m_drawer.GetHoveredIndex();
                if (hovered >= 0) {
                    m_drawer.StartInlineRename(hovered);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            if (wParam == VK_ESCAPE) {
                m_drawer.DismissCommitCard();
                auto* pCurDoc = GetActiveDoc();
                if (pCurDoc && pCurDoc->HasSelection()) {
                    pCurDoc->ClearSelection();
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
            if (pDoc && pDoc->isSpecialView && pDoc->viewType == L"keybindings") {
                if (m_editor.GetKeyboardShortcutsView().IsSearchFocused()) {
                    if (m_editor.GetKeyboardShortcutsView().HandleKeyDown(wParam)) {
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
                        pDoc->buffer.Backspace();
                    }
                    MarkDocumentModified(pDoc);
                    if (pDoc->isDiffView) ComputeDiffHunks(*pDoc);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                } else if (wParam == VK_LEFT) {
                    if (pDoc->HasSelection() && !(GetKeyState(VK_SHIFT) & 0x8000)) {
                        pDoc->ClearSelection();
                    }
                    pDoc->buffer.MoveCursorLeft();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_RIGHT) {
                    if (pDoc->HasSelection() && !(GetKeyState(VK_SHIFT) & 0x8000)) {
                        pDoc->ClearSelection();
                    }
                    pDoc->buffer.MoveCursorRight();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_UP) {
                    if (pDoc->HasSelection() && !(GetKeyState(VK_SHIFT) & 0x8000)) {
                        pDoc->ClearSelection();
                    }
                    pDoc->buffer.MoveCursorUp();
                    size_t curLine = pDoc->buffer.GetCursor().line;
                    if (curLine < pDoc->scrollLine) {
                        pDoc->scrollLine = curLine;
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_DOWN) {
                    if (pDoc->HasSelection() && !(GetKeyState(VK_SHIFT) & 0x8000)) {
                        pDoc->ClearSelection();
                    }
                    pDoc->buffer.MoveCursorDown();
                    size_t curLine = pDoc->buffer.GetCursor().line;
                    RECT rc;
                    GetClientRect(m_hwnd, &rc);
                    float h = static_cast<float>(rc.bottom - rc.top);
                    size_t visibleLines = static_cast<size_t>(h / m_config.ScaleF(20.0f));
                    if (curLine >= pDoc->scrollLine + visibleLines - 4) {
                        pDoc->scrollLine = (curLine + 4 > visibleLines) ? (curLine + 4 - visibleLines) : 0;
                    }
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
                    pDoc->buffer.InsertChar(L'\n');
                    MarkDocumentModified(pDoc);
                    if (pDoc->isDiffView) ComputeDiffHunks(*pDoc);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam == m_caretTimerId) {
                m_editor.ToggleCaretBlink();
                m_drawer.ToggleCaretBlink();
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

                // Source Control background auto-refresh & animation update
                // Background polling passes showLoading = false so it NEVER continuously spins the activity rail icon
                // Uses RefreshGitStatusOnly to avoid reloading or blinking the commit graph!
                static ULONGLONG s_lastGitPoll = 0;
                if (m_config.activeRailTab == 2 && m_drawerAnimProgress > 0.5f && (now - s_lastGitPoll > 30000) && !m_drawer.IsGitLoading() && !m_drawer.IsGitRefreshing()) {
                    s_lastGitPoll = now;
                    m_drawer.RefreshGitStatusOnly(true);
                }

                if (m_drawer.IsGitLoading()) {
                    m_rail.UpdateGitAnim(14.0f);
                    m_drawer.UpdateGitLoadingAnim(14.0f);
                    m_rail.SetGitLoading(true);
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else {
                    m_rail.SetGitLoading(false);
                }
            } else if (wParam == m_animTimerId) {
                const float speed = 0.22f; // Smooth, quick sliding animation
                float diff = m_targetDrawerProgress - m_drawerAnimProgress;
                if (std::abs(diff) < 0.005f) {
                    m_drawerAnimProgress = m_targetDrawerProgress;
                    m_isDrawerAnimating = false;
                    KillTimer(m_hwnd, m_animTimerId);
                } else {
                    m_drawerAnimProgress += diff * speed;
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
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
            TriggerDebouncedGitRefresh(1500);
            return 0;
        }

        case WM_CLOSE: {
            SaveSessionState();
            DestroyWindow(m_hwnd);
            return 0;
        }

        case WM_DESTROY: {
            StopWorkspaceWatcher();
            m_explorerUndo.CleanupTrash();
            SaveSessionState();
            PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
    }
}

} // namespace phide::app
