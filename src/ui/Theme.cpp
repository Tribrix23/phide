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
    pRT->CreateSolidColorBrush(colors.treeSelected, &brushTreeSelected);
    pRT->CreateSolidColorBrush(colors.captionHover, &brushCaptionHover);
    pRT->CreateSolidColorBrush(colors.headerIcon, &brushHeaderIcon);
    pRT->CreateSolidColorBrush(colors.closeHoverBg, &brushCloseHoverBg);
    pRT->CreateSolidColorBrush(colors.brandAccent, &brushBrandAccent);
    pRT->CreateSolidColorBrush(colors.homeBadgeBg, &brushHomeBadgeBg);
    pRT->CreateSolidColorBrush(colors.runGreen, &brushRunGreen);
    pRT->CreateSolidColorBrush(colors.runGreenHover, &brushRunGreenHover);
    pRT->CreateSolidColorBrush(colors.tooltipBg, &brushTooltipBg);
    pRT->CreateSolidColorBrush(colors.tooltipBorder, &brushTooltipBorder);
    pRT->CreateSolidColorBrush(colors.tooltipText, &brushTooltipText);
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
    pRT->CreateSolidColorBrush(colors.syntaxTag, &brushSyntaxTag);
    pRT->CreateSolidColorBrush(colors.syntaxAttribute, &brushSyntaxAttribute);
    pRT->CreateSolidColorBrush(colors.lineNumber, &brushLineNumber);
    pRT->CreateSolidColorBrush(colors.currentLineBg, &brushCurrentLineBg);
    pRT->CreateSolidColorBrush(colors.tabBarBg, &brushTabBarBg);
    pRT->CreateSolidColorBrush(colors.tabActiveBg, &brushTabActiveBg);
    pRT->CreateSolidColorBrush(colors.tabBorder, &brushTabBorder);

    // Rainbow Bracket Brushes (Purple, Yellow, Cyan)
    pRT->CreateSolidColorBrush(colors.bracket1, &brushBracket1);
    pRT->CreateSolidColorBrush(colors.bracket2, &brushBracket2);
    pRT->CreateSolidColorBrush(colors.bracket3, &brushBracket3);

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

    // 8. Editor Tab Font (12px Regular, Leading-aligned, Centered vertically)
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"en-US",
        &fontTab
    );
    if (fontTab) {
        fontTab->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fontTab->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontTab->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 9. Editor Tab Preview Font (12px Italic, Leading-aligned, Centered vertically)
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_ITALIC,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"en-US",
        &fontTabPreview
    );
    if (fontTabPreview) {
        fontTabPreview->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fontTabPreview->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontTabPreview->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 10. Keycap Badge Font (11.5px Semi-Bold, Centered Horizontally & Vertically)
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.5f,
        L"en-US",
        &fontKeycap
    );
    if (fontKeycap) {
        fontKeycap->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fontKeycap->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontKeycap->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // 11. Counter Badge Font (11.0px Semi-Bold, Centered Horizontally & Vertically)
    pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.0f,
        L"en-US",
        &fontCounterBadge
    );
    if (fontCounterBadge) {
        fontCounterBadge->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fontCounterBadge->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fontCounterBadge->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    return S_OK;
}

