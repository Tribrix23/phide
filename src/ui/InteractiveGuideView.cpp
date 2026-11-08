/**
 * @file InteractiveGuideView.cpp
 * @brief Implementation of the interactive feature guide and walkthrough view for Phide IDE.
 */

#include "phide/ui/InteractiveGuideView.hpp"
#include <algorithm>

namespace phide::ui {

InteractiveGuideView::InteractiveGuideView() {
    m_tasks = {
        { L"Set up database credentials and connection pool in db.php", true },
        { L"Configure routing and middlewares for REST API endpoints", true },
        { L"Implement user session authentication and token refresh", false },
        { L"Add rate limiting and CSRF protection headers", false },
        { L"Test live SSE streaming completion in API Tester", false }
    };
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

    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcBadge, 3.5f, 3.5f);
    pRT->FillRoundedRectangle(rrc, theme.brushItemHover ? theme.brushItemHover.Get() : theme.brushMenuBg.Get());
    pRT->DrawRoundedRectangle(rrc, theme.brushBorderHover ? theme.brushBorderHover.Get() : theme.brushBorderSubtle.Get(), 1.0f);

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

    // Subtle dark background with left colored accent border
    pRT->FillRoundedRectangle(rrcBox, theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushMenuBg.Get());
    pRT->DrawRoundedRectangle(rrcBox, theme.brushBorderSubtle ? theme.brushBorderSubtle.Get() : theme.brushBorderHover.Get(), 1.0f);

    // Left accent pill line
    pRT->DrawLine(
        D2D1::Point2F(x + 1.0f, y + 3.0f),
        D2D1::Point2F(x + 1.0f, y + totalH - 3.0f),
        theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(),
        2.5f
    );

    // Render title + text inside box
    D2D1_RECT_F rcText = D2D1::RectF(x + padX, y + padY, x + width - padX, y + totalH - padY);
    std::wstring fullMsg = title + L" " + text;
    pRT->DrawText(fullMsg.c_str(), static_cast<UINT32>(fullMsg.length()),
                  pBodyFmt, rcText, theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushTextPrimary.Get());
}

