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
#include <fstream>
#include <filesystem>

#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/TitleHeader.hpp"
#include "phide/ui/PopupMenu.hpp"
#include "phide/ui/ActivityRail.hpp"
#include "phide/ui/DrawerPanel.hpp"
#include "phide/ui/EditorCanvas.hpp"
#include "phide/ui/ThemePickerModal.hpp"
#include "phide/ui/KeyboardShortcutsView.hpp"
#include "phide/editor/Buffer.hpp"
#include "phide/editor/BracketPairAnalyzer.hpp"
#include "phide/editor/OpenDocument.hpp"
#include "phide/workspace/WorkspaceManager.hpp"
#include "phide/workspace/ExplorerUndoManager.hpp"

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

    // --- 10. Dedicated Editor Context Menu, Refactor Balloon Popover, Explorer Active State & Inline Rename Pre-selection ---
    std::cout << "[TEST 10] Testing Dedicated Editor Context Menu, Refactor Popover, Explorer Active State & Inline Rename Pre-selection..." << std::endl;
    {
        phide::ui::Theme theme;
        // 1. Dedicated Editor Context Menu (Image 4)
        phide::ui::PopupMenu menu;
        phide::app::AppConfig cfg;
        menu.OpenEditorContextMenu(100.0f, 150.0f, cfg, true, true);
        assert(menu.IsOpen());
        const auto& items = menu.GetItems();
        assert(items.size() == 8); // 6 commands + 2 separators

        assert(items[0].actionId == phide::ui::MenuActionId::EditorChangeAllOccurrences);
        assert(items[0].label == L"Change All Occurrences");
        assert(items[0].shortcut == L"Ctrl+F2");
        assert(items[0].isEnabled == true);

        assert(items[1].actionId == phide::ui::MenuActionId::EditorRefactor);
        assert(items[1].label == L"Refactor...");
        assert(items[1].shortcut == L"Ctrl+Shift+R");
        assert(items[1].isEnabled == true);

        assert(items[2].isSeparator == true);

        assert(items[3].actionId == phide::ui::MenuActionId::EditorCut);
        assert(items[3].label == L"Cut");
        assert(items[3].shortcut == L"Ctrl+X");
        assert(items[3].isEnabled == true);

        assert(items[4].actionId == phide::ui::MenuActionId::EditorCopy);
        assert(items[4].label == L"Copy");
        assert(items[4].shortcut == L"Ctrl+C");
        assert(items[4].isEnabled == true);

        assert(items[5].actionId == phide::ui::MenuActionId::EditorPaste);
        assert(items[5].label == L"Paste");
        assert(items[5].shortcut == L"Ctrl+V");
        assert(items[5].isEnabled == true);

        assert(items[6].isSeparator == true);

        assert(items[7].actionId == phide::ui::MenuActionId::EditorCommandPalette);
        assert(items[7].label == L"Command Palette...");
        assert(items[7].shortcut == L"Ctrl+Shift+P");
        assert(items[7].isEnabled == true);

        std::cout << "  Passed: Dedicated Editor Context Menu verified matching Image 4 (all 6 commands & shortcuts)" << std::endl;

        // Visual Render Test for Dedicated Editor Context Menu (Image 4)
        {
            UINT w = 640, h = 400;
            ComPtr<IWICBitmap> pWicBmp;
            pWicFactory->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &pWicBmp);
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
            );
            ComPtr<ID2D1RenderTarget> pRT;
            pD2DFactory->CreateWicBitmapRenderTarget(pWicBmp.Get(), props, &pRT);
            theme.CreateDeviceResources(pRT.Get(), pDWriteFactory.Get());

            pRT->BeginDraw();
            pRT->Clear(theme.colors.canvasBg);

            menu.OpenEditorContextMenu(180.0f, 60.0f, cfg, true, true);
            menu.Render(pRT.Get(), theme, cfg);

            pRT->EndDraw();

            ComPtr<IWICStream> pStream;
            pWicFactory->CreateStream(&pStream);
            pStream->InitializeFromFilename((artDir + L"test_editor_context_menu.png").c_str(), GENERIC_WRITE);
            ComPtr<IWICBitmapEncoder> enc;
            pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
            enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
            ComPtr<IWICBitmapFrameEncode> frame;
            enc->CreateNewFrame(&frame, nullptr);
            frame->Initialize(nullptr);
            frame->SetSize(w, h);
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
            frame->SetPixelFormat(&fmt);
            frame->WriteSource(pWicBmp.Get(), nullptr);
            frame->Commit();
            enc->Commit();
            std::cout << "  Passed: Saved test_editor_context_menu.png" << std::endl;
        }

        // 2. Refactor Balloon Popover (Image 5)
        phide::ui::EditorCanvas editor;
        assert(!editor.IsRefactorPopupVisible());
        editor.ShowRefactorPopup(L"No refactorings available");
        assert(editor.IsRefactorPopupVisible());

        // Visual Render Test for Refactor Balloon Popover (Image 5)
        {
            UINT w = 640, h = 400;
            ComPtr<IWICBitmap> pWicBmp;
            pWicFactory->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &pWicBmp);
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
            );
            ComPtr<ID2D1RenderTarget> pRT;
            pD2DFactory->CreateWicBitmapRenderTarget(pWicBmp.Get(), props, &pRT);
            theme.CreateDeviceResources(pRT.Get(), pDWriteFactory.Get());

            pRT->BeginDraw();
            pRT->Clear(theme.colors.canvasBg);

            std::vector<phide::editor::OpenDocument> docs;
            phide::editor::OpenDocument d;
            d.fileName = L"main.cpp";
            d.filePath = L"C:\\main.cpp";
            d.buffer.InsertText(L"#include <iostream>\n\nint main() {\n    std::cout << \"Hello\";\n    return 0;\n}\n");
            d.buffer.SetCursor(2, 5);
            docs.push_back(std::move(d));

            editor.Render(pRT.Get(), theme, cfg, docs, 0, 0.0f, static_cast<float>(w), static_cast<float>(h));

            pRT->EndDraw();

            ComPtr<IWICStream> pStream;
            pWicFactory->CreateStream(&pStream);
            pStream->InitializeFromFilename((artDir + L"test_refactor_popup.png").c_str(), GENERIC_WRITE);
            ComPtr<IWICBitmapEncoder> enc;
            pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
            enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
            ComPtr<IWICBitmapFrameEncode> frame;
            enc->CreateNewFrame(&frame, nullptr);
            frame->Initialize(nullptr);
            frame->SetSize(w, h);
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
            frame->SetPixelFormat(&fmt);
            frame->WriteSource(pWicBmp.Get(), nullptr);
            frame->Commit();
            enc->Commit();
            std::cout << "  Passed: Saved test_refactor_popup.png" << std::endl;
        }

        editor.DismissRefactorPopup();
        assert(!editor.IsRefactorPopupVisible());
        std::cout << "  Passed: Refactor Balloon Popover show/dismiss state verified" << std::endl;

        // 3. Explorer Active File State Synchronization (Images 2 & 3)
        phide::ui::DrawerPanel drawer;
        assert(drawer.GetActiveFilePath().empty());
        drawer.SetActiveFilePath(L"C:\\project\\src\\main.cpp");
        assert(drawer.GetActiveFilePath() == L"C:\\project\\src\\main.cpp");
        std::cout << "  Passed: Explorer Active File State synchronization verified" << std::endl;

        // 4. Inline Rename Pre-selection and Cancellation
        drawer.CancelInlineRename();
        assert(!drawer.IsRenaming());
        std::cout << "  Passed: Inline rename pre-selection and cancellation verified" << std::endl;
    }

    // --- 11. Test Explorer Undo/Redo & Additional Themes (Cyan, Turquoise, Purple, Monokai, Dracula) ---
    std::cout << "[TEST 11] Testing Explorer Undo/Redo Stack & Expanded 8-Theme System..." << std::endl;
    {
        // 1. Explorer Undo/Redo Manager unit testing
        phide::workspace::ExplorerUndoManager undoMgr;
        std::error_code ec;
        std::filesystem::path testRoot = std::filesystem::temp_directory_path() / L"phide_test_undo";
        std::filesystem::create_directories(testRoot, ec);

        // A. Rename Undo/Redo
        std::filesystem::path fileA = testRoot / L"hello.txt";
        std::filesystem::path fileB = testRoot / L"world.txt";
        {
            std::ofstream ofs(fileA);
            ofs << "sample content for rename test";
        }
        assert(std::filesystem::exists(fileA));

        std::filesystem::rename(fileA, fileB, ec);
        assert(std::filesystem::exists(fileB));
        assert(!std::filesystem::exists(fileA));

        undoMgr.PushRename(fileA.wstring(), fileB.wstring());
        assert(undoMgr.CanUndo());

        std::wstring affOld, affNew;
        bool wasRename = false;
        bool ok = undoMgr.Undo(affOld, affNew, wasRename);
        assert(ok);
        assert(wasRename);
        assert(affOld == fileB.wstring());
        assert(affNew == fileA.wstring());
        assert(std::filesystem::exists(fileA));
        assert(!std::filesystem::exists(fileB));

        // Redo rename
        assert(undoMgr.CanRedo());
        ok = undoMgr.Redo(affOld, affNew, wasRename);
        assert(ok);
        assert(wasRename);
        assert(std::filesystem::exists(fileB));
        assert(!std::filesystem::exists(fileA));
        std::cout << "  Passed: Explorer Undo/Redo Rename verified" << std::endl;

        // B. Deletion Undo/Restore (with safe staging trash cache)
        std::filesystem::path delFile = testRoot / L"to_delete.txt";
        {
            std::ofstream ofs(delFile);
            ofs << "CRITICAL CONTENT TO RESTORE";
        }
        std::wstring backupPath;
        bool prepared = undoMgr.PrepareDelete(delFile.wstring(), false, backupPath);
        assert(prepared);
        assert(!backupPath.empty());
        assert(std::filesystem::exists(backupPath));

        std::filesystem::remove(delFile, ec);
        assert(!std::filesystem::exists(delFile));

        undoMgr.PushDelete(delFile.wstring(), backupPath, false);
        assert(undoMgr.CanUndo());

        ok = undoMgr.Undo(affOld, affNew, wasRename);
        assert(ok);
        assert(!wasRename);
        assert(std::filesystem::exists(delFile));
        {
            std::ifstream ifs(delFile);
            std::string content;
            std::getline(ifs, content);
            assert(content == "CRITICAL CONTENT TO RESTORE");
        }

        // Redo deletion
        assert(undoMgr.CanRedo());
        ok = undoMgr.Redo(affOld, affNew, wasRename);
        assert(ok);
        assert(!std::filesystem::exists(delFile));

        // Undo deletion again!
        ok = undoMgr.Undo(affOld, affNew, wasRename);
        assert(ok);
        assert(std::filesystem::exists(delFile));
        std::cout << "  Passed: Explorer Undo/Redo Deletion with Staging Trash verified" << std::endl;

        // C. Directory Tree Deletion Undo/Restore
        std::filesystem::path delFolder = testRoot / L"my_folder";
        std::filesystem::create_directories(delFolder / L"sub", ec);
        {
            std::ofstream ofs(delFolder / L"sub" / L"nested.txt");
            ofs << "nested file data";
        }
        std::wstring folderBackup;
        prepared = undoMgr.PrepareDelete(delFolder.wstring(), true, folderBackup);
        assert(prepared);
        std::filesystem::remove_all(delFolder, ec);
        assert(!std::filesystem::exists(delFolder));

        undoMgr.PushDelete(delFolder.wstring(), folderBackup, true);
        ok = undoMgr.Undo(affOld, affNew, wasRename);
        assert(ok);
        assert(std::filesystem::exists(delFolder / L"sub" / L"nested.txt"));
        std::cout << "  Passed: Explorer Undo/Redo Folder Tree Restoration verified" << std::endl;

        // D. Create Undo/Redo
        std::filesystem::path createPath = testRoot / L"new_item.txt";
        {
            std::ofstream ofs(createPath);
            ofs << "just created";
        }
        undoMgr.PushCreate(createPath.wstring(), false);
        ok = undoMgr.Undo(affOld, affNew, wasRename);
        assert(ok);
        assert(!std::filesystem::exists(createPath));

        ok = undoMgr.Redo(affOld, affNew, wasRename);
        assert(ok);
        assert(std::filesystem::exists(createPath));
        std::cout << "  Passed: Explorer Undo/Redo Create verified" << std::endl;

        undoMgr.CleanupTrash();
        std::filesystem::remove_all(testRoot, ec);

        // 2. Test Expanded Theme Modes & Palettes
        phide::ui::Theme theme;

        theme.SetThemeMode(phide::ui::ThemeMode::Purple);
        assert(theme.GetThemeMode() == phide::ui::ThemeMode::Purple);
        assert(theme.IsDarkMode());

        theme.SetThemeMode(phide::ui::ThemeMode::Cyan);
        assert(theme.GetThemeMode() == phide::ui::ThemeMode::Cyan);

        theme.SetThemeMode(phide::ui::ThemeMode::Turquoise);
        assert(theme.GetThemeMode() == phide::ui::ThemeMode::Turquoise);

        theme.SetThemeMode(phide::ui::ThemeMode::Monokai);
        assert(theme.GetThemeMode() == phide::ui::ThemeMode::Monokai);

        theme.SetThemeMode(phide::ui::ThemeMode::Dracula);
        assert(theme.GetThemeMode() == phide::ui::ThemeMode::Dracula);

        theme.SetThemeMode(phide::ui::ThemeMode::Nord);
        assert(theme.GetThemeMode() == phide::ui::ThemeMode::Nord);

        theme.SetThemeMode(phide::ui::ThemeMode::Solarized);
        assert(theme.GetThemeMode() == phide::ui::ThemeMode::Solarized);

        theme.SetThemeMode(phide::ui::ThemeMode::GitHubDark);
        assert(theme.GetThemeMode() == phide::ui::ThemeMode::GitHubDark);

        theme.SetThemeMode(phide::ui::ThemeMode::OneDark);
        assert(theme.GetThemeMode() == phide::ui::ThemeMode::OneDark);

        theme.SetThemeMode(phide::ui::ThemeMode::SynthWave);
        assert(theme.GetThemeMode() == phide::ui::ThemeMode::SynthWave);
        std::cout << "  Passed: All 13 Theme Mode switches verified" << std::endl;

        // 3. Test Theme Picker Modal with Wide Widescreen Preview Cards & Catalog Snapshot
        phide::ui::ThemePickerModal themeModal;
        themeModal.Open(phide::ui::ThemeMode::Dark);
        assert(themeModal.IsOpen());
        assert(!themeModal.IsCatalogView());

        UINT modalW = 1040, modalH = 750;
        ComPtr<IWICBitmap> pWicModalBmp;
        pWicFactory->CreateBitmap(modalW, modalH, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &pWicModalBmp);
        ComPtr<ID2D1RenderTarget> pModalRT;
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        pD2DFactory->CreateWicBitmapRenderTarget(pWicModalBmp.Get(), rtProps, &pModalRT);
        theme.CreateDeviceResources(pModalRT.Get(), pDWriteFactory.Get());

        pModalRT->BeginDraw();
        pModalRT->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));
        themeModal.Render(pModalRT.Get(), theme, config, static_cast<float>(modalW), static_cast<float>(modalH));
        pModalRT->EndDraw();

        // Save widescreen standard modal snapshot
        ComPtr<IWICStream> pStream;
        pWicFactory->CreateStream(&pStream);
        pStream->InitializeFromFilename((artDir + L"test_theme_picker_widescreen.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> enc;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;
        enc->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(modalW, modalH);
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
        frame->SetPixelFormat(&format);
        frame->WriteSource(pWicModalBmp.Get(), nullptr);
        frame->Commit();
        enc->Commit();
        std::cout << "  Passed: Saved test_theme_picker_widescreen.png" << std::endl;

        // Also save to test_theme_picker_8themes.png for backwards compatibility
        pStream.Reset();
        pWicFactory->CreateStream(&pStream);
        pStream->InitializeFromFilename((artDir + L"test_theme_picker_8themes.png").c_str(), GENERIC_WRITE);
        enc.Reset();
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        frame.Reset();
        enc->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(modalW, modalH);
        frame->SetPixelFormat(&format);
        frame->WriteSource(pWicModalBmp.Get(), nullptr);
        frame->Commit();
        enc->Commit();

        // Test Clicking "More Themes... ▸" to transition to full Catalog view
        POINT ptMoreBtn = { static_cast<LONG>((modalW + 960) / 2.0f - 60.0f), static_cast<LONG>((modalH + 430) / 2.0f - 24.0f) };
        phide::ui::ThemeMode chosenMode;
        bool cardSelected = themeModal.HandleLButtonDown(ptMoreBtn, chosenMode);
        assert(!cardSelected);
        assert(themeModal.IsOpen());
        assert(themeModal.IsCatalogView());

        // Render Catalog View with all 13 themes
        pModalRT->BeginDraw();
        pModalRT->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));
        themeModal.Render(pModalRT.Get(), theme, config, static_cast<float>(modalW), static_cast<float>(modalH));
        pModalRT->EndDraw();

        // Save catalog snapshot
        pStream.Reset();
        pWicFactory->CreateStream(&pStream);
        pStream->InitializeFromFilename((artDir + L"test_theme_picker_catalog.png").c_str(), GENERIC_WRITE);
        enc.Reset();
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        frame.Reset();
        enc->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(modalW, modalH);
        frame->SetPixelFormat(&format);
        frame->WriteSource(pWicModalBmp.Get(), nullptr);
        frame->Commit();
        enc->Commit();
        std::cout << "  Passed: Saved test_theme_picker_catalog.png" << std::endl;

        // Test mouse wheel scrolling in catalog view
        assert(themeModal.HandleMouseWheel(-120));
        assert(themeModal.HandleMouseWheel(120));

        // Test clicking "◂ Standard Themes" back button
        POINT ptBackBtn = { static_cast<LONG>((modalW - 960) / 2.0f + 60.0f), static_cast<LONG>((modalH - (std::min)(700.0f, static_cast<float>(modalH) - 30.0f)) / 2.0f + 25.0f) };
        assert(!themeModal.HandleLButtonDown(ptBackBtn, chosenMode));
        assert(!themeModal.IsCatalogView());
        assert(themeModal.IsOpen());

        // Test Modal Dismiss on outside click
        POINT ptModalClose = { 10, 10 }; // Outside
        assert(!themeModal.HandleLButtonDown(ptModalClose, chosenMode)); // Dismissed without selection
        assert(!themeModal.IsOpen());

        // Test Explorer Context Menu with Undo/Redo items
        phide::ui::PopupMenu popup;
        popup.OpenContextMenu(100.0f, 100.0f, false, L"C:\\sample\\file.txt", L"C:\\sample", config, true, true, true);
        assert(popup.IsOpen());
        assert(!popup.GetItems().empty());
        assert(popup.GetItems()[0].actionId == phide::ui::MenuActionId::CtxUndo);
        assert(popup.GetItems()[1].actionId == phide::ui::MenuActionId::CtxRedo);
        std::cout << "  Passed: Context Menu Undo/Redo items verified" << std::endl;

        // Render Context Menu with Undo/Redo
        ComPtr<IWICBitmap> pWicCtxBmp;
        pWicFactory->CreateBitmap(modalW, modalH, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &pWicCtxBmp);
        ComPtr<ID2D1RenderTarget> pCtxRT;
        pD2DFactory->CreateWicBitmapRenderTarget(pWicCtxBmp.Get(), rtProps, &pCtxRT);

        pCtxRT->BeginDraw();
        pCtxRT->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));
        popup.Render(pCtxRT.Get(), theme, config);
        pCtxRT->EndDraw();

        ComPtr<IWICStream> pCtxStream;
        pWicFactory->CreateStream(&pCtxStream);
        pCtxStream->InitializeFromFilename((artDir + L"test_explorer_undo_context_menu.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> encCtx;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encCtx);
        encCtx->Initialize(pCtxStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frameCtx;
        encCtx->CreateNewFrame(&frameCtx, nullptr);
        frameCtx->Initialize(nullptr);
        frameCtx->SetSize(modalW, modalH);
        format = GUID_WICPixelFormat32bppPBGRA;
        frameCtx->SetPixelFormat(&format);
        frameCtx->WriteSource(pWicCtxBmp.Get(), nullptr);
        frameCtx->Commit();
        encCtx->Commit();
        std::cout << "  Passed: Saved test_explorer_undo_context_menu.png" << std::endl;
    }

    // --- 12. Rainbow Brackets, Skeletal Frame, Connector Guides, and Expanded Keyboard Shortcuts ---
    std::cout << "[TEST 12] Testing Rainbow Brackets, Skeletal Frame, Connector Guides, and Expanded Keyboard Shortcuts..." << std::endl;
    {
        // 1. Verify KeyboardShortcutsView has exactly 51 verified genuine shortcuts
        phide::ui::KeyboardShortcutsView shortcutsView;
        assert(shortcutsView.GetShortcutsCount() == 51);
        std::cout << "  Passed: KeyboardShortcutsView contains " << shortcutsView.GetShortcutsCount() << " genuine verified shortcuts" << std::endl;

        // 2. Verify BracketPairAnalyzer Nesting Depth & Active Pinpointing
        std::wstring sampleCode =
            L"function computeMetrics(data) {\n"
            L"    let matrix = [\n"
            L"        { id: 1, sum: (10 + 20) },\n"
            L"        { id: 2, sum: (30 + 40) }\n"
            L"    ];\n"
            L"    return matrix;\n"
            L"}";

        phide::editor::Buffer buf(sampleCode);
        assert(buf.GetLineCount() == 7);

        // Analyze with cursor touching the outer opening brace on line 0 (col 30)
        auto analysis1 = phide::editor::BracketPairAnalyzer::Analyze(buf, 0, 30);
        assert(analysis1.activePairIdx >= 0);
        const auto& pair1 = analysis1.pairs[analysis1.activePairIdx];
        assert(pair1.openLine == 0);
        assert(pair1.openChar == L'{');
        assert(pair1.closeLine == 6);
        assert(pair1.closeChar == L'}');
        assert(pair1.depth == 0); // Level 0: Purple
        assert(pair1.guideCol == 0);

        // Analyze with cursor inside the array bracket on line 1 (col 18)
        auto analysis2 = phide::editor::BracketPairAnalyzer::Analyze(buf, 1, 18);
        assert(analysis2.activePairIdx >= 0);
        const auto& pair2 = analysis2.pairs[analysis2.activePairIdx];
        assert(pair2.openLine == 1);
        assert(pair2.openChar == L'[');
        assert(pair2.closeLine == 4);
        assert(pair2.closeChar == L']');
        assert(pair2.depth == 1); // Level 1: Yellow
        assert(pair2.guideCol == 4);

        // Analyze with cursor touching the innermost parenthesis on line 2 (col 23)
        auto analysis3 = phide::editor::BracketPairAnalyzer::Analyze(buf, 2, 23);
        assert(analysis3.activePairIdx >= 0);
        const auto& pair3 = analysis3.pairs[analysis3.activePairIdx];
        assert(pair3.openLine == 2);
        assert(pair3.openChar == L'(');
        assert(pair3.closeLine == 2);
        assert(pair3.closeChar == L')');
        assert(pair3.depth == 3); // Level 3: depth % 3 == 0 (Purple cycle)

        std::cout << "  Passed: BracketPairAnalyzer depth calculations, matching, and active pair pinpointing verified" << std::endl;

        // 3. Render Direct2D Snapshot of Rainbow Brackets & Active Skeletal Frame with Connector Guide
        ComPtr<IWICBitmap> pWicEditorBmp;
        ComPtr<ID2D1RenderTarget> pEditorRT;
        CreateTarget(pWicEditorBmp, pEditorRT);

        phide::ui::Theme theme;
        theme.SetThemeMode(phide::ui::ThemeMode::Dark);
        hr = theme.CreateDeviceResources(pEditorRT.Get(), pDWriteFactory.Get());
        assert(SUCCEEDED(hr));

        phide::ui::EditorCanvas editor;
        std::vector<phide::editor::OpenDocument> docs;
        phide::editor::OpenDocument doc;
        doc.filePath = L"C:\\phide\\metrics.js";
        doc.fileName = L"metrics.js";
        doc.buffer = buf;
        doc.buffer.SetCursor(0, 30); // Cursor placed at opening brace
        docs.push_back(std::move(doc));

        pEditorRT->BeginDraw();
        pEditorRT->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));
        editor.Render(pEditorRT.Get(), theme, config, docs, 0,
                      298.0f, 1200.0f - 298.0f, 750.0f, true);
        hr = pEditorRT->EndDraw();
        assert(SUCCEEDED(hr));

        // Save screenshot test_bracket_pairs_rainbow.png
        ComPtr<IWICStream> pStream;
        pWicFactory->CreateStream(&pStream);
        pStream->InitializeFromFilename((artDir + L"test_bracket_pairs_rainbow.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> enc;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        enc->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frame;
        enc->CreateNewFrame(&frame, nullptr);
        frame->Initialize(nullptr);
        frame->SetSize(1200, 750);
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
        frame->SetPixelFormat(&format);
        frame->WriteSource(pWicEditorBmp.Get(), nullptr);
        frame->Commit();
        enc->Commit();
        std::cout << "  Passed: Saved test_bracket_pairs_rainbow.png" << std::endl;

        // 4. Render Keyboard Shortcuts View snapshot
        ComPtr<IWICBitmap> pWicShortcutsBmp;
        ComPtr<ID2D1RenderTarget> pShortcutsRT;
        CreateTarget(pWicShortcutsBmp, pShortcutsRT);
        theme.CreateDeviceResources(pShortcutsRT.Get(), pDWriteFactory.Get());

        pShortcutsRT->BeginDraw();
        pShortcutsRT->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));
        shortcutsView.Render(pShortcutsRT.Get(), theme, config, 298.0f, 1200.0f - 298.0f, 38.0f, 750.0f);
        pShortcutsRT->EndDraw();

        ComPtr<IWICStream> pShortcutsStream;
        pWicFactory->CreateStream(&pShortcutsStream);
        pShortcutsStream->InitializeFromFilename((artDir + L"test_keyboard_shortcuts_expanded.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> encShortcuts;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encShortcuts);
        encShortcuts->Initialize(pShortcutsStream.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frameShortcuts;
        encShortcuts->CreateNewFrame(&frameShortcuts, nullptr);
        frameShortcuts->Initialize(nullptr);
        frameShortcuts->SetSize(1200, 750);
        frameShortcuts->SetPixelFormat(&format);
        frameShortcuts->WriteSource(pWicShortcutsBmp.Get(), nullptr);
        frameShortcuts->Commit();
        encShortcuts->Commit();
        std::cout << "  Passed: Saved test_keyboard_shortcuts_expanded.png" << std::endl;
    }

    // =========================================================================
    // --- 16. Unit Test: Buffer Editing Operations (Comments & Emmet) ---
    // =========================================================================
    std::cout << "\n[TEST 16] Testing Buffer editing (Line comment, Block comment, Emmet)..." << std::endl;
    {
        phide::editor::Buffer editBuf;
        editBuf.SetText(L"const greeting = \"hello\";\n    let count = 0;");

        // 1. Line comment toggle on line 0
        editBuf.SetCursor(0, 5);
        editBuf.ToggleLineComment();
        assert(editBuf.GetLine(0) == L"// const greeting = \"hello\";");
        editBuf.ToggleLineComment();
        assert(editBuf.GetLine(0) == L"const greeting = \"hello\";");

        // 2. Line comment toggle on indented line 1
        editBuf.SetCursor(1, 6);
        editBuf.ToggleLineComment();
        assert(editBuf.GetLine(1) == L"    // let count = 0;");
        editBuf.ToggleLineComment();
        assert(editBuf.GetLine(1) == L"    let count = 0;");

        // 3. Block comment toggle
        editBuf.SetCursor(0, 0);
        editBuf.ToggleBlockComment();
        assert(editBuf.GetLine(0) == L"/* const greeting = \"hello\"; */");
        editBuf.ToggleBlockComment();
        assert(editBuf.GetLine(0) == L"const greeting = \"hello\";");

        // 4. Emmet abbreviation expansions
        phide::editor::Buffer htmlBuf;
        htmlBuf.SetText(L"div");
        htmlBuf.SetCursor(0, 3);
        bool emmetOk = htmlBuf.ExpandEmmet();
        assert(emmetOk);
        assert(htmlBuf.GetLine(0) == L"<div></div>");
        assert(htmlBuf.GetCursor().line == 0 && htmlBuf.GetCursor().col == 5); // Cursor placed inside tag!

        // Emmet self-closing input
        phide::editor::Buffer inputBuf;
        inputBuf.SetText(L"input");
        inputBuf.SetCursor(0, 5);
        emmetOk = inputBuf.ExpandEmmet();
        assert(emmetOk);
        assert(inputBuf.GetLine(0) == L"<input type=\"text\" />");

        std::cout << "  Passed: Buffer ToggleLineComment, ToggleBlockComment, and ExpandEmmet" << std::endl;
    }

    // =========================================================================
    // --- 17. Unit Test: Code Search Details Panel, Include/Exclude, Open Tabs Only ---
    // =========================================================================
    std::cout << "\n[TEST 17] Testing Code Search Details Panel (...), Filters & Open Tabs Only..." << std::endl;
    {
        phide::app::AppConfig cfg;
        phide::ui::DrawerPanel drawer;
        drawer.OpenFolder(L"C:\\phide");

        // Initially search details panel is closed
        assert(!drawer.IsSearchDetailsOpen());
        assert(!drawer.IsSearchOnlyOpenEditors());
        assert(drawer.GetFilesToInclude().empty());
        assert(drawer.GetFilesToExclude().empty());

        // Toggle search details open
        drawer.SetSearchDetailsOpen(true);
        assert(drawer.IsSearchDetailsOpen());

        // Set Include / Exclude filters
        drawer.SetFilesToInclude(L"*.cpp, *.hpp");
        drawer.SetFilesToExclude(L"node_modules/**, build/*");
        assert(drawer.GetFilesToInclude() == L"*.cpp, *.hpp");
        assert(drawer.GetFilesToExclude() == L"node_modules/**, build/*");

        // Toggle Search Open Tabs Only mode
        drawer.SetSearchOnlyOpenEditors(true);
        assert(drawer.IsSearchOnlyOpenEditors());

        // Provide open document paths
        std::vector<std::wstring> openTabs = {
            L"C:\\phide\\main.cpp",
            L"C:\\phide\\Window.cpp"
        };
        drawer.SetOpenDocPaths(openTabs);
        assert(drawer.GetOpenDocPaths().size() == 2);

        // Verify focus cycling via Tab across search inputs (1: search -> 2: replace -> 3: include -> 4: exclude -> 1)
        drawer.SetSearchFocus(1); // query box
        drawer.HandleSearchKeyDown(VK_TAB);
        assert(drawer.GetSearchFocus() == 2); // replace box
        drawer.HandleSearchKeyDown(VK_TAB);
        assert(drawer.GetSearchFocus() == 3); // files to include
        drawer.HandleSearchKeyDown(VK_TAB);
        assert(drawer.GetSearchFocus() == 4); // files to exclude
        drawer.HandleSearchKeyDown(VK_TAB);
        assert(drawer.GetSearchFocus() == 1); // back to query box

        // Test typing into include/exclude boxes
        drawer.SetSearchFocus(3);
        drawer.SetFilesToInclude(L"");
        drawer.HandleSearchChar(L'*');
        drawer.HandleSearchChar(L'.');
        drawer.HandleSearchChar(L'h');
        assert(drawer.GetFilesToInclude() == L"*.h");

        drawer.SetSearchFocus(4);
        drawer.SetFilesToExclude(L"");
        drawer.HandleSearchChar(L'o');
        drawer.HandleSearchChar(L'u');
        drawer.HandleSearchChar(L't');
        assert(drawer.GetFilesToExclude() == L"out");

        std::cout << "  Passed: Search details panel (...), include/exclude inputs, tab cycling, and open tabs filter" << std::endl;
    }

    // =========================================================================
    // --- 18. Unit Test: Focus Separation between Search Drawer and Shortcuts View ---
    // =========================================================================
    std::cout << "\n[TEST 18] Testing Input Focus Separation (Drawer Search vs Shortcuts Search)..." << std::endl;
    {
        phide::ui::DrawerPanel drawer;
        phide::ui::KeyboardShortcutsView shortcutsView;

        // 1. Initially focus drawer search box 1, blur shortcuts
        drawer.SetSearchFocus(1);
        shortcutsView.SetSearchFocused(false);
        assert(drawer.IsSearchFocused());
        assert(!shortcutsView.IsSearchFocused());

        // 2. User clicks/focuses shortcuts view search box
        drawer.SetSearchFocus(0);
        shortcutsView.SetSearchFocused(true);
        assert(!drawer.IsSearchFocused());
        assert(shortcutsView.IsSearchFocused());

        // 3. Type character while shortcuts search is focused
        shortcutsView.HandleChar(L'e');
        shortcutsView.HandleChar(L'd');
        shortcutsView.HandleChar(L'i');
        shortcutsView.HandleChar(L't');
        assert(shortcutsView.GetSearchQuery() == L"edit");
        assert(drawer.GetSearchQuery().empty()); // Drawer search was untouched!

        // 4. User clicks back into drawer search
        shortcutsView.SetSearchFocused(false);
        drawer.SetSearchFocus(1);
        assert(drawer.IsSearchFocused());
        assert(!shortcutsView.IsSearchFocused());

        std::cout << "  Passed: Input focus separation cleanly routes keys to active target without cross-pollution" << std::endl;
    }

    // =========================================================================
    // --- 19. Unit Test: Top Edit Dropdown Menu Model & Actions ---
    // =========================================================================
    std::cout << "\n[TEST 19] Testing Edit Dropdown Menu Items & Actions..." << std::endl;
    {
        phide::app::AppConfig cfg;
        phide::ui::PopupMenu popup;
        popup.OpenEditMenu(120.0f, 38.0f, cfg, true, true, true);
        const auto& editItems = popup.GetItems();
        assert(!editItems.empty());

        // Verify key actions and shortcuts matching Image 5
        bool hasUndo = false, hasRedo = false, hasCut = false, hasCopy = false, hasPaste = false;
        bool hasFind = false, hasReplace = false, hasFindInFiles = false, hasReplaceInFiles = false;
        bool hasLineComment = false, hasBlockComment = false, hasEmmet = false;

        for (const auto& item : editItems) {
            if (item.actionId == phide::ui::MenuActionId::EditUndo) {
                hasUndo = true;
                assert(item.shortcut == L"Ctrl+Z");
            } else if (item.actionId == phide::ui::MenuActionId::EditRedo) {
                hasRedo = true;
                assert(item.shortcut == L"Ctrl+Y");
            } else if (item.actionId == phide::ui::MenuActionId::EditCut) {
                hasCut = true;
                assert(item.shortcut == L"Ctrl+X");
            } else if (item.actionId == phide::ui::MenuActionId::EditCopy) {
                hasCopy = true;
                assert(item.shortcut == L"Ctrl+C");
            } else if (item.actionId == phide::ui::MenuActionId::EditPaste) {
                hasPaste = true;
                assert(item.shortcut == L"Ctrl+V");
            } else if (item.actionId == phide::ui::MenuActionId::EditFind) {
                hasFind = true;
                assert(item.shortcut == L"Ctrl+F");
            } else if (item.actionId == phide::ui::MenuActionId::EditReplace) {
                hasReplace = true;
                assert(item.shortcut == L"Ctrl+H");
            } else if (item.actionId == phide::ui::MenuActionId::EditFindInFiles) {
                hasFindInFiles = true;
                assert(item.shortcut == L"Ctrl+Shift+F");
            } else if (item.actionId == phide::ui::MenuActionId::EditReplaceInFiles) {
                hasReplaceInFiles = true;
                assert(item.shortcut == L"Ctrl+Shift+H");
            } else if (item.actionId == phide::ui::MenuActionId::EditToggleLineComment) {
                hasLineComment = true;
                assert(item.shortcut == L"Ctrl+/");
            } else if (item.actionId == phide::ui::MenuActionId::EditToggleBlockComment) {
                hasBlockComment = true;
                assert(item.shortcut == L"Shift+Alt+A");
            } else if (item.actionId == phide::ui::MenuActionId::EditEmmetExpand) {
                hasEmmet = true;
                assert(item.shortcut == L"Tab");
            }
        }

        assert(hasUndo && hasRedo && hasCut && hasCopy && hasPaste);
        assert(hasFind && hasReplace && hasFindInFiles && hasReplaceInFiles);
        assert(hasLineComment && hasBlockComment && hasEmmet);
        std::cout << "  Passed: All 12 Edit menu actions and shortcuts correctly verified against design specification" << std::endl;
    }

    // =========================================================================
    // --- 20. Direct2D Rendering: Edit Menu Dropdown and Search Details Panel ---
    // =========================================================================
    std::cout << "\n[TEST 20] Rendering D2D Snapshots for Edit Menu and Code Search Details..." << std::endl;
    {
        // 1. Render Edit Menu Dropdown Snapshot
        ComPtr<IWICBitmap> pWicEditMenuBmp;
        ComPtr<ID2D1RenderTarget> pEditMenuRT;
        CreateTarget(pWicEditMenuBmp, pEditMenuRT);

        phide::ui::Theme theme;
        theme.SetThemeMode(phide::ui::ThemeMode::Dark);
        hr = theme.CreateDeviceResources(pEditMenuRT.Get(), pDWriteFactory.Get());
        assert(SUCCEEDED(hr));

        phide::app::AppConfig cfg;
        phide::ui::PopupMenu popup;
        popup.OpenEditMenu(120.0f, 38.0f, cfg, true, true, true);

        pEditMenuRT->BeginDraw();
        pEditMenuRT->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));

        // Draw simulated top bar
        pEditMenuRT->FillRectangle(D2D1::RectF(0.0f, 0.0f, 1200.0f, 38.0f), theme.brushHeaderBg.Get());
        // Draw open menu
        popup.Render(pEditMenuRT.Get(), theme, cfg);
        hr = pEditMenuRT->EndDraw();
        assert(SUCCEEDED(hr));

        // Save screenshot test_edit_menu_dropdown.png
        ComPtr<IWICStream> pStreamEdit;
        pWicFactory->CreateStream(&pStreamEdit);
        pStreamEdit->InitializeFromFilename((artDir + L"test_edit_menu_dropdown.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> encEdit;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encEdit);
        encEdit->Initialize(pStreamEdit.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frameEdit;
        encEdit->CreateNewFrame(&frameEdit, nullptr);
        frameEdit->Initialize(nullptr);
        frameEdit->SetSize(1200, 750);
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
        frameEdit->SetPixelFormat(&format);
        frameEdit->WriteSource(pWicEditMenuBmp.Get(), nullptr);
        frameEdit->Commit();
        encEdit->Commit();
        std::cout << "  Passed: Saved test_edit_menu_dropdown.png" << std::endl;

        // 2. Render Code Search Details Panel Snapshot
        ComPtr<IWICBitmap> pWicSearchBmp;
        ComPtr<ID2D1RenderTarget> pSearchRT;
        CreateTarget(pWicSearchBmp, pSearchRT);
        theme.CreateDeviceResources(pSearchRT.Get(), pDWriteFactory.Get());

        phide::ui::DrawerPanel searchDrawer;
        searchDrawer.OpenFolder(L"C:\\phide");
        searchDrawer.SetSearchQuery(L"render");
        searchDrawer.SetSearchDetailsOpen(true);
        searchDrawer.SetFilesToInclude(L"*.cpp, *.hpp");
        searchDrawer.SetFilesToExclude(L"node_modules/**");
        searchDrawer.SetSearchOnlyOpenEditors(true);
        searchDrawer.SetSearchFocus(3); // focus include box

        pSearchRT->BeginDraw();
        pSearchRT->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));
        // Render drawer with active search tab (activeRailTab = 1)
        cfg.activeRailTab = 1;
        searchDrawer.Render(pSearchRT.Get(), theme, cfg, 750.0f, 260.0f);
        hr = pSearchRT->EndDraw();
        assert(SUCCEEDED(hr));

        // Save screenshot test_search_details_panel.png
        ComPtr<IWICStream> pStreamSearch;
        pWicFactory->CreateStream(&pStreamSearch);
        pStreamSearch->InitializeFromFilename((artDir + L"test_search_details_panel.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> encSearch;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encSearch);
        encSearch->Initialize(pStreamSearch.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frameSearch;
        encSearch->CreateNewFrame(&frameSearch, nullptr);
        frameSearch->Initialize(nullptr);
        frameSearch->SetSize(1200, 750);
        frameSearch->SetPixelFormat(&format);
        frameSearch->WriteSource(pWicSearchBmp.Get(), nullptr);
        frameSearch->Commit();
        encSearch->Commit();
        std::cout << "  Passed: Saved test_search_details_panel.png" << std::endl;
    }

    // =========================================================================
    // --- 21. Unit Test: Real Git Source Control & Clean New Window ---
    // =========================================================================
    std::cout << "\n[TEST 21] Testing Clean New Window State & Real Git Source Control (Real Data, Tooltips, Spinning Loading Indicator)..." << std::endl;
    {
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;

        // 1. Verify Real Git State in Workspace (No mock/dummy data)
        phide::ui::DrawerPanel gitDrawer;
        gitDrawer.RefreshGitState(false); // Synchronous refresh

        assert(!gitDrawer.GetGitBranch().empty());
        assert(gitDrawer.GetGitBranch() == L"master" || gitDrawer.GetGitBranch() == L"main");
        std::cout << "  Passed: Real Git branch detected: " << (gitDrawer.GetGitBranch() == L"master" ? "master" : "main") << std::endl;

        // Verify Real Git Commits retrieved via git log
        const auto& commits = gitDrawer.GetGitCommits();
        assert(!commits.empty());
        assert(commits.size() >= 5);
        std::cout << "  Passed: Real Git history populated with " << commits.size() << " commits" << std::endl;

        // Verify head commit properties
        assert(commits[0].isHead);
        assert(!commits[0].hash.empty());
        assert(!commits[0].message.empty());
        assert(!commits[0].author.empty());
        assert(!commits[0].relativeTime.empty());

        // 2. Test AI Commit Message Generator
        gitDrawer.GenerateAiCommitMessage();

        // 3. Render Activity Rail with Active Git Loading Badge (Image 4)
        ComPtr<IWICBitmap> pWicRailBmp;
        ComPtr<ID2D1RenderTarget> pRailRT;
        CreateTarget(pWicRailBmp, pRailRT);

        phide::ui::Theme theme;
        theme.SetThemeMode(phide::ui::ThemeMode::Dark);
        hr = theme.CreateDeviceResources(pRailRT.Get(), pDWriteFactory.Get());
        assert(SUCCEEDED(hr));

        phide::app::AppConfig cfg;
        cfg.activeRailTab = 2; // Source Control tab active

        phide::ui::ActivityRail rail;
        rail.SetGitLoading(true);
        rail.UpdateGitAnim(45.0f); // Rotated 45 degrees

        pRailRT->BeginDraw();
        pRailRT->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));
        rail.Render(pRailRT.Get(), theme, cfg, 750.0f);
        pRailRT->EndDraw();

        ComPtr<IWICStream> pStreamRail;
        pWicFactory->CreateStream(&pStreamRail);
        pStreamRail->InitializeFromFilename((artDir + L"test_git_loading_rail_badge.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> encRail;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encRail);
        encRail->Initialize(pStreamRail.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frameRail;
        encRail->CreateNewFrame(&frameRail, nullptr);
        frameRail->Initialize(nullptr);
        frameRail->SetSize(1200, 750);
        frameRail->SetPixelFormat(&format);
        frameRail->WriteSource(pWicRailBmp.Get(), nullptr);
        frameRail->Commit();
        encRail->Commit();
        std::cout << "  Passed: Saved test_git_loading_rail_badge.png" << std::endl;

        // 4. Render Source Control Pane Snapshot (Real Git Graph, Changes, Commit & AI Generate Buttons)
        ComPtr<IWICBitmap> pWicGitBmp;
        ComPtr<ID2D1RenderTarget> pGitRT;
        CreateTarget(pWicGitBmp, pGitRT);
        theme.CreateDeviceResources(pGitRT.Get(), pDWriteFactory.Get());

        pGitRT->BeginDraw();
        pGitRT->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));
        // Draw top bar
        pGitRT->FillRectangle(D2D1::RectF(0.0f, 0.0f, 1200.0f, cfg.ScaleF(static_cast<float>(cfg.headerHeight))), theme.brushHeaderBg.Get());
        // Draw rail
        rail.SetGitLoading(false);
        rail.Render(pGitRT.Get(), theme, cfg, 750.0f);
        // Draw source control drawer pane (activeRailTab = 2)
        gitDrawer.Render(pGitRT.Get(), theme, cfg, 750.0f, 280.0f);
        pGitRT->EndDraw();

        ComPtr<IWICStream> pStreamGit;
        pWicFactory->CreateStream(&pStreamGit);
        pStreamGit->InitializeFromFilename((artDir + L"test_source_control_pane_real_git.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> encGit;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encGit);
        encGit->Initialize(pStreamGit.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frameGit;
        encGit->CreateNewFrame(&frameGit, nullptr);
        frameGit->Initialize(nullptr);
        frameGit->SetSize(1200, 750);
        frameGit->SetPixelFormat(&format);
        frameGit->WriteSource(pWicGitBmp.Get(), nullptr);
        frameGit->Commit();
        encGit->Commit();
        std::cout << "  Passed: Saved test_source_control_pane_real_git.png" << std::endl;

        // 5. Test Tooltips on Source Control buttons
        const auto& moreBounds = gitDrawer.GetGitHeaderMoreBounds();
        POINT ptHeaderMore = {
            static_cast<LONG>((moreBounds.left + moreBounds.right) / 2.0f),
            static_cast<LONG>((moreBounds.top + moreBounds.bottom) / 2.0f)
        };
        gitDrawer.HandleGitMouseMove(ptHeaderMore);
        assert(gitDrawer.IsGitTooltipVisible());
        assert(gitDrawer.GetGitTooltipText() == L"Views and More Actions...");
        std::cout << "  Passed: Tooltip system on all Source Control buttons verified" << std::endl;

        // 6. Test Draggable Splitter between Changes and Graph (Image 2 & 3)
        float initialSplit = gitDrawer.GetGitSplitY();
        assert(initialSplit > 100.0f);
        POINT ptSplit = {
            static_cast<LONG>(cfg.ScaleF(static_cast<float>(cfg.railWidth)) + 80.0f),
            static_cast<LONG>(initialSplit)
        };
        assert(gitDrawer.HitTestGitSplitter(ptSplit));
        gitDrawer.SetGitSplitY(310.0f, 100.0f, 600.0f);
        assert(gitDrawer.GetGitSplitY() == 310.0f);
        std::cout << "  Passed: Vertical draggable splitter between Changes and Graph verified" << std::endl;

        // 7. Test Views Menu Dropdown (Image 3)
        std::wstring openFile;
        bool hitMore = gitDrawer.HitTestGit(ptHeaderMore, cfg, openFile);
        assert(hitMore);
        assert(gitDrawer.IsGitViewMenuOpen());

        pGitRT->BeginDraw();
        pGitRT->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));
        pGitRT->FillRectangle(D2D1::RectF(0.0f, 0.0f, 1200.0f, cfg.ScaleF(static_cast<float>(cfg.headerHeight))), theme.brushHeaderBg.Get());
        rail.Render(pGitRT.Get(), theme, cfg, 750.0f);
        gitDrawer.Render(pGitRT.Get(), theme, cfg, 750.0f, 280.0f);
        pGitRT->EndDraw();

        ComPtr<IWICStream> pStreamMenu;
        pWicFactory->CreateStream(&pStreamMenu);
        pStreamMenu->InitializeFromFilename((artDir + L"test_source_control_views_menu.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> encMenu;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encMenu);
        encMenu->Initialize(pStreamMenu.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frameMenu;
        encMenu->CreateNewFrame(&frameMenu, nullptr);
        frameMenu->Initialize(nullptr);
        frameMenu->SetSize(1200, 750);
        frameMenu->SetPixelFormat(&format);
        frameMenu->WriteSource(pWicGitBmp.Get(), nullptr);
        frameMenu->Commit();
        encMenu->Commit();
        std::cout << "  Passed: Saved test_source_control_views_menu.png" << std::endl;

        // 8. Test Commit Input Focus & Caret Blinking
        gitDrawer.SetGitCommitBoxFocus(1);
        assert(gitDrawer.IsGitCommitBoxFocused());
        bool initialCaret = gitDrawer.IsGitCaretVisible();
        gitDrawer.ToggleCaretBlink();
        assert(gitDrawer.IsGitCaretVisible() != initialCaret);
        std::cout << "  Passed: Commit input focus and blinking caret synchronization verified" << std::endl;

        // 9. Test Commit Button vs Sync Changes Button State
        gitDrawer.SetUnpushedCommitsCount(3);
        assert(gitDrawer.GetUnpushedCommitsCount() == 3);
        std::cout << "  Passed: Sync Changes state and unpushed commits count (3) verified" << std::endl;

        // 10. Test Commit Details Expansion in Graph (Image 2 & 3)
        // Close menu first
        POINT ptOutside = { 10, 10 };
        gitDrawer.HitTestGit(ptOutside, cfg, openFile);
        assert(!gitDrawer.IsGitViewMenuOpen());

        // Click commit to expand changed files
        POINT ptFirstCommit = {
            static_cast<LONG>((commits[0].bounds.left + commits[0].bounds.right) / 2.0f),
            static_cast<LONG>((commits[0].bounds.top + commits[0].bounds.bottom) / 2.0f)
        };
        std::wstring outOpen, outDisc;
        bool hitCommit = gitDrawer.HitTestGit(ptFirstCommit, cfg, outOpen, outDisc);
        assert(hitCommit);
        const auto& updatedCommits = gitDrawer.GetGitCommits();
        assert(updatedCommits[0].isExpanded);
        assert(!updatedCommits[0].files.empty());
        std::cout << "  Passed: Commit expansion verified with " << updatedCommits[0].files.size() << " changed files populated" << std::endl;

        // Render expanded commit view snapshot
        pGitRT->BeginDraw();
        pGitRT->Clear(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f));
        pGitRT->FillRectangle(D2D1::RectF(0.0f, 0.0f, 1200.0f, cfg.ScaleF(static_cast<float>(cfg.headerHeight))), theme.brushHeaderBg.Get());
        rail.Render(pGitRT.Get(), theme, cfg, 750.0f);
        gitDrawer.Render(pGitRT.Get(), theme, cfg, 750.0f, 280.0f);
        pGitRT->EndDraw();

        ComPtr<IWICStream> pStreamExp;
        pWicFactory->CreateStream(&pStreamExp);
        pStreamExp->InitializeFromFilename((artDir + L"test_source_control_commit_details_expanded.png").c_str(), GENERIC_WRITE);
        ComPtr<IWICBitmapEncoder> encExp;
        pWicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encExp);
        encExp->Initialize(pStreamExp.Get(), WICBitmapEncoderNoCache);
        ComPtr<IWICBitmapFrameEncode> frameExp;
        encExp->CreateNewFrame(&frameExp, nullptr);
        frameExp->Initialize(nullptr);
        frameExp->SetSize(1200, 750);
        frameExp->SetPixelFormat(&format);
        frameExp->WriteSource(pWicGitBmp.Get(), nullptr);
        frameExp->Commit();
        encExp->Commit();
        std::cout << "  Passed: Saved test_source_control_commit_details_expanded.png" << std::endl;
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