void Theme::DiscardDeviceResources() {
    fontCounterBadge.Reset();
    fontKeycap.Reset();
    fontTab.Reset();
    fontTabPreview.Reset();
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
    brushTreeSelected.Reset();
    brushCaptionHover.Reset();
    brushHeaderIcon.Reset();
    brushCloseHoverBg.Reset();
    brushBrandAccent.Reset();
    brushHomeBadgeBg.Reset();
    brushRunGreen.Reset();
    brushRunGreenHover.Reset();
    brushTooltipBg.Reset();
    brushTooltipBorder.Reset();
    brushTooltipText.Reset();
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
    brushSyntaxTag.Reset();
    brushSyntaxAttribute.Reset();
    brushLineNumber.Reset();
    brushCurrentLineBg.Reset();
    brushTabBarBg.Reset();
    brushTabActiveBg.Reset();
    brushTabBorder.Reset();
    brushBracket1.Reset();
    brushBracket2.Reset();
    brushBracket3.Reset();
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

void Theme::SetEditorFontSize(float size) {
    if (dwriteFactory) {
        fontCode.Reset();
        dwriteFactory->CreateTextFormat(
            L"Consolas",
            nullptr,
            DWRITE_FONT_WEIGHT_REGULAR,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            (std::max)(6.0f, size),
            L"en-US",
            &fontCode
        );
        if (fontCode) {
            fontCode->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            fontCode->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            fontCode->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
}

/**
 * @brief Sets the active theme mode and updates the color palette.
 * 
 * In Auto mode, queries the Windows registry key:
 * HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme
 * to dynamically mirror the host OS light/dark mode preference.
 * 
 * Sets up all IDE surface colors (windowBg, canvasBg, headerBg, railBg, drawerBg, tooltips, etc.).
 * Note: windowBg is purposefully set to dark purple (#3D1461) in Dark mode and light purple (#CFB3E8)
 * in Light mode so that the upper-left 14px rounded arc of DrawerPanel and EditorCanvas curves smoothly against
 * the title bar and activity rail without creating a square notch.
 */
void Theme::SetThemeMode(ThemeMode mode) {
    m_mode = mode;
    if (mode == ThemeMode::Dark) {
        m_isDarkActive = true;
    } else if (mode == ThemeMode::Light) {
        m_isDarkActive = false;
    } else if (mode == ThemeMode::Auto) {
        HKEY hKey = nullptr;
        DWORD dwValue = 0;
        DWORD dwSize = sizeof(dwValue);
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                          0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(&dwValue), &dwSize) == ERROR_SUCCESS) {
                m_isDarkActive = (dwValue == 0);
            } else {
                m_isDarkActive = true;
            }
            RegCloseKey(hKey);
        } else {
            m_isDarkActive = true;
        }
    } else {
        // Purple, Cyan, Turquoise, Monokai, Dracula are all vibrant dark-aesthetic themes
        m_isDarkActive = true;
    }

    if (!m_isDarkActive) {
        // Light Theme Colors
        colors.windowBg        = D2D1::ColorF(0.812f, 0.702f, 0.910f, 1.0f); // #CFB3E8 Light Purple base
        colors.canvasBg        = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);       // #FFFFFF
        colors.headerBg        = D2D1::ColorF(0.812f, 0.702f, 0.910f, 1.0f); // #CFB3E8 Light Purple
        colors.railBg          = D2D1::ColorF(0.812f, 0.702f, 0.910f, 1.0f); // #CFB3E8 Light Purple
        colors.drawerBg        = D2D1::ColorF(0.953f, 0.953f, 0.953f, 1.0f); // #F3F3F3
        colors.borderSubtle    = D2D1::ColorF(0.88f, 0.88f, 0.88f, 1.0f);    // #E0E0E0
        colors.borderHover     = D2D1::ColorF(0.80f, 0.80f, 0.80f, 1.0f);    // #CCCCCC
        colors.textPrimary     = D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f);    // #1E1E1E
        colors.textSecondary   = D2D1::ColorF(0.38f, 0.38f, 0.38f, 1.0f);    // #616161
        colors.textMuted       = D2D1::ColorF(0.55f, 0.55f, 0.55f, 1.0f);    // #8E8E8E
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);        // Always pure white
        colors.itemHover       = D2D1::ColorF(0.91f, 0.91f, 0.91f, 1.0f);    // #E8E8E8
        colors.itemActive      = D2D1::ColorF(0.84f, 0.84f, 0.84f, 1.0f);    // #D6D6D6
        colors.treeSelected    = D2D1::ColorF(0.016f, 0.380f, 0.647f, 0.35f);
        colors.captionHover    = D2D1::ColorF(0.88f, 0.88f, 0.88f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.08f, 0.08f, 0.08f, 1.0f);   // #141414 Deep Black
        colors.brandAccent     = D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f);   // #0078D4
        colors.homeBadgeBg     = D2D1::ColorF(0.85f, 0.85f, 0.85f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.30f, 0.30f, 0.34f, 1.0f);
        colors.tooltipText     = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.96f, 0.96f, 0.96f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.82f, 0.82f, 0.82f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.90f, 0.90f, 0.90f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.20f, 0.20f, 0.20f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.44f, 0.44f, 0.44f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.925f, 0.925f, 0.925f, 1.0f); // #ECECEC
        colors.tabActiveBg     = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);       // #FFFFFF
        colors.tabBorder       = D2D1::ColorF(0.88f, 0.88f, 0.88f, 1.0f);    // #E0E0E0
        colors.currentLineBg   = D2D1::ColorF(0.97f, 0.97f, 0.97f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.43f, 0.46f, 0.51f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f);       // #0000FF
        colors.syntaxType      = D2D1::ColorF(0.16f, 0.50f, 0.50f, 1.0f);    // #267F99
        colors.syntaxString    = D2D1::ColorF(0.65f, 0.16f, 0.16f, 1.0f);    // #A31515
        colors.syntaxComment   = D2D1::ColorF(0.0f, 0.50f, 0.0f, 1.0f);       // #008000
        colors.syntaxNumber    = D2D1::ColorF(0.035f, 0.518f, 0.329f, 1.0f); // #098658
        colors.syntaxOperator  = D2D1::ColorF(0.20f, 0.20f, 0.20f, 1.0f);
        colors.syntaxTag       = D2D1::ColorF(0.502f, 0.0f, 0.0f, 1.0f);      // #800000
        colors.syntaxAttribute = D2D1::ColorF(0.9f, 0.1f, 0.1f, 1.0f);       // #E61919
        colors.bracket1        = D2D1::ColorF(0.482f, 0.122f, 0.635f, 1.0f); // #7B1FA2 Purple
        colors.bracket2        = D2D1::ColorF(0.851f, 0.467f, 0.024f, 1.0f); // #D97706 Warm Amber Yellow
        colors.bracket3        = D2D1::ColorF(0.008f, 0.518f, 0.780f, 1.0f); // #0284C7 Sky Blue
    } else if (m_mode == ThemeMode::Cyan) {
        // Cyan Cyber Theme
        colors.windowBg        = D2D1::ColorF(0.047f, 0.157f, 0.251f, 1.0f); // #0C2840 Vibrant Cyber Frame
        colors.canvasBg        = D2D1::ColorF(0.039f, 0.098f, 0.184f, 1.0f); // #0A192F
        colors.headerBg        = D2D1::ColorF(0.047f, 0.157f, 0.251f, 1.0f); // #0C2840
        colors.railBg          = D2D1::ColorF(0.047f, 0.157f, 0.251f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.024f, 0.071f, 0.125f, 1.0f); // #061220
        colors.borderSubtle    = D2D1::ColorF(0.055f, 0.165f, 0.278f, 1.0f); // #0E2A47
        colors.borderHover     = D2D1::ColorF(0.0f, 0.898f, 1.0f, 1.0f);     // #00E5FF
        colors.textPrimary     = D2D1::ColorF(0.878f, 0.984f, 0.988f, 1.0f); // #E0FBFC
        colors.textSecondary   = D2D1::ColorF(0.557f, 0.792f, 0.902f, 1.0f); // #8ECAE6
        colors.textMuted       = D2D1::ColorF(0.282f, 0.396f, 0.506f, 1.0f); // #486581
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.063f, 0.165f, 0.263f, 1.0f); // #102A43
        colors.itemActive      = D2D1::ColorF(0.094f, 0.231f, 0.361f, 1.0f); // #183B5C
        colors.treeSelected    = D2D1::ColorF(0.0f, 0.373f, 0.451f, 0.85f);  // #005F73
        colors.captionHover    = D2D1::ColorF(0.063f, 0.165f, 0.263f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.0f, 0.898f, 1.0f, 1.0f);     // #00E5FF
        colors.brandAccent     = D2D1::ColorF(0.0f, 0.898f, 1.0f, 1.0f);     // #00E5FF
        colors.homeBadgeBg     = D2D1::ColorF(0.063f, 0.165f, 0.263f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.039f, 0.098f, 0.184f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.0f, 0.898f, 1.0f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.878f, 0.984f, 0.988f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.024f, 0.071f, 0.125f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.055f, 0.165f, 0.278f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.055f, 0.165f, 0.278f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.0f, 0.706f, 0.847f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.878f, 0.984f, 0.988f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.557f, 0.792f, 0.902f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.024f, 0.071f, 0.125f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.039f, 0.098f, 0.184f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.055f, 0.165f, 0.278f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.063f, 0.165f, 0.263f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.282f, 0.450f, 0.600f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.0f, 0.898f, 1.0f, 1.0f);     // #00E5FF
        colors.syntaxType      = D2D1::ColorF(0.220f, 0.937f, 0.490f, 1.0f); // #38EF7D
        colors.syntaxString    = D2D1::ColorF(0.565f, 0.878f, 0.937f, 1.0f); // #90E0EF
        colors.syntaxComment   = D2D1::ColorF(0.361f, 0.463f, 0.553f, 1.0f); // #5C768D
        colors.syntaxNumber    = D2D1::ColorF(1.0f, 0.439f, 0.651f, 1.0f);   // #FF70A6
        colors.syntaxOperator  = D2D1::ColorF(0.0f, 0.706f, 0.847f, 1.0f);   // #00B4D8
        colors.syntaxTag       = D2D1::ColorF(0.0f, 0.898f, 1.0f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.792f, 0.941f, 0.973f, 1.0f); // #CAF0F8
        colors.bracket1        = D2D1::ColorF(0.702f, 0.533f, 1.000f, 1.0f); // #B388FF Purple
        colors.bracket2        = D2D1::ColorF(1.000f, 0.820f, 0.400f, 1.0f); // #FFD166 Gold
        colors.bracket3        = D2D1::ColorF(0.000f, 0.898f, 1.000f, 1.0f); // #00E5FF Cyan
    } else if (m_mode == ThemeMode::Turquoise) {
        // Turquoise Teal Theme
        colors.windowBg        = D2D1::ColorF(0.039f, 0.212f, 0.196f, 1.0f); // #0A3632 Deep Emerald Teal Frame
        colors.canvasBg        = D2D1::ColorF(0.024f, 0.157f, 0.149f, 1.0f); // #062826
        colors.headerBg        = D2D1::ColorF(0.039f, 0.212f, 0.196f, 1.0f); // #0A3632
        colors.railBg          = D2D1::ColorF(0.039f, 0.212f, 0.196f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.008f, 0.102f, 0.098f, 1.0f); // #021A19
        colors.borderSubtle    = D2D1::ColorF(0.043f, 0.235f, 0.224f, 1.0f); // #0B3C39
        colors.borderHover     = D2D1::ColorF(0.114f, 0.914f, 0.714f, 1.0f); // #1DE9B6
        colors.textPrimary     = D2D1::ColorF(0.878f, 0.949f, 0.945f, 1.0f); // #E0F2F1
        colors.textSecondary   = D2D1::ColorF(0.502f, 0.796f, 0.769f, 1.0f); // #80CBC4
        colors.textMuted       = D2D1::ColorF(0.302f, 0.486f, 0.471f, 1.0f); // #4D7C78
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.039f, 0.212f, 0.200f, 1.0f); // #0A3633
        colors.itemActive      = D2D1::ColorF(0.055f, 0.278f, 0.263f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.0f, 0.302f, 0.251f, 0.85f);  // #004D40
        colors.captionHover    = D2D1::ColorF(0.039f, 0.212f, 0.200f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.114f, 0.914f, 0.714f, 1.0f); // #1DE9B6
        colors.brandAccent     = D2D1::ColorF(0.114f, 0.914f, 0.714f, 1.0f); // #1DE9B6
        colors.homeBadgeBg     = D2D1::ColorF(0.039f, 0.212f, 0.200f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.024f, 0.157f, 0.149f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.114f, 0.914f, 0.714f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.878f, 0.949f, 0.945f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.008f, 0.102f, 0.098f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.043f, 0.235f, 0.224f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.043f, 0.235f, 0.224f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.0f, 0.706f, 0.847f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.878f, 0.949f, 0.945f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.502f, 0.796f, 0.769f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.008f, 0.102f, 0.098f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.024f, 0.157f, 0.149f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.043f, 0.235f, 0.224f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.039f, 0.212f, 0.200f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.250f, 0.500f, 0.480f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.114f, 0.914f, 0.714f, 1.0f); // #1DE9B6
        colors.syntaxType      = D2D1::ColorF(0.392f, 1.0f, 0.855f, 1.0f);   // #64FFDA
        colors.syntaxString    = D2D1::ColorF(0.655f, 1.0f, 0.922f, 1.0f);   // #A7FFEB
        colors.syntaxComment   = D2D1::ColorF(0.306f, 0.502f, 0.490f, 1.0f); // #4E807D
        colors.syntaxNumber    = D2D1::ColorF(1.0f, 0.820f, 0.400f, 1.0f);   // #FFD166
        colors.syntaxOperator  = D2D1::ColorF(0.282f, 0.792f, 0.894f, 1.0f); // #48CAE4
        colors.syntaxTag       = D2D1::ColorF(0.114f, 0.914f, 0.714f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.502f, 1.0f, 0.910f, 1.0f);   // #80FFE8
        colors.bracket1        = D2D1::ColorF(0.780f, 0.490f, 1.000f, 1.0f); // #C77DFF Purple
        colors.bracket2        = D2D1::ColorF(1.000f, 0.878f, 0.400f, 1.0f); // #FFE066 Warm Yellow
        colors.bracket3        = D2D1::ColorF(0.114f, 0.914f, 0.714f, 1.0f); // #1DE9B6 Seafoam Turquoise
    } else if (m_mode == ThemeMode::Purple) {
        // Synthwave Deep Purple Theme
        colors.windowBg        = D2D1::ColorF(0.231f, 0.078f, 0.369f, 1.0f); // #3B145E Royal Deep Purple Frame
        colors.canvasBg        = D2D1::ColorF(0.075f, 0.035f, 0.122f, 1.0f); // #13091F
        colors.headerBg        = D2D1::ColorF(0.231f, 0.078f, 0.369f, 1.0f); // #3B145E
        colors.railBg          = D2D1::ColorF(0.231f, 0.078f, 0.369f, 1.0f); // #3B145E
        colors.drawerBg        = D2D1::ColorF(0.063f, 0.027f, 0.102f, 1.0f); // #10071A
        colors.borderSubtle    = D2D1::ColorF(0.196f, 0.078f, 0.310f, 1.0f); // #32144F
        colors.borderHover     = D2D1::ColorF(0.616f, 0.306f, 0.867f, 1.0f); // #9D4EDD
        colors.textPrimary     = D2D1::ColorF(0.953f, 0.910f, 1.0f, 1.0f);   // #F3E8FF
        colors.textSecondary   = D2D1::ColorF(0.753f, 0.518f, 0.988f, 1.0f); // #C084FC
        colors.textMuted       = D2D1::ColorF(0.420f, 0.310f, 0.510f, 1.0f); // #6B4F82
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.141f, 0.067f, 0.231f, 1.0f); // #24113B
        colors.itemActive      = D2D1::ColorF(0.200f, 0.090f, 0.320f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.353f, 0.094f, 0.604f, 0.85f); // #5A189A
        colors.captionHover    = D2D1::ColorF(0.141f, 0.067f, 0.231f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.878f, 0.667f, 1.0f, 1.0f);   // #E0AAFF
        colors.brandAccent     = D2D1::ColorF(0.616f, 0.306f, 0.867f, 1.0f); // #9D4EDD
        colors.homeBadgeBg     = D2D1::ColorF(0.141f, 0.067f, 0.231f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.075f, 0.035f, 0.122f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.616f, 0.306f, 0.867f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.953f, 0.910f, 1.0f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.063f, 0.027f, 0.102f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.196f, 0.078f, 0.310f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.196f, 0.078f, 0.310f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.482f, 0.173f, 0.749f, 1.0f); // #7B2CBF
        colors.menuText        = D2D1::ColorF(0.953f, 0.910f, 1.0f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.753f, 0.518f, 0.988f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.063f, 0.027f, 0.102f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.075f, 0.035f, 0.122f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.196f, 0.078f, 0.310f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.141f, 0.067f, 0.231f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.450f, 0.320f, 0.550f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.878f, 0.667f, 1.0f, 1.0f);   // #E0AAFF
        colors.syntaxType      = D2D1::ColorF(1.0f, 0.439f, 0.651f, 1.0f);   // #FF70A6
        colors.syntaxString    = D2D1::ColorF(1.0f, 0.718f, 0.012f, 1.0f);   // #FFB703
        colors.syntaxComment   = D2D1::ColorF(0.427f, 0.349f, 0.478f, 1.0f); // #6D597A
        colors.syntaxNumber    = D2D1::ColorF(0.220f, 0.937f, 0.490f, 1.0f); // #38EF7D
        colors.syntaxOperator  = D2D1::ColorF(0.780f, 0.490f, 1.0f, 1.0f);   // #C77DFF
        colors.syntaxTag       = D2D1::ColorF(0.780f, 0.490f, 1.0f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.878f, 0.667f, 1.0f, 1.0f);
        colors.bracket1        = D2D1::ColorF(0.878f, 0.667f, 1.000f, 1.0f); // #E0AAFF Lilac Purple
        colors.bracket2        = D2D1::ColorF(1.000f, 0.718f, 0.012f, 1.0f); // #FFB703 Warm Yellow
        colors.bracket3        = D2D1::ColorF(0.392f, 1.000f, 0.855f, 1.0f); // #64FFDA Cyan Mint
    } else if (m_mode == ThemeMode::Monokai) {
        // Monokai Pro Theme
        colors.windowBg        = D2D1::ColorF(0.180f, 0.176f, 0.165f, 1.0f); // #2E2D2A Warm Monokai Frame
        colors.canvasBg        = D2D1::ColorF(0.153f, 0.157f, 0.133f, 1.0f); // #272822
        colors.headerBg        = D2D1::ColorF(0.180f, 0.176f, 0.165f, 1.0f); // #2E2D2A
        colors.railBg          = D2D1::ColorF(0.180f, 0.176f, 0.165f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.098f, 0.102f, 0.090f, 1.0f); // #191A17
        colors.borderSubtle    = D2D1::ColorF(0.243f, 0.239f, 0.196f, 1.0f); // #3E3D32
        colors.borderHover     = D2D1::ColorF(0.651f, 0.886f, 0.180f, 1.0f); // #A6E22E
        colors.textPrimary     = D2D1::ColorF(0.973f, 0.973f, 0.949f, 1.0f); // #F8F8F2
        colors.textSecondary   = D2D1::ColorF(0.647f, 0.624f, 0.522f, 1.0f); // #A59F85
        colors.textMuted       = D2D1::ColorF(0.459f, 0.443f, 0.369f, 1.0f); // #75715E
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.243f, 0.239f, 0.196f, 1.0f); // #3E3D32
        colors.itemActive      = D2D1::ColorF(0.300f, 0.290f, 0.240f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.243f, 0.239f, 0.196f, 1.0f);
        colors.captionHover    = D2D1::ColorF(0.243f, 0.239f, 0.196f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.976f, 0.149f, 0.447f, 1.0f); // #F92672
        colors.brandAccent     = D2D1::ColorF(0.976f, 0.149f, 0.447f, 1.0f); // #F92672
        colors.homeBadgeBg     = D2D1::ColorF(0.243f, 0.239f, 0.196f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.153f, 0.157f, 0.133f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.976f, 0.149f, 0.447f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.973f, 0.973f, 0.949f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.098f, 0.102f, 0.090f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.243f, 0.239f, 0.196f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.243f, 0.239f, 0.196f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.976f, 0.149f, 0.447f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.973f, 0.973f, 0.949f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.647f, 0.624f, 0.522f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.098f, 0.102f, 0.090f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.153f, 0.157f, 0.133f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.243f, 0.239f, 0.196f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.243f, 0.239f, 0.196f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.550f, 0.530f, 0.440f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.976f, 0.149f, 0.447f, 1.0f); // #F92672
        colors.syntaxType      = D2D1::ColorF(0.400f, 0.851f, 0.937f, 1.0f); // #66D9EF
        colors.syntaxString    = D2D1::ColorF(0.902f, 0.859f, 0.455f, 1.0f); // #E6DB74
        colors.syntaxComment   = D2D1::ColorF(0.459f, 0.443f, 0.369f, 1.0f); // #75715E
        colors.syntaxNumber    = D2D1::ColorF(0.682f, 0.506f, 1.0f, 1.0f);   // #AE81FF
        colors.syntaxOperator  = D2D1::ColorF(0.992f, 0.592f, 0.122f, 1.0f); // #FD971F
        colors.syntaxTag       = D2D1::ColorF(0.976f, 0.149f, 0.447f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.651f, 0.886f, 0.180f, 1.0f); // #A6E22E
        colors.bracket1        = D2D1::ColorF(0.976f, 0.149f, 0.447f, 1.0f); // #F92672 Magenta Purple
        colors.bracket2        = D2D1::ColorF(0.902f, 0.859f, 0.455f, 1.0f); // #E6DB74 Warm Yellow
        colors.bracket3        = D2D1::ColorF(0.400f, 0.851f, 0.937f, 1.0f); // #66D9EF Cyan
    } else if (m_mode == ThemeMode::Dracula) {
        // Dracula Theme
        colors.windowBg        = D2D1::ColorF(0.208f, 0.212f, 0.298f, 1.0f); // #35364C Gothic Dracula Frame
        colors.canvasBg        = D2D1::ColorF(0.157f, 0.165f, 0.212f, 1.0f); // #282A36
        colors.headerBg        = D2D1::ColorF(0.208f, 0.212f, 0.298f, 1.0f); // #35364C
        colors.railBg          = D2D1::ColorF(0.208f, 0.212f, 0.298f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.118f, 0.122f, 0.161f, 1.0f); // #1E1F29
        colors.borderSubtle    = D2D1::ColorF(0.267f, 0.278f, 0.353f, 1.0f); // #44475A
        colors.borderHover     = D2D1::ColorF(0.741f, 0.576f, 0.976f, 1.0f); // #BD93F9
        colors.textPrimary     = D2D1::ColorF(0.973f, 0.973f, 0.949f, 1.0f); // #F8F8F2
        colors.textSecondary   = D2D1::ColorF(0.384f, 0.447f, 0.643f, 1.0f); // #6272A4
        colors.textMuted       = D2D1::ColorF(0.384f, 0.447f, 0.643f, 1.0f);
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.267f, 0.278f, 0.353f, 1.0f); // #44475A
        colors.itemActive      = D2D1::ColorF(0.300f, 0.310f, 0.400f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.267f, 0.278f, 0.353f, 0.85f);
        colors.captionHover    = D2D1::ColorF(0.267f, 0.278f, 0.353f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.741f, 0.576f, 0.976f, 1.0f); // #BD93F9
        colors.brandAccent     = D2D1::ColorF(0.741f, 0.576f, 0.976f, 1.0f); // #BD93F9
        colors.homeBadgeBg     = D2D1::ColorF(0.267f, 0.278f, 0.353f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.157f, 0.165f, 0.212f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.741f, 0.576f, 0.976f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.973f, 0.973f, 0.949f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.129f, 0.133f, 0.173f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.267f, 0.278f, 0.353f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.267f, 0.278f, 0.353f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.741f, 0.576f, 0.976f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.973f, 0.973f, 0.949f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.384f, 0.447f, 0.643f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.118f, 0.122f, 0.161f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.157f, 0.165f, 0.212f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.267f, 0.278f, 0.353f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.267f, 0.278f, 0.353f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.384f, 0.447f, 0.643f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(1.0f, 0.475f, 0.776f, 1.0f);   // #FF79C6
        colors.syntaxType      = D2D1::ColorF(0.545f, 0.914f, 0.992f, 1.0f); // #8BE9FD
        colors.syntaxString    = D2D1::ColorF(0.945f, 0.980f, 0.549f, 1.0f); // #F1FA8C
        colors.syntaxComment   = D2D1::ColorF(0.384f, 0.447f, 0.643f, 1.0f); // #6272A4
        colors.syntaxNumber    = D2D1::ColorF(0.741f, 0.576f, 0.976f, 1.0f); // #BD93F9
        colors.syntaxOperator  = D2D1::ColorF(0.314f, 0.980f, 0.482f, 1.0f); // #50FA7B
        colors.syntaxTag       = D2D1::ColorF(1.0f, 0.475f, 0.776f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.314f, 0.980f, 0.482f, 1.0f);
        colors.bracket1        = D2D1::ColorF(0.741f, 0.576f, 0.976f, 1.0f); // #BD93F9 Purple
        colors.bracket2        = D2D1::ColorF(0.945f, 0.980f, 0.549f, 1.0f); // #F1FA8C Yellow
        colors.bracket3        = D2D1::ColorF(0.545f, 0.914f, 0.992f, 1.0f); // #8BE9FD Cyan
    } else if (m_mode == ThemeMode::Nord) {
        // Nord Arctic Theme
        colors.windowBg        = D2D1::ColorF(0.231f, 0.259f, 0.322f, 1.0f); // #3B4252 Nord Polar Frame
        colors.canvasBg        = D2D1::ColorF(0.180f, 0.204f, 0.251f, 1.0f); // #2E3440
        colors.headerBg        = D2D1::ColorF(0.231f, 0.259f, 0.322f, 1.0f); // #3B4252
        colors.railBg          = D2D1::ColorF(0.231f, 0.259f, 0.322f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.141f, 0.161f, 0.200f, 1.0f); // #242933
        colors.borderSubtle    = D2D1::ColorF(0.298f, 0.337f, 0.416f, 1.0f); // #4C566A
        colors.borderHover     = D2D1::ColorF(0.533f, 0.753f, 0.816f, 1.0f); // #88C0D0
        colors.textPrimary     = D2D1::ColorF(0.925f, 0.937f, 0.957f, 1.0f); // #ECEFF4
        colors.textSecondary   = D2D1::ColorF(0.847f, 0.871f, 0.914f, 1.0f); // #D8DEE9
        colors.textMuted       = D2D1::ColorF(0.545f, 0.584f, 0.647f, 1.0f); // #8B95A5
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.200f, 0.230f, 0.290f, 1.0f);
        colors.itemActive      = D2D1::ColorF(0.263f, 0.302f, 0.369f, 1.0f); // #434C5E
        colors.treeSelected    = D2D1::ColorF(0.506f, 0.631f, 0.757f, 0.85f); // #81A1C1
        colors.captionHover    = D2D1::ColorF(0.200f, 0.230f, 0.290f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.533f, 0.753f, 0.816f, 1.0f); // #88C0D0
        colors.brandAccent     = D2D1::ColorF(0.533f, 0.753f, 0.816f, 1.0f); // #88C0D0
        colors.homeBadgeBg     = D2D1::ColorF(0.200f, 0.230f, 0.290f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.180f, 0.204f, 0.251f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.533f, 0.753f, 0.816f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.925f, 0.937f, 0.957f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.141f, 0.161f, 0.200f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.298f, 0.337f, 0.416f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.298f, 0.337f, 0.416f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.533f, 0.753f, 0.816f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.925f, 0.937f, 0.957f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.847f, 0.871f, 0.914f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.141f, 0.161f, 0.200f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.180f, 0.204f, 0.251f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.298f, 0.337f, 0.416f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.231f, 0.259f, 0.322f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.380f, 0.431f, 0.533f, 1.0f); // #616E88
        colors.syntaxKeyword   = D2D1::ColorF(0.506f, 0.631f, 0.757f, 1.0f); // #81A1C1
        colors.syntaxType      = D2D1::ColorF(0.561f, 0.737f, 0.733f, 1.0f); // #8FBCBB
        colors.syntaxString    = D2D1::ColorF(0.639f, 0.745f, 0.549f, 1.0f); // #A3BE8C
        colors.syntaxComment   = D2D1::ColorF(0.380f, 0.431f, 0.533f, 1.0f); // #616E88
        colors.syntaxNumber    = D2D1::ColorF(0.706f, 0.557f, 0.678f, 1.0f); // #B48EAD
        colors.syntaxOperator  = D2D1::ColorF(0.506f, 0.631f, 0.757f, 1.0f);
        colors.syntaxTag       = D2D1::ColorF(0.506f, 0.631f, 0.757f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.561f, 0.737f, 0.733f, 1.0f);
        colors.bracket1        = D2D1::ColorF(0.706f, 0.557f, 0.678f, 1.0f); // #B48EAD
        colors.bracket2        = D2D1::ColorF(0.922f, 0.796f, 0.545f, 1.0f); // #EBCB8B
        colors.bracket3        = D2D1::ColorF(0.533f, 0.753f, 0.816f, 1.0f); // #88C0D0
    } else if (m_mode == ThemeMode::Solarized) {
        // Solarized Dark Theme
        colors.windowBg        = D2D1::ColorF(0.027f, 0.212f, 0.259f, 1.0f); // #073642 Solarized Frame
        colors.canvasBg        = D2D1::ColorF(0.000f, 0.169f, 0.212f, 1.0f); // #002B36
        colors.headerBg        = D2D1::ColorF(0.027f, 0.212f, 0.259f, 1.0f); // #073642
        colors.railBg          = D2D1::ColorF(0.027f, 0.212f, 0.259f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.000f, 0.129f, 0.169f, 1.0f); // #00212B
        colors.borderSubtle    = D2D1::ColorF(0.345f, 0.431f, 0.459f, 1.0f); // #586E75
        colors.borderHover     = D2D1::ColorF(0.149f, 0.545f, 0.824f, 1.0f); // #268BD2
        colors.textPrimary     = D2D1::ColorF(0.576f, 0.631f, 0.631f, 1.0f); // #93A1A1
        colors.textSecondary   = D2D1::ColorF(0.514f, 0.580f, 0.588f, 1.0f); // #839496
        colors.textMuted       = D2D1::ColorF(0.345f, 0.431f, 0.459f, 1.0f); // #586E75
        colors.textWhite       = D2D1::ColorF(0.992f, 0.965f, 0.890f, 1.0f); // #FDF6E3
        colors.itemHover       = D2D1::ColorF(0.040f, 0.250f, 0.300f, 1.0f);
        colors.itemActive      = D2D1::ColorF(0.050f, 0.280f, 0.340f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.149f, 0.545f, 0.824f, 0.85f); // #268BD2
        colors.captionHover    = D2D1::ColorF(0.040f, 0.250f, 0.300f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.149f, 0.545f, 0.824f, 1.0f);
        colors.brandAccent     = D2D1::ColorF(0.149f, 0.545f, 0.824f, 1.0f);
        colors.homeBadgeBg     = D2D1::ColorF(0.040f, 0.250f, 0.300f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.000f, 0.169f, 0.212f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.149f, 0.545f, 0.824f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.992f, 0.965f, 0.890f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.027f, 0.212f, 0.259f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.345f, 0.431f, 0.459f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.345f, 0.431f, 0.459f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.149f, 0.545f, 0.824f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.576f, 0.631f, 0.631f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.514f, 0.580f, 0.588f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.027f, 0.212f, 0.259f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.000f, 0.169f, 0.212f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.345f, 0.431f, 0.459f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.027f, 0.212f, 0.259f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.345f, 0.431f, 0.459f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.522f, 0.600f, 0.000f, 1.0f); // #859900
        colors.syntaxType      = D2D1::ColorF(0.710f, 0.537f, 0.000f, 1.0f); // #B58900
        colors.syntaxString    = D2D1::ColorF(0.165f, 0.631f, 0.596f, 1.0f); // #2AA198
        colors.syntaxComment   = D2D1::ColorF(0.345f, 0.431f, 0.459f, 1.0f); // #586E75
        colors.syntaxNumber    = D2D1::ColorF(0.827f, 0.212f, 0.510f, 1.0f); // #D33682
        colors.syntaxOperator  = D2D1::ColorF(0.522f, 0.600f, 0.000f, 1.0f);
        colors.syntaxTag       = D2D1::ColorF(0.149f, 0.545f, 0.824f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.710f, 0.537f, 0.000f, 1.0f);
        colors.bracket1        = D2D1::ColorF(0.827f, 0.212f, 0.510f, 1.0f); // #D33682
        colors.bracket2        = D2D1::ColorF(0.796f, 0.294f, 0.086f, 1.0f); // #CB4B16
        colors.bracket3        = D2D1::ColorF(0.149f, 0.545f, 0.824f, 1.0f); // #268BD2
    } else if (m_mode == ThemeMode::GitHubDark) {
        // GitHub Dark Theme
        colors.windowBg        = D2D1::ColorF(0.086f, 0.106f, 0.133f, 1.0f); // #161B22 GitHub Frame
        colors.canvasBg        = D2D1::ColorF(0.051f, 0.067f, 0.090f, 1.0f); // #0D1117
        colors.headerBg        = D2D1::ColorF(0.086f, 0.106f, 0.133f, 1.0f); // #161B22
        colors.railBg          = D2D1::ColorF(0.086f, 0.106f, 0.133f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.035f, 0.051f, 0.071f, 1.0f); // #090D12
        colors.borderSubtle    = D2D1::ColorF(0.188f, 0.212f, 0.239f, 1.0f); // #30363D
        colors.borderHover     = D2D1::ColorF(0.345f, 0.651f, 1.000f, 1.0f); // #58A6FF
        colors.textPrimary     = D2D1::ColorF(0.788f, 0.820f, 0.851f, 1.0f); // #C9D1D9
        colors.textSecondary   = D2D1::ColorF(0.545f, 0.580f, 0.620f, 1.0f); // #8B949E
        colors.textMuted       = D2D1::ColorF(0.282f, 0.314f, 0.349f, 1.0f); // #484F58
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.129f, 0.153f, 0.188f, 1.0f); // #21262D
        colors.itemActive      = D2D1::ColorF(0.188f, 0.212f, 0.239f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.122f, 0.231f, 0.373f, 0.85f); // #1F3B5F
        colors.captionHover    = D2D1::ColorF(0.129f, 0.153f, 0.188f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.345f, 0.651f, 1.000f, 1.0f);
        colors.brandAccent     = D2D1::ColorF(0.345f, 0.651f, 1.000f, 1.0f); // #58A6FF
        colors.homeBadgeBg     = D2D1::ColorF(0.129f, 0.153f, 0.188f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.051f, 0.067f, 0.090f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.345f, 0.651f, 1.000f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.788f, 0.820f, 0.851f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.086f, 0.106f, 0.133f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.188f, 0.212f, 0.239f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.188f, 0.212f, 0.239f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.345f, 0.651f, 1.000f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.788f, 0.820f, 0.851f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.545f, 0.580f, 0.620f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.086f, 0.106f, 0.133f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.051f, 0.067f, 0.090f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.188f, 0.212f, 0.239f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.086f, 0.106f, 0.133f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.282f, 0.314f, 0.349f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(1.000f, 0.482f, 0.447f, 1.0f); // #FF7B72
        colors.syntaxType      = D2D1::ColorF(1.000f, 0.651f, 0.341f, 1.0f); // #FFA657
        colors.syntaxString    = D2D1::ColorF(0.647f, 0.839f, 1.000f, 1.0f); // #A5D6FF
        colors.syntaxComment   = D2D1::ColorF(0.545f, 0.580f, 0.620f, 1.0f); // #8B949E
        colors.syntaxNumber    = D2D1::ColorF(0.475f, 0.898f, 0.537f, 1.0f); // #7EE787
        colors.syntaxOperator  = D2D1::ColorF(1.000f, 0.482f, 0.447f, 1.0f);
        colors.syntaxTag       = D2D1::ColorF(0.475f, 0.898f, 0.537f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.475f, 0.753f, 1.000f, 1.0f);
        colors.bracket1        = D2D1::ColorF(0.824f, 0.659f, 1.000f, 1.0f); // #D2A8FF
        colors.bracket2        = D2D1::ColorF(1.000f, 0.651f, 0.341f, 1.0f); // #FFA657
        colors.bracket3        = D2D1::ColorF(0.475f, 0.753f, 1.000f, 1.0f); // #79C0FF
    } else if (m_mode == ThemeMode::OneDark) {
        // One Dark Pro Theme
        colors.windowBg        = D2D1::ColorF(0.129f, 0.145f, 0.169f, 1.0f); // #21252B OneDark Frame
        colors.canvasBg        = D2D1::ColorF(0.157f, 0.173f, 0.204f, 1.0f); // #282C34
        colors.headerBg        = D2D1::ColorF(0.129f, 0.145f, 0.169f, 1.0f); // #21252B
        colors.railBg          = D2D1::ColorF(0.129f, 0.145f, 0.169f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.094f, 0.102f, 0.122f, 1.0f); // #181A1F
        colors.borderSubtle    = D2D1::ColorF(0.243f, 0.271f, 0.318f, 1.0f); // #3E4451
        colors.borderHover     = D2D1::ColorF(0.380f, 0.686f, 0.937f, 1.0f); // #61AFEF
        colors.textPrimary     = D2D1::ColorF(0.671f, 0.698f, 0.749f, 1.0f); // #ABB2BF
        colors.textSecondary   = D2D1::ColorF(0.533f, 0.569f, 0.627f, 1.0f); // #8891A0
        colors.textMuted       = D2D1::ColorF(0.361f, 0.388f, 0.439f, 1.0f); // #5C6370
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.180f, 0.200f, 0.235f, 1.0f); // #2E333C
        colors.itemActive      = D2D1::ColorF(0.243f, 0.271f, 0.318f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.173f, 0.310f, 0.443f, 0.85f); // #2C4F71
        colors.captionHover    = D2D1::ColorF(0.180f, 0.200f, 0.235f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.380f, 0.686f, 0.937f, 1.0f);
        colors.brandAccent     = D2D1::ColorF(0.380f, 0.686f, 0.937f, 1.0f); // #61AFEF
        colors.homeBadgeBg     = D2D1::ColorF(0.180f, 0.200f, 0.235f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.157f, 0.173f, 0.204f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.380f, 0.686f, 0.937f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.671f, 0.698f, 0.749f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.129f, 0.145f, 0.169f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.243f, 0.271f, 0.318f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.243f, 0.271f, 0.318f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.380f, 0.686f, 0.937f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.671f, 0.698f, 0.749f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.533f, 0.569f, 0.627f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.129f, 0.145f, 0.169f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.157f, 0.173f, 0.204f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.243f, 0.271f, 0.318f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.180f, 0.200f, 0.235f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.361f, 0.388f, 0.439f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.776f, 0.471f, 0.867f, 1.0f); // #C678DD
        colors.syntaxType      = D2D1::ColorF(0.898f, 0.753f, 0.482f, 1.0f); // #E5C07B
        colors.syntaxString    = D2D1::ColorF(0.596f, 0.765f, 0.475f, 1.0f); // #98C379
        colors.syntaxComment   = D2D1::ColorF(0.361f, 0.388f, 0.439f, 1.0f); // #5C6370
        colors.syntaxNumber    = D2D1::ColorF(0.820f, 0.584f, 0.420f, 1.0f); // #D19A66
        colors.syntaxOperator  = D2D1::ColorF(0.337f, 0.706f, 0.776f, 1.0f); // #56B6C2
        colors.syntaxTag       = D2D1::ColorF(0.878f, 0.424f, 0.459f, 1.0f); // #E06C75
        colors.syntaxAttribute = D2D1::ColorF(0.820f, 0.584f, 0.420f, 1.0f);
        colors.bracket1        = D2D1::ColorF(0.776f, 0.471f, 0.867f, 1.0f); // #C678DD
        colors.bracket2        = D2D1::ColorF(0.898f, 0.753f, 0.482f, 1.0f); // #E5C07B
        colors.bracket3        = D2D1::ColorF(0.380f, 0.686f, 0.937f, 1.0f); // #61AFEF
    } else if (m_mode == ThemeMode::SynthWave) {
        // SynthWave '84 Theme
        colors.windowBg        = D2D1::ColorF(0.204f, 0.125f, 0.314f, 1.0f); // #342050 SynthWave Frame
        colors.canvasBg        = D2D1::ColorF(0.149f, 0.137f, 0.208f, 1.0f); // #262335
        colors.headerBg        = D2D1::ColorF(0.204f, 0.125f, 0.314f, 1.0f); // #342050
        colors.railBg          = D2D1::ColorF(0.204f, 0.125f, 0.314f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.106f, 0.086f, 0.149f, 1.0f); // #1B1626
        colors.borderSubtle    = D2D1::ColorF(0.204f, 0.161f, 0.310f, 1.0f); // #34294F
        colors.borderHover     = D2D1::ColorF(1.000f, 0.494f, 0.859f, 1.0f); // #FF7EDB
        colors.textPrimary     = D2D1::ColorF(0.976f, 0.957f, 0.988f, 1.0f);
        colors.textSecondary   = D2D1::ColorF(0.761f, 0.706f, 0.859f, 1.0f);
        colors.textMuted       = D2D1::ColorF(0.518f, 0.545f, 0.596f, 1.0f);
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.188f, 0.145f, 0.259f, 1.0f);
        colors.itemActive      = D2D1::ColorF(0.235f, 0.176f, 0.329f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.400f, 0.125f, 0.450f, 0.85f);
        colors.captionHover    = D2D1::ColorF(0.188f, 0.145f, 0.259f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(1.000f, 0.494f, 0.859f, 1.0f);
        colors.brandAccent     = D2D1::ColorF(1.000f, 0.494f, 0.859f, 1.0f); // #FF7EDB
        colors.homeBadgeBg     = D2D1::ColorF(0.188f, 0.145f, 0.259f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.149f, 0.137f, 0.208f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(1.000f, 0.494f, 0.859f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(1.000f, 0.494f, 0.859f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.141f, 0.106f, 0.184f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.204f, 0.161f, 0.310f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.204f, 0.161f, 0.310f, 1.0f);
        colors.menuHover       = D2D1::ColorF(1.000f, 0.494f, 0.859f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.976f, 0.957f, 0.988f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.761f, 0.706f, 0.859f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.141f, 0.106f, 0.184f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.149f, 0.137f, 0.208f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.204f, 0.161f, 0.310f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.188f, 0.145f, 0.259f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.518f, 0.545f, 0.596f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.996f, 0.871f, 0.365f, 1.0f); // #FEDE5D Neon Gold
        colors.syntaxType      = D2D1::ColorF(0.212f, 0.976f, 0.965f, 1.0f); // #36F9F6 Neon Cyan
        colors.syntaxString    = D2D1::ColorF(1.000f, 0.494f, 0.859f, 1.0f); // #FF7EDB Neon Pink
        colors.syntaxComment   = D2D1::ColorF(0.518f, 0.545f, 0.596f, 1.0f); // #848B98
        colors.syntaxNumber    = D2D1::ColorF(0.996f, 0.267f, 0.314f, 1.0f); // #FE4450 Red
        colors.syntaxOperator  = D2D1::ColorF(0.976f, 0.165f, 0.678f, 1.0f); // #F92AAD
        colors.syntaxTag       = D2D1::ColorF(0.976f, 0.165f, 0.678f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.212f, 0.976f, 0.965f, 1.0f);
        colors.bracket1        = D2D1::ColorF(1.000f, 0.494f, 0.859f, 1.0f); // #FF7EDB
        colors.bracket2        = D2D1::ColorF(0.996f, 0.871f, 0.365f, 1.0f); // #FEDE5D
        colors.bracket3        = D2D1::ColorF(0.212f, 0.976f, 0.965f, 1.0f); // #36F9F6
    } else if (m_mode == ThemeMode::RetroGrid80s) {
        // Synthwave Neon 3D Grid Theme (Image 5)
        colors.windowBg        = D2D1::ColorF(0.165f, 0.051f, 0.282f, 1.0f); // #2A0D48 80s Neon Purple Frame
        colors.canvasBg        = D2D1::ColorF(0.071f, 0.027f, 0.133f, 1.0f); // #120722
        colors.headerBg        = D2D1::ColorF(0.165f, 0.051f, 0.282f, 1.0f); // #2A0D48
        colors.railBg          = D2D1::ColorF(0.165f, 0.051f, 0.282f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.055f, 0.020f, 0.106f, 1.0f); // #0E051B
        colors.borderSubtle    = D2D1::ColorF(0.227f, 0.067f, 0.345f, 1.0f); // #3A1158
        colors.borderHover     = D2D1::ColorF(1.000f, 0.165f, 0.522f, 1.0f); // #FF2A85
        colors.textPrimary     = D2D1::ColorF(0.996f, 0.906f, 0.996f, 1.0f); // #FEE7FE
        colors.textSecondary   = D2D1::ColorF(0.969f, 0.447f, 0.851f, 1.0f); // #F772D9
        colors.textMuted       = D2D1::ColorF(0.518f, 0.263f, 0.639f, 1.0f); // #8443A3
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.180f, 0.060f, 0.280f, 1.0f);
        colors.itemActive      = D2D1::ColorF(0.250f, 0.080f, 0.380f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.482f, 0.173f, 0.749f, 0.85f);
        colors.captionHover    = D2D1::ColorF(0.180f, 0.060f, 0.280f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(1.000f, 0.165f, 0.522f, 1.0f); // #FF2A85
        colors.brandAccent     = D2D1::ColorF(1.000f, 0.165f, 0.522f, 1.0f); // #FF2A85
        colors.homeBadgeBg     = D2D1::ColorF(0.180f, 0.060f, 0.280f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.071f, 0.027f, 0.133f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(1.000f, 0.165f, 0.522f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.996f, 0.906f, 0.996f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.055f, 0.020f, 0.106f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.227f, 0.067f, 0.345f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.227f, 0.067f, 0.345f, 1.0f);
        colors.menuHover       = D2D1::ColorF(1.000f, 0.165f, 0.522f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.996f, 0.906f, 0.996f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.969f, 0.447f, 0.851f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.055f, 0.020f, 0.106f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.071f, 0.027f, 0.133f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.227f, 0.067f, 0.345f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.141f, 0.050f, 0.220f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.518f, 0.263f, 0.639f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(1.000f, 0.165f, 0.522f, 1.0f); // #FF2A85 Hot Pink
        colors.syntaxType      = D2D1::ColorF(0.000f, 0.941f, 1.000f, 1.0f); // #00F0FF Electric Cyan
        colors.syntaxString    = D2D1::ColorF(1.000f, 0.902f, 0.000f, 1.0f); // #FFE600 Neon Yellow
        colors.syntaxComment   = D2D1::ColorF(0.518f, 0.263f, 0.639f, 1.0f); // #8443A3
        colors.syntaxNumber    = D2D1::ColorF(0.000f, 1.000f, 0.533f, 1.0f); // #00FF88 Cyber Lime
        colors.syntaxOperator  = D2D1::ColorF(0.839f, 0.000f, 1.000f, 1.0f); // #D600FF Vivid Magenta
        colors.syntaxTag       = D2D1::ColorF(1.000f, 0.165f, 0.522f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.000f, 0.941f, 1.000f, 1.0f);
        colors.bracket1        = D2D1::ColorF(1.000f, 0.165f, 0.522f, 1.0f); // #FF2A85 Hot Pink
        colors.bracket2        = D2D1::ColorF(1.000f, 0.902f, 0.000f, 1.0f); // #FFE600 Neon Yellow
        colors.bracket3        = D2D1::ColorF(0.000f, 0.941f, 1.000f, 1.0f); // #00F0FF Cyan
    } else if (m_mode == ThemeMode::CosmicStarfield) {
        // Cosmic Starfield Particle Theme
        colors.windowBg        = D2D1::ColorF(0.055f, 0.063f, 0.125f, 1.0f); // #0E1020
        colors.canvasBg        = D2D1::ColorF(0.035f, 0.039f, 0.078f, 1.0f); // #090A14
        colors.headerBg        = D2D1::ColorF(0.055f, 0.063f, 0.125f, 1.0f); // #0E1020
        colors.railBg          = D2D1::ColorF(0.055f, 0.063f, 0.125f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.027f, 0.031f, 0.063f, 1.0f); // #070810
        colors.borderSubtle    = D2D1::ColorF(0.118f, 0.133f, 0.251f, 1.0f); // #1E2240
        colors.borderHover     = D2D1::ColorF(0.478f, 0.635f, 0.969f, 1.0f); // #7AA2F7
        colors.textPrimary     = D2D1::ColorF(0.886f, 0.910f, 0.941f, 1.0f); // #E2E8F0
        colors.textSecondary   = D2D1::ColorF(0.580f, 0.639f, 0.722f, 1.0f); // #94A3B8
        colors.textMuted       = D2D1::ColorF(0.278f, 0.333f, 0.412f, 1.0f); // #475569
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.090f, 0.102f, 0.200f, 1.0f);
        colors.itemActive      = D2D1::ColorF(0.130f, 0.145f, 0.270f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.545f, 0.361f, 0.965f, 0.85f); // #8B5CF6
        colors.captionHover    = D2D1::ColorF(0.090f, 0.102f, 0.200f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.655f, 0.545f, 0.980f, 1.0f); // #A78BFA
        colors.brandAccent     = D2D1::ColorF(0.545f, 0.361f, 0.965f, 1.0f); // #8B5CF6
        colors.homeBadgeBg     = D2D1::ColorF(0.090f, 0.102f, 0.200f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.035f, 0.039f, 0.078f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.545f, 0.361f, 0.965f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.886f, 0.910f, 0.941f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.027f, 0.031f, 0.063f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.118f, 0.133f, 0.251f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.118f, 0.133f, 0.251f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.545f, 0.361f, 0.965f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.886f, 0.910f, 0.941f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.580f, 0.639f, 0.722f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.027f, 0.031f, 0.063f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.035f, 0.039f, 0.078f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.118f, 0.133f, 0.251f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.070f, 0.080f, 0.150f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.278f, 0.333f, 0.412f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.655f, 0.545f, 0.980f, 1.0f); // #A78BFA
        colors.syntaxType      = D2D1::ColorF(0.220f, 0.741f, 0.973f, 1.0f); // #38BDF8
        colors.syntaxString    = D2D1::ColorF(0.992f, 0.878f, 0.278f, 1.0f); // #FDE047
        colors.syntaxComment   = D2D1::ColorF(0.278f, 0.333f, 0.412f, 1.0f); // #475569
        colors.syntaxNumber    = D2D1::ColorF(0.957f, 0.447f, 0.714f, 1.0f); // #F472B6
        colors.syntaxOperator  = D2D1::ColorF(0.753f, 0.518f, 0.988f, 1.0f);
        colors.syntaxTag       = D2D1::ColorF(0.655f, 0.545f, 0.980f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.220f, 0.741f, 0.973f, 1.0f);
        colors.bracket1        = D2D1::ColorF(0.753f, 0.518f, 0.988f, 1.0f); // #C084FC
        colors.bracket2        = D2D1::ColorF(0.992f, 0.878f, 0.278f, 1.0f); // #FDE047
        colors.bracket3        = D2D1::ColorF(0.220f, 0.741f, 0.973f, 1.0f); // #38BDF8
    } else if (m_mode == ThemeMode::MatrixDigitalRain) {
        // Matrix Green Terminal Particle Theme
        colors.windowBg        = D2D1::ColorF(0.024f, 0.071f, 0.031f, 1.0f); // #061208
        colors.canvasBg        = D2D1::ColorF(0.012f, 0.031f, 0.016f, 1.0f); // #030804
        colors.headerBg        = D2D1::ColorF(0.024f, 0.071f, 0.031f, 1.0f); // #061208
        colors.railBg          = D2D1::ColorF(0.024f, 0.071f, 0.031f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.008f, 0.020f, 0.012f, 1.0f); // #020503
        colors.borderSubtle    = D2D1::ColorF(0.051f, 0.165f, 0.071f, 1.0f); // #0D2A12
        colors.borderHover     = D2D1::ColorF(0.000f, 1.000f, 0.400f, 1.0f); // #00FF66
        colors.textPrimary     = D2D1::ColorF(0.800f, 1.000f, 0.867f, 1.0f); // #CCFFDD
        colors.textSecondary   = D2D1::ColorF(0.333f, 0.867f, 0.467f, 1.0f); // #55DD77
        colors.textMuted       = D2D1::ColorF(0.106f, 0.369f, 0.157f, 1.0f); // #1B5E28
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.039f, 0.118f, 0.051f, 1.0f);
        colors.itemActive      = D2D1::ColorF(0.059f, 0.176f, 0.078f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.000f, 0.700f, 0.280f, 0.85f);
        colors.captionHover    = D2D1::ColorF(0.039f, 0.118f, 0.051f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.000f, 1.000f, 0.400f, 1.0f); // #00FF66
        colors.brandAccent     = D2D1::ColorF(0.000f, 1.000f, 0.400f, 1.0f); // #00FF66
        colors.homeBadgeBg     = D2D1::ColorF(0.039f, 0.118f, 0.051f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.012f, 0.031f, 0.016f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.000f, 1.000f, 0.400f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.800f, 1.000f, 0.867f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.008f, 0.020f, 0.012f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.051f, 0.165f, 0.071f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.051f, 0.165f, 0.071f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.000f, 1.000f, 0.400f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.800f, 1.000f, 0.867f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.333f, 0.867f, 0.467f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.008f, 0.020f, 0.012f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.012f, 0.031f, 0.016f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.051f, 0.165f, 0.071f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.024f, 0.071f, 0.031f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.106f, 0.369f, 0.157f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.000f, 1.000f, 0.400f, 1.0f); // #00FF66
        colors.syntaxType      = D2D1::ColorF(0.200f, 1.000f, 0.733f, 1.0f); // #33FFBB
        colors.syntaxString    = D2D1::ColorF(0.533f, 1.000f, 0.667f, 1.0f); // #88FFAA
        colors.syntaxComment   = D2D1::ColorF(0.106f, 0.369f, 0.157f, 1.0f); // #1B5E28
        colors.syntaxNumber    = D2D1::ColorF(0.400f, 1.000f, 0.667f, 1.0f); // #66FFAA
        colors.syntaxOperator  = D2D1::ColorF(0.000f, 1.000f, 0.400f, 1.0f);
        colors.syntaxTag       = D2D1::ColorF(0.000f, 1.000f, 0.400f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.200f, 1.000f, 0.733f, 1.0f);
        colors.bracket1        = D2D1::ColorF(0.000f, 1.000f, 0.400f, 1.0f); // #00FF66
        colors.bracket2        = D2D1::ColorF(0.400f, 1.000f, 0.667f, 1.0f); // #66FFAA
        colors.bracket3        = D2D1::ColorF(0.200f, 1.000f, 0.800f, 1.0f); // #33FFCC
    } else if (m_mode == ThemeMode::AuroraBorealis) {
        // Aurora Borealis Polar Teal Theme
        colors.windowBg        = D2D1::ColorF(0.051f, 0.118f, 0.157f, 1.0f); // #0D1E28
        colors.canvasBg        = D2D1::ColorF(0.031f, 0.075f, 0.102f, 1.0f); // #08131A
        colors.headerBg        = D2D1::ColorF(0.051f, 0.118f, 0.157f, 1.0f); // #0D1E28
        colors.railBg          = D2D1::ColorF(0.051f, 0.118f, 0.157f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.024f, 0.059f, 0.078f, 1.0f); // #060F14
        colors.borderSubtle    = D2D1::ColorF(0.078f, 0.196f, 0.259f, 1.0f); // #143242
        colors.borderHover     = D2D1::ColorF(0.176f, 0.831f, 0.749f, 1.0f); // #2DD4BF
        colors.textPrimary     = D2D1::ColorF(0.902f, 1.000f, 0.980f, 1.0f); // #E6FFFA
        colors.textSecondary   = D2D1::ColorF(0.369f, 0.918f, 0.831f, 1.0f); // #5EEAD4
        colors.textMuted       = D2D1::ColorF(0.180f, 0.322f, 0.400f, 1.0f); // #2E5266
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.078f, 0.180f, 0.239f, 1.0f);
        colors.itemActive      = D2D1::ColorF(0.106f, 0.243f, 0.322f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.176f, 0.831f, 0.749f, 0.85f);
        colors.captionHover    = D2D1::ColorF(0.078f, 0.180f, 0.239f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.176f, 0.831f, 0.749f, 1.0f);
        colors.brandAccent     = D2D1::ColorF(0.176f, 0.831f, 0.749f, 1.0f);
        colors.homeBadgeBg     = D2D1::ColorF(0.078f, 0.180f, 0.239f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.031f, 0.075f, 0.102f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.176f, 0.831f, 0.749f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.902f, 1.000f, 0.980f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.024f, 0.059f, 0.078f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.078f, 0.196f, 0.259f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.078f, 0.196f, 0.259f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.176f, 0.831f, 0.749f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.902f, 1.000f, 0.980f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.369f, 0.918f, 0.831f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.024f, 0.059f, 0.078f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.031f, 0.075f, 0.102f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.078f, 0.196f, 0.259f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.051f, 0.118f, 0.157f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.180f, 0.322f, 0.400f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.176f, 0.831f, 0.749f, 1.0f); // #2DD4BF
        colors.syntaxType      = D2D1::ColorF(0.220f, 0.741f, 0.973f, 1.0f); // #38BDF8
        colors.syntaxString    = D2D1::ColorF(0.655f, 0.953f, 0.816f, 1.0f); // #A7F3D0
        colors.syntaxComment   = D2D1::ColorF(0.180f, 0.322f, 0.400f, 1.0f); // #2E5266
        colors.syntaxNumber    = D2D1::ColorF(0.984f, 0.749f, 0.141f, 1.0f); // #FBBF24
        colors.syntaxOperator  = D2D1::ColorF(0.369f, 0.918f, 0.831f, 1.0f);
        colors.syntaxTag       = D2D1::ColorF(0.176f, 0.831f, 0.749f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.220f, 0.741f, 0.973f, 1.0f);
        colors.bracket1        = D2D1::ColorF(0.176f, 0.831f, 0.749f, 1.0f); // #2DD4BF
        colors.bracket2        = D2D1::ColorF(0.220f, 0.741f, 0.973f, 1.0f); // #38BDF8
        colors.bracket3        = D2D1::ColorF(0.655f, 0.953f, 0.816f, 1.0f); // #A7F3D0
    } else if (m_mode == ThemeMode::SunsetGlow) {
        // Sunset Glow Dusk Theme
        colors.windowBg        = D2D1::ColorF(0.118f, 0.082f, 0.153f, 1.0f); // #1E1527
        colors.canvasBg        = D2D1::ColorF(0.078f, 0.063f, 0.102f, 1.0f); // #14101A
        colors.headerBg        = D2D1::ColorF(0.118f, 0.082f, 0.153f, 1.0f); // #1E1527
        colors.railBg          = D2D1::ColorF(0.118f, 0.082f, 0.153f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.059f, 0.047f, 0.078f, 1.0f); // #0F0C14
        colors.borderSubtle    = D2D1::ColorF(0.200f, 0.133f, 0.267f, 1.0f); // #332244
        colors.borderHover     = D2D1::ColorF(1.000f, 0.459f, 0.337f, 1.0f); // #FF7556
        colors.textPrimary     = D2D1::ColorF(1.000f, 0.945f, 0.949f, 1.0f); // #FFF1F2
        colors.textSecondary   = D2D1::ColorF(0.992f, 0.643f, 0.686f, 1.0f); // #FDA4AF
        colors.textMuted       = D2D1::ColorF(0.420f, 0.302f, 0.431f, 1.0f); // #6B4D6E
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.176f, 0.118f, 0.227f, 1.0f);
        colors.itemActive      = D2D1::ColorF(0.235f, 0.157f, 0.302f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(1.000f, 0.369f, 0.494f, 0.85f); // #FF5E7E
        colors.captionHover    = D2D1::ColorF(0.176f, 0.118f, 0.227f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(1.000f, 0.369f, 0.494f, 1.0f);
        colors.brandAccent     = D2D1::ColorF(1.000f, 0.369f, 0.494f, 1.0f);
        colors.homeBadgeBg     = D2D1::ColorF(0.176f, 0.118f, 0.227f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.078f, 0.063f, 0.102f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(1.000f, 0.369f, 0.494f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(1.000f, 0.945f, 0.949f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.059f, 0.047f, 0.078f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.200f, 0.133f, 0.267f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.200f, 0.133f, 0.267f, 1.0f);
        colors.menuHover       = D2D1::ColorF(1.000f, 0.369f, 0.494f, 1.0f);
        colors.menuText        = D2D1::ColorF(1.000f, 0.945f, 0.949f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.992f, 0.643f, 0.686f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.059f, 0.047f, 0.078f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.078f, 0.063f, 0.102f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.200f, 0.133f, 0.267f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.118f, 0.082f, 0.153f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.420f, 0.302f, 0.431f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(1.000f, 0.369f, 0.494f, 1.0f); // #FF5E7E
        colors.syntaxType      = D2D1::ColorF(1.000f, 0.620f, 0.392f, 1.0f); // #FF9E64
        colors.syntaxString    = D2D1::ColorF(0.988f, 0.827f, 0.302f, 1.0f); // #FCD34D
        colors.syntaxComment   = D2D1::ColorF(0.420f, 0.302f, 0.431f, 1.0f); // #6B4D6E
        colors.syntaxNumber    = D2D1::ColorF(0.655f, 0.545f, 0.980f, 1.0f); // #A78BFA
        colors.syntaxOperator  = D2D1::ColorF(1.000f, 0.620f, 0.392f, 1.0f);
        colors.syntaxTag       = D2D1::ColorF(1.000f, 0.369f, 0.494f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(1.000f, 0.620f, 0.392f, 1.0f);
        colors.bracket1        = D2D1::ColorF(1.000f, 0.369f, 0.494f, 1.0f); // #FF5E7E
        colors.bracket2        = D2D1::ColorF(0.988f, 0.827f, 0.302f, 1.0f); // #FCD34D
        colors.bracket3        = D2D1::ColorF(1.000f, 0.620f, 0.392f, 1.0f); // #FF9E64
    } else if (m_mode == ThemeMode::EmeraldForest) {
        // Emerald Forest Jungle Theme
        colors.windowBg        = D2D1::ColorF(0.051f, 0.125f, 0.090f, 1.0f); // #0D2017
        colors.canvasBg        = D2D1::ColorF(0.031f, 0.078f, 0.055f, 1.0f); // #08140E
        colors.headerBg        = D2D1::ColorF(0.051f, 0.125f, 0.090f, 1.0f); // #0D2017
        colors.railBg          = D2D1::ColorF(0.051f, 0.125f, 0.090f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.024f, 0.059f, 0.039f, 1.0f); // #060F0A
        colors.borderSubtle    = D2D1::ColorF(0.086f, 0.220f, 0.157f, 1.0f); // #163828
        colors.borderHover     = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f); // #10B981
        colors.textPrimary     = D2D1::ColorF(0.925f, 0.992f, 0.961f, 1.0f); // #ECFDF5
        colors.textSecondary   = D2D1::ColorF(0.431f, 0.906f, 0.718f, 1.0f); // #6EE7B7
        colors.textMuted       = D2D1::ColorF(0.145f, 0.322f, 0.227f, 1.0f); // #25523A
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.078f, 0.180f, 0.129f, 1.0f);
        colors.itemActive      = D2D1::ColorF(0.106f, 0.243f, 0.176f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.063f, 0.725f, 0.506f, 0.85f);
        colors.captionHover    = D2D1::ColorF(0.078f, 0.180f, 0.129f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        colors.brandAccent     = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        colors.homeBadgeBg     = D2D1::ColorF(0.078f, 0.180f, 0.129f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.031f, 0.078f, 0.055f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.063f, 0.725f, 0.506f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.925f, 0.992f, 0.961f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.024f, 0.059f, 0.039f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.086f, 0.220f, 0.157f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.086f, 0.220f, 0.157f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.925f, 0.992f, 0.961f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.431f, 0.906f, 0.718f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.024f, 0.059f, 0.039f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.031f, 0.078f, 0.055f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.086f, 0.220f, 0.157f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.051f, 0.125f, 0.090f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.145f, 0.322f, 0.227f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.204f, 0.827f, 0.600f, 1.0f); // #34D399
        colors.syntaxType      = D2D1::ColorF(0.431f, 0.906f, 0.718f, 1.0f); // #6EE7B7
        colors.syntaxString    = D2D1::ColorF(0.639f, 0.902f, 0.208f, 1.0f); // #A3E635
        colors.syntaxComment   = D2D1::ColorF(0.145f, 0.322f, 0.227f, 1.0f); // #25523A
        colors.syntaxNumber    = D2D1::ColorF(0.984f, 0.749f, 0.141f, 1.0f); // #FBBF24
        colors.syntaxOperator  = D2D1::ColorF(0.204f, 0.827f, 0.600f, 1.0f);
        colors.syntaxTag       = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.431f, 0.906f, 0.718f, 1.0f);
        colors.bracket1        = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f); // #10B981
        colors.bracket2        = D2D1::ColorF(0.639f, 0.902f, 0.208f, 1.0f); // #A3E635
        colors.bracket3        = D2D1::ColorF(0.204f, 0.827f, 0.600f, 1.0f); // #34D399
    } else if (m_mode == ThemeMode::RosePine) {
        // Rosé Pine Chic Velvet Theme
        colors.windowBg        = D2D1::ColorF(0.122f, 0.114f, 0.180f, 1.0f); // #1F1D2E
        colors.canvasBg        = D2D1::ColorF(0.098f, 0.090f, 0.141f, 1.0f); // #191724
        colors.headerBg        = D2D1::ColorF(0.122f, 0.114f, 0.180f, 1.0f); // #1F1D2E
        colors.railBg          = D2D1::ColorF(0.122f, 0.114f, 0.180f, 1.0f);
        colors.drawerBg        = D2D1::ColorF(0.082f, 0.075f, 0.118f, 1.0f); // #15131E
        colors.borderSubtle    = D2D1::ColorF(0.149f, 0.137f, 0.227f, 1.0f); // #26233A
        colors.borderHover     = D2D1::ColorF(0.922f, 0.435f, 0.573f, 1.0f); // #EB6F92
        colors.textPrimary     = D2D1::ColorF(0.878f, 0.871f, 0.957f, 1.0f); // #E0DEF4
        colors.textSecondary   = D2D1::ColorF(0.565f, 0.549f, 0.667f, 1.0f); // #908CAA
        colors.textMuted       = D2D1::ColorF(0.431f, 0.416f, 0.525f, 1.0f); // #6E6A86
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.165f, 0.149f, 0.243f, 1.0f);
        colors.itemActive      = D2D1::ColorF(0.208f, 0.188f, 0.302f, 1.0f);
        colors.treeSelected    = D2D1::ColorF(0.922f, 0.435f, 0.573f, 0.85f);
        colors.captionHover    = D2D1::ColorF(0.165f, 0.149f, 0.243f, 1.0f);
        colors.headerIcon      = D2D1::ColorF(0.922f, 0.435f, 0.573f, 1.0f);
        colors.brandAccent     = D2D1::ColorF(0.922f, 0.435f, 0.573f, 1.0f);
        colors.homeBadgeBg     = D2D1::ColorF(0.165f, 0.149f, 0.243f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(0.098f, 0.090f, 0.141f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.922f, 0.435f, 0.573f, 0.80f);
        colors.tooltipText     = D2D1::ColorF(0.878f, 0.871f, 0.957f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.082f, 0.075f, 0.118f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.149f, 0.137f, 0.227f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.149f, 0.137f, 0.227f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.922f, 0.435f, 0.573f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.878f, 0.871f, 0.957f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.565f, 0.549f, 0.667f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.082f, 0.075f, 0.118f, 1.0f);
        colors.tabActiveBg     = D2D1::ColorF(0.098f, 0.090f, 0.141f, 1.0f);
        colors.tabBorder       = D2D1::ColorF(0.149f, 0.137f, 0.227f, 1.0f);
        colors.currentLineBg   = D2D1::ColorF(0.122f, 0.114f, 0.180f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.431f, 0.416f, 0.525f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.192f, 0.455f, 0.561f, 1.0f); // #31748F Pine Foam
        colors.syntaxType      = D2D1::ColorF(0.612f, 0.808f, 0.847f, 1.0f); // #9CCFD8 Teal Ice
        colors.syntaxString    = D2D1::ColorF(0.965f, 0.757f, 0.467f, 1.0f); // #F6C177 Gold Sand
        colors.syntaxComment   = D2D1::ColorF(0.431f, 0.416f, 0.525f, 1.0f); // #6E6A86
        colors.syntaxNumber    = D2D1::ColorF(0.922f, 0.737f, 0.729f, 1.0f); // #EBBCBA
        colors.syntaxOperator  = D2D1::ColorF(0.769f, 0.655f, 0.906f, 1.0f); // #C4A7E7
        colors.syntaxTag       = D2D1::ColorF(0.922f, 0.435f, 0.573f, 1.0f);
        colors.syntaxAttribute = D2D1::ColorF(0.612f, 0.808f, 0.847f, 1.0f);
        colors.bracket1        = D2D1::ColorF(0.922f, 0.435f, 0.573f, 1.0f); // #EB6F92
        colors.bracket2        = D2D1::ColorF(0.965f, 0.757f, 0.467f, 1.0f); // #F6C177
        colors.bracket3        = D2D1::ColorF(0.769f, 0.655f, 0.906f, 1.0f); // #C4A7E7
    } else {
        // Default Dark Theme Colors
        colors.windowBg        = D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f);   // #3D1461
        colors.canvasBg        = D2D1::ColorF(0.118f, 0.118f, 0.118f, 1.0f); // #1E1E1E
        colors.headerBg        = D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f);   // #3D1461
        colors.railBg          = D2D1::ColorF(0.24f, 0.08f, 0.38f, 1.0f);   // #3D1461
        colors.drawerBg        = D2D1::ColorF(0.094f, 0.094f, 0.094f, 1.0f); // #181818
        colors.borderSubtle    = D2D1::ColorF(0.173f, 0.173f, 0.173f, 1.0f); // #2C2C2C
        colors.borderHover     = D2D1::ColorF(0.243f, 0.243f, 0.243f, 1.0f); // #3E3E3E
        colors.textPrimary     = D2D1::ColorF(0.85f, 0.85f, 0.85f, 1.0f);    // #D8D8D8
        colors.textSecondary   = D2D1::ColorF(0.58f, 0.58f, 0.58f, 1.0f);    // #949494
        colors.textMuted       = D2D1::ColorF(0.40f, 0.40f, 0.40f, 1.0f);    // #666666
        colors.textWhite       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
        colors.itemHover       = D2D1::ColorF(0.165f, 0.176f, 0.180f, 1.0f); // #2A2D2E
        colors.itemActive      = D2D1::ColorF(0.216f, 0.216f, 0.239f, 1.0f); // #37373D
        colors.treeSelected    = D2D1::ColorF(0.016f, 0.380f, 0.647f, 0.85f); // #0461A5
        colors.captionHover    = D2D1::ColorF(0.200f, 0.200f, 0.200f, 1.0f); // #333333
        colors.headerIcon      = D2D1::ColorF(0.82f, 0.82f, 0.86f, 1.0f);   // #D1D1DB Bright silver
        colors.brandAccent     = D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f);   // #0078D4
        colors.homeBadgeBg     = D2D1::ColorF(0.165f, 0.176f, 0.180f, 1.0f);
        colors.tooltipBg       = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f);
        colors.tooltipBorder   = D2D1::ColorF(0.78f, 0.78f, 0.82f, 1.0f);
        colors.tooltipText     = D2D1::ColorF(0.08f, 0.08f, 0.08f, 1.0f);
        colors.menuBg          = D2D1::ColorF(0.122f, 0.122f, 0.122f, 0.98f);
        colors.menuBorder      = D2D1::ColorF(0.271f, 0.271f, 0.271f, 1.0f);
        colors.menuSeparator   = D2D1::ColorF(0.235f, 0.235f, 0.235f, 1.0f);
        colors.menuHover       = D2D1::ColorF(0.0f, 0.471f, 0.831f, 1.0f);
        colors.menuText        = D2D1::ColorF(0.800f, 0.800f, 0.800f, 1.0f);
        colors.menuShortcut    = D2D1::ColorF(0.550f, 0.550f, 0.550f, 1.0f);
        colors.tabBarBg        = D2D1::ColorF(0.094f, 0.094f, 0.094f, 1.0f); // #181818
        colors.tabActiveBg     = D2D1::ColorF(0.118f, 0.118f, 0.118f, 1.0f); // #1E1E1E
        colors.tabBorder       = D2D1::ColorF(0.173f, 0.173f, 0.173f, 1.0f); // #2C2C2C
        colors.currentLineBg   = D2D1::ColorF(0.145f, 0.145f, 0.145f, 1.0f);
        colors.lineNumber      = D2D1::ColorF(0.522f, 0.522f, 0.522f, 1.0f);
        colors.syntaxKeyword   = D2D1::ColorF(0.773f, 0.525f, 0.753f, 1.0f);  // #C586C0
        colors.syntaxType      = D2D1::ColorF(0.306f, 0.788f, 0.690f, 1.0f);  // #4EC9B0
        colors.syntaxString    = D2D1::ColorF(0.808f, 0.569f, 0.471f, 1.0f);  // #CE9178
        colors.syntaxComment   = D2D1::ColorF(0.416f, 0.600f, 0.333f, 1.0f);  // #6A9955
        colors.syntaxNumber    = D2D1::ColorF(0.710f, 0.808f, 0.659f, 1.0f);  // #B5CEA8
        colors.syntaxOperator  = D2D1::ColorF(0.831f, 0.831f, 0.831f, 1.0f);  // #D4D4D4
        colors.syntaxTag       = D2D1::ColorF(0.337f, 0.612f, 0.839f, 1.0f);  // #569CD6
        colors.syntaxAttribute = D2D1::ColorF(0.612f, 0.863f, 0.996f, 1.0f);  // #9CDCFE
        colors.bracket1        = D2D1::ColorF(0.776f, 0.471f, 0.867f, 1.0f);  // #C678DD Purple
        colors.bracket2        = D2D1::ColorF(1.000f, 0.843f, 0.000f, 1.0f);  // #FFD700 Yellow
        colors.bracket3        = D2D1::ColorF(0.000f, 0.898f, 1.000f, 1.0f);  // #00E5FF Cyan
    }

    // Refresh dynamic brush colors
    if (brushWindowBg) brushWindowBg->SetColor(colors.windowBg);
    if (brushCanvasBg) brushCanvasBg->SetColor(colors.canvasBg);
    if (brushHeaderBg) brushHeaderBg->SetColor(colors.headerBg);
    if (brushRailBg) brushRailBg->SetColor(colors.railBg);
    if (brushDrawerBg) brushDrawerBg->SetColor(colors.drawerBg);
    if (brushBorderSubtle) brushBorderSubtle->SetColor(colors.borderSubtle);
    if (brushBorderHover) brushBorderHover->SetColor(colors.borderHover);
    if (brushTextPrimary) brushTextPrimary->SetColor(colors.textPrimary);
    if (brushTextSecondary) brushTextSecondary->SetColor(colors.textSecondary);
    if (brushTextMuted) brushTextMuted->SetColor(colors.textMuted);
    if (brushTextWhite) brushTextWhite->SetColor(colors.textWhite);
    if (brushItemHover) brushItemHover->SetColor(colors.itemHover);
    if (brushItemActive) brushItemActive->SetColor(colors.itemActive);
    if (brushTreeSelected) brushTreeSelected->SetColor(colors.treeSelected);
    if (brushCaptionHover) brushCaptionHover->SetColor(colors.captionHover);
    if (brushHeaderIcon) brushHeaderIcon->SetColor(colors.headerIcon);
    if (brushBrandAccent) brushBrandAccent->SetColor(colors.brandAccent);
    if (brushHomeBadgeBg) brushHomeBadgeBg->SetColor(colors.homeBadgeBg);
    if (brushTooltipBg) brushTooltipBg->SetColor(colors.tooltipBg);
    if (brushTooltipBorder) brushTooltipBorder->SetColor(colors.tooltipBorder);
    if (brushTooltipText) brushTooltipText->SetColor(colors.tooltipText);
    if (brushMenuBg) brushMenuBg->SetColor(colors.menuBg);
    if (brushMenuBorder) brushMenuBorder->SetColor(colors.menuBorder);
    if (brushMenuSeparator) brushMenuSeparator->SetColor(colors.menuSeparator);
    if (brushMenuHover) brushMenuHover->SetColor(colors.menuHover);
    if (brushMenuText) brushMenuText->SetColor(colors.menuText);
    if (brushMenuShortcut) brushMenuShortcut->SetColor(colors.menuShortcut);
    if (brushTabBarBg) brushTabBarBg->SetColor(colors.tabBarBg);
    if (brushTabActiveBg) brushTabActiveBg->SetColor(colors.tabActiveBg);
    if (brushTabBorder) brushTabBorder->SetColor(colors.tabBorder);
    if (brushCurrentLineBg) brushCurrentLineBg->SetColor(colors.currentLineBg);
    if (brushLineNumber) brushLineNumber->SetColor(colors.lineNumber);
    if (brushSyntaxKeyword) brushSyntaxKeyword->SetColor(colors.syntaxKeyword);
    if (brushSyntaxType) brushSyntaxType->SetColor(colors.syntaxType);
    if (brushSyntaxString) brushSyntaxString->SetColor(colors.syntaxString);
    if (brushSyntaxComment) brushSyntaxComment->SetColor(colors.syntaxComment);
    if (brushSyntaxNumber) brushSyntaxNumber->SetColor(colors.syntaxNumber);
    if (brushSyntaxOperator) brushSyntaxOperator->SetColor(colors.syntaxOperator);
    if (brushSyntaxTag) brushSyntaxTag->SetColor(colors.syntaxTag);
    if (brushSyntaxAttribute) brushSyntaxAttribute->SetColor(colors.syntaxAttribute);
    if (brushBracket1) brushBracket1->SetColor(colors.bracket1);
    if (brushBracket2) brushBracket2->SetColor(colors.bracket2);
    if (brushBracket3) brushBracket3->SetColor(colors.bracket3);
}

