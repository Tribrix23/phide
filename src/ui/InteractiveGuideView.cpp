/**
 * @file InteractiveGuideView.cpp
 * @brief Implementation of the interactive feature guide with real live editable mini-editors.
 */

#include "phide/ui/InteractiveGuideView.hpp"
#include <algorithm>

namespace phide::ui {

struct CanvasApiItem {
    const wchar_t* icon;
    const wchar_t* name;
    const wchar_t* type;
};

static const CanvasApiItem kCanvasApiCompletions[] = {
    { L"m", L"arc(x, y, r, sAngle, eAngle)", L"method" },
    { L"m", L"beginPath()", L"method" },
    { L"m", L"clearRect(x, y, w, h)", L"method" },
    { L"p", L"fillStyle", L"property" },
    { L"m", L"fillRect(x, y, w, h)", L"method" },
    { L"m", L"stroke()", L"method" }
};

InteractiveGuideView::InteractiveGuideView() {
    InitializeEditors();
}

void InteractiveGuideView::InitializeEditors() {
    m_editors.clear();

    // =========================================================================
    // Editor 0: Multi-Cursor Editing (CSS)
    // =========================================================================
    GuideEditorBlock ed0;
    ed0.id = 0;
    ed0.languageExt = L".css";
    ed0.doc.fileName = L"styles.css";
    ed0.doc.filePath = L"styles.css";
    ed0.doc.buffer.SetText(
        L"#p1 { background-color: #ff0000; }               /* red in HEX format */\n"
        L"#p2 { background-color: hsl(120, 100%, 50%); }      /* green in HSL format */\n"
        L"#p3 { background-color: rgba(0, 4, 255, 0.733); }   /* blue with alpha channel in RGBA format */\n"
        L"#p4 { background-color: #f59e0b; }               /* amber in HEX format */"
    );
    ed0.doc.buffer.SetCursor(0, 32);
    m_editors.push_back(std::move(ed0));

    // =========================================================================
    // Editor 1: IntelliSense (JavaScript)
    // =========================================================================
    GuideEditorBlock ed1;
    ed1.id = 1;
    ed1.languageExt = L".js";
    ed1.doc.fileName = L"canvas.js";
    ed1.doc.filePath = L"canvas.js";
    ed1.doc.buffer.SetText(
        L"const canvas = document.querySelector('canvas');\n"
        L"const context = canvas.getContext('2d');\n"
        L"\n"
        L"context.strokeStyle = 'blue';\n"
        L"context.fillRect(0, 0, 150, 75);\n"
        L"context."
    );
    ed1.doc.buffer.SetCursor(5, 8);
    ed1.showIntelliSense = true;
    ed1.intelliSenseSelectedIdx = 4; // fillRect
    m_editors.push_back(std::move(ed1));

    // =========================================================================
    // Editor 2: Predictive Autocomplete & Adaptive Learning (PHP)
    // =========================================================================
    GuideEditorBlock ed2;
    ed2.id = 2;
    ed2.languageExt = L".php";
    ed2.doc.fileName = L"UserController.php";
    ed2.doc.filePath = L"UserController.php";
    ed2.doc.buffer.SetText(
        L"<?php\n"
        L"function fetchUserStats(int $userId): array {\n"
        L"    $pdo = Database::getConnection();\n"
        L"    $stmt = $pdo->prepare(\"SELECT * FROM stats WHERE user_id = :id\");\n"
        L"    $stmt->execute(['id' => $userId]);\n"
        L"    return $stmt->"
    );
    ed2.doc.buffer.SetCursor(5, 18);
    ed2.ghostText = L"fetchAll(PDO::FETCH_ASSOC);";
    ed2.ghostLine = 5;
    ed2.ghostCol = 18;
    m_editors.push_back(std::move(ed2));

    // =========================================================================
    // Editor 3: Interactive Task Checkboxes (Tasks)
    // =========================================================================
    GuideEditorBlock ed3;
    ed3.id = 3;
    ed3.languageExt = L".js";
    ed3.doc.fileName = L"tasks.md";
    ed3.doc.filePath = L"tasks.md";
    ed3.doc.buffer.SetText(
        L"// [x] TASK Set up database credentials and connection pool in db.php\n"
        L"// [x] TASK Configure routing and middlewares for REST API endpoints\n"
        L"// [ ] TASK Implement user session authentication and token refresh\n"
        L"// [ ] TASK Add rate limiting and CSRF protection headers\n"
        L"// [ ] TASK Test live SSE streaming completion in API Tester"
    );
    ed3.doc.buffer.SetCursor(0, 0);
    m_editors.push_back(std::move(ed3));
}

void InteractiveGuideView::InsertCompletion(GuideEditorBlock& ed, const std::wstring& completionName) {
    // Extract base method name up to '('
    size_t paren = completionName.find(L'(');
    std::wstring ins = (paren != std::wstring::npos) ? completionName.substr(0, paren + 1) : completionName;
    if (paren != std::wstring::npos) ins += L");";
    for (wchar_t c : ins) {
        ed.doc.buffer.InsertChar(c);
    }
}

void InteractiveGuideView::DrawKeycapBadge(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                           const std::wstring& text, float& curX, float centerY, float badgeH) {
    float textW = 0.0f;
    IDWriteTextFormat* pFmt = theme.fontKeycap ? theme.fontKeycap.Get() : theme.fontMenuShortcut.Get();
    if (theme.dwriteFactory && pFmt) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.length()),
                                                          pFmt, 300.0f, badgeH, &pLayout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                textW = tm.widthIncludingTrailingWhitespace;
            }
        }
    }

    const float padX = config.ScaleF(7.0f);
    const float badgeW = (std::max)(config.ScaleF(24.0f), textW + padX * 2.0f);
    D2D1_RECT_F rcBadge = D2D1::RectF(
        curX,
        centerY - badgeH / 2.0f,
        curX + badgeW,
        centerY + badgeH / 2.0f
    );

    // Dark neutral badge (NEVER purple!)
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcBadge, 3.5f, 3.5f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.20f, 1.0f), &pBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pBorder;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.28f, 0.28f, 0.32f, 1.0f), &pBorder);

    if (pBg) pRT->FillRoundedRectangle(rrc, pBg.Get());
    if (pBorder) pRT->DrawRoundedRectangle(rrc, pBorder.Get(), 1.0f);

    pRT->DrawText(text.c_str(), static_cast<UINT32>(text.length()),
                  pFmt, rcBadge, theme.brushTextPrimary.Get());

    curX += badgeW + config.ScaleF(4.0f);
}

void InteractiveGuideView::DrawCalloutBox(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                          float x, float y, float width, const std::wstring& title,
                                          const std::wstring& text, float& outHeight) {
    const float padX = config.ScaleF(14.0f);
    const float padY = config.ScaleF(10.0f);
    const float textW = width - padX * 2.0f;

    float computedTextH = config.ScaleF(44.0f);
    IDWriteTextFormat* pBodyFmt = theme.fontBody ? theme.fontBody.Get() : theme.fontHint.Get();
    if (theme.dwriteFactory && pBodyFmt) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        std::wstring fullMsg = title + L" " + text;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(fullMsg.c_str(), static_cast<UINT32>(fullMsg.length()),
                                                          pBodyFmt, textW, 2000.0f, &pLayout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                computedTextH = tm.height;
            }
        }
    }

    const float totalH = computedTextH + padY * 2.0f;
    outHeight = totalH;

    D2D1_RECT_F rcBox = D2D1::RectF(x, y, x + width, y + totalH);
    D2D1_ROUNDED_RECT rrcBox = D2D1::RoundedRect(rcBox, 4.0f, 4.0f);

    // Dark neutral callout background (NEVER purple!)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pCardBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f), &pCardBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pCardBorder;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.22f, 0.25f, 1.0f), &pCardBorder);

    if (pCardBg) pRT->FillRoundedRectangle(rrcBox, pCardBg.Get());
    if (pCardBorder) pRT->DrawRoundedRectangle(rrcBox, pCardBorder.Get(), 1.0f);

    // Left accent pill line
    pRT->DrawLine(
        D2D1::Point2F(x + 1.5f, y + 4.0f),
        D2D1::Point2F(x + 1.5f, y + totalH - 4.0f),
        theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(),
        3.0f
    );

    // Render title + text inside box
    D2D1_RECT_F rcText = D2D1::RectF(x + padX, y + padY, x + width - padX, y + totalH - padY);
    std::wstring fullMsg = title + L" " + text;
    pRT->DrawText(fullMsg.c_str(), static_cast<UINT32>(fullMsg.length()),
                  pBodyFmt, rcText, theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushTextPrimary.Get());
}

