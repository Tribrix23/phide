#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <filesystem>

#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/ActivityRail.hpp"
#include "phide/ui/DrawerPanel.hpp"
#include "phide/ui/RemoteWindowModal.hpp"
#include "phide/ui/EditorCanvas.hpp"
#include "phide/ui/TerminalPanel.hpp"
#include "phide/editor/OpenDocument.hpp"
#include <thread>
#include <chrono>

using Microsoft::WRL::ComPtr;

static void SaveWicBitmapToPng(IWICBitmap* pWicBitmap, IWICImagingFactory* pWicFactory, const std::wstring& outPath) {
    if (!pWicBitmap || !pWicFactory) return;

    UINT width = 0, height = 0;
    pWicBitmap->GetSize(&width, &height);

    ComPtr<IWICStream> pStream;
    HRESULT hr = pWicFactory->CreateStream(&pStream);
    if (FAILED(hr)) return;

    hr = pStream->InitializeFromFilename(outPath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) return;

    ComPtr<IWICBitmapEncoder> pEncoder;
    hr = pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &pEncoder);
    if (FAILED(hr)) return;

    hr = pEncoder->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return;

    ComPtr<IWICBitmapFrameEncode> pFrame;
    hr = pEncoder->CreateNewFrame(&pFrame, nullptr);
    if (FAILED(hr)) return;

    hr = pFrame->Initialize(nullptr);
    if (FAILED(hr)) return;

    hr = pFrame->SetSize(width, height);
    if (FAILED(hr)) return;

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    hr = pFrame->SetPixelFormat(&format);
    if (FAILED(hr)) return;

    hr = pFrame->WriteSource(pWicBitmap, nullptr);
    if (FAILED(hr)) return;

    hr = pFrame->Commit();
    if (FAILED(hr)) return;

    hr = pEncoder->Commit();
    if (FAILED(hr)) return;

    std::wcout << L"[SAVED] " << outPath << std::endl;
}