void InteractiveGuideView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                  float left, float width, float top, float height) {
    if (!pRT) return;

    m_viewBounds = D2D1::RectF(left, top, left + width, top + height);

    // Fill main canvas background
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
    IDWriteTextFormat* pCodeFmt = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();
    IDWriteTextFormat* pGutterFmt = theme.fontLineNumber ? theme.fontLineNumber.Get() : theme.fontCode.Get();

    // =========================================================================
    // 1. Header: "Interactive Guide"
    // =========================================================================
    D2D1_RECT_F rcTitle = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(38.0f));
    const wchar_t* titleStr = L"Interactive Guide";
    pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)),
                  pTitleFmt, rcTitle, theme.brushTextWhite.Get());
    curY += config.ScaleF(40.0f);

    const wchar_t* subStr = L"Explore and try out the powerful core features of Phide directly inside this interactive walkthrough. "
                            L"For full details on the editor, settings, and keybindings, see the Help and Preferences menus.";
    D2D1_RECT_F rcSub = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(44.0f));
    pRT->DrawText(subStr, static_cast<UINT32>(wcslen(subStr)),
                  pBodyFmt, rcSub, theme.brushTextSecondary.Get());
    curY += config.ScaleF(48.0f);

    // =========================================================================
    // Feature Highlights Grid (8 core features)
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

    for (size_t i = 0; i < 8; ++i) {
        float cx = contentLeft + (i % 2) * (cardW + cardGap);
        float cy = curY + (i / 2) * (cardH + config.ScaleF(8.0f));

        D2D1_RECT_F rcCard = D2D1::RectF(cx, cy, cx + cardW, cy + cardH);
        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, 4.0f, 4.0f);
        pRT->FillRoundedRectangle(rrcCard, theme.brushDrawerBg.Get());
        pRT->DrawRoundedRectangle(rrcCard, theme.brushBorderSubtle.Get(), 0.8f);

        // Feature title
        D2D1_RECT_F rcFeatTitle = D2D1::RectF(cx + config.ScaleF(12.0f), cy + config.ScaleF(8.0f),
                                              cx + cardW - config.ScaleF(12.0f), cy + config.ScaleF(26.0f));
        pRT->DrawText(highlights[i].title, static_cast<UINT32>(wcslen(highlights[i].title)),
                      theme.fontTitle ? theme.fontTitle.Get() : pHeadingFmt,
                      rcFeatTitle, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get());

        // Feature desc
        D2D1_RECT_F rcFeatDesc = D2D1::RectF(cx + config.ScaleF(12.0f), cy + config.ScaleF(28.0f),
                                             cx + cardW - config.ScaleF(12.0f), cy + cardH - config.ScaleF(6.0f));
        pRT->DrawText(highlights[i].desc, static_cast<UINT32>(wcslen(highlights[i].desc)),
                      theme.fontHint ? theme.fontHint.Get() : pBodyFmt,
                      rcFeatDesc, theme.brushTextMuted.Get());
    }
    curY += 4 * (cardH + config.ScaleF(8.0f)) + config.ScaleF(24.0f);

    // =========================================================================
    // SECTION 1: Multi-Cursor Editing
    // =========================================================================
    D2D1_RECT_F rcSec1Title = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(28.0f));
    const wchar_t* sec1Title = L"Multi-Cursor Editing";
    pRT->DrawText(sec1Title, static_cast<UINT32>(wcslen(sec1Title)),
                  pHeadingFmt, rcSec1Title, theme.brushTextWhite.Get());
    curY += config.ScaleF(32.0f);

    const wchar_t* sec1Desc = L"Using multiple cursors allows you to edit multiple parts of the document at once, greatly improving your productivity. Try the following actions in the code block below:";
    D2D1_RECT_F rcSec1Desc = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(36.0f));
    pRT->DrawText(sec1Desc, static_cast<UINT32>(wcslen(sec1Desc)),
                  pBodyFmt, rcSec1Desc, theme.brushTextSecondary.Get());
    curY += config.ScaleF(40.0f);

    // Step 1
    float stepX = contentLeft;
    float badgeH = config.ScaleF(20.0f);
    const wchar_t* step1Text = L"1. Box Selection - press any combination of Shift+DownArrow, Shift+RightArrow, Shift+UpArrow, Shift+LeftArrow while holding Shift+Alt to select a block of text. You can also drag-select using the middle mouse button.";
    D2D1_RECT_F rcStep1 = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(34.0f));
    pRT->DrawText(step1Text, static_cast<UINT32>(wcslen(step1Text)), pBodyFmt, rcStep1, theme.brushTextPrimary.Get());
    curY += config.ScaleF(38.0f);

    // Step 2
    const wchar_t* step2Text = L"2. Add a cursor - press Ctrl+Alt+UpArrow to add a new cursor above, or Ctrl+Alt+DownArrow to add a cursor below. You can also use your mouse with Alt+Click to add a cursor anywhere.";
    D2D1_RECT_F rcStep2 = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(34.0f));
    pRT->DrawText(step2Text, static_cast<UINT32>(wcslen(step2Text)), pBodyFmt, rcStep2, theme.brushTextPrimary.Get());
    curY += config.ScaleF(38.0f);

    // Step 3
    const wchar_t* step3Text = L"3. Create cursors on all occurrences of a string - select one instance of a string e.g. background-color and press Ctrl+D or Ctrl+F2. Now you can replace all instances simultaneously.";
    D2D1_RECT_F rcStep3 = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(34.0f));
    pRT->DrawText(step3Text, static_cast<UINT32>(wcslen(step3Text)), pBodyFmt, rcStep3, theme.brushTextPrimary.Get());
    curY += config.ScaleF(42.0f);

    // Code Box for Section 1 with Line Numbers and Color Swatches
    const float codeBoxH = config.ScaleF(118.0f);
    D2D1_RECT_F rcCodeBox1 = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + codeBoxH);
    D2D1_ROUNDED_RECT rrcCode1 = D2D1::RoundedRect(rcCodeBox1, 4.0f, 4.0f);
    pRT->FillRoundedRectangle(rrcCode1, theme.brushWindowBg.Get());
    pRT->DrawRoundedRectangle(rrcCode1, theme.brushBorderSubtle.Get(), 1.0f);

    // Gutter
    const float gutterW = config.ScaleF(36.0f);
    D2D1_RECT_F rcGutter1 = D2D1::RectF(contentLeft, curY, contentLeft + gutterW, curY + codeBoxH);
    pRT->FillRectangle(rcGutter1, theme.brushDrawerBg.Get());
    pRT->DrawLine(D2D1::Point2F(contentLeft + gutterW, curY),
                  D2D1::Point2F(contentLeft + gutterW, curY + codeBoxH),
                  theme.brushBorderSubtle.Get(), 0.8f);

    // Lines in Code Box 1
    struct CssCodeLine {
        const wchar_t* lineNum;
        const wchar_t* selector;
        const wchar_t* prop;
        const wchar_t* val;
        const wchar_t* comment;
        D2D1_COLOR_F swatchColor;
    };
    static const CssCodeLine cssLines[] = {
        { L"1", L"#p1", L"background-color", L"#ff0000;", L"/* red in HEX format */", D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f) },
        { L"2", L"#p2", L"background-color", L"hsl(120, 100%, 50%);", L"/* green in HSL format */", D2D1::ColorF(0.0f, 1.0f, 0.0f, 1.0f) },
        { L"3", L"#p3", L"background-color", L"rgba(0, 4, 255, 0.733);", L"/* blue with alpha channel in RGBA format */", D2D1::ColorF(0.0f, 0.015f, 1.0f, 0.733f) },
        { L"4", L"#p4", L"background-color", L"#f59e0b;", L"/* amber in HEX format */", D2D1::ColorF(0.96f, 0.62f, 0.04f, 1.0f) }
    };

    float lineH = config.ScaleF(24.0f);
    for (int i = 0; i < 4; ++i) {
        float ly = curY + config.ScaleF(8.0f) + i * lineH;

        // Line number
        D2D1_RECT_F rcNum = D2D1::RectF(contentLeft, ly, contentLeft + gutterW - config.ScaleF(8.0f), ly + lineH);
        pRT->DrawText(cssLines[i].lineNum, static_cast<UINT32>(wcslen(cssLines[i].lineNum)),
                      pGutterFmt, rcNum, theme.brushLineNumber.Get());

        // Code tokens
        float cx = contentLeft + gutterW + config.ScaleF(12.0f);

        // Selector: #p1 {
        std::wstring sel = std::wstring(cssLines[i].selector) + L" {";
        D2D1_RECT_F rcSel = D2D1::RectF(cx, ly, cx + config.ScaleF(50.0f), ly + lineH);
        pRT->DrawText(sel.c_str(), static_cast<UINT32>(sel.length()), pCodeFmt, rcSel,
                      theme.brushSyntaxKeyword ? theme.brushSyntaxKeyword.Get() : theme.brushBrandAccent.Get());
        cx += config.ScaleF(52.0f);

        // Property: background-color:
        std::wstring prop = std::wstring(cssLines[i].prop) + L": ";
        D2D1_RECT_F rcProp = D2D1::RectF(cx, ly, cx + config.ScaleF(150.0f), ly + lineH);
        pRT->DrawText(prop.c_str(), static_cast<UINT32>(prop.length()), pCodeFmt, rcProp,
                      theme.brushSyntaxType ? theme.brushSyntaxType.Get() : theme.brushTextPrimary.Get());
        cx += config.ScaleF(152.0f);

        // Inline Color Swatch
        const float swatchSz = config.ScaleF(12.0f);
        D2D1_RECT_F rcSwatch = D2D1::RectF(cx, ly + (lineH - swatchSz) / 2.0f, cx + swatchSz, ly + (lineH - swatchSz) / 2.0f + swatchSz);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pSwatchBrush;
        pRT->CreateSolidColorBrush(cssLines[i].swatchColor, &pSwatchBrush);
        if (pSwatchBrush) {
            pRT->FillRectangle(rcSwatch, pSwatchBrush.Get());
            pRT->DrawRectangle(rcSwatch, theme.brushTextWhite.Get(), 0.8f);
        }
        cx += swatchSz + config.ScaleF(6.0f);

        // Value: #ff0000;
        D2D1_RECT_F rcVal = D2D1::RectF(cx, ly, cx + config.ScaleF(190.0f), ly + lineH);
        pRT->DrawText(cssLines[i].val, static_cast<UINT32>(wcslen(cssLines[i].val)), pCodeFmt, rcVal,
                      theme.brushSyntaxString ? theme.brushSyntaxString.Get() : theme.brushTextPrimary.Get());
        cx += config.ScaleF(192.0f);

        // Comment
        D2D1_RECT_F rcComm = D2D1::RectF(cx, ly, contentLeft + contentW - config.ScaleF(10.0f), ly + lineH);
        pRT->DrawText(cssLines[i].comment, static_cast<UINT32>(wcslen(cssLines[i].comment)), pCodeFmt, rcComm,
                      theme.brushSyntaxComment.Get());
    }
    curY += codeBoxH + config.ScaleF(14.0f);

    // Callout box for CSS Tip
    float calloutH1 = 0.0f;
    DrawCalloutBox(pRT, theme, config, contentLeft, curY, contentW,
                   L"CSS Tip:",
                   L"You may have noticed in the example above we also provide color swatches inline for CSS; additionally if you hover over an element such as #p1 we will show how this is represented in HTML. These swatches also act as color pickers that allow you to easily change a color value. A simple example of some language-specific editor features.",
                   calloutH1);
    curY += calloutH1 + config.ScaleF(32.0f);

    // =========================================================================
    // SECTION 2: IntelliSense
    // =========================================================================
    D2D1_RECT_F rcSec2Title = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(28.0f));
    const wchar_t* sec2Title = L"IntelliSense";
    pRT->DrawText(sec2Title, static_cast<UINT32>(wcslen(sec2Title)),
                  pHeadingFmt, rcSec2Title, theme.brushTextWhite.Get());
    curY += config.ScaleF(32.0f);

    const wchar_t* sec2Desc = L"Phide comes with powerful IntelliSense for JavaScript, TypeScript, PHP, Python, and Web APIs pre-installed. In the below example, position the text cursor right after the dot and press Ctrl+Space to invoke IntelliSense. Notice how suggestions come from the Canvas API.";
    D2D1_RECT_F rcSec2Desc = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(36.0f));
    pRT->DrawText(sec2Desc, static_cast<UINT32>(wcslen(sec2Desc)),
                  pBodyFmt, rcSec2Desc, theme.brushTextSecondary.Get());
    curY += config.ScaleF(42.0f);

    // Code Box for Section 2 with floating autocomplete preview
    const float codeBox2H = config.ScaleF(175.0f);
    D2D1_RECT_F rcCodeBox2 = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + codeBox2H);
    D2D1_ROUNDED_RECT rrcCode2 = D2D1::RoundedRect(rcCodeBox2, 4.0f, 4.0f);
    pRT->FillRoundedRectangle(rrcCode2, theme.brushWindowBg.Get());
    pRT->DrawRoundedRectangle(rrcCode2, theme.brushBorderSubtle.Get(), 1.0f);

    D2D1_RECT_F rcGutter2 = D2D1::RectF(contentLeft, curY, contentLeft + gutterW, curY + codeBox2H);
    pRT->FillRectangle(rcGutter2, theme.brushDrawerBg.Get());
    pRT->DrawLine(D2D1::Point2F(contentLeft + gutterW, curY),
                  D2D1::Point2F(contentLeft + gutterW, curY + codeBox2H),
                  theme.brushBorderSubtle.Get(), 0.8f);

    static const wchar_t* jsLines[] = {
        L"const canvas = document.querySelector('canvas');",
        L"const context = canvas.getContext('2d');",
        L"",
        L"context.strokeStyle = 'blue';",
        L"context.fillRect(0, 0, 150, 75);",
        L"context."
    };

    for (int i = 0; i < 6; ++i) {
        float ly = curY + config.ScaleF(6.0f) + i * lineH;
        std::wstring numStr = std::to_wstring(i + 1);
        D2D1_RECT_F rcNum = D2D1::RectF(contentLeft, ly, contentLeft + gutterW - config.ScaleF(8.0f), ly + lineH);
        pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()), pGutterFmt, rcNum, theme.brushLineNumber.Get());

        float cx = contentLeft + gutterW + config.ScaleF(12.0f);
        D2D1_RECT_F rcTxt = D2D1::RectF(cx, ly, contentLeft + contentW - config.ScaleF(10.0f), ly + lineH);
        pRT->DrawText(jsLines[i], static_cast<UINT32>(wcslen(jsLines[i])), pCodeFmt, rcTxt, theme.brushTextPrimary.Get());
    }

    // Simulated floating autocomplete popup under line 6 (context.)
    float popupX = contentLeft + gutterW + config.ScaleF(85.0f);
    float popupY = curY + config.ScaleF(6.0f) + 6 * lineH - config.ScaleF(4.0f);
    float popupW = config.ScaleF(260.0f);
    float popupH = config.ScaleF(120.0f);

    D2D1_RECT_F rcPopup = D2D1::RectF(popupX, popupY, popupX + popupW, popupY + popupH);
    D2D1_ROUNDED_RECT rrcPopup = D2D1::RoundedRect(rcPopup, 3.0f, 3.0f);
    pRT->FillRoundedRectangle(rrcPopup, theme.brushMenuBg.Get());
    pRT->DrawRoundedRectangle(rrcPopup, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushBorderHover.Get(), 1.0f);

    struct AutoItem {
        const wchar_t* icon;
        const wchar_t* name;
        const wchar_t* type;
        bool active;
    };
    static const AutoItem autoList[] = {
        { L"m", L"arc(x, y, r, sAngle, eAngle)", L"method", false },
        { L"m", L"beginPath()", L"method", false },
        { L"m", L"clearRect(x, y, w, h)", L"method", false },
        { L"p", L"fillStyle", L"property", false },
        { L"m", L"fillRect(x, y, w, h)", L"method", true },
        { L"m", L"stroke()", L"method", false }
    };

    float autoRowH = config.ScaleF(19.0f);
    for (int i = 0; i < 6; ++i) {
        float ay = popupY + config.ScaleF(3.0f) + i * autoRowH;
        D2D1_RECT_F rcRow = D2D1::RectF(popupX + 2.0f, ay, popupX + popupW - 2.0f, ay + autoRowH);
        if (autoList[i].active) {
            pRT->FillRectangle(rcRow, theme.brushItemHover ? theme.brushItemHover.Get() : theme.brushBrandAccent.Get());
        }

        // Icon badge
        D2D1_RECT_F rcBadge = D2D1::RectF(popupX + config.ScaleF(6.0f), ay + config.ScaleF(2.0f),
                                          popupX + config.ScaleF(20.0f), ay + autoRowH - config.ScaleF(2.0f));
        pRT->DrawText(autoList[i].icon, 1, theme.fontBadge ? theme.fontBadge.Get() : pGutterFmt,
                      rcBadge, theme.brushBrandAccent.Get());

        // Name
        D2D1_RECT_F rcName = D2D1::RectF(popupX + config.ScaleF(24.0f), ay, popupX + popupW - config.ScaleF(6.0f), ay + autoRowH);
        pRT->DrawText(autoList[i].name, static_cast<UINT32>(wcslen(autoList[i].name)),
                      theme.fontMenuItem ? theme.fontMenuItem.Get() : pBodyFmt,
                      rcName, autoList[i].active ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());
    }
    curY += codeBox2H + config.ScaleF(14.0f);

    // Callout box for IntelliSense Tip
    float calloutH2 = 0.0f;
    DrawCalloutBox(pRT, theme, config, contentLeft, curY, contentW,
                   L"Tip:",
                   L"While we ship JavaScript, TypeScript, PHP, and Python support out of the box, other languages can be upgraded with better IntelliSense and completions through one of the many extensions or plugins.",
                   calloutH2);
    curY += calloutH2 + config.ScaleF(32.0f);

    // =========================================================================
    // SECTION 3: Predictive Autocomplete & Adaptive Learning
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

    // Code Box for Section 3 with Ghost Text
    const float codeBox3H = config.ScaleF(145.0f);
    D2D1_RECT_F rcCodeBox3 = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + codeBox3H);
    D2D1_ROUNDED_RECT rrcCode3 = D2D1::RoundedRect(rcCodeBox3, 4.0f, 4.0f);
    pRT->FillRoundedRectangle(rrcCode3, theme.brushWindowBg.Get());
    pRT->DrawRoundedRectangle(rrcCode3, theme.brushBorderSubtle.Get(), 1.0f);

    D2D1_RECT_F rcGutter3 = D2D1::RectF(contentLeft, curY, contentLeft + gutterW, curY + codeBox3H);
    pRT->FillRectangle(rcGutter3, theme.brushDrawerBg.Get());
    pRT->DrawLine(D2D1::Point2F(contentLeft + gutterW, curY),
                  D2D1::Point2F(contentLeft + gutterW, curY + codeBox3H),
                  theme.brushBorderSubtle.Get(), 0.8f);

    static const wchar_t* phpLines[] = {
        L"<?php",
        L"function fetchUserStats(int $userId): array {",
        L"    $pdo = Database::getConnection();",
        L"    $stmt = $pdo->prepare(\"SELECT * FROM stats WHERE user_id = :id\");",
        L"    $stmt->execute(['id' => $userId]);",
        L"    return $stmt->fetchAll(PDO::FETCH_ASSOC);",
        L"}"
    };

    for (int i = 0; i < 7; ++i) {
        float ly = curY + config.ScaleF(6.0f) + i * lineH;
        std::wstring numStr = std::to_wstring(i + 1);
        D2D1_RECT_F rcNum = D2D1::RectF(contentLeft, ly, contentLeft + gutterW - config.ScaleF(8.0f), ly + lineH);
        pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()), pGutterFmt, rcNum, theme.brushLineNumber.Get());

        float cx = contentLeft + gutterW + config.ScaleF(12.0f);
        if (i == 5) {
            // Render line with ghost text and Tab badge
            const wchar_t* typedText = L"    return $stmt->";
            D2D1_RECT_F rcTyped = D2D1::RectF(cx, ly, cx + config.ScaleF(145.0f), ly + lineH);
            pRT->DrawText(typedText, static_cast<UINT32>(wcslen(typedText)), pCodeFmt, rcTyped, theme.brushTextPrimary.Get());

            // Ghost text in muted color
            float ghostX = cx + config.ScaleF(145.0f);
            const wchar_t* ghostText = L"fetchAll(PDO::FETCH_ASSOC);";
            D2D1_RECT_F rcGhost = D2D1::RectF(ghostX, ly, ghostX + config.ScaleF(240.0f), ly + lineH);
            pRT->DrawText(ghostText, static_cast<UINT32>(wcslen(ghostText)), pCodeFmt, rcGhost, theme.brushTextMuted.Get());

            // Keycap Tab badge
            float tabBadgeX = ghostX + config.ScaleF(250.0f);
            DrawKeycapBadge(pRT, theme, config, L"Tab to accept", tabBadgeX, ly + lineH / 2.0f, config.ScaleF(18.0f));
        } else {
            D2D1_RECT_F rcTxt = D2D1::RectF(cx, ly, contentLeft + contentW - config.ScaleF(10.0f), ly + lineH);
            pRT->DrawText(phpLines[i], static_cast<UINT32>(wcslen(phpLines[i])), pCodeFmt, rcTxt, theme.brushTextPrimary.Get());
        }
    }
    curY += codeBox3H + config.ScaleF(14.0f);

    // Callout box for Prediction Tip
    float calloutH3 = 0.0f;
    DrawCalloutBox(pRT, theme, config, contentLeft, curY, contentW,
                   L"Tip:",
                   L"Press Tab to accept predictive ghost text suggestions instantly, or press Esc to dismiss them. The engine adapts in real-time as you write code, tailoring suggestions to your specific project naming conventions.",
                   calloutH3);
    curY += calloutH3 + config.ScaleF(32.0f);

    // =========================================================================
    // SECTION 4: Interactive TODO Checkboxes
    // =========================================================================
    D2D1_RECT_F rcSec4Title = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(28.0f));
    const wchar_t* sec4Title = L"Interactive Task Checkboxes";
    pRT->DrawText(sec4Title, static_cast<UINT32>(wcslen(sec4Title)),
                  pHeadingFmt, rcSec4Title, theme.brushTextWhite.Get());
    curY += config.ScaleF(32.0f);

    const wchar_t* sec4Desc = L"Project documentation and markdown files support interactive checkboxes. Click on any checkbox below to interactively toggle its completion state:";
    D2D1_RECT_F rcSec4Desc = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + config.ScaleF(28.0f));
    pRT->DrawText(sec4Desc, static_cast<UINT32>(wcslen(sec4Desc)),
                  pBodyFmt, rcSec4Desc, theme.brushTextSecondary.Get());
    curY += config.ScaleF(34.0f);

    // Checkboxes List Box
    const float taskRowH = config.ScaleF(28.0f);
    const float taskBoxH = m_tasks.size() * taskRowH + config.ScaleF(16.0f);
    D2D1_RECT_F rcTaskBox = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + taskBoxH);
    D2D1_ROUNDED_RECT rrcTaskBox = D2D1::RoundedRect(rcTaskBox, 4.0f, 4.0f);
    pRT->FillRoundedRectangle(rrcTaskBox, theme.brushDrawerBg.Get());
    pRT->DrawRoundedRectangle(rrcTaskBox, theme.brushBorderSubtle.Get(), 1.0f);

    for (size_t i = 0; i < m_tasks.size(); ++i) {
        float ty = curY + config.ScaleF(8.0f) + i * taskRowH;
        float boxSz = config.ScaleF(16.0f);

        // Store hit box for mouse clicks
        m_tasks[i].boxBounds = D2D1::RectF(
            contentLeft + config.ScaleF(14.0f),
            ty + (taskRowH - boxSz) / 2.0f,
            contentLeft + config.ScaleF(14.0f) + boxSz,
            ty + (taskRowH - boxSz) / 2.0f + boxSz
        );

        // Draw checkbox square
        D2D1_ROUNDED_RECT rrcBox = D2D1::RoundedRect(m_tasks[i].boxBounds, 3.0f, 3.0f);
        if (m_tasks[i].checked) {
            pRT->FillRoundedRectangle(rrcBox, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushRunGreen.Get());
            pRT->DrawRoundedRectangle(rrcBox, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushRunGreen.Get(), 1.0f);

            // Checkmark glyph ✓
            const wchar_t* checkGlyph = L"✓";
            pRT->DrawText(checkGlyph, 1, theme.fontButtonCenter ? theme.fontButtonCenter.Get() : pBodyFmt,
                          m_tasks[i].boxBounds, theme.brushTextWhite.Get());
        } else {
            pRT->DrawRoundedRectangle(rrcBox, theme.brushBorderHover ? theme.brushBorderHover.Get() : theme.brushTextMuted.Get(), 1.5f);
        }

        // Label
        float labelX = contentLeft + config.ScaleF(14.0f) + boxSz + config.ScaleF(10.0f);
        D2D1_RECT_F rcLabel = D2D1::RectF(labelX, ty + config.ScaleF(3.0f), contentLeft + contentW - config.ScaleF(10.0f), ty + taskRowH);
        ID2D1SolidColorBrush* pTxtBrush = m_tasks[i].checked
            ? (theme.brushTextMuted ? theme.brushTextMuted.Get() : theme.brushTextSecondary.Get())
            : theme.brushTextPrimary.Get();

        pRT->DrawText(m_tasks[i].label.c_str(), static_cast<UINT32>(m_tasks[i].label.length()),
                      pBodyFmt, rcLabel, pTxtBrush);

        // If checked, draw subtle strikethrough line
        if (m_tasks[i].checked) {
            float textLenEst = m_tasks[i].label.length() * config.ScaleF(8.0f);
            float strikeY = ty + taskRowH / 2.0f;
            pRT->DrawLine(D2D1::Point2F(labelX, strikeY),
                          D2D1::Point2F((std::min)(labelX + textLenEst, contentLeft + contentW - config.ScaleF(20.0f)), strikeY),
                          theme.brushTextMuted.Get(), 1.0f);
        }
    }
    curY += taskBoxH + config.ScaleF(14.0f);

    // Callout box for Checkboxes Tip
    float calloutH4 = 0.0f;
    DrawCalloutBox(pRT, theme, config, contentLeft, curY, contentW,
                   L"Tip:",
                   L"In any Markdown file (.md), write `- [ ]` or `- [x]` to create interactive task checkboxes that you can click right in the editor canvas to manage to-do items effortlessly.",
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

    // API Tester Preview Card
    const float apiCardH = config.ScaleF(80.0f);
    D2D1_RECT_F rcApiCard = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + apiCardH);
    D2D1_ROUNDED_RECT rrcApiCard = D2D1::RoundedRect(rcApiCard, 4.0f, 4.0f);
    pRT->FillRoundedRectangle(rrcApiCard, theme.brushWindowBg.Get());
    pRT->DrawRoundedRectangle(rrcApiCard, theme.brushBorderSubtle.Get(), 1.0f);

    // Method badge [POST]
    float badgeCurX = contentLeft + config.ScaleF(14.0f);
    float badgeCenterY = curY + config.ScaleF(24.0f);
    DrawKeycapBadge(pRT, theme, config, L"POST", badgeCurX, badgeCenterY, config.ScaleF(22.0f));

    // URL
    D2D1_RECT_F rcUrl = D2D1::RectF(badgeCurX + config.ScaleF(6.0f), curY + config.ScaleF(14.0f),
                                    contentLeft + contentW - config.ScaleF(12.0f), curY + config.ScaleF(34.0f));
    const wchar_t* apiUrl = L"https://api.xkiro.com/v1/chat/completions";
    pRT->DrawText(apiUrl, static_cast<UINT32>(wcslen(apiUrl)), pCodeFmt, rcUrl, theme.brushTextPrimary.Get());

    // Badges: 200 OK, 3.37 s, 4.76 KB
    float statY = curY + config.ScaleF(46.0f);
    float statX = contentLeft + config.ScaleF(14.0f);

    // 200 OK badge (green)
    D2D1_RECT_F rc200 = D2D1::RectF(statX, statY, statX + config.ScaleF(64.0f), statY + config.ScaleF(20.0f));
    D2D1_ROUNDED_RECT rrc200 = D2D1::RoundedRect(rc200, 3.0f, 3.0f);
    pRT->FillRoundedRectangle(rrc200, theme.brushRunGreen ? theme.brushRunGreen.Get() : theme.brushItemHover.Get());
    const wchar_t* stat200 = L"200 OK";
    pRT->DrawText(stat200, static_cast<UINT32>(wcslen(stat200)),
                  theme.fontBadge ? theme.fontBadge.Get() : pBodyFmt, rc200, theme.brushTextWhite.Get());
    statX += config.ScaleF(72.0f);

    // Timing
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
    int oldHovered = m_hoveredTask;
    m_hoveredTask = -1;

    for (size_t i = 0; i < m_tasks.size(); ++i) {
        if (clientPt.x >= m_tasks[i].boxBounds.left && clientPt.x <= m_tasks[i].boxBounds.right &&
            clientPt.y >= m_tasks[i].boxBounds.top && clientPt.y <= m_tasks[i].boxBounds.bottom) {
            m_hoveredTask = static_cast<int>(i);
            break;
        }
    }
    return (m_hoveredTask != oldHovered);
}