void InteractiveGuideView::RenderEditorBlock(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                             GuideEditorBlock& ed, float x, float y, float width, float& outHeight) {
    // Measure character width for Koda Mono font
    if (m_cachedCharWidth <= 0.0f && theme.dwriteFactory && theme.fontCode) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pTestL;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(L"MMMMMMMMMM", 10, theme.fontCode.Get(), 1000.0f, 100.0f, &pTestL))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pTestL->GetMetrics(&tm))) {
                m_cachedCharWidth = tm.widthIncludingTrailingWhitespace / 10.0f;
            }
        }
    }
    const float charW = (m_cachedCharWidth > 0.0f) ? m_cachedCharWidth : config.ScaleF(8.5f);
    const float lineH = config.ScaleF(24.0f);
    const size_t lineCount = ed.doc.buffer.GetLineCount();
    const float padY = config.ScaleF(8.0f);
    const float baseEditorH = lineCount * lineH + padY * 2.0f;

    // Extra space if IntelliSense popup is open in Editor 1
    float popupExtraSpace = (ed.id == 1 && ed.showIntelliSense) ? config.ScaleF(130.0f) : 0.0f;
    const float totalEditorH = baseEditorH;
    outHeight = totalEditorH + popupExtraSpace;

    ed.bounds = D2D1::RectF(x, y, x + width, y + totalEditorH);
    const float gutterW = config.ScaleF(42.0f);
    ed.gutterBounds = D2D1::RectF(x, y, x + gutterW, y + totalEditorH);
    ed.codeAreaBounds = D2D1::RectF(x + gutterW + config.ScaleF(12.0f), y + padY, x + width - config.ScaleF(12.0f), y + totalEditorH - padY);

    // 1. Editor Container Background (Authentic dark neutral editor charcoal, NEVER purple!)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pEditorBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.09f, 0.10f, 1.0f), &pEditorBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pGutterBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.07f, 0.07f, 0.08f, 1.0f), &pGutterBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pBorderNorm;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.20f, 0.22f, 1.0f), &pBorderNorm);

    D2D1_ROUNDED_RECT rrcEditor = D2D1::RoundedRect(ed.bounds, 4.0f, 4.0f);
    if (pEditorBg) pRT->FillRoundedRectangle(rrcEditor, pEditorBg.Get());

    // 2. Gutter background
    if (pGutterBg) {
        D2D1_ROUNDED_RECT rrcGutter = D2D1::RoundedRect(ed.gutterBounds, 4.0f, 4.0f);
        pRT->FillRoundedRectangle(rrcGutter, pGutterBg.Get());
        // Straighten right edge of gutter
        pRT->FillRectangle(D2D1::RectF(x + gutterW - 4.0f, y, x + gutterW, y + totalEditorH), pGutterBg.Get());
    }
    // Gutter separator line
    pRT->DrawLine(D2D1::Point2F(x + gutterW, y), D2D1::Point2F(x + gutterW, y + totalEditorH),
                  pBorderNorm ? pBorderNorm.Get() : theme.brushBorderSubtle.Get(), 0.8f);

    // 3. Render Lines
    IDWriteTextFormat* pCodeFmt = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();
    IDWriteTextFormat* pGutterFmt = theme.fontLineNumber ? theme.fontLineNumber.Get() : theme.fontCode.Get();

    for (size_t l = 0; l < lineCount; ++l) {
        float ly = y + padY + l * lineH;

        // Active line highlight
        if (ed.isFocused && l == ed.doc.buffer.GetCursor().line) {
            D2D1_RECT_F rcActiveLine = D2D1::RectF(x + gutterW + 1.0f, ly, x + width - 1.0f, ly + lineH);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pActiveBg;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.14f, 0.16f, 0.8f), &pActiveBg);
            if (pActiveBg) pRT->FillRectangle(rcActiveLine, pActiveBg.Get());
        }

        // Line Number in Gutter
        std::wstring numStr = std::to_wstring(l + 1);
        D2D1_RECT_F rcNum = D2D1::RectF(x, ly, x + gutterW - config.ScaleF(10.0f), ly + lineH);
        pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()), pGutterFmt, rcNum, theme.brushLineNumber.Get());

        // Code Line Text
        std::wstring lineStr = ed.doc.buffer.GetLine(l);
        float codeX = ed.codeAreaBounds.left;

        if (!lineStr.empty() && theme.dwriteFactory && pCodeFmt) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pLineLayout;
            float maxLineW = (std::max)(2000.0f, width * 2.0f);
            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                    lineStr.c_str(), static_cast<UINT32>(lineStr.length()),
                    pCodeFmt, maxLineW, lineH, &pLineLayout))) {

                // Apply syntax highlighting tokens
                auto tokens = ed.doc.buffer.GetTokensForLine(l, ed.languageExt);
                for (const auto& tok : tokens) {
                    if (tok.startCol + tok.length > lineStr.length()) continue;
                    ID2D1Brush* brush = theme.brushTextPrimary.Get();
                    switch (tok.type) {
                        case editor::TokenType::Keyword:   brush = theme.brushSyntaxKeyword.Get(); break;
                        case editor::TokenType::Type:      brush = theme.brushSyntaxType.Get(); break;
                        case editor::TokenType::String:    brush = theme.brushSyntaxString.Get(); break;
                        case editor::TokenType::Comment:   brush = theme.brushSyntaxComment.Get(); break;
                        case editor::TokenType::Number:    brush = theme.brushSyntaxNumber.Get(); break;
                        case editor::TokenType::Operator:  brush = theme.brushSyntaxOperator.Get(); break;
                        case editor::TokenType::Tag:       brush = theme.brushSyntaxTag.Get(); break;
                        case editor::TokenType::Attribute: brush = theme.brushSyntaxAttribute.Get(); break;
                        default: break;
                    }
                    if (!brush) brush = theme.brushTextPrimary.Get();
                    DWRITE_TEXT_RANGE range = { static_cast<UINT32>(tok.startCol), static_cast<UINT32>(tok.length) };
                    pLineLayout->SetDrawingEffect(brush, range);
                }

                // Draw formatted text layout (100% exact character widths, ZERO layering!)
                pRT->DrawTextLayout(D2D1::Point2F(codeX, ly), pLineLayout.Get(), theme.brushTextPrimary.Get());
            }
        }

        // =====================================================================
        // Interactive Task Checkboxes in Editor 3 (Tasks)
        // =====================================================================
        if (ed.id == 3) {
            size_t xPos = lineStr.find(L"[x]");
            if (xPos == std::wstring::npos) xPos = lineStr.find(L"[X]");
            size_t emptyPos = lineStr.find(L"[ ]");
            size_t cbCol = (xPos != std::wstring::npos) ? xPos : emptyPos;

            if (cbCol != std::wstring::npos) {
                float cbX = codeX + cbCol * charW;
                float boxSz = config.ScaleF(14.0f);
                float cbY = ly + (lineH - boxSz) / 2.0f;
                D2D1_RECT_F rcCb = D2D1::RectF(cbX, cbY, cbX + boxSz, cbY + boxSz);
                D2D1_ROUNDED_RECT rrcCb = D2D1::RoundedRect(rcCb, 3.0f, 3.0f);

                // Cover the raw "[x]" or "[ ]" text with a crisp checkbox widget
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pCoverBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.09f, 0.10f, 1.0f), &pCoverBrush);
                D2D1_RECT_F rcCover = D2D1::RectF(cbX - 1.0f, ly, cbX + 3.0f * charW + 1.0f, ly + lineH);
                if (pCoverBrush) pRT->FillRectangle(rcCover, pCoverBrush.Get());

                if (xPos != std::wstring::npos) {
                    // Checked state: brand accent filled square with white checkmark
                    pRT->FillRoundedRectangle(rrcCb, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushRunGreen.Get());
                    pRT->DrawRoundedRectangle(rrcCb, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushRunGreen.Get(), 1.0f);

                    // Checkmark ✓
                    float midY = rcCb.top + boxSz * 0.5f;
                    pRT->DrawLine(D2D1::Point2F(rcCb.left + boxSz * 0.22f, midY + boxSz * 0.04f),
                                  D2D1::Point2F(rcCb.left + boxSz * 0.42f, midY + boxSz * 0.26f),
                                  theme.brushTextWhite.Get(), 1.6f);
                    pRT->DrawLine(D2D1::Point2F(rcCb.left + boxSz * 0.42f, midY + boxSz * 0.26f),
                                  D2D1::Point2F(rcCb.left + boxSz * 0.78f, midY - boxSz * 0.24f),
                                  theme.brushTextWhite.Get(), 1.6f);

                    // Strikethrough line over the completed task title
                    float strikeStartX = cbX + 3.0f * charW + config.ScaleF(4.0f);
                    float strikeEndX = codeX + lineStr.length() * charW;
                    float strikeY = ly + lineH * 0.5f;
                    pRT->DrawLine(D2D1::Point2F(strikeStartX, strikeY), D2D1::Point2F(strikeEndX, strikeY),
                                  theme.brushCommentDone ? theme.brushCommentDone.Get() : theme.brushTextMuted.Get(), 1.2f);
                } else {
                    // Unchecked state: empty square with border
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pUncheckBorder;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.40f, 0.45f, 1.0f), &pUncheckBorder);
                    if (pUncheckBorder) pRT->DrawRoundedRectangle(rrcCb, pUncheckBorder.Get(), 1.5f);
                }
            }
        }

        // =====================================================================
        // Color Swatches in Editor 0 (CSS)
        // =====================================================================
        if (ed.id == 0) {
            // Check for color spans (#ff0000, hsl, rgba, #f59e0b)
            struct SwatchRule {
                const wchar_t* pattern;
                D2D1_COLOR_F color;
            };
            static const SwatchRule swatches[] = {
                { L"#ff0000", D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f) },
                { L"hsl(120, 100%, 50%)", D2D1::ColorF(0.0f, 1.0f, 0.0f, 1.0f) },
                { L"rgba(0, 4, 255, 0.733)", D2D1::ColorF(0.0f, 0.015f, 1.0f, 0.733f) },
                { L"#f59e0b", D2D1::ColorF(0.96f, 0.62f, 0.04f, 1.0f) }
            };
            for (const auto& sw : swatches) {
                size_t pos = lineStr.find(sw.pattern);
                if (pos != std::wstring::npos) {
                    float swColX = codeX + pos * charW - config.ScaleF(16.0f);
                    float swSz = config.ScaleF(11.0f);
                    float swY = ly + (lineH - swSz) / 2.0f;
                    D2D1_RECT_F rcSw = D2D1::RectF(swColX, swY, swColX + swSz, swY + swSz);

                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pSwBrush;
                    pRT->CreateSolidColorBrush(sw.color, &pSwBrush);
                    if (pSwBrush) {
                        pRT->FillRectangle(rcSw, pSwBrush.Get());
                        pRT->DrawRectangle(rcSw, theme.brushTextWhite.Get(), 0.8f);
                    }
                }
            }
        }

        // =====================================================================
        // Predictive Ghost Text in Editor 2 (PHP)
        // =====================================================================
        if (ed.id == 2 && !ed.ghostText.empty() && l == ed.ghostLine) {
            float ghostX = codeX + ed.ghostCol * charW;
            D2D1_RECT_F rcGhost = D2D1::RectF(ghostX, ly, ghostX + config.ScaleF(300.0f), ly + lineH);
            pRT->DrawText(ed.ghostText.c_str(), static_cast<UINT32>(ed.ghostText.length()),
                          pCodeFmt, rcGhost, theme.brushTextMuted.Get());

            // Tab badge
            float tabBadgeX = ghostX + ed.ghostText.length() * charW + config.ScaleF(10.0f);
            DrawKeycapBadge(pRT, theme, config, L"Tab to accept", tabBadgeX, ly + lineH / 2.0f, config.ScaleF(18.0f));
        }

        // =====================================================================
        // Carets (Primary & Multi-Cursors)
        // =====================================================================
        if (ed.isFocused && m_caretBlink) {
            // Primary caret
            if (l == ed.doc.buffer.GetCursor().line) {
                float caretX = codeX + ed.doc.buffer.GetCursor().col * charW;
                pRT->DrawLine(D2D1::Point2F(caretX, ly + 2.0f), D2D1::Point2F(caretX, ly + lineH - 2.0f),
                              theme.brushTextWhite.Get(), 1.5f);
            }
            // Secondary multi-cursors
            for (const auto& sel : ed.doc.secondarySelections) {
                if (l == sel.cursor.line) {
                    float sCaretX = codeX + sel.cursor.col * charW;
                    pRT->DrawLine(D2D1::Point2F(sCaretX, ly + 2.0f), D2D1::Point2F(sCaretX, ly + lineH - 2.0f),
                                  theme.brushTextWhite.Get(), 1.5f);
                }
            }
        }
    }

    // 4. Draw Editor Outline (Glows vibrant accent when focused, neutral when unfocused)
    if (ed.isFocused) {
        pRT->DrawRoundedRectangle(rrcEditor, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : pBorderNorm.Get(), 1.5f);
    } else {
        if (pBorderNorm) pRT->DrawRoundedRectangle(rrcEditor, pBorderNorm.Get(), 1.0f);
    }

    // =========================================================================
    // Floating IntelliSense Popup for Editor 1 (JavaScript)
    // =========================================================================
    if (ed.id == 1 && ed.showIntelliSense) {
        float curLineY = y + padY + ed.doc.buffer.GetCursor().line * lineH;
        float popupX = ed.codeAreaBounds.left + ed.doc.buffer.GetCursor().col * charW;
        float popupY = curLineY + lineH + config.ScaleF(2.0f);
        float popupW = config.ScaleF(240.0f);
        float popupH = config.ScaleF(124.0f);

        ed.intelliSensePopupBounds = D2D1::RectF(popupX, popupY, popupX + popupW, popupY + popupH);
        D2D1_ROUNDED_RECT rrcPopup = D2D1::RoundedRect(ed.intelliSensePopupBounds, 3.5f, 3.5f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pPopBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.13f, 0.13f, 0.15f, 1.0f), &pPopBg);
        if (pPopBg) pRT->FillRoundedRectangle(rrcPopup, pPopBg.Get());
        pRT->DrawRoundedRectangle(rrcPopup, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : pBorderNorm.Get(), 1.2f);

        float autoRowH = config.ScaleF(19.0f);
        for (int i = 0; i < 6; ++i) {
            float ay = popupY + config.ScaleF(4.0f) + i * autoRowH;
            D2D1_RECT_F rcRow = D2D1::RectF(popupX + 2.0f, ay, popupX + popupW - 2.0f, ay + autoRowH);
            bool isActive = (i == ed.intelliSenseSelectedIdx);

            if (isActive) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pSelRow;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.24f, 0.32f, 1.0f), &pSelRow);
                if (pSelRow) pRT->FillRectangle(rcRow, pSelRow.Get());
            }

            // Glyph icon
            D2D1_RECT_F rcBadge = D2D1::RectF(popupX + config.ScaleF(8.0f), ay, popupX + config.ScaleF(22.0f), ay + autoRowH);
            pRT->DrawText(kCanvasApiCompletions[i].icon, 1, theme.fontBadge ? theme.fontBadge.Get() : pCodeFmt,
                          rcBadge, theme.brushBrandAccent.Get());

            // Method/Property name
            D2D1_RECT_F rcName = D2D1::RectF(popupX + config.ScaleF(26.0f), ay, popupX + popupW - config.ScaleF(6.0f), ay + autoRowH);
            pRT->DrawText(kCanvasApiCompletions[i].name, static_cast<UINT32>(wcslen(kCanvasApiCompletions[i].name)),
                          theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                          rcName, isActive ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());
        }
    }
}

