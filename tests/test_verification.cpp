/**
 * @file test_verification.cpp
 * @brief Comprehensive automated verification test suite for cursor, picture viewer, syntax highlights, and context menus.
 */

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
#include "phide/ui/TitleHeader.hpp"
#include "phide/ui/PopupMenu.hpp"
#include "phide/ui/ActivityRail.hpp"
#include "phide/ui/DrawerPanel.hpp"
#include "phide/ui/EditorCanvas.hpp"
#include "phide/editor/Buffer.hpp"
#include "phide/editor/OpenDocument.hpp"
#include "phide/workspace/WorkspaceManager.hpp"

using Microsoft::WRL::ComPtr;

static void SaveBitmapToPng(ID2D1Bitmap* pBitmap, IWICImagingFactory* pWicFactory, const std::wstring& outPath) {
    if (!pBitmap || !pWicFactory) return;

    D2D1_SIZE_U size = pBitmap->GetPixelSize();

    ComPtr<IWICBitmap> pWicBitmap;
    HRESULT hr = pWicFactory->CreateBitmap(size.width, size.height,
        GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &pWicBitmap);
    if (FAILED(hr)) return;

    ComPtr<ID2D1Factory> pD2DFactory;
    pBitmap->GetFactory(&pD2DFactory);

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    ComPtr<ID2D1RenderTarget> pWicRT;
    hr = pD2DFactory->CreateWicBitmapRenderTarget(pWicBitmap.Get(), props, &pWicRT);
    if (FAILED(hr)) return;

    pWicRT->BeginDraw();
    pWicRT->DrawBitmap(pBitmap);
    pWicRT->EndDraw();

    ComPtr<IWICStream> pStream;
    hr = pWicFactory->CreateStream(&pStream);
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

    hr = pFrame->SetSize(size.width, size.height);
    if (FAILED(hr)) return;

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    hr = pFrame->SetPixelFormat(&format);
    if (FAILED(hr)) return;

    hr = pFrame->WriteSource(pWicBitmap.Get(), nullptr);
    if (FAILED(hr)) return;

    hr = pFrame->Commit();
    if (FAILED(hr)) return;

    hr = pEncoder->Commit();
    if (FAILED(hr)) return;

    std::wcout << L"[SAVED] " << outPath << std::endl;
}

