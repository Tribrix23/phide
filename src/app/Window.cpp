/**
 * @file Window.cpp
 * @brief Implementation of Dark IDE native Win32 window, message router, and Direct2D rendering pipeline.
 * 
 * Implements custom Direct2D minimize, maximize, and close buttons (with red hover on close),
 * removes the header horizontal divider line, adds rounded container styling, and coordinates
 * the Home, Search, and Git activity rail with customized tooltips.
 */

#include "phide/app/Window.hpp"
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

Window::Window() = default;

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

void Window::SaveSessionState() {
    std::wstring filePath = GetSessionFilePath();
    if (filePath.empty()) return;

    std::wofstream out(filePath, std::ios::trunc);
    if (!out.is_open()) return;

    out << L"{\n";
    out << L"  \"isSidebarOpen\": " << (m_config.isDrawerOpen ? L"true" : L"false") << L",\n";
    out << L"  \"activeRailTab\": " << m_config.activeRailTab << L",\n";
    out << L"  \"themeMode\": " << static_cast<int>(m_theme.GetThemeMode()) << L",\n";
    out << L"  \"activeDocIdx\": " << m_activeDocIdx << L",\n";
    if (m_drawer.HasOpenFolder()) {
        out << L"  \"lastOpenFolderPath\": \"" << EscapeJson(m_drawer.GetOpenFolderPath()) << L"\",\n";
    } else {
        out << L"  \"lastOpenFolderPath\": \"\",\n";
    }
    out << L"  \"openDocs\": [\n";
    size_t docCount = 0;
    for (const auto& doc : m_openDocs) {
        if (!doc.filePath.empty()) {
            out << L"    \"" << EscapeJson(doc.filePath) << L"\"" << (++docCount < m_openDocs.size() ? L"," : L"") << L"\n";
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
    }

    m_config.activeRailTab = activeRailTab;

    // Apply restored theme mode with bounds validation (0: Dark, 1: Light, 2: Auto)
    if (themeMode < 0 || themeMode > 2) {
        themeMode = 0; // Default to Dark Mode if corrupt or uninitialized
    }
    m_theme.SetThemeMode(static_cast<ui::ThemeMode>(themeMode));

    if (!lastOpenFolder.empty() && std::filesystem::exists(lastOpenFolder, ec)) {
        m_drawer.OpenFolder(lastOpenFolder, &expandedDirs);
        m_workspace.OpenWorkspace(lastOpenFolder);
    }

    for (const auto& p : openDocPaths) {
        if (!p.empty() && std::filesystem::exists(p, ec)) {
            OpenFileDocument(p, false);
        }
    }

    if (savedActiveDocIdx >= 0 && savedActiveDocIdx < static_cast<int>(m_openDocs.size())) {
        m_activeDocIdx = savedActiveDocIdx;
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

void Window::DoNewTextFile() {
    editor::OpenDocument doc;
    m_untitledCounter++;
    doc.fileName = L"Untitled-" + std::to_wstring(m_untitledCounter);
    doc.filePath = L"";
    doc.buffer = editor::Buffer(); // Empty blank buffer
    doc.isPreview = false;
    doc.isDirty = false;   // Clean, not dirty initially
    doc.isSpecialView = false;
    doc.viewType.clear();

    m_openDocs.push_back(std::move(doc));
    m_activeDocIdx = static_cast<int>(m_openDocs.size() - 1);
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
    ShellExecuteW(nullptr, L"open", szExe, L"--windowed", nullptr, SW_SHOWNORMAL);
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

    // 1. Check if document is already open
    for (size_t i = 0; i < m_openDocs.size(); ++i) {
        if (m_openDocs[i].filePath == path) {
            m_activeDocIdx = static_cast<int>(i);
            if (!isPreview) {
                m_openDocs[i].isPreview = false;
            }
            SaveSessionState();
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // 2. Check if current active document can be replaced
    // VS Code Rule: a tab is replaced ONLY if it is a preview tab AND has NO unsaved edits (!isDirty) AND not a special view.
    bool canReplace = (m_activeDocIdx >= 0 && m_activeDocIdx < static_cast<int>(m_openDocs.size()) &&
                       m_openDocs[m_activeDocIdx].isPreview &&
                       !m_openDocs[m_activeDocIdx].isDirty &&
                       !m_openDocs[m_activeDocIdx].isSpecialView);

    // Check if file is a supported image (PNG, JPG, BMP, GIF, ICO, WEBP)
    std::wstring ext;
    auto dotPos = path.find_last_of(L".");
    if (dotPos != std::wstring::npos) {
        ext = path.substr(dotPos);
        for (auto& ch : ext) ch = towlower(ch);
    }

    bool isImage = (ext == L".png" || ext == L".jpg" || ext == L".jpeg" ||
                    ext == L".bmp" || ext == L".gif" || ext == L".ico" ||
                    ext == L".webp" || ext == L".tiff" || ext == L".tif");

    editor::OpenDocument newDoc;
    newDoc.filePath = path;
    auto pos = path.find_last_of(L"\\/");
    newDoc.fileName = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
    newDoc.isPreview = isPreview;
    newDoc.isDirty = false;
    newDoc.isSpecialView = false;
    newDoc.isImageView = isImage;
    newDoc.viewType.clear();

    if (isImage) {
        std::error_code ec;
        newDoc.imageFileSize = std::filesystem::file_size(path, ec);
    } else {
        newDoc.buffer.LoadFromFile(path);
    }

    if (canReplace) {
        m_openDocs[m_activeDocIdx] = std::move(newDoc);
    } else {
        m_openDocs.push_back(std::move(newDoc));
        m_activeDocIdx = static_cast<int>(m_openDocs.size() - 1);
    }

    // Add to recent paths
    auto it = std::find(m_recentPaths.begin(), m_recentPaths.end(), path);
    if (it != m_recentPaths.end()) m_recentPaths.erase(it);
    m_recentPaths.insert(m_recentPaths.begin(), path);
    if (m_recentPaths.size() > 10) m_recentPaths.resize(10);

    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::OpenVirtualDocument(const std::wstring& name, const std::wstring& content, bool isPreview) {
    for (size_t i = 0; i < m_openDocs.size(); ++i) {
        if (m_openDocs[i].fileName == name && m_openDocs[i].filePath.empty()) {
            m_activeDocIdx = static_cast<int>(i);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    bool canReplace = (m_activeDocIdx >= 0 && m_activeDocIdx < static_cast<int>(m_openDocs.size()) &&
                       m_openDocs[m_activeDocIdx].isPreview &&
                       !m_openDocs[m_activeDocIdx].isDirty &&
                       !m_openDocs[m_activeDocIdx].isSpecialView);

    editor::OpenDocument newDoc;
    newDoc.fileName = name;
    newDoc.filePath.clear();
    newDoc.buffer.SetText(content);
    newDoc.isPreview = isPreview;
    newDoc.isDirty = false;
    newDoc.isSpecialView = false;

    if (canReplace) {
        m_openDocs[m_activeDocIdx] = std::move(newDoc);
    } else {
        m_openDocs.push_back(std::move(newDoc));
        m_activeDocIdx = static_cast<int>(m_openDocs.size() - 1);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::OpenSpecialView(const std::wstring& viewType, const std::wstring& title) {
    for (size_t i = 0; i < m_openDocs.size(); ++i) {
        if (m_openDocs[i].isSpecialView && m_openDocs[i].viewType == viewType) {
            m_activeDocIdx = static_cast<int>(i);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    bool canReplace = (m_activeDocIdx >= 0 && m_activeDocIdx < static_cast<int>(m_openDocs.size()) &&
                       m_openDocs[m_activeDocIdx].isPreview &&
                       !m_openDocs[m_activeDocIdx].isDirty &&
                       !m_openDocs[m_activeDocIdx].isSpecialView);

    editor::OpenDocument newDoc;
    newDoc.fileName = title;
    newDoc.filePath.clear();
    newDoc.isSpecialView = true;
    newDoc.viewType = viewType;
    newDoc.isPreview = false;
    newDoc.isDirty = false;

    if (canReplace) {
        m_openDocs[m_activeDocIdx] = std::move(newDoc);
    } else {
        m_openDocs.push_back(std::move(newDoc));
        m_activeDocIdx = static_cast<int>(m_openDocs.size() - 1);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::CloseDocument(int index) {
    if (index < 0 || index >= static_cast<int>(m_openDocs.size())) return;

    if (!m_openDocs[index].filePath.empty()) {
        m_lastClosedFilePath = m_openDocs[index].filePath;
    }

    m_openDocs.erase(m_openDocs.begin() + index);

    if (m_openDocs.empty()) {
        m_activeDocIdx = -1;
    } else if (m_activeDocIdx >= static_cast<int>(m_openDocs.size())) {
        m_activeDocIdx = static_cast<int>(m_openDocs.size() - 1);
    } else if (index < m_activeDocIdx) {
        m_activeDocIdx--;
    }

    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
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

    // Track in recent paths
    auto it = std::find(m_recentPaths.begin(), m_recentPaths.end(), path);
    if (it != m_recentPaths.end()) m_recentPaths.erase(it);
    m_recentPaths.insert(m_recentPaths.begin(), path);
    if (m_recentPaths.size() > 10) m_recentPaths.resize(10);

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
    for (auto& doc : m_openDocs) {
        if (doc.isDirty && !doc.filePath.empty()) {
            doc.buffer.SaveToFile(doc.filePath);
            doc.isDirty = false;
        }
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
    OpenSpecialView(L"keybindings", L"Keyboard Shortcuts");
}

void Window::DoOpenUserSnippets() {
    OpenVirtualDocument(
        L"snippets.json",
        L"{\n"
        L"    // Place your global snippets here\n"
        L"    \"Print to console\": {\n"
        L"        \"prefix\": \"log\",\n"
        L"        \"body\": [\n"
        L"            \"std::cout << \\\"$1\\\" << std::endl;\"\n"
        L"        ],\n"
        L"        \"description\": \"Log output to console\"\n"
        L"    }\n"
        L"}\n",
        false
    );
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
    if (m_activeDocIdx >= 0) {
        CloseDocument(m_activeDocIdx);
    }
}

void Window::DoCloseFolder() {
    m_drawer.CloseFolder();
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoReopenClosedEditor() {
    if (!m_lastClosedFilePath.empty() && std::filesystem::exists(m_lastClosedFilePath)) {
        OpenFileDocument(m_lastClosedFilePath, false);
    }
}

void Window::DoCut() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        DoCopy();
        pDoc->buffer.Backspace();
        pDoc->isDirty = true;
        pDoc->isPreview = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::DoCopy() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        auto cursor = pDoc->buffer.GetCursor();
        std::wstring lineText = pDoc->buffer.GetLine(cursor.line);
        if (OpenClipboard(m_hwnd)) {
            EmptyClipboard();
            size_t bytes = (lineText.length() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hMem) {
                void* pMem = GlobalLock(hMem);
                if (pMem) {
                    memcpy(pMem, lineText.c_str(), bytes);
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
                    pDoc->buffer.InsertText(clip);
                    pDoc->isDirty = true;
                    pDoc->isPreview = false;
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
            pDoc->isDirty = true;
            pDoc->isPreview = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
}

void Window::DoRedo() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        if (pDoc->buffer.Redo()) {
            pDoc->isDirty = true;
            pDoc->isPreview = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
}

void Window::DoSelectAll() {
    auto* pDoc = GetActiveDoc();
    if (pDoc && !pDoc->isSpecialView) {
        size_t count = pDoc->buffer.GetLineCount();
        if (count > 0) {
            pDoc->buffer.SetCursor(count - 1, pDoc->buffer.GetLine(count - 1).length());
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
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
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoShowSearch() {
    m_config.activeRailTab = 1;
    if (!m_config.isDrawerOpen) {
        ToggleSidebar();
    }
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoShowSourceControl() {
    m_config.activeRailTab = 2;
    if (!m_config.isDrawerOpen) {
        ToggleSidebar();
    }
    SaveSessionState();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::DoCreateTerminal() {
    std::wstring dir;
    if (m_drawer.HasOpenFolder()) {
        dir = m_drawer.GetOpenFolderPath();
    }
    ShellExecuteW(nullptr, L"open", L"cmd.exe", nullptr, dir.empty() ? nullptr : dir.c_str(), SW_SHOWNORMAL);
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
            pDoc->fileName = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
            pDoc->isDirty = false;
            pDoc->isPreview = false;
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
    wc.style = CS_HREDRAW | CS_VREDRAW;
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

    // Restore saved IDE session state (open folder, expanded tree dirs, open file, sidebar open/close state)
    LoadSessionState();

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
    m_editor.Render(m_pRenderTarget.Get(), m_theme, m_config, m_openDocs, m_activeDocIdx,
                    canvasLeft, canvasWidth, clientH, hasLeftDrawer);

    // 2. Render Slide-out Drawer Panel (when visible or animating, with 14px rounded curve at top-left)
    if (hasLeftDrawer) {
        m_drawer.Render(m_pRenderTarget.Get(), m_theme, m_config, clientH, currentDrawerW);
    }

    // 3. Render Collapsed Activity Rail (Home -> Search -> Git with customized tooltips)
    m_rail.Render(m_pRenderTarget.Get(), m_theme, m_config, clientH);

    // 4. Render Dark Frameless Title Header at Top (with active highlight on Sidebar button when open)
    m_header.Render(m_pRenderTarget.Get(), m_theme, m_config, clientW, IsZoomed(m_hwnd));

    // 5. Render Active Popup Dropdown Menu Overlay
    if (m_popupMenu.IsOpen()) {
        m_popupMenu.Render(m_pRenderTarget.Get(), m_theme, m_config);
    }

    // 6. Render Modal Theme Picker Popup (Dark, Light, Auto with preview screenshot cards)
    if (m_themePicker.IsOpen()) {
        m_themePicker.Render(m_pRenderTarget.Get(), m_theme, m_config, clientW, clientH);
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

    if (m_themePicker.IsOpen()) {
        if (m_themePicker.HandleMouseMove(clientPt)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    bool needRepaint = false;

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
    auto* pActiveDoc = GetActiveDoc();
    if (pActiveDoc && pActiveDoc->isDraggingScrollbar) {
        float deltaY = static_cast<float>(clientPt.y) - pActiveDoc->dragStartY;
        float trackH = pActiveDoc->scrollbarTrack.bottom - pActiveDoc->scrollbarTrack.top;
        float thumbH = pActiveDoc->scrollbarThumb.bottom - pActiveDoc->scrollbarThumb.top;
        float scrollRange = trackH - thumbH;
        if (scrollRange > 0.0f) {
            size_t totalLines = pActiveDoc->buffer.GetLineCount();
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

    // Forward to keyboard shortcuts view if active doc is keybindings
    if (pActiveDoc && pActiveDoc->isSpecialView && pActiveDoc->viewType == L"keybindings") {
        if (m_editor.GetKeyboardShortcutsView().HandleMouseMove(clientPt)) {
            needRepaint = true;
        }
    }

    // Check Header menu, execution buttons, and caption button hover
    auto hoveredMenu = m_header.HitTestMenu(clientPt);
    auto hoveredCaption = m_header.HitTestCaptionButton(clientPt);
    auto hoveredExec = m_header.HitTestExecButton(clientPt);
    if (m_header.UpdateHoverState(hoveredMenu, hoveredCaption, hoveredExec)) needRepaint = true;

    // Check Rail icon hover (triggers customized tooltip)
    auto hoveredRail = m_rail.HitTest(clientPt);
    if (m_rail.SetHoveredTab(hoveredRail)) needRepaint = true;

    // Check Drawer hover
    if (m_drawerAnimProgress > 0.001f) {
        if (!m_drawer.HasOpenFolder()) {
            bool btnHover = m_drawer.HitTestOpenFolderButton(clientPt, m_config);
            if (m_drawer.SetHoveredOpenFolderBtn(btnHover)) needRepaint = true;
        } else {
            int hoveredTree = m_drawer.HitTest(clientPt, m_config);
            if (m_drawer.SetHoveredIndex(hoveredTree)) needRepaint = true;
        }
    }

    if (needRepaint) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::HandleLButtonDown(POINT clientPt) {
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

                    // View Menu Actions
                    case ui::MenuActionId::TogglePrimarySideBar:
                        ToggleSidebar();
                        break;
                    case ui::MenuActionId::FullScreen:
                        if (IsZoomed(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
                        else ShowWindow(m_hwnd, SW_MAXIMIZE);
                        break;
                    case ui::MenuActionId::WordWrap:
                        m_wordWrap = !m_wordWrap;
                        break;
                    case ui::MenuActionId::Terminal:
                        ToggleSidebar();
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
                                std::filesystem::remove_all(src, ec);
                                m_clipboardPath.clear();
                                m_clipboardIsCut = false;
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
                            std::filesystem::path p(m_contextTargetPath);
                            std::wstring currentName = p.filename().wstring();
                            std::wstring newName;
                            if (PromptInputDialog(m_hwnd, L"Rename", L"Enter new name:", currentName, newName) && !newName.empty() && newName != currentName) {
                                std::filesystem::path newPath = p.parent_path() / newName;
                                std::error_code ec;
                                std::filesystem::rename(p, newPath, ec);
                                if (!ec) {
                                    for (auto& doc : m_openDocs) {
                                        if (doc.filePath == m_contextTargetPath) {
                                            doc.filePath = newPath.wstring();
                                            doc.fileName = newName;
                                        }
                                    }
                                    m_drawer.RefreshTree();
                                }
                            }
                        }
                        break;
                    case ui::MenuActionId::CtxDelete:
                        if (!m_contextTargetPath.empty()) {
                            std::wstring confirmMsg = L"Are you sure you want to delete '" +
                                std::filesystem::path(m_contextTargetPath).filename().wstring() + L"'?";
                            if (MessageBoxW(m_hwnd, confirmMsg.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
                                std::error_code ec;
                                std::filesystem::remove_all(m_contextTargetPath, ec);
                                for (int i = 0; i < static_cast<int>(m_openDocs.size()); ++i) {
                                    if (m_openDocs[i].filePath == m_contextTargetPath) {
                                        CloseDocument(i);
                                        break;
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
                            m_drawer.RefreshTree();
                        }
                        break;
                    }
                    case ui::MenuActionId::CtxFindInFolder:
                        DoShowSearch();
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
        if (execHit == ui::HeaderExecId::Terminal) {
            ToggleSidebar();
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
        } else if (menuHit == ui::HeaderMenuId::View) {
            D2D1_RECT_F rcView = m_header.GetMenuItemBounds(ui::HeaderMenuId::View);
            m_popupMenu.OpenViewMenu(rcView.left, rcView.bottom + 2.0f, m_config,
                                     m_config.isDrawerOpen, IsZoomed(m_hwnd), m_wordWrap);
            m_header.SetActiveMenu(ui::HeaderMenuId::View);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    // 4. Tab Close Button Click (closes clicked editor tab)
    int closeIdx = m_editor.HitTestTabClose(clientPt, m_openDocs);
    if (closeIdx >= 0) {
        CloseDocument(closeIdx);
        return;
    }

    // 4b. Tab Header Click (switches active tab)
    int tabHitIdx = m_editor.HitTestTab(clientPt, m_openDocs);
    if (tabHitIdx >= 0) {
        m_activeDocIdx = tabHitIdx;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 5. Drawer Tree Item Clicks (open file in preview mode, toggle folder, or open folder button)
    if (m_drawerAnimProgress > 0.001f) {
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

    // 6. Click inside Editor Area (sets cursor position or focuses keybindings search)
    auto* pDoc = GetActiveDoc();
    if (pDoc) {
        if (pDoc->isSpecialView && pDoc->viewType == L"keybindings") {
            if (m_editor.GetKeyboardShortcutsView().HandleLButtonDown(clientPt)) {
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        } else {
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            float clientH = static_cast<float>(rc.bottom - rc.top);
            float railW = m_config.ScaleF(static_cast<float>(m_config.railWidth));
            float currentDrawerW = m_config.ScaleF(static_cast<float>(m_config.drawerWidth)) * m_drawerAnimProgress;
            float canvasLeft = railW + currentDrawerW;

            // 1. Check fold chevron click in the gutter (Images 2 & 3)
            int foldLine = m_editor.HitTestFoldChevron(clientPt);
            if (foldLine >= 0) {
                size_t fl = static_cast<size_t>(foldLine);
                if (pDoc->foldedLines.count(fl)) {
                    pDoc->foldedLines.erase(fl);
                } else {
                    pDoc->foldedLines.insert(fl);
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }

            // 2. Check vertical scrollbar click & drag (Image 1)
            bool hitThumb = false;
            if (m_editor.HitTestScrollbar(clientPt, *pDoc, hitThumb)) {
                if (hitThumb) {
                    pDoc->isDraggingScrollbar = true;
                    pDoc->dragStartY = static_cast<float>(clientPt.y);
                    pDoc->dragStartScrollLine = pDoc->scrollLine;
                } else {
                    // Clicked track above or below thumb -> jump page
                    if (clientPt.y < pDoc->scrollbarThumb.top) {
                        if (pDoc->scrollLine >= 10) pDoc->scrollLine -= 10;
                        else pDoc->scrollLine = 0;
                    } else if (clientPt.y > pDoc->scrollbarThumb.bottom) {
                        pDoc->scrollLine += 10;
                    }
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }

            // 3. Code Area click
            size_t line = 0, col = 0;
            if (m_editor.HitTestCodeArea(clientPt, m_config, *pDoc, canvasLeft, clientH, line, col)) {
                pDoc->buffer.SetCursor(line, col);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        }
    }

    // 7. Activity Rail Clicks (Files, Search, Git, Debug, Extensions)
    auto railHit = m_rail.HitTest(clientPt);
    if (railHit != ui::RailTabId::None) {
        int clickedTab = static_cast<int>(railHit);
        m_config.activeRailTab = clickedTab;
        SaveSessionState();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
}

void Window::HandleLButtonUp(POINT clientPt) {
    (void)clientPt;
    auto* pDoc = GetActiveDoc();
    if (pDoc) {
        pDoc->isDraggingScrollbar = false;
    }
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
                canPaste
            );
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // 2. Right-click inside Editor Tab Bar or Canvas
    if (clientPt.x >= railW + drawerW && clientPt.y >= headerH) {
        int tabHit = m_editor.HitTestTab(clientPt, m_openDocs);
        std::wstring targetPath;
        if (tabHit >= 0 && tabHit < static_cast<int>(m_openDocs.size())) {
            targetPath = m_openDocs[tabHit].filePath;
        } else if (m_activeDocIdx >= 0 && m_activeDocIdx < static_cast<int>(m_openDocs.size())) {
            targetPath = m_openDocs[m_activeDocIdx].filePath;
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
                canPaste
            );
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
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

                // Set pointer (hand) cursor when hovering over sidebar icons
                if (m_rail.HitTest(pt) != ui::RailTabId::None) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Also provide pointer hand cursor over caption buttons, menus, and execution buttons
                if (m_header.HitTestCaptionButton(pt) != ui::CaptionButtonType::None ||
                    m_header.HitTestMenu(pt) != ui::HeaderMenuId::None ||
                    m_header.HitTestExecButton(pt) != ui::HeaderExecId::None) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Tab bar close buttons or folding chevrons
                if (m_editor.HitTestTabClose(pt, m_openDocs) >= 0 || m_editor.HitTestFoldChevron(pt) >= 0) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Open Folder button in drawer when empty
                if (!m_drawer.HasOpenFolder() && m_drawer.HitTestOpenFolderButton(pt, m_config)) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }

                // Editor code text area: show I-beam cursor (matching Image 1)
                if (!m_openDocs.empty() && m_activeDocIdx >= 0 && m_activeDocIdx < static_cast<int>(m_openDocs.size())) {
                    const auto& activeDoc = m_openDocs[m_activeDocIdx];
                    if (!activeDoc.isSpecialView && !activeDoc.isImageView) {
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

            float railW = m_config.ScaleF(static_cast<float>(m_config.railWidth));
            float drawerW = m_config.ScaleF(static_cast<float>(m_config.drawerWidth)) * m_drawerAnimProgress;
            float headerH = m_config.ScaleF(static_cast<float>(m_config.headerHeight));

            // 1. Scroll Drawer Panel tree if mouse is over sidebar
            if (m_drawerAnimProgress > 0.001f && pt.x >= railW && pt.x < railW + drawerW && pt.y >= headerH) {
                if (m_drawer.HandleMouseWheel(delta)) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // 2. Scroll Editor Canvas
            auto* pDoc = GetActiveDoc();
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
            auto* pDoc = GetActiveDoc();
            if (pDoc) {
                wchar_t ch = static_cast<wchar_t>(wParam);
                if (pDoc->isSpecialView && pDoc->viewType == L"keybindings") {
                    if (m_editor.GetKeyboardShortcutsView().HandleChar(ch)) {
                        InvalidateRect(m_hwnd, nullptr, FALSE);
                        return 0;
                    }
                } else {
                    if (ch >= 32 || ch == L'\t' || ch == L'\r' || ch == L'\n') {
                        pDoc->buffer.InsertChar(ch);
                        pDoc->isDirty = true;
                        pDoc->isPreview = false;
                        if (m_autoSave && !pDoc->filePath.empty()) {
                            pDoc->buffer.SaveToFile(pDoc->filePath);
                            pDoc->isDirty = false;
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
            return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
        }

        case WM_KEYDOWN: {
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

            auto* pDoc = GetActiveDoc();
            if (pDoc && pDoc->isSpecialView && pDoc->viewType == L"keybindings") {
                if (m_editor.GetKeyboardShortcutsView().HandleKeyDown(wParam)) {
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            if (wParam == VK_F11) {
                if (IsZoomed(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
                else ShowWindow(m_hwnd, SW_MAXIMIZE);
                return 0;
            }

            // Keyboard Shortcuts with Ctrl key
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool altDown   = (GetKeyState(VK_MENU) & 0x8000) != 0;

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
                    }
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
                    if (shiftDown) {
                        DoRedo();
                    } else {
                        DoUndo();
                    }
                    return 0;
                }
                if (wParam == 'Y') {
                    DoRedo();
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
                if (wParam == VK_BACK) {
                    pDoc->buffer.Backspace();
                    pDoc->isDirty = true;
                    pDoc->isPreview = false;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_LEFT) {
                    pDoc->buffer.MoveCursorLeft();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_RIGHT) {
                    pDoc->buffer.MoveCursorRight();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_UP) {
                    pDoc->buffer.MoveCursorUp();
                    size_t curLine = pDoc->buffer.GetCursor().line;
                    if (curLine < pDoc->scrollLine) {
                        pDoc->scrollLine = curLine;
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_DOWN) {
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
                    pDoc->buffer.InsertChar(L' ');
                    pDoc->buffer.InsertChar(L' ');
                    pDoc->buffer.InsertChar(L' ');
                    pDoc->buffer.InsertChar(L' ');
                    pDoc->isDirty = true;
                    pDoc->isPreview = false;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_RETURN) {
                    pDoc->buffer.InsertChar(L'\n');
                    pDoc->isDirty = true;
                    pDoc->isPreview = false;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam == m_caretTimerId) {
                m_editor.ToggleCaretBlink();
                InvalidateRect(m_hwnd, nullptr, FALSE);
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

        case WM_CLOSE: {
            SaveSessionState();
            DestroyWindow(m_hwnd);
            return 0;
        }

        case WM_DESTROY: {
            SaveSessionState();
            PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
    }
}

} // namespace phide::app