void InteractiveGuideView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                  float left, float width, float top, float height) {
    if (!pRT) return;

    m_viewBounds = D2D1::RectF(left, top, left + width, top + height);

    // Fill main canvas background (Dark neutral editor background, NEVER purple!)
    pRT->FillRectangle(m_viewBounds, theme.brushCanvasBg.Get());

    // Clip to view bounds for smooth scrolling
    pRT->PushAxisAlignedClip(m_viewBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const float marginX = config.ScaleF(32.0f);
    const float contentW = (std::max)(config.ScaleF(400.0f), width - marginX * 2.0f - config.ScaleF(16.0f));
    const float contentLeft = left + marginX;

    float curY = top + config.ScaleF(24.0f) - m_scrollY;

    IDWriteTextFormat* pTitleFmt = theme.fontCourseTitle ? theme.fontCourseTitle.Get() : theme.fontTitle.Get();
    IDWriteTextFormat* pHeadingFmt = theme.fontCourseHeading ? theme.fontCourseHeading.Get() : theme.fontSectionHeader.Get();
    IDWriteTextFormat* pBodyFmt = theme.fontCourseBody ? theme.fontCourseBody.Get() : theme.fontBody.Get();

    // =========================================================================
    // 1. Header: "Interactive Guide"
    // =========================================================================
    D2D1_RECT_F rcTitle = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(38.0f));
    const wchar_t* titleStr = L"Interactive Guide";
    pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)),
                  pTitleFmt, rcTitle, theme.brushTextWhite.Get());
    curY += config.ScaleF(40.0f);

    const wchar_t* subStr = L"Explore and try out the powerful core features of Phide directly inside this interactive walkthrough. "
                            L"Each code block below is an interactive editor — click, type, and test features live!";
    D2D1_RECT_F rcSub = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(44.0f));
    pRT->DrawText(subStr, static_cast<UINT32>(wcslen(subStr)),
                  pBodyFmt, rcSub, theme.brushTextSecondary.Get());
    curY += config.ScaleF(48.0f);

    // =========================================================================
    // Feature Highlights Grid (8 core features, neutral dark cards, NEVER purple!)
    // =========================================================================
    struct FeatureHighlight {
        const wchar_t* title;
        const wchar_t* desc;
    };
    static const FeatureHighlight highlights[] = {
        { L"Multi-Cursor Editing", L"Select all occurrences, add additional cursors, and box-select." },
        { L"Smart IntelliSense", L"Parameter hints and completions for JS, TS, PHP, Python, React." },
        { L"Predictive Autocomplete", L"Adaptive Markov-chain ghost text suggestions as you write." },
        { L"Interactive TODO Tasks", L"Clickable markdown task checkboxes with instant live toggling." },
        { L"CSS Color Swatches & Picker", L"Inline color swatches and visual picker for hex, rgb, and hsl." },
        { L"Built-in API Tester", L"Test REST and SSE streaming endpoints directly inside the IDE." },
        { L"Integrated Terminal", L"Embedded native PowerShell and CMD terminal with split panels." },
        { L"Zen Mode & Centered Layout", L"Distraction-free coding view with customizable centered layout." }
    };

    const float cardGap = config.ScaleF(12.0f);
    const float cardW = (contentW - cardGap) / 2.0f;
    const float cardH = config.ScaleF(60.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pFeatCardBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f), &pFeatCardBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pFeatCardBorder;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.20f, 0.23f, 1.0f), &pFeatCardBorder);

    for (size_t i = 0; i < 8; ++i) {
        float cx = contentLeft + (i % 2) * (cardW + cardGap);
        float cy = curY + (i / 2) * (cardH + config.ScaleF(8.0f));

        D2D1_RECT_F rcCard = D2D1::RectF(cx, cy, cx + cardW, cy + cardH);
        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, 4.0f, 4.0f);
        if (pFeatCardBg) pRT->FillRoundedRectangle(rrcCard, pFeatCardBg.Get());
        if (pFeatCardBorder) pRT->DrawRoundedRectangle(rrcCard, pFeatCardBorder.Get(), 0.8f);

        D2D1_RECT_F rcFeatTitle = D2D1::RectF(cx + config.ScaleF(12.0f), cy + config.ScaleF(8.0f),
                                              cx + cardW - config.ScaleF(12.0f), cy + config.ScaleF(26.0f));
        pRT->DrawText(highlights[i].title, static_cast<UINT32>(wcslen(highlights[i].title)),
                      theme.fontTitle ? theme.fontTitle.Get() : pHeadingFmt,
                      rcFeatTitle, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get());

        D2D1_RECT_F rcFeatDesc = D2D1::RectF(cx + config.ScaleF(12.0f), cy + config.ScaleF(28.0f),
                                             cx + cardW - config.ScaleF(12.0f), cy + cardH - config.ScaleF(6.0f));
        pRT->DrawText(highlights[i].desc, static_cast<UINT32>(wcslen(highlights[i].desc)),
                      theme.fontHint ? theme.fontHint.Get() : pBodyFmt,
                      rcFeatDesc, theme.brushTextMuted.Get());
    }
    curY += 4 * (cardH + config.ScaleF(8.0f)) + config.ScaleF(24.0f);

    // =========================================================================
    // SECTION 1: Multi-Cursor Editing (Live Editor 0)
    // =========================================================================
    D2D1_RECT_F rcSec1Title = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(28.0f));
    const wchar_t* sec1Title = L"Multi-Cursor Editing";
    pRT->DrawText(sec1Title, static_cast<UINT32>(wcslen(sec1Title)),
                  pHeadingFmt, rcSec1Title, theme.brushTextWhite.Get());
    curY += config.ScaleF(32.0f);

    const wchar_t* sec1Desc = L"Using multiple cursors allows you to edit multiple parts of the document at once, greatly improving your productivity. Try the following actions in the live editor below:";
    D2D1_RECT_F rcSec1Desc = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(36.0f));
    pRT->DrawText(sec1Desc, static_cast<UINT32>(wcslen(sec1Desc)),
                  pBodyFmt, rcSec1Desc, theme.brushTextSecondary.Get());
    curY += config.ScaleF(40.0f);

    const wchar_t* step1Text = L"1. Box Selection - press any combination of Shift+DownArrow, Shift+RightArrow, Shift+UpArrow, Shift+LeftArrow while holding Shift+Alt to select a block of text. You can also drag-select using the middle mouse button.";
    D2D1_RECT_F rcStep1 = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(34.0f));
    pRT->DrawText(step1Text, static_cast<UINT32>(wcslen(step1Text)), pBodyFmt, rcStep1, theme.brushTextPrimary.Get());
    curY += config.ScaleF(38.0f);

    const wchar_t* step2Text = L"2. Add a cursor - press Ctrl+Alt+UpArrow to add a new cursor above, or Ctrl+Alt+DownArrow to add a cursor below. You can also use your mouse with Alt+Click to add a cursor anywhere in the editor.";
    D2D1_RECT_F rcStep2 = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(34.0f));
    pRT->DrawText(step2Text, static_cast<UINT32>(wcslen(step2Text)), pBodyFmt, rcStep2, theme.brushTextPrimary.Get());
    curY += config.ScaleF(38.0f);

    const wchar_t* step3Text = L"3. Create cursors on all occurrences of a string - select one instance of a string e.g. background-color and press Ctrl+D or Ctrl+F2. Now you can replace all instances simultaneously.";
    D2D1_RECT_F rcStep3 = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(34.0f));
    pRT->DrawText(step3Text, static_cast<UINT32>(wcslen(step3Text)), pBodyFmt, rcStep3, theme.brushTextPrimary.Get());
    curY += config.ScaleF(42.0f);

    // Live Editable CSS Block (Editor 0)
    float ed0H = 0.0f;
    RenderEditorBlock(pRT, theme, config, m_editors[0], contentLeft, curY, contentW, ed0H);
    curY += ed0H + config.ScaleF(16.0f);

    // Callout box for CSS Tip
    float calloutH1 = 0.0f;
    DrawCalloutBox(pRT, theme, config, contentLeft, curY, contentW,
                   L"CSS Tip:",
                   L"You may have noticed in the live editor above we also provide color swatches inline for CSS; additionally if you hover over an element such as #p1 we will show how this is represented in HTML. These swatches also act as color pickers that allow you to easily change a color value. A simple example of some language-specific editor features.",
                   calloutH1);
    curY += calloutH1 + config.ScaleF(32.0f);

    // =========================================================================
    // SECTION 2: IntelliSense (Live Editor 1)
    // =========================================================================
    D2D1_RECT_F rcSec2Title = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(28.0f));
    const wchar_t* sec2Title = L"IntelliSense";
    pRT->DrawText(sec2Title, static_cast<UINT32>(wcslen(sec2Title)),
                  pHeadingFmt, rcSec2Title, theme.brushTextWhite.Get());
    curY += config.ScaleF(32.0f);

    const wchar_t* sec2Desc = L"Phide comes with powerful IntelliSense for JavaScript, TypeScript, PHP, Python, and Web APIs pre-installed. In the live editor below, position the text cursor right after the dot and press Ctrl+Space to invoke IntelliSense. Notice how suggestions come from the Canvas API.";
    D2D1_RECT_F rcSec2Desc = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(36.0f));
    pRT->DrawText(sec2Desc, static_cast<UINT32>(wcslen(sec2Desc)),
                  pBodyFmt, rcSec2Desc, theme.brushTextSecondary.Get());
    curY += config.ScaleF(42.0f);

    // Live Editable JS Block (Editor 1)
    float ed1H = 0.0f;
    RenderEditorBlock(pRT, theme, config, m_editors[1], contentLeft, curY, contentW, ed1H);
    curY += ed1H + config.ScaleF(16.0f);

    // Callout box for IntelliSense Tip
    float calloutH2 = 0.0f;
    DrawCalloutBox(pRT, theme, config, contentLeft, curY, contentW,
                   L"Tip:",
                   L"While we ship JavaScript, TypeScript, PHP, and Python support out of the box, other languages can be upgraded with better IntelliSense and completions through one of the many extensions or plugins.",
                   calloutH2);
    curY += calloutH2 + config.ScaleF(32.0f);

    // =========================================================================
    // SECTION 3: Predictive Autocomplete & Adaptive Learning (Live Editor 2)
    // =========================================================================
    D2D1_RECT_F rcSec3Title = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(28.0f));
    const wchar_t* sec3Title = L"Predictive Autocomplete & Adaptive Learning";
    pRT->DrawText(sec3Title, static_cast<UINT32>(wcslen(sec3Title)),
                  pHeadingFmt, rcSec3Title, theme.brushTextWhite.Get());
    curY += config.ScaleF(32.0f);

    const wchar_t* sec3Desc = L"Phide features an adaptive predictive engine that continuously learns from your coding context and project patterns. Subtly rendered ghost text appears ahead of your cursor to autocomplete entire expressions, methods, or statements.";
    D2D1_RECT_F rcSec3Desc = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(36.0f));
    pRT->DrawText(sec3Desc, static_cast<UINT32>(wcslen(sec3Desc)),
                  pBodyFmt, rcSec3Desc, theme.brushTextSecondary.Get());
    curY += config.ScaleF(42.0f);

    // Live Editable PHP Block (Editor 2)
    float ed2H = 0.0f;
    RenderEditorBlock(pRT, theme, config, m_editors[2], contentLeft, curY, contentW, ed2H);
    curY += ed2H + config.ScaleF(16.0f);

    // Callout box for Prediction Tip
    float calloutH3 = 0.0f;
    DrawCalloutBox(pRT, theme, config, contentLeft, curY, contentW,
                   L"Tip:",
                   L"Press Tab to accept predictive ghost text suggestions instantly, or press Esc to dismiss them. The engine adapts in real-time as you write code, tailoring suggestions to your specific project naming conventions.",
                   calloutH3);
    curY += calloutH3 + config.ScaleF(32.0f);

    // =========================================================================
    // SECTION 4: Interactive Task Checkboxes (Live Editor 3 with // TASK comments)
    // =========================================================================
    D2D1_RECT_F rcSec4Title = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(28.0f));
    const wchar_t* sec4Title = L"Interactive Task Checkboxes";
    pRT->DrawText(sec4Title, static_cast<UINT32>(wcslen(sec4Title)),
                  pHeadingFmt, rcSec4Title, theme.brushTextWhite.Get());
    curY += config.ScaleF(32.0f);

    const wchar_t* sec4Desc = L"In Phide, project tasks are written directly in code comments starting with `// [ ] TASK` or `// [x] TASK`. In the live editor below, click on any checkbox to toggle its completed state, or edit the task text directly:";
    D2D1_RECT_F rcSec4Desc = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(34.0f));
    pRT->DrawText(sec4Desc, static_cast<UINT32>(wcslen(sec4Desc)),
                  pBodyFmt, rcSec4Desc, theme.brushTextSecondary.Get());
    curY += config.ScaleF(40.0f);

    // Live Editable Task Comments Block (Editor 3)
    float ed3H = 0.0f;
    RenderEditorBlock(pRT, theme, config, m_editors[3], contentLeft, curY, contentW, ed3H);
    curY += ed3H + config.ScaleF(16.0f);

    // Callout box for Checkboxes Tip
    float calloutH4 = 0.0f;
    DrawCalloutBox(pRT, theme, config, contentLeft, curY, contentW,
                   L"Tip:",
                   L"In any file, write `// [ ] TASK ...` or `// [x] TASK ...` to create interactive task checkboxes directly inside your code. Clicking the checkbox toggles completion, strikes through completed tasks, and tracks progress effortlessly.",
                   calloutH4);
    curY += calloutH4 + config.ScaleF(32.0f);

    // =========================================================================
    // SECTION 5: Built-in API Tester
    // =========================================================================
    D2D1_RECT_F rcSec5Title = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(28.0f));
    const wchar_t* sec5Title = L"Built-in API Tester";
    pRT->DrawText(sec5Title, static_cast<UINT32>(wcslen(sec5Title)),
                  pHeadingFmt, rcSec5Title, theme.brushTextWhite.Get());
    curY += config.ScaleF(32.0f);

    const wchar_t* sec5Desc = L"Test REST, SSE streaming, and GraphQL APIs directly inside Phide without switching to external tools. Inspect live responses, response timing breakdowns (DNS, TCP, SSL, TTFB), and copy payloads with a single click.";
    D2D1_RECT_F rcSec5Desc = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(36.0f));
    pRT->DrawText(sec5Desc, static_cast<UINT32>(wcslen(sec5Desc)),
                  pBodyFmt, rcSec5Desc, theme.brushTextSecondary.Get());
    curY += config.ScaleF(40.0f);

    // API Tester Preview Card (Dark neutral charcoal, NEVER purple!)
    const float apiCardH = config.ScaleF(80.0f);
    D2D1_RECT_F rcApiCard = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + apiCardH);
    D2D1_ROUNDED_RECT rrcApiCard = D2D1::RoundedRect(rcApiCard, 4.0f, 4.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pApiCardBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.09f, 0.10f, 1.0f), &pApiCardBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pApiCardBorder;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.20f, 0.22f, 1.0f), &pApiCardBorder);

    if (pApiCardBg) pRT->FillRoundedRectangle(rrcApiCard, pApiCardBg.Get());
    if (pApiCardBorder) pRT->DrawRoundedRectangle(rrcApiCard, pApiCardBorder.Get(), 1.0f);

    // Method badge [POST]
    float badgeCurX = contentLeft + config.ScaleF(14.0f);
    float badgeCenterY = curY + config.ScaleF(24.0f);
    DrawKeycapBadge(pRT, theme, config, L"POST", badgeCurX, badgeCenterY, config.ScaleF(22.0f));

    // URL
    IDWriteTextFormat* pCodeFmt = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();
    D2D1_RECT_F rcUrl = D2D1::RectF(badgeCurX + config.ScaleF(6.0f), curY + config.ScaleF(14.0f),
                                    contentLeft + contentW - config.ScaleF(12.0f), curY + config.ScaleF(34.0f));
    const wchar_t* apiUrl = L"https://api.xkiro.com/v1/chat/completions";
    pRT->DrawText(apiUrl, static_cast<UINT32>(wcslen(apiUrl)), pCodeFmt, rcUrl, theme.brushTextPrimary.Get());

    // Badges: 200 OK, 3.37 s, 4.76 KB
    float statY = curY + config.ScaleF(46.0f);
    float statX = contentLeft + config.ScaleF(14.0f);

    D2D1_RECT_F rc200 = D2D1::RectF(statX, statY, statX + config.ScaleF(64.0f), statY + config.ScaleF(20.0f));
    D2D1_ROUNDED_RECT rrc200 = D2D1::RoundedRect(rc200, 3.0f, 3.0f);
    pRT->FillRoundedRectangle(rrc200, theme.brushRunGreen ? theme.brushRunGreen.Get() : theme.brushItemHover.Get());
    const wchar_t* stat200 = L"200 OK";
    pRT->DrawText(stat200, static_cast<UINT32>(wcslen(stat200)),
                  theme.fontBadge ? theme.fontBadge.Get() : pBodyFmt, rc200, theme.brushTextWhite.Get());
    statX += config.ScaleF(72.0f);

    const wchar_t* timingStr = L"3.37 s    4.76 KB    SSE Stream: Active";
    D2D1_RECT_F rcTiming = D2D1::RectF(statX, statY, contentLeft + contentW - config.ScaleF(12.0f), statY + config.ScaleF(20.0f));
    pRT->DrawText(timingStr, static_cast<UINT32>(wcslen(timingStr)),
                  theme.fontHint ? theme.fontHint.Get() : pBodyFmt, rcTiming, theme.brushTextMuted.Get());

    curY += apiCardH + config.ScaleF(14.0f);

    // Callout box for API Tester Tip
    float calloutH5 = 0.0f;
    DrawCalloutBox(pRT, theme, config, contentLeft, curY, contentW,
                   L"Tip:",
                   L"Open the API Tester anytime using the API icon on the Activity Rail (left navigation bar) or by selecting 'API Tester' from the Command Palette.",
                   calloutH5);
    curY += calloutH5 + config.ScaleF(32.0f);

    // =========================================================================
    // SECTION 6: Zen Mode & Extensibility
    // =========================================================================
    D2D1_RECT_F rcSec6Title = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(28.0f));
    const wchar_t* sec6Title = L"Zen Mode & Extensibility";
    pRT->DrawText(sec6Title, static_cast<UINT32>(wcslen(sec6Title)),
                  pHeadingFmt, rcSec6Title, theme.brushTextWhite.Get());
    curY += config.ScaleF(32.0f);

    const wchar_t* sec6Desc = L"Maximize your screen real estate and eliminate distractions with Zen Mode [Ctrl+K Z] and Centered Layout. Phide also features a comprehensive extension and plugin architecture for themes, formatters, and language servers.";
    D2D1_RECT_F rcSec6Desc = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(36.0f));
    pRT->DrawText(sec6Desc, static_cast<UINT32>(wcslen(sec6Desc)),
                  pBodyFmt, rcSec6Desc, theme.brushTextSecondary.Get());
    curY += config.ScaleF(48.0f);

    // Record total content height
    m_contentTotalHeight = (curY + m_scrollY) - top;
    m_maxScrollY = (std::max)(0.0f, m_contentTotalHeight - height + config.ScaleF(30.0f));

    // Pop clipping
    pRT->PopAxisAlignedClip();

    // =========================================================================
    // Vertical Scrollbar (if content overflows)
    // =========================================================================
    if (m_maxScrollY > 0.0f) {
        const float sbW = config.ScaleF(10.0f);
        m_scrollTrackBounds = D2D1::RectF(left + width - sbW, top, left + width, top + height);
        pRT->FillRectangle(m_scrollTrackBounds, theme.brushCanvasBg.Get());

        float visibleRatio = (std::min)(1.0f, height / m_contentTotalHeight);
        float thumbH = (std::max)(config.ScaleF(30.0f), height * visibleRatio);
        float scrollRatio = m_scrollY / m_maxScrollY;
        float thumbY = top + scrollRatio * (height - thumbH);

        m_scrollThumbBounds = D2D1::RectF(left + width - sbW + 2.0f, thumbY, left + width - 2.0f, thumbY + thumbH);
        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(m_scrollThumbBounds, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrcThumb, theme.brushBorderHover ? theme.brushBorderHover.Get() : theme.brushBorderSubtle.Get());
    }
}

