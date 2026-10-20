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

Window::Window() = default;

Window::~Window() {
    DiscardDeviceResources();
    if (m_hwnd) {
        KillTimer(m_hwnd, m_caretTimerId);
        KillTimer(m_hwnd, m_animTimerId);
    }
}

void Window::ToggleSidebar() {
    m_config.isDrawerOpen = !m_config.isDrawerOpen;
    m_targetDrawerProgress = m_config.isDrawerOpen ? 1.0f : 0.0f;
    m_isDrawerAnimating = true;
    SetTimer(m_hwnd, m_animTimerId, 16, nullptr);
}

void Window::DoNewTextFile() {
    m_buffer.Clear();
    m_currentFilePath.clear();
    m_currentFileName = L"Untitled-1";
    m_hasOpenFile = true;
    m_isDirty = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
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
        LoadFileFromPath(szFile);
    }
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
                    m_workspace.OpenWorkspace(pszPath);
                    CoTaskMemFree(pszPath);
                    if (!m_config.isDrawerOpen) {
                        ToggleSidebar();
                    }
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            }
        }
    }
}

void Window::DoSaveFile() {
    if (m_currentFilePath.empty()) {
        DoSaveFileAs();
    } else {
        SaveFileToPath(m_currentFilePath);
    }
}

void Window::DoSaveFileAs() {
    wchar_t szFile[MAX_PATH] = { 0 };
    if (!m_currentFilePath.empty()) {
        wcsncpy_s(szFile, m_currentFilePath.c_str(), _TRUNCATE);
    } else if (!m_currentFileName.empty()) {
        wcsncpy_s(szFile, m_currentFileName.c_str(), _TRUNCATE);
    }
    OPENFILENAMEW ofn = { sizeof(OPENFILENAMEW) };
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"All Files (*.*)\0*.*\0C/C++ Files (*.cpp;*.h;*.hpp)\0*.cpp;*.h;*.hpp\0PHP Files (*.php)\0*.php\0JSON Files (*.json)\0*.json\0Text Files (*.txt)\0*.txt\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_ENABLESIZING;

    if (GetSaveFileNameW(&ofn)) {
        SaveFileToPath(szFile);
    }
}

