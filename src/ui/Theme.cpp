/**
 * @file Theme.cpp
 * @brief Implementation of Dark Theme GPU brush allocation, typography, and tooltip formats.
 */

#include "phide/ui/Theme.hpp"

namespace phide::ui {

Theme::Theme() = default;

HRESULT Theme::CreateDeviceResources(ID2D1RenderTarget* pRT, IDWriteFactory* pDWriteFactory) {
    if (!pRT || !pDWriteFactory) return E_POINTER;

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
}

} // namespace phide::ui