bool InteractiveGuideView::HandleMouseMove(POINT clientPt) {
    bool needRepaint = false;

    // Check if dragging mouse to select text inside an editor block
    if (m_focusedEditorIdx >= 0 && m_focusedEditorIdx < static_cast<int>(m_editors.size())) {
        auto& ed = m_editors[m_focusedEditorIdx];
        if (ed.doc.isSelecting) {
            float lineH = 24.0f;
            float charW = (m_cachedCharWidth > 0.0f) ? m_cachedCharWidth : 8.5f;

            float relY = clientPt.y - (ed.codeAreaBounds.top);
            size_t curLine = (relY > 0) ? static_cast<size_t>(relY / lineH) : 0;
            if (curLine >= ed.doc.buffer.GetLineCount()) {
                curLine = (ed.doc.buffer.GetLineCount() > 0) ? (ed.doc.buffer.GetLineCount() - 1) : 0;
            }

            float relX = clientPt.x - ed.codeAreaBounds.left;
            std::wstring lineStr = ed.doc.buffer.GetLine(curLine);
            size_t curCol = (relX > 0) ? static_cast<size_t>((relX + charW * 0.5f) / charW) : 0;
            if (curCol > lineStr.length()) curCol = lineStr.length();

            ed.doc.selEnd = { curLine, curCol };
            ed.doc.hasSelection = (ed.doc.selAnchor.line != ed.doc.selEnd.line || ed.doc.selAnchor.col != ed.doc.selEnd.col);
            ed.doc.buffer.SetCursor(curLine, curCol);
            needRepaint = true;
        }

        // Hover over IntelliSense dropdown
        if (ed.showIntelliSense &&
            clientPt.x >= ed.intelliSensePopupBounds.left && clientPt.x <= ed.intelliSensePopupBounds.right &&
            clientPt.y >= ed.intelliSensePopupBounds.top && clientPt.y <= ed.intelliSensePopupBounds.bottom) {
            float rowH = 19.0f;
            int hoveredRow = static_cast<int>((clientPt.y - (ed.intelliSensePopupBounds.top + 4.0f)) / rowH);
            if (hoveredRow >= 0 && hoveredRow < 6 && hoveredRow != ed.intelliSenseSelectedIdx) {
                ed.intelliSenseSelectedIdx = hoveredRow;
                needRepaint = true;
            }
        }
    }

    return needRepaint;
}