Theme::SetiIconInfo Theme::GetSetiIcon(const std::wstring& filename, bool isFolder, bool isExpanded) const {
    if (isFolder) {
        return { 0xE032, isExpanded ? (brushSetiFolderOpen ? brushSetiFolderOpen.Get() : brushIconFolderOpen.Get())
                                     : (brushSetiFolder ? brushSetiFolder.Get() : brushIconFolder.Get()) };
    }

    std::wstring lower = filename;
    for (auto& c : lower) c = towlower(c);

    // Untitled or new buffer
    if (lower.rfind(L"untitled", 0) == 0) {
        return { 0xE023, brushSetiDefault ? brushSetiDefault.Get() : brushIconGray.Get() };
    }

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
        if (ext == L".sql") return { 0xE022, brushIconOrange ? brushIconOrange.Get() : brushIconBlue.Get() };
        if (ext == L".zip" || ext == L".tar" || ext == L".gz" || ext == L".7z" || ext == L".rar") return { 0xE0A8, brushSetiJson ? brushSetiJson.Get() : brushIconYellow.Get() };
        if (ext == L".lock") return { 0xE05D, brushSetiDefault ? brushSetiDefault.Get() : brushIconGray.Get() };
        if (ext == L".txt" || ext == L".log" || ext == L".ini" || ext == L".conf") return { 0xE023, brushSetiDefault ? brushSetiDefault.Get() : brushIconGray.Get() };
    }

    return { 0xE023, brushSetiDefault ? brushSetiDefault.Get() : brushIconGray.Get() };
}

} // namespace phide::ui