int main() {
    std::cout << "Starting Performance Monitor & Activity Rail Visual Verification..." << std::endl;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;

    {
    ComPtr<ID2D1Factory> pD2DFactory;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, pD2DFactory.GetAddressOf());
    assert(SUCCEEDED(hr));

    ComPtr<IDWriteFactory> pDWriteFactory;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(pDWriteFactory.GetAddressOf()));
    assert(SUCCEEDED(hr));

    ComPtr<IWICImagingFactory> pWicFactory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(pWicFactory.GetAddressOf()));
    assert(SUCCEEDED(hr));

    const UINT w = 1280;
    const UINT h = 800;

    ComPtr<IWICBitmap> pWicBitmap;
    hr = pWicFactory->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &pWicBitmap);
    assert(SUCCEEDED(hr));

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    ComPtr<ID2D1RenderTarget> pRT;
    hr = pD2DFactory->CreateWicBitmapRenderTarget(pWicBitmap.Get(), props, &pRT);
    assert(SUCCEEDED(hr));

    phide::ui::Theme theme;
    theme.SetThemeMode(phide::ui::ThemeMode::Dark);
    theme.CreateDeviceResources(pRT.Get(), pDWriteFactory.Get());

    phide::app::AppConfig cfg;
    cfg.railWidth = 48;
    cfg.headerHeight = 40;

    phide::ui::ActivityRail rail;
    phide::ui::EditorCanvas editor;
    editor.SetWorkspaceRoot(L"C:\\Users\\Administrator\\Downloads\\php-crud-test");

    phide::editor::OpenDocument doc;
    doc.isSpecialView = true;
    doc.viewType = L"monitor";
    doc.fileName = L"Performance Monitor";
    doc.filePath = L"special://monitor";

    std::vector<phide::editor::OpenDocument> docs;
    docs.push_back(std::move(doc));

    pRT->BeginDraw();
    pRT->Clear(theme.colors.windowBg);

    // Render Activity Rail on the left
    rail.Render(pRT.Get(), theme, cfg, static_cast<float>(h));

    // Render EditorCanvas to the right of rail
    float railW = cfg.ScaleF(static_cast<float>(cfg.railWidth));
    editor.Render(pRT.Get(), theme, cfg, docs, 0, railW, static_cast<float>(w) - railW, static_cast<float>(h), true);

    hr = pRT->EndDraw();
    assert(SUCCEEDED(hr));

    std::wstring outPath = L"C:\\Users\\Administrator\\.gemini\\antigravity\\brain\\2e65eefb-cd8f-409b-add6-886e19899c16\\test_monitor_visual_verification.png";
    SaveWicBitmapToPng(pWicBitmap.Get(), pWicFactory.Get(), outPath);

    // Now render a narrow split screen test (width = 520, height = 500)
    {
        const UINT sw = 520;
        const UINT sh = 500;
        ComPtr<IWICBitmap> pSplitBitmap;
        hr = pWicFactory->CreateBitmap(sw, sh, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &pSplitBitmap);
        if (SUCCEEDED(hr)) {
            ComPtr<ID2D1RenderTarget> pSplitRT;
            hr = pD2DFactory->CreateWicBitmapRenderTarget(pSplitBitmap.Get(), props, &pSplitRT);
            if (SUCCEEDED(hr)) {
                phide::ui::Theme splitTheme;
                splitTheme.SetThemeMode(phide::ui::ThemeMode::Dark);
                splitTheme.CreateDeviceResources(pSplitRT.Get(), pDWriteFactory.Get());

                pSplitRT->BeginDraw();
                pSplitRT->Clear(splitTheme.colors.windowBg);

                // Render in split mode (canvasLeft = 0, canvasWidth = 520)
                editor.Render(pSplitRT.Get(), splitTheme, cfg, docs, 0, 0.0f, static_cast<float>(sw), static_cast<float>(sh), true);

                pSplitRT->EndDraw();
                std::wstring splitPath = L"C:\\Users\\Administrator\\.gemini\\antigravity\\brain\\2e65eefb-cd8f-409b-add6-886e19899c16\\test_monitor_split_screen.png";
                SaveWicBitmapToPng(pSplitBitmap.Get(), pWicFactory.Get(), splitPath);

                // Now test scrolling down in split screen
                for (int i = 0; i < 6; ++i) {
                    editor.HandleMonitorMouseWheel(-120);
                }
                pSplitRT->BeginDraw();
                pSplitRT->Clear(splitTheme.colors.windowBg);
                editor.Render(pSplitRT.Get(), splitTheme, cfg, docs, 0, 0.0f, static_cast<float>(sw), static_cast<float>(sh), true);
                pSplitRT->EndDraw();
                std::wstring scrolledPath = L"C:\\Users\\Administrator\\.gemini\\antigravity\\brain\\2e65eefb-cd8f-409b-add6-886e19899c16\\test_monitor_split_screen_scrolled.png";
                SaveWicBitmapToPng(pSplitBitmap.Get(), pWicFactory.Get(), scrolledPath);

                // Now test scrolling to the very bottom to verify bottom diagnostic panels
                for (int i = 0; i < 15; ++i) {
                    editor.HandleMonitorMouseWheel(-120);
                }
                pSplitRT->BeginDraw();
                pSplitRT->Clear(splitTheme.colors.windowBg);
                editor.Render(pSplitRT.Get(), splitTheme, cfg, docs, 0, 0.0f, static_cast<float>(sw), static_cast<float>(sh), true);
                pSplitRT->EndDraw();
                std::wstring bottomPath = L"C:\\Users\\Administrator\\.gemini\\antigravity\\brain\\2e65eefb-cd8f-409b-add6-886e19899c16\\test_monitor_split_screen_bottom.png";
                SaveWicBitmapToPng(pSplitBitmap.Get(), pWicFactory.Get(), bottomPath);
            }
        }
    }

    std::cout << "Visual verification snapshot generated successfully." << std::endl;

    // Visual verification for Remote Explorer Activity Rail icon, Sidebar, StatusBar `><` pill, and RemoteWindowModal
    {
        ComPtr<IWICBitmap> pRemBitmap;
        hr = pWicFactory->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &pRemBitmap);
        assert(SUCCEEDED(hr));
        ComPtr<ID2D1RenderTarget> pRemRT;
        hr = pD2DFactory->CreateWicBitmapRenderTarget(pRemBitmap.Get(), props, &pRemRT);
        assert(SUCCEEDED(hr));

        phide::ui::Theme remTheme;
        remTheme.SetThemeMode(phide::ui::ThemeMode::Dark);
        remTheme.CreateDeviceResources(pRemRT.Get(), pDWriteFactory.Get());

        phide::app::AppConfig remCfg;
        remCfg.railWidth = 48;
        remCfg.headerHeight = 40;
        remCfg.drawerWidth = 310;
        remCfg.isDrawerOpen = true;
        remCfg.activeRailTab = static_cast<int>(phide::ui::RailTabId::RemoteExplorer);

        phide::ui::ActivityRail remRail;
        phide::ui::DrawerPanel remDrawer;
        remDrawer.OpenRemoteExplorer();
        // Give async real-environment probe a brief window so honest status text settles without blocking indefinitely
        Sleep(250);

        phide::ui::LucideIcons statusIcons;

        auto renderFrame = [&](phide::ui::RemoteWindowModal* pModal) {
            const float headerH = remCfg.ScaleF(static_cast<float>(remCfg.headerHeight));
            const float railW = remCfg.ScaleF(static_cast<float>(remCfg.railWidth));
            const float drawerW = remCfg.ScaleF(static_cast<float>(remCfg.drawerWidth));
            const float statusH = remCfg.ScaleF(22.0f);
            const float clientH = static_cast<float>(h);
            const float clientW = static_cast<float>(w);

            pRemRT->BeginDraw();
            pRemRT->Clear(remTheme.colors.windowBg);

            // Top title strip
            D2D1_RECT_F rcHdr = D2D1::RectF(0.0f, 0.0f, clientW, headerH);
            if (remTheme.brushHeaderBg) pRemRT->FillRectangle(rcHdr, remTheme.brushHeaderBg.Get());

            // Activity Rail + Remote Explorer Drawer
            remRail.Render(pRemRT.Get(), remTheme, remCfg, clientH - statusH);
            remDrawer.Render(pRemRT.Get(), remTheme, remCfg, clientH - statusH, drawerW);

            // Right canvas subtle placeholder
            D2D1_RECT_F rcCanvas = D2D1::RectF(railW + drawerW, headerH, clientW, clientH - statusH);
            if (remTheme.brushCanvasBg) pRemRT->FillRectangle(rcCanvas, remTheme.brushCanvasBg.Get());

            // Status bar + far-left Remote `><` accent pill + `main`
            D2D1_RECT_F rcBar = D2D1::RectF(0.0f, clientH - statusH, clientW, clientH);
            if (remTheme.brushHeaderBg) pRemRT->FillRectangle(rcBar, remTheme.brushHeaderBg.Get());

            const float pillW = remCfg.ScaleF(34.0f);
            D2D1_RECT_F rcRemPill = D2D1::RectF(0.0f, clientH - statusH, pillW, clientH);
            if (remTheme.brushBrandAccent) pRemRT->FillRectangle(rcRemPill, remTheme.brushBrandAccent.Get());
            const float icSz = remCfg.ScaleF(13.0f);
            const float icX = (pillW - icSz) * 0.5f;
            const float icY = (clientH - statusH) + (statusH - icSz) * 0.5f;
            statusIcons.DrawRemoteArrows(pRemRT.Get(), D2D1::RectF(icX, icY, icX + icSz, icY + icSz),
                remTheme.brushTextWhite ? remTheme.brushTextWhite.Get() : remTheme.brushTextPrimary.Get(), 1.7f);

            if (pModal && pModal->IsOpen()) {
                pModal->Render(pRemRT.Get(), remTheme, remCfg, clientW, clientH);
            }

            HRESULT hrEnd = pRemRT->EndDraw();
            assert(SUCCEEDED(hrEnd));
        };

        // 1. Sidebar + ActivityRail + StatusBar snapshot
        renderFrame(nullptr);
        const auto& btnPopup = remDrawer.GetRemoteOpenPopupBtnBounds();
        const auto& btnFilter = remDrawer.GetRemoteFilterBtnBounds();
        assert(btnPopup.right > btnPopup.left && btnPopup.bottom > btnPopup.top);
        assert(btnFilter.right > btnFilter.left && btnFilter.bottom > btnFilter.top);
        assert(btnFilter.bottom < btnPopup.top && "Filter dropdown must sit cleanly above Open Remote Window button without overlap!");
        SaveWicBitmapToPng(pRemBitmap.Get(), pWicFactory.Get(),
            L"C:\\Users\\Administrator\\.gemini\\antigravity\\brain\\2e65eefb-cd8f-409b-add6-886e19899c16\\test_remote_explorer_sidebar.png");

        // 2. Floating RemoteWindowModal snapshot
        phide::ui::RemoteWindowModal modal;
        modal.Open(phide::ui::RemoteModalMode::MainMenu, true);
        renderFrame(&modal);
        const auto& rcModal = modal.GetModalBounds();
        assert(rcModal.right > rcModal.left && rcModal.bottom > rcModal.top);
        SaveWicBitmapToPng(pRemBitmap.Get(), pWicFactory.Get(),
            L"C:\\Users\\Administrator\\.gemini\\antigravity\\brain\\2e65eefb-cd8f-409b-add6-886e19899c16\\test_remote_window_modal.png");
    }

        // 3. ConPTY real process output capture + clean EOF + DrawerPanel honest state verification
        {
            std::cout << "[TEST] Verifying live ConPTY output capture & SSH error visibility...\n";
            phide::ui::TerminalPanel term;
            std::atomic<int> outCallbacks{0};
            term.SetOutputCallback([&]() { outCallbacks++; });

            // 3a. Real cmd.exe under ConPTY must capture stdout and hit clean EOF without hanging
            term.NewRemoteCommandTab(L"ConPTY Echo", L"cmd.exe /c echo CONPTY_LIVE_OK", L"", true);
            auto* echoTab = term.GetActiveTab();
            assert(echoTab != nullptr && echoTab->isConPty);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (echoTab->isAlive.load() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            assert(!echoTab->isAlive.load() && "ConPTY echo process must exit cleanly via EOF");
            for (int w = 0; w < 20 && echoTab->processExitCode.load() < 0; ++w) {
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
            }
            std::wstring capturedEcho;
            {
                std::lock_guard<std::mutex> lk(echoTab->bufferMutex);
                for (const auto& ln : echoTab->lines) {
                    capturedEcho += ln.rawText + L"\n";
                }
            }
            assert(capturedEcho.find(L"CONPTY_LIVE_OK") != std::wstring::npos &&
                   "ConPTY must capture child stdout text");
            assert(capturedEcho.find(L"exit code 0") != std::wstring::npos &&
                   "ConPTY must report exit code when child finishes");
            assert(outCallbacks.load() > 0);
            std::cout << "  [PASS] Live ConPTY cmd.exe output & clean EOF verified\n";

            // 3b. Real ssh.exe against unreachable local port (fast connection refused) must display real error text
            if (phide::ui::remote::HasSshClientTool()) {
                phide::ui::remote::SshHostEntry deadHost{};
                deadHost.alias = L"127.0.0.1";
                deadHost.hostName = L"127.0.0.1";
                deadHost.user = L"nobody";
                deadHost.port = L"1";
                std::wstring sshCmd = phide::ui::remote::BuildInteractiveSshCommand(deadHost, L"");
                term.NewRemoteCommandTab(L"SSH: 127.0.0.1", sshCmd, L"", true);
                auto* sshTab = term.GetActiveTab();
                assert(sshTab != nullptr && sshTab->isConPty);
                auto sshDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
                while (sshTab->isAlive.load() && std::chrono::steady_clock::now() < sshDeadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                assert(!sshTab->isAlive.load() && "Unreachable SSH ConPTY session must terminate cleanly");
                std::wstring capturedSsh;
                {
                    std::lock_guard<std::mutex> lk(sshTab->bufferMutex);
                    for (const auto& ln : sshTab->lines) {
                        if (!ln.rawText.empty()) {
                            capturedSsh += ln.rawText + L"\n";
                        }
                    }
                }
                if ((capturedSsh.find(L"Connection refused") == std::wstring::npos &&
                     capturedSsh.find(L"127.0.0.1") == std::wstring::npos &&
                     capturedSsh.find(L"ssh:") == std::wstring::npos) ||
                    capturedSsh.find(L"exit code") == std::wstring::npos) {
                    std::wcerr << L"[FAIL] Unexpected ConPTY SSH output:\n" << capturedSsh << L"\n";
                }
                assert((capturedSsh.find(L"Connection refused") != std::wstring::npos ||
                        capturedSsh.find(L"127.0.0.1") != std::wstring::npos ||
                        capturedSsh.find(L"ssh:") != std::wstring::npos) &&
                       "ConPTY SSH tab must show real SSH diagnostic instead of blank black terminal");
                assert(capturedSsh.find(L"exit code") != std::wstring::npos &&
                       "ConPTY SSH tab must report remote exit code on close");
                std::cout << "  [PASS] Live ConPTY ssh.exe error output & status captured (never blank black)\n";

                // 3c. DrawerPanel must NOT report Connected green state when SSH fails
                phide::ui::DrawerPanel sshDrawer;
                std::atomic<int> repaints{0};
                phide::ui::DrawerPanel::RemoteCallbacks cbs;
                cbs.onRequestRepaint = [&]() {
                    repaints++;
                };
                sshDrawer.SetRemoteCallbacks(std::move(cbs));
                sshDrawer.BrowseRemoteSshPathAsync(deadHost, L"~");
                auto probeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
                while (repaints.load() < 2 && std::chrono::steady_clock::now() < probeDeadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(40));
                }
                assert(repaints.load() >= 2 && "BrowseRemoteSshPathAsync probe must complete");
                assert(!sshDrawer.IsRemoteConnected() && "Failed SSH host must never report IsRemoteConnected=true");
                assert(sshDrawer.GetActiveRemoteBadgeLabel().empty() && "Failed SSH host must not show green status bar badge");
                std::cout << "  [PASS] DrawerPanel honest SSH failure state verified (no false green Connected badge)\n";
            }

            term.CloseAllTabs();
        }
    }
    CoUninitialize();
    return 0;
}