bool InteractiveGuideView::HandleLButtonDown(POINT clientPt, bool altPressed) {
    int oldFocused = m_focusedEditorIdx;

    for (size_t eIdx = 0; eIdx < m_editors.size(); ++eIdx) {
        auto& ed = m_editors[eIdx];
        if (clientPt.x >= ed.bounds.left && clientPt.x <= ed.bounds.right &&
            clientPt.y >= ed.bounds.top && clientPt.y <= ed.bounds.bottom) {

            m_focusedEditorIdx = static_cast<int>(eIdx);
            for (size_t j = 0; j < m_editors.size(); ++j) {
                m_editors[j].isFocused = (static_cast<int>(j) == m_focusedEditorIdx);
            }

            // Check if clicking inside IntelliSense popup (if open)
            if (ed.showIntelliSense &&
                clientPt.x >= ed.intelliSensePopupBounds.left && clientPt.x <= ed.intelliSensePopupBounds.right &&
                clientPt.y >= ed.intelliSensePopupBounds.top && clientPt.y <= ed.intelliSensePopupBounds.bottom) {
                float rowH = 19.0f;
                int clickedRow = static_cast<int>((clientPt.y - (ed.intelliSensePopupBounds.top + 4.0f)) / rowH);
                if (clickedRow >= 0 && clickedRow < 6) {
                    InsertCompletion(ed, kCanvasApiCompletions[clickedRow].name);
                    ed.showIntelliSense = false;
                    return true;
                }
            }

            // Calculate clicked line
            float lineH = 24.0f;
            float relY = clientPt.y - (ed.codeAreaBounds.top);
            size_t clickedLine = (relY > 0) ? static_cast<size_t>(relY / lineH) : 0;
            if (clickedLine >= ed.doc.buffer.GetLineCount()) {
                clickedLine = (ed.doc.buffer.GetLineCount() > 0) ? (ed.doc.buffer.GetLineCount() - 1) : 0;
            }

            // Calculate clicked col
            float charW = (m_cachedCharWidth > 0.0f) ? m_cachedCharWidth : 8.5f;
            float relX = clientPt.x - ed.codeAreaBounds.left;
            std::wstring lineStr = ed.doc.buffer.GetLine(clickedLine);
            size_t clickedCol = (relX > 0) ? static_cast<size_t>((relX + charW * 0.5f) / charW) : 0;
            if (clickedCol > lineStr.length()) clickedCol = lineStr.length();

            // Check if clicked an interactive TODO checkbox (especially in Task Editor 3)
            size_t xPos = lineStr.find(L"[x]");
            if (xPos == std::wstring::npos) xPos = lineStr.find(L"[X]");
            size_t emptyPos = lineStr.find(L"[ ]");
            size_t cbCol = (xPos != std::wstring::npos) ? xPos : emptyPos;

            if (cbCol != std::wstring::npos) {
                float cbLeft = ed.codeAreaBounds.left + cbCol * charW - 4.0f;
                float cbRight = cbLeft + 3.0f * charW + 8.0f;
                if (clientPt.x >= cbLeft && clientPt.x <= cbRight) {
                    std::wstring newLine = lineStr;
                    if (xPos != std::wstring::npos) {
                        newLine.replace(xPos, 3, L"[ ]");
                    } else if (emptyPos != std::wstring::npos) {
                        newLine.replace(emptyPos, 3, L"[x]");
                    }
                    ed.doc.buffer.SetLine(clickedLine, newLine);
                    return true;
                }
            }

            // Multi-cursor or standard cursor positioning
            if (altPressed) {
                // Add secondary cursor!
                ed.doc.secondarySelections.push_back({ { clickedLine, clickedCol }, { clickedLine, clickedCol } });
            } else {
                ed.doc.secondarySelections.clear();
                ed.doc.buffer.SetCursor(clickedLine, clickedCol);
                ed.doc.selAnchor = { clickedLine, clickedCol };
                ed.doc.selEnd = { clickedLine, clickedCol };
                ed.doc.hasSelection = false;
                ed.doc.isSelecting = true;
            }

            return true;
        }
    }

    if (m_focusedEditorIdx != -1) {
        m_focusedEditorIdx = -1;
        for (auto& ed : m_editors) ed.isFocused = false;
        return true;
    }
    return false;
}

