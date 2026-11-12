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
#include "phide/ui/EditorCanvas.hpp"
#include "phide/editor/OpenDocument.hpp"

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
    CoUninitialize();
    return 0;
}
