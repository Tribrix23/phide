/**
 * @file Theme.cpp
 * @brief Implementation of Dark Theme GPU brush allocation, typography, and tooltip formats.
 */

#include "phide/ui/Theme.hpp"
#include <filesystem>
#include <windows.h>

namespace phide::ui {

static std::wstring FindFontPath(const std::wstring& filename) {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

    std::filesystem::path cand1 = exeDir / L"assets" / L"fonts" / filename;
    if (std::filesystem::exists(cand1)) return cand1.wstring();

    std::filesystem::path cand2 = exeDir.parent_path() / L"assets" / L"fonts" / filename;
    if (std::filesystem::exists(cand2)) return cand2.wstring();

    std::filesystem::path cand3 = std::filesystem::current_path() / L"assets" / L"fonts" / filename;
    if (std::filesystem::exists(cand3)) return cand3.wstring();

    return L"";
}

Theme::Theme() = default;

HRESULT Theme::CreateDeviceResources(ID2D1RenderTarget* pRT, IDWriteFactory* pDWriteFactory) {
    if (!pRT || !pDWriteFactory) return E_POINTER;
    dwriteFactory = pDWriteFactory;

    // Allocate Solid Brushes
    HRESULT hr = pRT->CreateSolidColorBrush(colors.windowBg, &brushWindowBg);
    if (FAILED(hr)) return hr;

    pRT->CreateSolidColorBrush(colors.canvasBg, &brushCanvasBg);
    pRT->CreateSolidColorBrush(colors.headerBg, &brushHeaderBg);
    pRT->CreateSolidColorBrush(colors.railBg, &brushRailBg);
    pRT->CreateSolidColorBrush(colors.drawerBg, &brushDrawerBg);
    pRT->CreateSolidColorBrush(colors.borderSubtle, &brushBorderSubtle);
    pRT->CreateSolidColorBrush(colors.borderHover, &brushBorderHover);
    pRT->CreateSolidColorBrush(colors.textPrimary, &brushTextPrimary);
    pRT->CreateSolidColorBrush(colors.textSecondary, &brushTextSecondary);
    pRT->CreateSolidColorBrush(colors.textMuted, &brushTextMuted);
    pRT->CreateSolidColorBrush(colors.textWhite, &brushTextWhite);
    pRT->CreateSolidColorBrush(colors.itemHover, &brushItemHover);
    pRT->CreateSolidColorBrush(colors.itemActive, &brushItemActive);
    pRT->CreateSolidColorBrush(colors.captionHover, &brushCaptionHover);
    pRT->CreateSolidColorBrush(colors.closeHoverBg, &brushCloseHoverBg);
    pRT->CreateSolidColorBrush(colors.brandAccent, &brushBrandAccent);
    pRT->CreateSolidColorBrush(colors.homeBadgeBg, &brushHomeBadgeBg);
    pRT->CreateSolidColorBrush(colors.runGreen, &brushRunGreen);
    pRT->CreateSolidColorBrush(colors.runGreenHover, &brushRunGreenHover);
    pRT->CreateSolidColorBrush(colors.tooltipBg, &brushTooltipBg);
    pRT->CreateSolidColorBrush(colors.tooltipBorder, &brushTooltipBorder);
    pRT->CreateSolidColorBrush(colors.menuBg, &brushMenuBg);
    pRT->CreateSolidColorBrush(colors.menuBorder, &brushMenuBorder);
    pRT->CreateSolidColorBrush(colors.menuSeparator, &brushMenuSeparator);
    pRT->CreateSolidColorBrush(colors.menuHover, &brushMenuHover);
    pRT->CreateSolidColorBrush(colors.menuText, &brushMenuText);
    pRT->CreateSolidColorBrush(colors.menuShortcut, &brushMenuShortcut);

    // Allocate Syntax & Editor Surface Brushes
    pRT->CreateSolidColorBrush(colors.syntaxKeyword, &brushSyntaxKeyword);
    pRT->CreateSolidColorBrush(colors.syntaxType, &brushSyntaxType);
    pRT->CreateSolidColorBrush(colors.syntaxString, &brushSyntaxString);
    pRT->CreateSolidColorBrush(colors.syntaxComment, &brushSyntaxComment);
    pRT->CreateSolidColorBrush(colors.syntaxNumber, &brushSyntaxNumber);
    pRT->CreateSolidColorBrush(colors.syntaxOperator, &brushSyntaxOperator);
    pRT->CreateSolidColorBrush(colors.lineNumber, &brushLineNumber);
    pRT->CreateSolidColorBrush(colors.currentLineBg, &brushCurrentLineBg);
    pRT->CreateSolidColorBrush(colors.tabBarBg, &brushTabBarBg);
    pRT->CreateSolidColorBrush(colors.tabActiveBg, &brushTabActiveBg);
    pRT->CreateSolidColorBrush(colors.tabBorder, &brushTabBorder);

    // Tree & File Type Icon Brushes (Seti/Material palette matching Image 5)
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.596f, 1.0f, 1.0f), &brushIconBlue);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.533f, 0.572f, 0.749f, 1.0f), &brushIconPurple);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.968f, 0.874f, 0.117f, 1.0f), &brushIconYellow);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.149f, 0.651f, 0.603f, 1.0f), &brushIconGreen);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.898f, 0.223f, 0.207f, 1.0f), &brushIconRed);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.894f, 0.301f, 0.149f, 1.0f), &brushIconOrange);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.474f, 0.592f, 0.709f, 1.0f), &brushIconFolder);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.600f, 0.721f, 0.835f, 1.0f), &brushIconFolderOpen);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.620f, 0.620f, 0.620f, 1.0f), &brushIconGray);

    // Official VS Code Seti Brand Palette Brushes
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.533f, 0.572f, 0.749f, 1.0f), &brushSetiPhp);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.968f, 0.874f, 0.117f, 1.0f), &brushSetiJs);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.318f, 0.604f, 0.729f, 1.0f), &brushSetiTs);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.318f, 0.604f, 0.729f, 1.0f), &brushSetiCpp);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.796f, 0.796f, 0.255f, 1.0f), &brushSetiJson);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.890f, 0.298f, 0.149f, 1.0f), &brushSetiHtml);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.259f, 0.647f, 0.961f, 1.0f), &brushSetiCss);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.208f, 0.447f, 0.647f, 1.0f), &brushSetiPython);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.871f, 0.647f, 0.518f, 1.0f), &brushSetiRust);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.800f, 0.243f, 0.267f, 1.0f), &brushSetiJava);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.945f, 0.306f, 0.196f, 1.0f), &brushSetiGit);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.302f, 0.576f, 0.459f, 1.0f), &brushSetiShell);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.796f, 0.090f, 0.118f, 1.0f), &brushSetiYaml);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.541f, 0.322f, 0.745f, 1.0f), &brushSetiImage);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.474f, 0.596f, 0.709f, 1.0f), &brushSetiFolder);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.550f, 0.680f, 0.810f, 1.0f), &brushSetiFolderOpen);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.620f, 0.620f, 0.620f, 1.0f), &brushSetiDefault);

    // Load Official VS Code Seti Font into DirectWrite Custom Font Collection
    std::wstring setiPath = FindFontPath(L"seti.ttf");
    if (!setiPath.empty()) {
        ComPtr<IDWriteFactory3> dwriteFactory3;
        if (SUCCEEDED(pDWriteFactory->QueryInterface(__uuidof(IDWriteFactory3), (void**)&dwriteFactory3))) {
            ComPtr<IDWriteFontFile> pFontFile;
            if (SUCCEEDED(pDWriteFactory->CreateFontFileReference(setiPath.c_str(), nullptr, &pFontFile))) {
                ComPtr<IDWriteFontFaceReference> pFaceRef;
                if (SUCCEEDED(dwriteFactory3->CreateFontFaceReference(pFontFile.Get(), 0, DWRITE_FONT_SIMULATIONS_NONE, &pFaceRef))) {
                    ComPtr<IDWriteFontSetBuilder> pBuilder;
                    if (SUCCEEDED(dwriteFactory3->CreateFontSetBuilder(&pBuilder))) {
                        pBuilder->AddFontFaceReference(pFaceRef.Get());
                        ComPtr<IDWriteFontSet> pFontSet;
                        if (SUCCEEDED(pBuilder->CreateFontSet(&pFontSet))) {
                            dwriteFactory3->CreateFontCollectionFromFontSet(pFontSet.Get(), &customFontCollection);
                        }
                    }
                }
            }
        }
    }

    if (customFontCollection) {
        pDWriteFactory->CreateTextFormat(
            L"seti",
            customFontCollection.Get(),
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            14.5f,
            L"en-US",
            &fontSeti
        );
        if (fontSeti) {
            fontSeti->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fontSeti->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            fontSeti->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }

    // Create DirectWrite Text Formats
    // 1. Title Font
    hr = pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"en-US",
        &fontTitle
    );
    if (FAILED(hr)) return hr;
    fontTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fontTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fontTitle->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // 2. Menu Font
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"en-US",
        &fontMenu
    );
    if (fontMenu) {
        fontMenu->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fontMenu->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontMenu->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 2b. Dropdown Menu Item Font (12px, Left-aligned, Vertically centered)
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"en-US",
        &fontMenuItem
    );
    if (fontMenuItem) {
        fontMenuItem->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fontMenuItem->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontMenuItem->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 2c. Dropdown Menu Shortcut Font (11.5px, Right-aligned, Vertically centered)
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.5f,
        L"en-US",
        &fontMenuShortcut
    );
    if (fontMenuShortcut) {
        fontMenuShortcut->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        fontMenuShortcut->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontMenuShortcut->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 3. Body Font
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f,
        L"en-US",
        &fontBody
    );
    if (fontBody) {
        fontBody->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fontBody->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontBody->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 3a. Monospace Code Font (13px Consolas)
    pDWriteFactory->CreateTextFormat(
        L"Consolas",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f,
        L"en-US",
        &fontCode
    );
    if (fontCode) {
        fontCode->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fontCode->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontCode->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 3b. Centered Button Font (13px, Centered horizontally and vertically)
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f,
        L"en-US",
        &fontButtonCenter
    );
    if (fontButtonCenter) {
        fontButtonCenter->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fontButtonCenter->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontButtonCenter->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 3c. File Type Badge Font (8.5px, Bold, Centered horizontally and vertically)
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        8.5f,
        L"en-US",
        &fontBadge
    );
    if (fontBadge) {
        fontBadge->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fontBadge->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontBadge->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 3b. Section Header Font (EXPLORER)
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.0f,
        L"en-US",
        &fontSectionHeader
    );
    if (fontSectionHeader) {
        fontSectionHeader->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fontSectionHeader->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontSectionHeader->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 4. Icon Glyph Font (Segoe UI Symbol)
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI Symbol",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        15.0f,
        L"en-US",
        &fontIcon
    );
    if (fontIcon) {
        fontIcon->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fontIcon->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontIcon->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 5. Caption Button Font (Segoe MDL2 Assets / Segoe UI Symbol)
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI Symbol",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        10.5f,
        L"en-US",
        &fontCaption
    );
    if (fontCaption) {
        fontCaption->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fontCaption->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontCaption->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 6. Workspace Hint Font
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f,
        L"en-US",
        &fontHint
    );
    if (fontHint) {
        fontHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fontHint->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontHint->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 7. Customized Tooltip Font
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_MEDIUM,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.5f,
        L"en-US",
        &fontTooltip
    );
    if (fontTooltip) {
        fontTooltip->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fontTooltip->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontTooltip->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    return S_OK;
}

void Theme::DiscardDeviceResources() {
    fontSectionHeader.Reset();
    fontMenuItem.Reset();
    fontMenuShortcut.Reset();
    fontCode.Reset();
    brushWindowBg.Reset();
    brushCanvasBg.Reset();
    brushHeaderBg.Reset();
    brushRailBg.Reset();
    brushDrawerBg.Reset();
    brushBorderSubtle.Reset();
    brushBorderHover.Reset();
    brushTextPrimary.Reset();
    brushTextSecondary.Reset();
    brushTextMuted.Reset();
    brushTextWhite.Reset();
    brushItemHover.Reset();
    brushItemActive.Reset();
    brushCaptionHover.Reset();
    brushCloseHoverBg.Reset();
    brushBrandAccent.Reset();
    brushHomeBadgeBg.Reset();
    brushRunGreen.Reset();
    brushRunGreenHover.Reset();
    brushTooltipBg.Reset();
    brushTooltipBorder.Reset();
    brushMenuBg.Reset();
    brushMenuBorder.Reset();
    brushMenuSeparator.Reset();
    brushMenuHover.Reset();
    brushMenuText.Reset();
    brushMenuShortcut.Reset();
    brushSyntaxKeyword.Reset();
    brushSyntaxType.Reset();
    brushSyntaxString.Reset();
    brushSyntaxComment.Reset();
    brushSyntaxNumber.Reset();
    brushSyntaxOperator.Reset();
    brushLineNumber.Reset();
    brushCurrentLineBg.Reset();
    brushTabBarBg.Reset();
    brushTabActiveBg.Reset();
    brushTabBorder.Reset();
    dwriteFactory.Reset();
    fontButtonCenter.Reset();
    fontBadge.Reset();
    brushIconBlue.Reset();
    brushIconPurple.Reset();
    brushIconYellow.Reset();
    brushIconGreen.Reset();
    brushIconRed.Reset();
    brushIconOrange.Reset();
    brushIconFolder.Reset();
    brushIconFolderOpen.Reset();
    brushIconGray.Reset();

    // Reset Seti Font and Brushes
    fontSeti.Reset();
    customFontCollection.Reset();
    brushSetiPhp.Reset();
    brushSetiJs.Reset();
    brushSetiTs.Reset();
    brushSetiCpp.Reset();
    brushSetiJson.Reset();
    brushSetiHtml.Reset();
    brushSetiCss.Reset();
    brushSetiPython.Reset();
    brushSetiRust.Reset();
    brushSetiJava.Reset();
    brushSetiGit.Reset();
    brushSetiShell.Reset();
    brushSetiYaml.Reset();
    brushSetiImage.Reset();
    brushSetiFolder.Reset();
    brushSetiFolderOpen.Reset();
    brushSetiDefault.Reset();
}

Theme::SetiIconInfo Theme::GetSetiIcon(const std::wstring& filename, bool isFolder, bool isExpanded) const {
    if (isFolder) {
        return { 0xE032, isExpanded ? (brushSetiFolderOpen ? brushSetiFolderOpen.Get() : brushIconFolderOpen.Get())
                                     : (brushSetiFolder ? brushSetiFolder.Get() : brushIconFolder.Get()) };
    }

    std::wstring lower = filename;
    for (auto& c : lower) c = towlower(c);

    // Exact filename matches
    if (lower == L"composer.json" || lower == L"package.json" || lower == L"tsconfig.json" || lower == L"jsconfig.json") {
        return { 0xE055, brushSetiJson ? brushSetiJson.Get() : brushIconYellow.Get() };
    }
    if (lower == L"cmakelists.txt" || lower == L"makefile") {
        return { 0xE05F, brushSetiShell ? brushSetiShell.Get() : brushIconGreen.Get() };
    }
    if (lower == L".gitignore" || lower == L".gitattributes" || lower == L".gitmodules") {
        return { 0xE034, brushSetiGit ? brushSetiGit.Get() : brushIconRed.Get() };
    }
    if (lower == L".editorconfig") {
        return { 0xE026, brushSetiDefault ? brushSetiDefault.Get() : brushIconGray.Get() };
    }
    if (lower == L"dockerfile") {
        return { 0xE025, brushSetiCss ? brushSetiCss.Get() : brushIconBlue.Get() };
    }
    if (lower == L"license" || lower == L"license.txt" || lower == L"license.md") {
        return { 0xE05A, brushSetiJson ? brushSetiJson.Get() : brushIconYellow.Get() };
    }

    auto dot = lower.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        std::wstring ext = lower.substr(dot);
        if (ext == L".php" || ext == L".phtml") return { 0xE070, brushSetiPhp ? brushSetiPhp.Get() : brushIconPurple.Get() };
        if (ext == L".js" || ext == L".mjs" || ext == L".cjs" || ext == L".jsx") return { 0xE051, brushSetiJs ? brushSetiJs.Get() : brushIconYellow.Get() };
        if (ext == L".ts" || ext == L".tsx") return { 0xE099, brushSetiTs ? brushSetiTs.Get() : brushIconBlue.Get() };
        if (ext == L".cpp" || ext == L".cc" || ext == L".cxx" || ext == L".hpp" || ext == L".hxx" || ext == L".hh") return { 0xE01A, brushSetiCpp ? brushSetiCpp.Get() : brushIconBlue.Get() };
        if (ext == L".c" || ext == L".h") return { 0xE00C, brushSetiCpp ? brushSetiCpp.Get() : brushIconBlue.Get() };
        if (ext == L".cs") return { 0xE00B, brushSetiShell ? brushSetiShell.Get() : brushIconGreen.Get() };
        if (ext == L".json" || ext == L".jsonc") return { 0xE055, brushSetiJson ? brushSetiJson.Get() : brushIconYellow.Get() };
        if (ext == L".md" || ext == L".markdown" || ext == L".mdown") return { 0xE060, brushSetiTs ? brushSetiTs.Get() : brushIconBlue.Get() };
        if (ext == L".html" || ext == L".htm") return { 0xE048, brushSetiHtml ? brushSetiHtml.Get() : brushIconOrange.Get() };
        if (ext == L".css" || ext == L".scss" || ext == L".sass" || ext == L".less") return { 0xE01D, brushSetiCss ? brushSetiCss.Get() : brushIconBlue.Get() };
        if (ext == L".py" || ext == L".pyw") return { 0xE07B, brushSetiPython ? brushSetiPython.Get() : brushIconBlue.Get() };
        if (ext == L".rs") return { 0xE082, brushSetiRust ? brushSetiRust.Get() : brushIconOrange.Get() };
        if (ext == L".go") return { 0xE039, brushSetiTs ? brushSetiTs.Get() : brushIconBlue.Get() };
        if (ext == L".java") return { 0xE050, brushSetiJava ? brushSetiJava.Get() : brushIconRed.Get() };
        if (ext == L".kt" || ext == L".kts") return { 0xE058, brushSetiHtml ? brushSetiHtml.Get() : brushIconOrange.Get() };
        if (ext == L".yaml" || ext == L".yml") return { 0xE0A6, brushSetiYaml ? brushSetiYaml.Get() : brushIconRed.Get() };
        if (ext == L".xml") return { 0xE0A4, brushSetiJson ? brushSetiJson.Get() : brushIconYellow.Get() };
        if (ext == L".svg") return { 0xE091, brushSetiImage ? brushSetiImage.Get() : brushIconPurple.Get() };
        if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".gif" || ext == L".ico" || ext == L".webp") return { 0xE04C, brushSetiImage ? brushSetiImage.Get() : brushIconPurple.Get() };
        if (ext == L".bat" || ext == L".cmd" || ext == L".sh" || ext == L".bash") return { 0xE089, brushSetiShell ? brushSetiShell.Get() : brushIconGreen.Get() };
        if (ext == L".ps1") return { 0xE074, brushSetiShell ? brushSetiShell.Get() : brushIconGreen.Get() };
        if (ext == L".exe" || ext == L".dll" || ext == L".bin" || ext == L".so" || ext == L".obj") return { 0xE088, brushSetiShell ? brushSetiShell.Get() : brushIconGreen.Get() };
        if (ext == L".zip" || ext == L".tar" || ext == L".gz" || ext == L".7z" || ext == L".rar") return { 0xE0A8, brushSetiJson ? brushSetiJson.Get() : brushIconYellow.Get() };
        if (ext == L".lock") return { 0xE05D, brushSetiDefault ? brushSetiDefault.Get() : brushIconGray.Get() };
        if (ext == L".txt" || ext == L".log" || ext == L".ini" || ext == L".conf") return { 0xE023, brushSetiDefault ? brushSetiDefault.Get() : brushIconGray.Get() };
    }

    return { 0xE023, brushSetiDefault ? brushSetiDefault.Get() : brushIconGray.Get() };
}

} // namespace phide::ui