bool InteractiveGuideView::HandleLButtonUp(POINT /*clientPt*/) {
    if (m_focusedEditorIdx >= 0 && m_focusedEditorIdx < static_cast<int>(m_editors.size())) {
        m_editors[m_focusedEditorIdx].doc.isSelecting = false;
        return true;
    }
    return false;
}

bool InteractiveGuideView::HandleChar(wchar_t ch) {
    if (m_focusedEditorIdx < 0 || m_focusedEditorIdx >= static_cast<int>(m_editors.size())) {
        return false;
    }
    auto& ed = m_editors[m_focusedEditorIdx];
    if (ch >= 32) {
        if (!ed.doc.secondarySelections.empty()) {
            ed.doc.buffer.InsertChar(ch);
            for (auto& sel : ed.doc.secondarySelections) {
                size_t l = sel.cursor.line;
                size_t c = sel.cursor.col;
                if (l < ed.doc.buffer.GetLineCount()) {
                    std::wstring line = ed.doc.buffer.GetLine(l);
                    if (c <= line.length()) {
                        line.insert(c, 1, ch);
                        ed.doc.buffer.SetLine(l, line);
                        sel.cursor.col++;
                        sel.anchor.col++;
                    }
                }
            }
        } else {
            if (ed.doc.HasSelection()) {
                size_t sLine, sCol, eLine, eCol;
                ed.doc.GetSelectionRange(sLine, sCol, eLine, eCol);
                ed.doc.buffer.DeleteRange(sLine, sCol, eLine, eCol);
                ed.doc.ClearSelection();
            }
            ed.doc.buffer.InsertChar(ch);
        }

        // In Editor 1 (JS): typing '.' opens IntelliSense
        if (ed.id == 1 && ch == L'.') {
            ed.showIntelliSense = true;
        }
        return true;
    }
    return false;
}