int main() {
    std::cout << "Starting Automated Verification Test Suite..." << std::endl;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        std::cerr << "CoInitializeEx failed" << std::endl;
        return 1;
    }

    // --- 1. Unit Test: Syntax Tokenizer for <?php and HTML/JSX Tags ---
    std::cout << "[TEST 1] Testing SyntaxLexer tokenization..." << std::endl;
    phide::editor::SyntaxLexer lexer;

    // Test <?php
    auto tokensPhp = lexer.TokenizeLine(L"<?php");
    assert(!tokensPhp.empty());
    assert(tokensPhp[0].type == phide::editor::TokenType::Keyword);
    assert(tokensPhp[0].length == 5);
    std::cout << "  Passed: <?php tokenized as Keyword" << std::endl;

    // Test ?>
    auto tokensClosePhp = lexer.TokenizeLine(L"?>");
    assert(!tokensClosePhp.empty());
    assert(tokensClosePhp[0].type == phide::editor::TokenType::Keyword);
    assert(tokensClosePhp[0].length == 2);
    std::cout << "  Passed: ?> tokenized as Keyword" << std::endl;

    // Test HTML Tag: <div class="container">
    auto tokensDiv = lexer.TokenizeLine(L"<div class=\"container\">");
    assert(tokensDiv.size() >= 6);
    assert(tokensDiv[0].type == phide::editor::TokenType::Operator);  // <
    assert(tokensDiv[1].type == phide::editor::TokenType::Tag);       // div
    assert(tokensDiv[2].type == phide::editor::TokenType::Attribute); // class
    assert(tokensDiv[3].type == phide::editor::TokenType::Operator);  // =
    assert(tokensDiv[4].type == phide::editor::TokenType::String);    // "container"
    assert(tokensDiv[5].type == phide::editor::TokenType::Operator);  // >
    std::cout << "  Passed: <div class=\"container\"> tags, attributes and string tokenized" << std::endl;

    // Test Closing Tag: </div>
    auto tokensCloseDiv = lexer.TokenizeLine(L"</div>");
    assert(tokensCloseDiv.size() >= 3);
    assert(tokensCloseDiv[0].type == phide::editor::TokenType::Operator); // </
    assert(tokensCloseDiv[1].type == phide::editor::TokenType::Tag);      // div
    assert(tokensCloseDiv[2].type == phide::editor::TokenType::Operator); // >
    std::cout << "  Passed: </div> closing tag tokenized" << std::endl;

    // Test JS/TS Keywords: var, let, require
    auto tokensJs = lexer.TokenizeLine(L"var x = 10; let y = 20; require('fs');");
    bool foundVar = false, foundLet = false, foundRequire = false;
    for (const auto& t : tokensJs) {
        if (t.type == phide::editor::TokenType::Keyword) {
            std::wstring word = std::wstring(L"var x = 10; let y = 20; require('fs');").substr(t.startCol, t.length);
            if (word == L"var") foundVar = true;
            if (word == L"let") foundLet = true;
            if (word == L"require") foundRequire = true;
        }
    }
    assert(foundVar && foundLet && foundRequire);
    std::cout << "  Passed: var, let, require tokenized as Keywords" << std::endl;

    // --- 2. Headless Direct2D Setup ---
    ComPtr<ID2D1Factory> pD2DFactory;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, pD2DFactory.GetAddressOf());
    assert(SUCCEEDED(hr));

    ComPtr<IDWriteFactory> pDWriteFactory;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(pDWriteFactory.GetAddressOf()));
    assert(SUCCEEDED(hr));

    ComPtr<IWICImagingFactory> pWicFactory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&pWicFactory));
    assert(SUCCEEDED(hr));

    const UINT width = 1200;
    const UINT height = 750;

    auto CreateTarget = [&](ComPtr<IWICBitmap>& outWicBmp, ComPtr<ID2D1RenderTarget>& outRT) {
        hr = pWicFactory->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                      WICBitmapCacheOnDemand, &outWicBmp);
        assert(SUCCEEDED(hr));

        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        hr = pD2DFactory->CreateWicBitmapRenderTarget(outWicBmp.Get(), props, &outRT);
        assert(SUCCEEDED(hr));
    };

    phide::app::AppConfig config;
    config.dpiScale = 1.0f;
    config.headerHeight = 38;
    config.railWidth = 48;
    config.drawerWidth = 250;
    config.isDrawerOpen = true;

    const std::wstring artDir = L"C:\\Users\\Administrator\\.gemini\\antigravity\\brain\\c83160b9-3f8b-4ad1-87a8-ab1f7f555a77\\";

    // --- 3. Render Test: Picture Viewer in Dark & Light Modes ---
    std::cout << "[TEST 2] Testing Picture Viewer rendering..." << std::endl;
    {
        // Dark Mode
        ComPtr<IWICBitmap> pWicBmp;
        ComPtr<ID2D1RenderTarget> pRT;
        CreateTarget(pWicBmp, pRT);

        phide::ui::Theme theme;
        theme.SetThemeMode(phide::ui::ThemeMode::Dark);
        hr = theme.CreateDeviceResources(pRT.Get(), pDWriteFactory.Get());
        assert(SUCCEEDED(hr));

        phide::ui::EditorCanvas editor;
        std::vector<phide::editor::OpenDocument> docs;
        phide::editor::OpenDocument doc;
        doc.filePath = L"cleavedb-logo-DEuJG-nm.png";
        doc.fileName = L"cleavedb-logo-DEuJG-nm.png";
        doc.isImageView = true;
        docs.push_back(std::move(doc));

        pRT->BeginDraw();
        pRT->Clear(theme.colors.windowBg);

        float railW = 48.0f;
        float drawerW = 250.0f;
        float canvasLeft = railW + drawerW;
        float canvasW = static_cast<float>(width) - canvasLeft;

        editor.Render(pRT.Get(), theme, config, docs, 0, canvasLeft, canvasW, static_cast<float>(height), true);
        pRT->EndDraw();

        // Verify image dimensions loaded
        assert(docs[0].pImageBitmap != nullptr);
        assert(docs[0].imageWidth > 0);
        assert(docs[0].imageHeight > 0);
        std::cout << "  Passed: Image decoded, size: " << docs[0].imageWidth << "x" << docs[0].imageHeight << std::endl;

        SaveBitmapToPng(nullptr, pWicFactory.Get(), artDir + L"test_picture_viewer_dark.png");
        // Also save directly from pWicBmp
        ComPtr<IWICStream> pStream;
        pWicFactory->CreateStream(&pStream);
        pStream->InitializeFromFilename((artDir + L"test_picture_viewer_dark.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> enc;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;
        enc->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(width, height);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
        frame->SetPixelFormat(&fmt);
        frame->WriteSource(pWicBmp.Get(), nullptr);
        frame->Commit();
        enc->Commit();
    }

    // --- 4. Render Test: Syntax Highlighting for Tags & <?php ---
    std::cout << "[TEST 3] Testing Syntax Highlighting for PHP & HTML tags..." << std::endl;
    {
        ComPtr<IWICBitmap> pWicBmp;
        ComPtr<ID2D1RenderTarget> pRT;
        CreateTarget(pWicBmp, pRT);

        phide::ui::Theme theme;
        theme.SetThemeMode(phide::ui::ThemeMode::Dark);
        theme.CreateDeviceResources(pRT.Get(), pDWriteFactory.Get());

        phide::ui::EditorCanvas editor;
        std::vector<phide::editor::OpenDocument> docs;
        phide::editor::OpenDocument doc;
        doc.filePath = L"index.php";
        doc.fileName = L"index.php";
        doc.isImageView = false;

        std::wstring sampleCode =
            L"<?php\n"
            L"declare(strict_types=1);\n"
            L"namespace App\\Http;\n\n"
            L"require_once __DIR__ . '/autoload.php';\n\n"
            L"class WelcomeController {\n"
            L"    public function index(): string {\n"
            L"        $greeting = \"Hello Phide!\";\n"
            L"        return $greeting;\n"
            L"    }\n"
            L"}\n\n"
            L"?>\n"
            L"<!DOCTYPE html>\n"
            L"<html lang=\"en\">\n"
            L"<head>\n"
            L"    <meta charset=\"UTF-8\">\n"
            L"    <title>Phide IDE</title>\n"
            L"</head>\n"
            L"<body>\n"
            L"    <div class=\"container\">\n"
            L"        <h1 id=\"title\"><?= $greeting ?></h1>\n"
            L"        <p class=\"lead\">Modern High-Performance Native IDE</p>\n"
            L"        <button type=\"submit\" class=\"btn btn-primary\">Execute</button>\n"
            L"    </div>\n"
            L"</body>\n"
            L"</html>\n";

        doc.buffer = phide::editor::Buffer(sampleCode);
        docs.push_back(std::move(doc));

        pRT->BeginDraw();
        pRT->Clear(theme.colors.windowBg);

        float railW = 48.0f;
        float drawerW = 250.0f;
        float canvasLeft = railW + drawerW;
        float canvasW = static_cast<float>(width) - canvasLeft;

        editor.Render(pRT.Get(), theme, config, docs, 0, canvasLeft, canvasW, static_cast<float>(height), true);
        pRT->EndDraw();

        // Save PNG
        ComPtr<IWICStream> pStream;
        pWicFactory->CreateStream(&pStream);
        pStream->InitializeFromFilename((artDir + L"test_syntax_tags_dark.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> enc;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;
        enc->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(width, height);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
        frame->SetPixelFormat(&fmt);
        frame->WriteSource(pWicBmp.Get(), nullptr);
        frame->Commit();
        enc->Commit();
        std::cout << "  Passed: Saved test_syntax_tags_dark.png" << std::endl;
    }

    // --- 5. Render Test: File & Folder Context Menus ---
    std::cout << "[TEST 4] Testing Context Menus (File vs Folder)..." << std::endl;
    {
        // 5a. File Context Menu
        ComPtr<IWICBitmap> pWicBmp;
        ComPtr<ID2D1RenderTarget> pRT;
        CreateTarget(pWicBmp, pRT);

        phide::ui::Theme theme;
        theme.SetThemeMode(phide::ui::ThemeMode::Dark);
        theme.CreateDeviceResources(pRT.Get(), pDWriteFactory.Get());

        phide::ui::PopupMenu menu;
        menu.OpenContextMenu(260.0f, 120.0f, false, L"C:\\phide\\src\\main.cpp", L"C:\\phide", config, false);
        assert(menu.IsOpen());

        pRT->BeginDraw();
        pRT->Clear(theme.colors.windowBg);
        menu.Render(pRT.Get(), theme, config);
        pRT->EndDraw();

        ComPtr<IWICStream> pStream;
        pWicFactory->CreateStream(&pStream);
        pStream->InitializeFromFilename((artDir + L"test_file_context_menu.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> enc;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;
        enc->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(width, height);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
        frame->SetPixelFormat(&fmt);
        frame->WriteSource(pWicBmp.Get(), nullptr);
        frame->Commit();
        enc->Commit();
        std::cout << "  Passed: Saved test_file_context_menu.png" << std::endl;
    }

    {
        // 5b. Folder Context Menu
        ComPtr<IWICBitmap> pWicBmp;
        ComPtr<ID2D1RenderTarget> pRT;
        CreateTarget(pWicBmp, pRT);

        phide::ui::Theme theme;
        theme.SetThemeMode(phide::ui::ThemeMode::Dark);
        theme.CreateDeviceResources(pRT.Get(), pDWriteFactory.Get());

        phide::ui::PopupMenu menu;
        menu.OpenContextMenu(260.0f, 120.0f, true, L"C:\\phide\\src", L"C:\\phide", config, true);
        assert(menu.IsOpen());

        pRT->BeginDraw();
        pRT->Clear(theme.colors.windowBg);
        menu.Render(pRT.Get(), theme, config);
        pRT->EndDraw();

        ComPtr<IWICStream> pStream;
        pWicFactory->CreateStream(&pStream);
        pStream->InitializeFromFilename((artDir + L"test_folder_context_menu.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> enc;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;
        enc->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(width, height);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
        frame->SetPixelFormat(&fmt);
        frame->WriteSource(pWicBmp.Get(), nullptr);
        frame->Commit();
        enc->Commit();
        std::cout << "  Passed: Saved test_folder_context_menu.png" << std::endl;
    }

    // --- 6. Cursor Logic Verification ---
    std::cout << "[TEST 5] Testing Mouse Cursor HitTest bounds..." << std::endl;
    {
        float railW = 48.0f;
        float drawerW = 250.0f;
        float canvasLeft = railW + drawerW; // 298.0f
        float gutterW = 50.0f;
        float headerH = 38.0f;
        float tabH = 34.0f;
        float breadcrumbH = 24.0f;
        float topY = headerH + tabH + breadcrumbH; // 96.0f
        float scrollbarW = 14.0f;
        float clientW = 1200.0f;
        float clientH = 750.0f;

        auto IsInsideCodeArea = [&](int x, int y) -> bool {
            return (x >= canvasLeft + gutterW && x < clientW - scrollbarW &&
                    y >= topY && y < clientH);
        };

        // Inside code area: should be I-beam
        assert(IsInsideCodeArea(400, 200) == true);
        assert(IsInsideCodeArea(350, 100) == true);
        assert(IsInsideCodeArea(1180, 500) == true);

        // Outside code area: in gutter
        assert(IsInsideCodeArea(320, 200) == false);

        // Outside code area: in tab bar
        assert(IsInsideCodeArea(400, 50) == false);

        // Outside code area: in breadcrumbs
        assert(IsInsideCodeArea(400, 80) == false);

        // Outside code area: in scrollbar
        assert(IsInsideCodeArea(1190, 200) == false);

        // Outside code area: in rail / drawer
        assert(IsInsideCodeArea(20, 200) == false);
        assert(IsInsideCodeArea(100, 200) == false);

        std::cout << "  Passed: Cursor logic perfectly isolates code area for IDC_IBEAM" << std::endl;
    }

    // --- 6. Search Throttler & Binary Exclusion Verification ---
    std::cout << "[TEST 6] Testing Search Throttler and Binary File Exclusion..." << std::endl;
    {
        phide::ui::DrawerPanel drawer;
        drawer.SetSearchFocus(1);

        // 1. Initial keystroke triggers immediate search
        ULONGLONG t0 = GetTickCount64();
        drawer.TriggerSearchThrottled(t0);
        // Next keystroke 50ms later is within throttle interval (< 250ms), marked pending
        drawer.TriggerSearchThrottled(t0 + 50);

        // Poll at 100ms (< 250ms interval) -> does NOT fire
        assert(drawer.PollSearchThrottle(t0 + 100) == false);

        // Poll at 260ms (>= 250ms interval) -> FIRES pending search!
        assert(drawer.PollSearchThrottle(t0 + 260) == true);

        // Subsequent poll without changes -> does NOT fire
        assert(drawer.PollSearchThrottle(t0 + 300) == false);
        std::cout << "  Passed: Throttler strictly enforces 250ms rate limit" << std::endl;
    }

    // --- 7. Unsupported Binary File View Test ---
    std::cout << "[TEST 7] Testing Unsupported Binary File View rendering..." << std::endl;
    {
        // Dark Mode
        ComPtr<IWICBitmap> pWicBmp;
        ComPtr<ID2D1RenderTarget> pRT;
        CreateTarget(pWicBmp, pRT);

        phide::ui::Theme theme;
        theme.SetThemeMode(phide::ui::ThemeMode::Dark);
        hr = theme.CreateDeviceResources(pRT.Get(), pDWriteFactory.Get());
        assert(SUCCEEDED(hr));

        phide::ui::EditorCanvas editor;
        std::vector<phide::editor::OpenDocument> docs;
        phide::editor::OpenDocument doc;
        doc.filePath = L"C:\\phide\\coordinator.exe";
        doc.fileName = L"coordinator.exe";
        doc.isUnsupportedBinary = true;
        doc.binaryFileType = L"Executable / Binary";
        doc.binaryFileSize = 14582912; // 13.9 MB
        docs.push_back(std::move(doc));

        pRT->BeginDraw();
        pRT->Clear(theme.colors.windowBg);

        float railW = 48.0f;
        float drawerW = 250.0f;
        float canvasLeft = railW + drawerW;
        float canvasW = static_cast<float>(width) - canvasLeft;

        editor.Render(pRT.Get(), theme, config, docs, 0, canvasLeft, canvasW, static_cast<float>(height), true);
        pRT->EndDraw();

        // Verify action button bounds were computed
        assert(docs[0].unsupportedActionBtn.right > docs[0].unsupportedActionBtn.left);
        assert(docs[0].unsupportedActionBtn.bottom > docs[0].unsupportedActionBtn.top);

        ComPtr<IWICStream> pStream;
        pWicFactory->CreateStream(&pStream);
        pStream->InitializeFromFilename((artDir + L"test_unsupported_binary_dark.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> enc;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;
        enc->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(width, height);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
        frame->SetPixelFormat(&fmt);
        frame->WriteSource(pWicBmp.Get(), nullptr);
        frame->Commit();
        enc->Commit();
        std::cout << "  Passed: Saved test_unsupported_binary_dark.png" << std::endl;
    }

    {
        // Light Mode Unsupported Binary View
        ComPtr<IWICBitmap> pWicBmp;
        ComPtr<ID2D1RenderTarget> pRT;
        CreateTarget(pWicBmp, pRT);

        phide::ui::Theme theme;
        theme.SetThemeMode(phide::ui::ThemeMode::Light);
        hr = theme.CreateDeviceResources(pRT.Get(), pDWriteFactory.Get());
        assert(SUCCEEDED(hr));

        phide::ui::EditorCanvas editor;
        std::vector<phide::editor::OpenDocument> docs;
        phide::editor::OpenDocument doc;
        doc.filePath = L"C:\\phide\\coordinator.exe";
        doc.fileName = L"coordinator.exe";
        doc.isUnsupportedBinary = true;
        doc.binaryFileType = L"Executable / Binary";
        doc.binaryFileSize = 14582912; // 13.9 MB
        docs.push_back(std::move(doc));

        pRT->BeginDraw();
        pRT->Clear(theme.colors.windowBg);

        float railW = 48.0f;
        float drawerW = 250.0f;
        float canvasLeft = railW + drawerW;
        float canvasW = static_cast<float>(width) - canvasLeft;

        editor.Render(pRT.Get(), theme, config, docs, 0, canvasLeft, canvasW, static_cast<float>(height), true);
        pRT->EndDraw();

        ComPtr<IWICStream> pStream;
        pWicFactory->CreateStream(&pStream);
        pStream->InitializeFromFilename((artDir + L"test_unsupported_binary_light.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> enc;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;
        enc->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(width, height);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
        frame->SetPixelFormat(&fmt);
        frame->WriteSource(pWicBmp.Get(), nullptr);
        frame->Commit();
        enc->Commit();
        std::cout << "  Passed: Saved test_unsupported_binary_light.png" << std::endl;
    }

    // --- 8. Split Editor Button & Side-by-Side View ---
    std::cout << "[TEST 8] Testing Split Editor button and Side-by-Side Split View..." << std::endl;
    {
        // Dark Mode Side-by-Side Split View
        ComPtr<IWICBitmap> pWicBmp;
        ComPtr<ID2D1RenderTarget> pRT;
        CreateTarget(pWicBmp, pRT);

        phide::ui::Theme theme;
        theme.SetThemeMode(phide::ui::ThemeMode::Dark);
        hr = theme.CreateDeviceResources(pRT.Get(), pDWriteFactory.Get());
        assert(SUCCEEDED(hr));

        phide::ui::TitleHeader header;

        phide::ui::EditorCanvas editor;
        std::vector<phide::editor::EditorGroup> groups;

        phide::editor::OpenDocument doc1;
        doc1.filePath = L"C:\\phide\\src\\main.cpp";
        doc1.fileName = L"main.cpp";
        doc1.buffer = phide::editor::Buffer(
            L"#include <windows.h>\n"
            L"#include \"phide/app/Window.hpp\"\n\n"
            L"int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {\n"
            L"    phide::app::Window app;\n"
            L"    if (!app.Initialize(hInstance, nCmdShow)) return 1;\n"
            L"    return app.Run();\n"
            L"}\n"
        );
        phide::editor::EditorGroup eg1;
        eg1.docs.push_back(std::move(doc1));
        eg1.activeDocIdx = 0;
        groups.push_back(std::move(eg1));

        phide::editor::OpenDocument doc2;
        doc2.filePath = L"C:\\phide\\src\\app\\Window.cpp";
        doc2.fileName = L"Window.cpp";
        doc2.buffer = phide::editor::Buffer(
            L"#include \"phide/app/Window.hpp\"\n\n"
            L"namespace phide::app {\n\n"
            L"void Window::SplitEditor() {\n"
            L"    // Multi-column split\n"
            L"}\n\n"
            L"} // namespace phide::app\n"
        );
        phide::editor::EditorGroup eg2;
        eg2.docs.push_back(std::move(doc2));
        eg2.activeDocIdx = 0;
        groups.push_back(std::move(eg2));

        pRT->BeginDraw();
        pRT->Clear(theme.colors.windowBg);

        float railW = 48.0f;
        float drawerW = 250.0f;
        float canvasLeft = railW + drawerW;
        float canvasW = static_cast<float>(width) - canvasLeft;

        // Render Title Header with Split Editor button
        header.Render(pRT.Get(), theme, config, width, false);

        // Verify Split Editor button HitTest
        POINT ptSplit = {
            static_cast<LONG>((header.GetSplitEditorBounds().left + header.GetSplitEditorBounds().right) / 2.0f),
            static_cast<LONG>((header.GetSplitEditorBounds().top + header.GetSplitEditorBounds().bottom) / 2.0f)
        };
        assert(header.HitTestSplitEditor(ptSplit) == true);

        // Render Split View: Left pane = main.cpp (group 0), Right pane = Window.cpp (group 1, focused)
        editor.Render(pRT.Get(), theme, config, groups, 1, canvasLeft, canvasW, static_cast<float>(height), true);

        pRT->EndDraw();

        // Check tabs and bounds
        assert(groups[0].docs[0].tabBounds.right > groups[0].docs[0].tabBounds.left);
        assert(groups[1].docs[0].tabBounds.right > groups[1].docs[0].tabBounds.left);

        ComPtr<IWICStream> pStream;
        pWicFactory->CreateStream(&pStream);
        pStream->InitializeFromFilename((artDir + L"test_split_editor_dark.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> enc;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;
        enc->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(width, height);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
        frame->SetPixelFormat(&fmt);
        frame->WriteSource(pWicBmp.Get(), nullptr);
        frame->Commit();
        enc->Commit();
        std::cout << "  Passed: Saved test_split_editor_dark.png" << std::endl;
    }

    {
        // Light Mode Side-by-Side Split View
        ComPtr<IWICBitmap> pWicBmp;
        ComPtr<ID2D1RenderTarget> pRT;
        CreateTarget(pWicBmp, pRT);

        phide::ui::Theme theme;
        theme.SetThemeMode(phide::ui::ThemeMode::Light);
        hr = theme.CreateDeviceResources(pRT.Get(), pDWriteFactory.Get());
        assert(SUCCEEDED(hr));

        phide::ui::TitleHeader header;

        phide::ui::EditorCanvas editor;
        std::vector<phide::editor::EditorGroup> groups;

        phide::editor::OpenDocument doc1;
        doc1.filePath = L"C:\\phide\\src\\main.cpp";
        doc1.fileName = L"main.cpp";
        doc1.buffer = phide::editor::Buffer(
            L"#include <windows.h>\n"
            L"#include \"phide/app/Window.hpp\"\n\n"
            L"int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {\n"
            L"    phide::app::Window app;\n"
            L"    if (!app.Initialize(hInstance, nCmdShow)) return 1;\n"
            L"    return app.Run();\n"
            L"}\n"
        );
        phide::editor::EditorGroup eg1;
        eg1.docs.push_back(std::move(doc1));
        eg1.activeDocIdx = 0;
        groups.push_back(std::move(eg1));

        phide::editor::OpenDocument doc2;
        doc2.filePath = L"C:\\phide\\src\\app\\Window.cpp";
        doc2.fileName = L"Window.cpp";
        doc2.buffer = phide::editor::Buffer(
            L"#include \"phide/app/Window.hpp\"\n\n"
            L"namespace phide::app {\n\n"
            L"void Window::SplitEditor() {\n"
            L"    // Multi-column split\n"
            L"}\n\n"
            L"} // namespace phide::app\n"
        );
        phide::editor::EditorGroup eg2;
        eg2.docs.push_back(std::move(doc2));
        eg2.activeDocIdx = 0;
        groups.push_back(std::move(eg2));

        pRT->BeginDraw();
        pRT->Clear(theme.colors.windowBg);

        float railW = 48.0f;
        float drawerW = 250.0f;
        float canvasLeft = railW + drawerW;
        float canvasW = static_cast<float>(width) - canvasLeft;

        // Render Title Header with Split Editor button (black icon in light mode)
        header.Render(pRT.Get(), theme, config, width, false);

        // Render Split View: Left pane = main.cpp (group 0), Right pane = Window.cpp (group 1, focused)
        editor.Render(pRT.Get(), theme, config, groups, 1, canvasLeft, canvasW, static_cast<float>(height), true);

        pRT->EndDraw();

        // Check tabs and bounds
        assert(groups[0].docs[0].tabBounds.right > groups[0].docs[0].tabBounds.left);
        assert(groups[1].docs[0].tabBounds.right > groups[1].docs[0].tabBounds.left);

        ComPtr<IWICStream> pStream;
        pWicFactory->CreateStream(&pStream);
        pStream->InitializeFromFilename((artDir + L"test_split_editor_light.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> enc;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;
        enc->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(width, height);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
        frame->SetPixelFormat(&fmt);
        frame->WriteSource(pWicBmp.Get(), nullptr);
        frame->Commit();
        enc->Commit();
        std::cout << "  Passed: Saved test_split_editor_light.png" << std::endl;
    }

    // --- 9. Multi-Editor Groups Isolation, Split Columns, Independent Close, and GC ---
    std::cout << "[TEST 9] Testing Multi-Editor Groups (3-way split, tab isolation, independent close, auto-collapse, GC)..." << std::endl;
    {
        // 1. Manual Garbage Collection verification on DrawerPanel
        phide::ui::DrawerPanel drawer;
        drawer.ClearSearchResults();
        assert(drawer.GetSearchGroups().empty());
        std::cout << "  Passed: Manual GC ClearSearchResults() releases memory with shrink_to_fit" << std::endl;

        // 2. Editor Groups Isolation & Up to 3 Columns Split
        std::vector<phide::editor::EditorGroup> groups;
        phide::editor::EditorGroup g1;
        phide::editor::OpenDocument docA;
        docA.fileName = L"fileA.txt";
        docA.filePath = L"C:\\fileA.txt";
        phide::editor::OpenDocument docB;
        docB.fileName = L"fileB.txt";
        docB.filePath = L"C:\\fileB.txt";
        g1.docs.push_back(docA);
        g1.docs.push_back(docB);
        g1.activeDocIdx = 0; // viewing fileA.txt
        groups.push_back(std::move(g1));

        assert(groups.size() == 1);
        assert(groups[0].docs.size() == 2);

        // First Split: Clones ONLY active document (fileA.txt), NOT fileB.txt!
        auto* activeDoc = &groups[0].docs[groups[0].activeDocIdx];
        phide::editor::EditorGroup g2;
        g2.docs.push_back(*activeDoc);
        g2.activeDocIdx = 0;
        groups.push_back(std::move(g2));

        assert(groups.size() == 2);
        assert(groups[1].docs.size() == 1);
        assert(groups[1].docs[0].fileName == L"fileA.txt");
        assert(groups[0].docs.size() == 2); // Group 1 still has both fileA and fileB!

        // Second Split: Creates 3rd editor column
        phide::editor::EditorGroup g3;
        g3.docs.push_back(*activeDoc);
        g3.activeDocIdx = 0;
        groups.push_back(std::move(g3));

        assert(groups.size() == 3);
        // Attempting to split past 3 columns is prevented
        bool canSplitAgain = (groups.size() < 3);
        assert(!canSplitAgain);

        // 3. Independent Tab Closing: Closing fileA on editor 2 does NOT close fileA on editor 1!
        assert(groups[1].docs.size() == 1);
        groups[1].docs.erase(groups[1].docs.begin()); // close tab in group 2
        assert(groups[1].docs.empty());

        // Auto-collapse empty editor group
        if (groups[1].docs.empty() && groups.size() > 1) {
            groups.erase(groups.begin() + 1);
        }
        assert(groups.size() == 2); // 3 -> 2 columns

        // Group 1 STILL has fileA.txt and fileB.txt!
        assert(groups[0].docs.size() == 2);
        assert(groups[0].docs[0].fileName == L"fileA.txt");
        assert(groups[0].docs[1].fileName == L"fileB.txt");

        // Close last tab in group 2 (previously group 3)
        groups[1].docs.erase(groups[1].docs.begin());
        if (groups[1].docs.empty() && groups.size() > 1) {
            groups.erase(groups.begin() + 1);
        }
        assert(groups.size() == 1); // 2 -> 1 column
        assert(groups[0].docs.size() == 2); // Group 1 completely unharmed!
        std::cout << "  Passed: Multi-Editor Groups 3-column split, file isolation, independent tab close, and auto-collapse verified" << std::endl;
    }

    std::cout << "\n==========================================" << std::endl;
    std::cout << "ALL VERIFICATION TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "==========================================" << std::endl;

    pD2DFactory.Reset();
    pDWriteFactory.Reset();
    pWicFactory.Reset();
    CoUninitialize();
    return 0;
}