void Window::DoCloseEditor() {
    m_hasOpenFile = false;
    m_currentFilePath.clear();
    m_currentFileName.clear();
    m_isDirty = false;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::LoadFileFromPath(const std::wstring& path) {
    if (m_buffer.LoadFromFile(path)) {
        m_currentFilePath = path;
        auto pos = path.find_last_of(L"\\/");
        m_currentFileName = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
        m_hasOpenFile = true;
        m_isDirty = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::SaveFileToPath(const std::wstring& path) {
    if (m_buffer.SaveToFile(path)) {
        m_currentFilePath = path;
        auto pos = path.find_last_of(L"\\/");
        m_currentFileName = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
        m_isDirty = false;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::OpenFileByName(const std::wstring& name) {
    m_currentFileName = name;
    m_hasOpenFile = true;
    m_isDirty = false;

    // Check if real file exists in current workspace / directory
    std::wstring localPath;
    if (name == L"main.cpp") localPath = L"src\\main.cpp";
    else if (name == L"CMakeLists.txt") localPath = L"CMakeLists.txt";
    else localPath = name;

    if (!localPath.empty() && std::filesystem::exists(localPath)) {
        if (m_buffer.LoadFromFile(localPath)) {
            m_currentFilePath = localPath;
            return;
        }
    }

    // Default rich templates representing PHP & C++ IDE
    if (name == L"index.php") {
        m_buffer.SetText(
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
            L"$kernel->terminate($request, $response);\n"
        );
        m_currentFilePath = L"src\\index.php";
    } else if (name == L"Kernel.php") {
        m_buffer.SetText(
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
            L"}\n"
        );
        m_currentFilePath = L"src\\Kernel.php";
    } else if (name == L"composer.json") {
        m_buffer.SetText(
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
            L"}\n"
        );
        m_currentFilePath = L"composer.json";
    } else if (name == L"CMakeLists.txt") {
        m_buffer.SetText(
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
            L")\n"
        );
        m_currentFilePath = L"CMakeLists.txt";
    } else if (name == L"main.cpp") {
        m_buffer.SetText(
            L"#include <windows.h>\n"
            L"#include \"phide/app/Window.hpp\"\n\n"
            L"int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {\n"
            L"    phide::app::Window app;\n"
            L"    if (!app.Create(hInstance, nCmdShow)) {\n"
            L"        return 1;\n"
            L"    }\n"
            L"    return app.Run();\n"
            L"}\n"
        );
        m_currentFilePath = L"src\\main.cpp";
    }
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

    // Initialize sidebar sliding animation state
    m_drawerAnimProgress   = m_config.isDrawerOpen ? 1.0f : 0.0f;
    m_targetDrawerProgress = m_drawerAnimProgress;
    m_isDrawerAnimating    = false;

    // Launch application whole screen (maximized) as requested
    ShowWindow(m_hwnd, SW_MAXIMIZE);
    UpdateWindow(m_hwnd);

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

    // 1. Render Workspace Canvas (shows placeholder watermark when no file is open, code editor when open)
    m_editor.Render(m_pRenderTarget.Get(), m_theme, m_config, m_buffer, canvasLeft, canvasWidth, clientH,
                    hasLeftDrawer, m_hasOpenFile, m_currentFileName, m_isDirty);

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
        int hoveredTree = m_drawer.HitTest(clientPt, m_config);
        if (m_drawer.SetHoveredIndex(hoveredTree)) needRepaint = true;
    }

    if (needRepaint) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void Window::HandleLButtonDown(POINT clientPt) {
    // 0. Popup Dropdown Menu Clicks (if menu is active)
    if (m_popupMenu.IsOpen()) {
        if (m_popupMenu.HitTestMenuArea(clientPt)) {
            auto action = m_popupMenu.HandleLButtonDown(clientPt);
            if (action != ui::MenuActionId::None) {
                switch (action) {
                    // File Menu Actions
                    case ui::MenuActionId::NewTextFile:
                        DoNewTextFile();
                        break;
                    case ui::MenuActionId::OpenFile:
                        DoOpenFile();
                        break;
                    case ui::MenuActionId::OpenFolder:
                        DoOpenFolder();
                        break;
                    case ui::MenuActionId::Save:
                        DoSaveFile();
                        break;
                    case ui::MenuActionId::SaveAs:
                        DoSaveFileAs();
                        break;
                    case ui::MenuActionId::ToggleAutoSave:
                        m_autoSave = !m_autoSave;
                        break;
                    case ui::MenuActionId::CloseEditor:
                        DoCloseEditor();
                        break;
                    case ui::MenuActionId::CloseFolder:
                        DoCloseEditor();
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
            // Toggle sidebar drawer with smooth sliding animation
            ToggleSidebar();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // 3. Header Menu / Icon Button Clicks
    auto menuHit = m_header.HitTestMenu(clientPt);
    if (menuHit != ui::HeaderMenuId::None) {
        if (menuHit == ui::HeaderMenuId::Sidebar) {
            // Toggle sidebar drawer panel with smooth sliding animation
            ToggleSidebar();
        } else if (menuHit == ui::HeaderMenuId::File) {
            // Open File Dropdown Menu directly beneath the File header button
            D2D1_RECT_F rcFile = m_header.GetMenuItemBounds(ui::HeaderMenuId::File);
            m_popupMenu.OpenFileMenu(rcFile.left, rcFile.bottom + 2.0f, m_config, m_autoSave);
            m_header.SetActiveMenu(ui::HeaderMenuId::File);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else if (menuHit == ui::HeaderMenuId::View) {
            // Open View Dropdown Menu directly beneath the View header button
            D2D1_RECT_F rcView = m_header.GetMenuItemBounds(ui::HeaderMenuId::View);
            m_popupMenu.OpenViewMenu(rcView.left, rcView.bottom + 2.0f, m_config,
                                     m_config.isDrawerOpen, IsZoomed(m_hwnd), m_wordWrap);
            m_header.SetActiveMenu(ui::HeaderMenuId::View);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    // 4. Tab Close Button Click (closes active editor tab, restoring placeholder)
    if (m_hasOpenFile && m_editor.HitTestTabClose(clientPt)) {
        DoCloseEditor();
        return;
    }

    // 5. Drawer Tree Item Clicks (open file or toggle folder)
    if (m_drawerAnimProgress > 0.001f) {
        int clickedTree = m_drawer.HitTest(clientPt, m_config);
        if (clickedTree >= 0) {
            const auto* pItem = m_drawer.GetItem(clickedTree);
            if (pItem) {
                if (pItem->isFolder) {
                    m_drawer.ToggleFolder(clickedTree);
                } else {
                    // Open file! Removes placeholder and displays file code
                    OpenFileByName(pItem->label);
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return;
            }
        }
    }

    // 6. Click inside Editor Code Area (sets cursor position)
    if (m_hasOpenFile) {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        float clientH = static_cast<float>(rc.bottom - rc.top);
        float railW = m_config.ScaleF(static_cast<float>(m_config.railWidth));
        float currentDrawerW = m_config.ScaleF(static_cast<float>(m_config.drawerWidth)) * m_drawerAnimProgress;
        float canvasLeft = railW + currentDrawerW;

        size_t line = 0, col = 0;
        if (m_editor.HitTestCodeArea(clientPt, m_config, m_buffer, canvasLeft, clientH, line, col)) {
            m_buffer.SetCursor(line, col);
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
    }

    // 7. Activity Rail Clicks (Files, Search, Git, Debug, Extensions)
    auto railHit = m_rail.HitTest(clientPt);
    if (railHit != ui::RailTabId::None) {
        int clickedTab = static_cast<int>(railHit);
        // Switch the active rail tab highlight badge
        m_config.activeRailTab = clickedTab;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
}

void Window::HandleLButtonUp(POINT clientPt) {
    (void)clientPt;
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

        case WM_CHAR: {
            if (m_hasOpenFile) {
                wchar_t ch = static_cast<wchar_t>(wParam);
                if (ch >= 32 || ch == L'\t' || ch == L'\r' || ch == L'\n') {
                    m_buffer.InsertChar(ch);
                    m_isDirty = true;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
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

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE && m_popupMenu.IsOpen()) {
                m_popupMenu.Close();
                m_header.SetActiveMenu(ui::HeaderMenuId::None);
                InvalidateRect(m_hwnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_F11) {
                if (IsZoomed(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
                else ShowWindow(m_hwnd, SW_MAXIMIZE);
                return 0;
            }

            // Keyboard Shortcuts with Ctrl key
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (wParam == 'B') {
                    ToggleSidebar();
                    return 0;
                }
                if (wParam == 'N') {
                    DoNewTextFile();
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
                    if (GetKeyState(VK_SHIFT) & 0x8000) {
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
                if (wParam == 'K') {
                    m_chordCtrlK = true;
                    return 0;
                }
            } else {
                m_chordCtrlK = false;
            }

            // Close Editor via Ctrl+F4
            if (wParam == VK_F4 && (GetKeyState(VK_CONTROL) & 0x8000)) {
                DoCloseEditor();
                return 0;
            }

            if (m_hasOpenFile) {
                if (wParam == VK_BACK) {
                    m_buffer.Backspace();
                    m_isDirty = true;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_LEFT) {
                    m_buffer.MoveCursorLeft();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_RIGHT) {
                    m_buffer.MoveCursorRight();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_UP) {
                    m_buffer.MoveCursorUp();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_DOWN) {
                    m_buffer.MoveCursorDown();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_TAB) {
                    m_buffer.InsertChar(L' ');
                    m_buffer.InsertChar(L' ');
                    m_buffer.InsertChar(L' ');
                    m_buffer.InsertChar(L' ');
                    m_isDirty = true;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else if (wParam == VK_RETURN) {
                    m_buffer.InsertChar(L'\n');
                    m_isDirty = true;
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

        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
    }
}

} // namespace phide::app