bool InteractiveGuideView::HandleKeyDown(WPARAM key) {
    if (m_focusedEditorIdx < 0 || m_focusedEditorIdx >= static_cast<int>(m_editors.size())) {
        return false;
    }
    auto& ed = m_editors[m_focusedEditorIdx];
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    // Ctrl+Space -> Toggle IntelliSense in Editor 1
    if (ctrl && key == VK_SPACE) {
        ed.showIntelliSense = !ed.showIntelliSense;
        return true;
    }

    // Esc -> dismiss IntelliSense / multi-cursors / selection
    if (key == VK_ESCAPE) {
        if (ed.showIntelliSense) { ed.showIntelliSense = false; return true; }
        if (!ed.doc.secondarySelections.empty()) { ed.doc.secondarySelections.clear(); return true; }
        if (ed.doc.HasSelection()) { ed.doc.ClearSelection(); return true; }
        return false;
    }

    // Tab -> accept ghost text (Editor 2) or complete IntelliSense (Editor 1) or indent
    if (key == VK_TAB) {
        if (ed.showIntelliSense) {
            InsertCompletion(ed, kCanvasApiCompletions[ed.intelliSenseSelectedIdx].name);
            ed.showIntelliSense = false;
            return true;
        }
        if (!ed.ghostText.empty() && ed.doc.buffer.GetCursor().line == ed.ghostLine) {
            for (wchar_t c : ed.ghostText) {
                ed.doc.buffer.InsertChar(c);
            }
            ed.ghostText.clear();
            return true;
        }
        for (int i = 0; i < 4; ++i) ed.doc.buffer.InsertChar(L' ');
        return true;
    }

    // Up / Down arrow navigation for IntelliSense
    if (ed.showIntelliSense) {
        if (key == VK_UP) {
            ed.intelliSenseSelectedIdx = (ed.intelliSenseSelectedIdx > 0) ? (ed.intelliSenseSelectedIdx - 1) : 5;
            return true;
        }
        if (key == VK_DOWN) {
            ed.intelliSenseSelectedIdx = (ed.intelliSenseSelectedIdx + 1) % 6;
            return true;
        }
        if (key == VK_RETURN) {
            InsertCompletion(ed, kCanvasApiCompletions[ed.intelliSenseSelectedIdx].name);
            ed.showIntelliSense = false;
            return true;
        }
    }

    // Ctrl+D -> Select next occurrence (multi-cursor in Editor 0)
    if (ctrl && (key == 'D' || key == 'd')) {
        auto cursor = ed.doc.buffer.GetCursor();
        const auto& line = ed.doc.buffer.GetLine(cursor.line);
        if (!line.empty()) {
            size_t col = (cursor.col < line.size()) ? cursor.col : (line.size() - 1);
            if (iswalnum(line[col]) || line[col] == L'_' || line[col] == L'-') {
                size_t startCol = col;
                while (startCol > 0 && (iswalnum(line[startCol - 1]) || line[startCol - 1] == L'_' || line[startCol - 1] == L'-')) --startCol;
                size_t endCol = col;
                while (endCol < line.size() && (iswalnum(line[endCol]) || line[endCol] == L'_' || line[endCol] == L'-')) ++endCol;
                std::wstring word = line.substr(startCol, endCol - startCol);
                for (size_t l = 0; l < ed.doc.buffer.GetLineCount(); ++l) {
                    if (l == cursor.line) continue;
                    const auto& otherLine = ed.doc.buffer.GetLine(l);
                    size_t pos = otherLine.find(word);
                    if (pos != std::wstring::npos) {
                        ed.doc.secondarySelections.push_back({ { l, pos }, { l, pos + word.length() } });
                    }
                }
            }
        }
        return true;
    }

    // Ctrl+A -> Select all
    if (ctrl && (key == 'A' || key == 'a')) {
        size_t count = ed.doc.buffer.GetLineCount();
        if (count > 0) {
            ed.doc.selAnchor = { 0, 0 };
            ed.doc.selEnd = { count - 1, ed.doc.buffer.GetLine(count - 1).length() };
            ed.doc.hasSelection = true;
            ed.doc.buffer.SetCursor(count - 1, ed.doc.buffer.GetLine(count - 1).length());
        }
        return true;
    }

    if (key == VK_BACK) {
        if (ed.doc.HasSelection()) {
            size_t sLine, sCol, eLine, eCol;
            ed.doc.GetSelectionRange(sLine, sCol, eLine, eCol);
            ed.doc.buffer.DeleteRange(sLine, sCol, eLine, eCol);
            ed.doc.ClearSelection();
        } else if (!ed.doc.secondarySelections.empty()) {
            ed.doc.buffer.Backspace();
            for (auto& sel : ed.doc.secondarySelections) {
                size_t l = sel.cursor.line;
                size_t c = sel.cursor.col;
                if (l < ed.doc.buffer.GetLineCount() && c > 0) {
                    std::wstring line = ed.doc.buffer.GetLine(l);
                    line.erase(c - 1, 1);
                    ed.doc.buffer.SetLine(l, line);
                    sel.cursor.col--;
                    sel.anchor.col--;
                }
            }
        } else {
            ed.doc.buffer.Backspace();
        }
        return true;
    }

    if (key == VK_DELETE) {
        if (ed.doc.HasSelection()) {
            size_t sLine, sCol, eLine, eCol;
            ed.doc.GetSelectionRange(sLine, sCol, eLine, eCol);
            ed.doc.buffer.DeleteRange(sLine, sCol, eLine, eCol);
            ed.doc.ClearSelection();
        } else {
            ed.doc.buffer.DeleteForward();
        }
        return true;
    }

    if (key == VK_RETURN) {
        ed.doc.buffer.InsertChar(L'\n');
        return true;
    }

    if (key == VK_LEFT) {
        ed.doc.buffer.MoveCursorLeft();
        ed.doc.ClearSelection();
        return true;
    }
    if (key == VK_RIGHT) {
        ed.doc.buffer.MoveCursorRight();
        ed.doc.ClearSelection();
        return true;
    }
    if (key == VK_UP) {
        ed.doc.buffer.MoveCursorUp();
        ed.doc.ClearSelection();
        return true;
    }
    if (key == VK_DOWN) {
        ed.doc.buffer.MoveCursorDown();
        ed.doc.ClearSelection();
        return true;
    }

    return false;
}

