/**
 * @file SplashScreen.cpp
 * @brief Implementation of Phide architectural branding splash screen.
 */

#include "phide/ui/SplashScreen.hpp"
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>

namespace phide::ui {

std::wstring SplashScreen::FindSplashImagePath() {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

    std::vector<std::filesystem::path> candidates = {
        exeDir / L"assets" / L"splash.jpg",
        exeDir.parent_path() / L"assets" / L"splash.jpg",
        std::filesystem::current_path() / L"assets" / L"splash.jpg",
        L"assets\\splash.jpg"
    };

    for (const auto& c : candidates) {
        if (std::filesystem::exists(c)) return c.wstring();
    }
    return candidates[0].wstring();
}

void SplashScreen::Show(HINSTANCE hInstance, int durationMs) {
    std::wstring imgPath = FindSplashImagePath();
    if (!std::filesystem::exists(imgPath)) {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"PhideSplashScreenClass";
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int splashW = 854;
    int splashH = 480;
    int x = (screenW - splashW) / 2;
    int y = (screenH - splashH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"PhideSplashScreenClass",
        L"Phide",
        WS_POPUP | WS_VISIBLE,
        x, y, splashW, splashH,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) return;

    // Initialize WIC and Direct2D to render splash.jpg
    Microsoft::WRL::ComPtr<ID2D1Factory> pD2DFactory;
    D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory),
        nullptr,
        reinterpret_cast<void**>(pD2DFactory.GetAddressOf())
    );

    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> pRT;
    if (pD2DFactory) {
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
            hwnd, D2D1::SizeU(splashW, splashH), D2D1_PRESENT_OPTIONS_IMMEDIATELY
        );
        pD2DFactory->CreateHwndRenderTarget(rtProps, hwndProps, &pRT);
    }

    Microsoft::WRL::ComPtr<ID2D1Bitmap> pBitmap;
    Microsoft::WRL::ComPtr<IWICImagingFactory> pWicFactory;
    CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pWicFactory)
    );

    if (pWicFactory && pRT) {
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDecoder;
        HRESULT hr = pWicFactory->CreateDecoderFromFilename(
            imgPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder
        );
        if (SUCCEEDED(hr) && pDecoder) {
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
            if (SUCCEEDED(pDecoder->GetFrame(0, &pFrame)) && pFrame) {
                Microsoft::WRL::ComPtr<IWICFormatConverter> pConverter;
                if (SUCCEEDED(pWicFactory->CreateFormatConverter(&pConverter)) && pConverter) {
                    if (SUCCEEDED(pConverter->Initialize(
                        pFrame.Get(), GUID_WICPixelFormat32bppPBGRA,
                        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom
                    ))) {
                        pRT->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &pBitmap);
                    }
                }
            }
        }
    }

    // Render splash image immediately
    if (pRT) {
        pRT->BeginDraw();
        pRT->Clear(D2D1::ColorF(0.02f, 0.03f, 0.05f, 1.0f));
        D2D1_RECT_F rcDest = D2D1::RectF(0.0f, 0.0f, static_cast<float>(splashW), static_cast<float>(splashH));
        if (pBitmap) {
            pRT->DrawBitmap(pBitmap.Get(), rcDest, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        pRT->EndDraw();
    }

    UpdateWindow(hwnd);

    // Modal message loop: guaranteed display for the full durationMs (5 seconds)
    ULONGLONG start = GetTickCount64();
    ULONGLONG targetDuration = static_cast<ULONGLONG>(durationMs > 0 ? durationMs : 5000);
    MSG msg;

    while (GetTickCount64() - start < targetDuration) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(msg.wParam));
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(20);
    }

    DestroyWindow(hwnd);
    UnregisterClassW(L"PhideSplashScreenClass", hInstance);
}

} // namespace phide::ui