bool InteractiveGuideView::HandleLButtonDown(POINT clientPt) {
    for (size_t i = 0; i < m_tasks.size(); ++i) {
        if (clientPt.x >= m_tasks[i].boxBounds.left && clientPt.x <= m_tasks[i].boxBounds.right &&
            clientPt.y >= m_tasks[i].boxBounds.top && clientPt.y <= m_tasks[i].boxBounds.bottom) {
            m_tasks[i].checked = !m_tasks[i].checked;
            return true;
        }
    }
    return false;
}

bool InteractiveGuideView::HandleMouseWheel(short delta, float viewHeight) {
    if (m_maxScrollY <= 0.0f) return false;

    float scrollAmount = (delta > 0) ? -60.0f : 60.0f;
    float oldScrollY = m_scrollY;
    m_scrollY = (std::clamp)(m_scrollY + scrollAmount, 0.0f, m_maxScrollY);

    return (m_scrollY != oldScrollY);
}

LPCWSTR InteractiveGuideView::GetCursorForPoint(POINT pt) const {
    for (const auto& task : m_tasks) {
        if (pt.x >= task.boxBounds.left && pt.x <= task.boxBounds.right &&
            pt.y >= task.boxBounds.top && pt.y <= task.boxBounds.bottom) {
            return IDC_HAND;
        }
    }
    return nullptr;
}

} // namespace phide::ui