bool InteractiveGuideView::HandleMouseWheel(short delta, float /*viewHeight*/) {
    if (m_maxScrollY <= 0.0f) return false;

    float scrollAmount = (delta > 0) ? -60.0f : 60.0f;
    float oldScrollY = m_scrollY;
    m_scrollY = (std::clamp)(m_scrollY + scrollAmount, 0.0f, m_maxScrollY);

    return (m_scrollY != oldScrollY);
}

LPCWSTR InteractiveGuideView::GetCursorForPoint(POINT pt) const {
    for (const auto& ed : m_editors) {
        if (pt.x >= ed.codeAreaBounds.left && pt.x <= ed.codeAreaBounds.right &&
            pt.y >= ed.codeAreaBounds.top && pt.y <= ed.codeAreaBounds.bottom) {

            // Check if hovering over a checkbox in Editor 3
            if (ed.id == 3) {
                float lineH = 24.0f;
                float relY = pt.y - ed.codeAreaBounds.top;
                size_t l = (relY > 0) ? static_cast<size_t>(relY / lineH) : 0;
                if (l < ed.doc.buffer.GetLineCount()) {
                    std::wstring lineStr = ed.doc.buffer.GetLine(l);
                    size_t xPos = lineStr.find(L"[x]");
                    if (xPos == std::wstring::npos) xPos = lineStr.find(L"[X]");
                    size_t emptyPos = lineStr.find(L"[ ]");
                    size_t cbCol = (xPos != std::wstring::npos) ? xPos : emptyPos;
                    if (cbCol != std::wstring::npos) {
                        float charW = (m_cachedCharWidth > 0.0f) ? m_cachedCharWidth : 8.5f;
                        float cbLeft = ed.codeAreaBounds.left + cbCol * charW - 4.0f;
                        float cbRight = cbLeft + 3.0f * charW + 8.0f;
                        if (pt.x >= cbLeft && pt.x <= cbRight) {
                            return IDC_HAND;
                        }
                    }
                }
            }

            return IDC_IBEAM;
        }

        // Hover over IntelliSense dropdown
        if (ed.showIntelliSense &&
            pt.x >= ed.intelliSensePopupBounds.left && pt.x <= ed.intelliSensePopupBounds.right &&
            pt.y >= ed.intelliSensePopupBounds.top && pt.y <= ed.intelliSensePopupBounds.bottom) {
            return IDC_HAND;
        }
    }

    return nullptr;
}

} // namespace phide::ui
