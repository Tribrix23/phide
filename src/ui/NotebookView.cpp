/**
 * @file NotebookView.cpp
 * @brief Implementation of the interactive Jupyter / Colab-style notebook interface.
 */

#include "phide/ui/NotebookView.hpp"
#include <filesystem>
#include <chrono>
#include <sstream>
#include <algorithm>

namespace {
void SetClipboardText(const std::wstring& text, HWND hwnd) {
    if (text.empty()) return;
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t bytes = (text.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* pMem = GlobalLock(hMem);
            if (pMem) {
                memcpy(pMem, text.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
}

std::wstring GetClipboardText(HWND hwnd) {
    std::wstring result;
    if (OpenClipboard(hwnd)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            LPCWSTR pText = static_cast<LPCWSTR>(GlobalLock(hData));
            if (pText) {
                result = pText;
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }
    return result;
}
} // anonymous namespace

namespace phide::ui {

float NotebookView::EnsureCharWidth(Theme& theme, const app::AppConfig& config) {
    if (m_charWidth <= 0.0f && theme.dwriteFactory && theme.fontCode) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                L"M", 1, theme.fontCode.Get(), 1000.0f, 100.0f, &pLayout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                m_charWidth = tm.widthIncludingTrailingWhitespace;
            }
        }
    }
    return (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.8f);
}

bool NotebookView::HitTestCodeArea(const editor::NotebookCell& cell, POINT clientPt, const app::AppConfig& config,
                                   size_t& outLine, size_t& outCol) const {
    const float lineH = config.ScaleF(20.0f);
    const float lineNumW = config.ScaleF(28.0f);
    const float textX = cell.codeAreaBounds.left + lineNumW + config.ScaleF(8.0f);
    float relY = static_cast<float>(clientPt.y) - cell.codeAreaBounds.top - config.ScaleF(6.0f);
    if (relY < 0.0f) relY = 0.0f;

    size_t totalLines = cell.inputBuffer.GetLineCount();
    if (totalLines == 0) {
        outLine = 0;
        outCol = 0;
        return true;
    }

    size_t lineIdx = static_cast<size_t>(relY / lineH);
    if (lineIdx >= totalLines) {
        lineIdx = totalLines - 1;
    }

    const auto& lineStr = cell.inputBuffer.GetLine(lineIdx);
    float charW = (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.8f);

    float relX = static_cast<float>(clientPt.x) - textX;
    if (relX <= 0.0f) {
        outLine = lineIdx;
        outCol = 0;
        return true;
    }

    // Find nearest column boundary
    size_t col = 0;
    float bestDiff = 999999.0f;
    for (size_t c = 0; c <= lineStr.length(); ++c) {
        float vx = static_cast<float>(c) * charW;
        float diff = std::abs(relX - vx);
        if (diff < bestDiff) {
            bestDiff = diff;
            col = c;
        }
    }
    if (relX >= static_cast<float>(lineStr.length()) * charW) {
        col = lineStr.length();
    }

    outLine = lineIdx;
    outCol = col;
    return true;
}

NotebookView::NotebookView() {
    m_lastBlinkTime = GetTickCount64();
    m_lastAnimTick = GetTickCount64();
}

NotebookView::~NotebookView() {
    StopExecution_Internal();
    if (m_execThread.joinable()) {
        m_execThread.detach();
    }
}

void NotebookView::DrawRunMorphButton(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     const D2D1_RECT_F& bounds, bool isExecuting, bool isQueued, bool hasError, bool isHovered) {
    if (!pRT) return;
    const float cx = (bounds.left + bounds.right) / 2.0f;
    const float cy = (bounds.top + bounds.bottom) / 2.0f;
    const float r = (std::min)(bounds.right - bounds.left, bounds.bottom - bounds.top) / 2.0f;

    if (isExecuting || isQueued) {
        // Inner circle background
        const float innerR = r * 0.72f;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushInner;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f), &brushInner);
        if (brushInner) {
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), innerR, innerR), brushInner.Get());
        }

        // Center Stop Square ⏹
        const float sqSize = r * 0.58f;
        D2D1_RECT_F sqRc = D2D1::RectF(cx - sqSize / 2.0f, cy - sqSize / 2.0f, cx + sqSize / 2.0f, cy + sqSize / 2.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushStop;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.98f, 1.0f), &brushStop);
        if (brushStop) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(sqRc, 1.5f, 1.5f), brushStop.Get());
        }

        const float ringR = r * 0.90f;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSpin;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.65f, 0.98f, 1.0f), &brushSpin); // Electric cyan/blue spinner
        if (brushSpin) {
            if (isQueued) {
                // Dashed line spinner for queued state (Image 3)
                const int dashCount = 10;
                const float arcSweep = 360.0f / static_cast<float>(dashCount);
                const float dashSweep = arcSweep * 0.50f;
                for (int i = 0; i < dashCount; ++i) {
                    float deg1 = m_spinAngle + static_cast<float>(i) * arcSweep;
                    float deg2 = deg1 + dashSweep;
                    float rad1 = deg1 * 3.14159265f / 180.0f;
                    float rad2 = deg2 * 3.14159265f / 180.0f;
                    D2D1_POINT_2F p1 = D2D1::Point2F(cx + ringR * cosf(rad1), cy + ringR * sinf(rad1));
                    D2D1_POINT_2F p2 = D2D1::Point2F(cx + ringR * cosf(rad2), cy + ringR * sinf(rad2));
                    pRT->DrawLine(p1, p2, brushSpin.Get(), config.ScaleF(1.8f));
                }
            } else {
                // Connected continuous solid line rotating arc for active execution (Image 1)
                const int segments = 32;
                const float sweepAngle = 270.0f;
                float baseRad = m_spinAngle * 3.14159265f / 180.0f;
                float sweepRad = sweepAngle * 3.14159265f / 180.0f;
                for (int s = 0; s < segments; ++s) {
                    float a1 = baseRad + sweepRad * (static_cast<float>(s) / segments);
                    float a2 = baseRad + sweepRad * (static_cast<float>(s + 1) / segments);
                    D2D1_POINT_2F p1 = D2D1::Point2F(cx + ringR * cosf(a1), cy + ringR * sinf(a1));
                    D2D1_POINT_2F p2 = D2D1::Point2F(cx + ringR * cosf(a2), cy + ringR * sinf(a2));
                    pRT->DrawLine(p1, p2, brushSpin.Get(), config.ScaleF(2.0f));
                }
            }
        }
    } else {
        // Idle state: Circle with ▶
        const float circleR = r * 0.82f;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBg;
        if (isHovered) {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.24f, 0.28f, 1.0f), &brushBg);
        } else {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.15f, 0.18f, 0.9f), &brushBg);
        }
        if (brushBg) {
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), circleR, circleR), brushBg.Get());
        }

        // Circle outline
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushOutline;
        if (hasError) {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.32f, 0.32f, 1.0f), &brushOutline); // Red ring (Image 5)
        } else if (isHovered) {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.40f, 0.45f, 1.0f), &brushOutline);
        } else {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.26f, 0.26f, 0.30f, 1.0f), &brushOutline);
        }
        if (brushOutline) {
            pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), circleR, circleR), brushOutline.Get(), hasError ? 1.8f : 1.0f);
        }

        // Triangle Play Glyph ▶
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushGlyph;
        if (hasError) {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.32f, 0.32f, 1.0f), &brushGlyph); // Red play icon (Image 5)
        } else {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.82f, 0.48f, 1.0f), &brushGlyph); // Green play icon
        }

        if (brushGlyph) {
            float triW = circleR * 0.65f;
            float triH = circleR * 0.75f;
            float triLeft = cx - triW * 0.38f;

            Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
            pRT->GetFactory(&pFactory);
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> pTri;
            if (pFactory && SUCCEEDED(pFactory->CreatePathGeometry(&pTri))) {
                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(pTri->Open(&sink))) {
                    sink->BeginFigure(D2D1::Point2F(triLeft, cy - triH / 2.0f), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(triLeft + triW, cy));
                    sink->AddLine(D2D1::Point2F(triLeft, cy + triH / 2.0f));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    pRT->FillGeometry(pTri.Get(), brushGlyph.Get());
                }
            }
        }
    }
}

void NotebookView::RenderTooltip(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                 const D2D1_RECT_F& targetBounds, const std::wstring& text) {
    if (!pRT || text.empty()) return;
    const float tipH = config.ScaleF(24.0f);
    const float tipPadX = config.ScaleF(10.0f);
    float textW = static_cast<float>(text.length()) * config.ScaleF(7.0f);
    float tipW = textW + tipPadX * 2.0f;
    float tipX = targetBounds.left + ((targetBounds.right - targetBounds.left) - tipW) / 2.0f;
    if (tipX < config.ScaleF(8.0f)) tipX = config.ScaleF(8.0f);
    float tipY = targetBounds.bottom + config.ScaleF(6.0f);

    D2D1_RECT_F rcTip = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);
    D2D1_ROUNDED_RECT rrcTip = D2D1::RoundedRect(rcTip, 4.0f, 4.0f);

    // Draw Tooltip Card Background & Border
    if (theme.brushTooltipBg) {
        pRT->FillRoundedRectangle(rrcTip, theme.brushTooltipBg.Get());
    } else {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.15f, 0.17f, 0.96f), &brushBg);
        if (brushBg) pRT->FillRoundedRectangle(rrcTip, brushBg.Get());
    }
    if (theme.brushTooltipBorder) {
        pRT->DrawRoundedRectangle(rrcTip, theme.brushTooltipBorder.Get(), 1.0f);
    } else {
        pRT->DrawRoundedRectangle(rrcTip, theme.brushBorderSubtle.Get(), 1.0f);
    }

    D2D1_RECT_F rcText = D2D1::RectF(tipX + tipPadX, tipY, tipX + tipW - tipPadX, tipY + tipH);
    ID2D1Brush* textBrush = theme.brushTooltipText ? theme.brushTooltipText.Get() : theme.brushTextPrimary.Get();
    IDWriteTextFormat* tf = theme.fontTooltip.Get() ? theme.fontTooltip.Get() : theme.fontBadge.Get();
    pRT->DrawText(text.c_str(), static_cast<UINT32>(text.length()), tf, rcText, textBrush);
}

void NotebookView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          editor::OpenDocument& doc, float left, float width, float top, float height) {
    if (!pRT) return;

    if (!doc.notebookDoc) {
        doc.notebookDoc = std::make_shared<editor::NotebookDocument>();
        if (!doc.filePath.empty()) {
            doc.notebookDoc->LoadFromFile(doc.filePath);
        }
    }
    if (doc.notebookDoc->cells.empty()) {
        doc.notebookDoc->InitDefaultScratchpad();
    }
    m_scrollY = doc.notebookDoc->scrollY;

    auto nb = doc.notebookDoc;
    m_viewHeight = height;

    // Blink timer update
    ULONGLONG now = GetTickCount64();
    if (now - m_lastBlinkTime >= 500) {
        m_caretVisible = !m_caretVisible;
        m_lastBlinkTime = now;
    }

    // Animation tick update for spinner (spin if executing OR if any cell is queued)
    bool isAnySpinning = m_isExecuting.load();
    if (!isAnySpinning && nb) {
        for (const auto& c : nb->cells) {
            if (c.isQueued || c.isExecuting) {
                isAnySpinning = true;
                break;
            }
        }
    }
    if (isAnySpinning) {
        float dt = static_cast<float>(now - m_lastAnimTick) / 1000.0f;
        m_spinAngle += dt * 360.0f;
        if (m_spinAngle >= 360.0f) m_spinAngle -= 360.0f;
    }
    m_lastAnimTick = now;

    // 1. Fill notebook canvas background
    D2D1_RECT_F rcCanvas = D2D1::RectF(left, top, left + width, top + height);
    if (theme.brushCanvasBg) {
        pRT->FillRectangle(rcCanvas, theme.brushCanvasBg.Get());
    } else {
        pRT->FillRectangle(rcCanvas, theme.brushWindowBg.Get());
    }

    // 2. Render Top Toolbar (Minimal Icon-only: Morphing Run/Stop Button + Clear Button)
    const float toolbarH = config.ScaleF(38.0f);
    D2D1_RECT_F rcToolbar = D2D1::RectF(left, top, left + width, top + toolbarH);

    // Toolbar background with subtle bottom border
    if (theme.brushHeaderBg) {
        pRT->FillRectangle(rcToolbar, theme.brushHeaderBg.Get());
    } else {
        pRT->FillRectangle(rcToolbar, theme.brushWindowBg.Get());
    }
    pRT->DrawLine(
        D2D1::Point2F(left, top + toolbarH - 0.5f),
        D2D1::Point2F(left + width, top + toolbarH - 0.5f),
        theme.brushBorderSubtle.Get(),
        1.0f
    );

    float curBtnX = left + config.ScaleF(14.0f);
    const float btnSize = config.ScaleF(26.0f);
    const float btnY = top + (toolbarH - btnSize) / 2.0f;

    // [▶ / ⏹ Morphing Run All Button]
    m_btnRunAll = D2D1::RectF(curBtnX, btnY, curBtnX + btnSize, btnY + btnSize);
    DrawRunMorphButton(pRT, theme, config, m_btnRunAll, m_isExecuting.load(), false, false, m_hoveredToolbarBtn == 1);
    curBtnX += btnSize + config.ScaleF(10.0f);

    // [Clear All Outputs Vector Button]
    m_btnClearAll = D2D1::RectF(curBtnX, btnY, curBtnX + btnSize, btnY + btnSize);
    D2D1_ROUNDED_RECT rrcClear = D2D1::RoundedRect(m_btnClearAll, config.ScaleF(4.0f), config.ScaleF(4.0f));
    if (m_hoveredToolbarBtn == 2) {
        pRT->FillRoundedRectangle(rrcClear, theme.brushItemHover.Get());
    }
    pRT->DrawRoundedRectangle(rrcClear, theme.brushBorderSubtle.Get(), 1.0f);

    // Draw crisp vector trashcan / clear icon
    ID2D1Brush* iconBrush = (m_hoveredToolbarBtn == 2)
        ? (theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushBrandAccent.Get())
        : (theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushTextPrimary.Get());
    if (iconBrush) {
        const float icCX = (m_btnClearAll.left + m_btnClearAll.right) / 2.0f;
        const float icCY = (m_btnClearAll.top + m_btnClearAll.bottom) / 2.0f;
        const float wHalf = config.ScaleF(5.0f);
        const float hHalf = config.ScaleF(5.5f);
        // Lid
        pRT->DrawLine(D2D1::Point2F(icCX - wHalf - 1.0f, icCY - hHalf + 2.0f), D2D1::Point2F(icCX + wHalf + 1.0f, icCY - hHalf + 2.0f), iconBrush, 1.3f);
        pRT->DrawLine(D2D1::Point2F(icCX - 2.0f, icCY - hHalf), D2D1::Point2F(icCX + 2.0f, icCY - hHalf), iconBrush, 1.3f);
        // Body
        pRT->DrawLine(D2D1::Point2F(icCX - wHalf, icCY - hHalf + 2.0f), D2D1::Point2F(icCX - wHalf * 0.75f, icCY + hHalf), iconBrush, 1.2f);
        pRT->DrawLine(D2D1::Point2F(icCX + wHalf, icCY - hHalf + 2.0f), D2D1::Point2F(icCX + wHalf * 0.75f, icCY + hHalf), iconBrush, 1.2f);
        pRT->DrawLine(D2D1::Point2F(icCX - wHalf * 0.75f, icCY + hHalf), D2D1::Point2F(icCX + wHalf * 0.75f, icCY + hHalf), iconBrush, 1.2f);
        // Slits
        pRT->DrawLine(D2D1::Point2F(icCX - 1.5f, icCY - hHalf + 4.5f), D2D1::Point2F(icCX - 1.5f, icCY + hHalf - 2.5f), iconBrush, 1.0f);
        pRT->DrawLine(D2D1::Point2F(icCX + 1.5f, icCY - hHalf + 4.5f), D2D1::Point2F(icCX + 1.5f, icCY + hHalf - 2.5f), iconBrush, 1.0f);
    }

    // Separator vertical bar beside Trash icon
    curBtnX += btnSize + config.ScaleF(8.0f);
    pRT->DrawLine(
        D2D1::Point2F(curBtnX, btnY + config.ScaleF(4.0f)),
        D2D1::Point2F(curBtnX, btnY + btnSize - config.ScaleF(4.0f)),
        theme.brushBorderSubtle.Get(), 1.0f
    );
    curBtnX += config.ScaleF(8.0f);

    // [+ Code] Toolbar Button (Crisp vector Code icon < > and label)
    const float btnCodeW = config.ScaleF(72.0f);
    m_btnAddCode = D2D1::RectF(curBtnX, btnY, curBtnX + btnCodeW, btnY + btnSize);
    D2D1_ROUNDED_RECT rrcAddCode = D2D1::RoundedRect(m_btnAddCode, config.ScaleF(4.0f), config.ScaleF(4.0f));
    if (m_hoveredToolbarBtn == 3) {
        pRT->FillRoundedRectangle(rrcAddCode, theme.brushItemHover.Get());
    }
    pRT->DrawRoundedRectangle(rrcAddCode, theme.brushBorderSubtle.Get(), 1.0f);

    ID2D1Brush* codeTextBrush = (m_hoveredToolbarBtn == 3)
        ? (theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushBrandAccent.Get())
        : (theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushTextPrimary.Get());
    if (codeTextBrush) {
        const float icCodeX = m_btnAddCode.left + config.ScaleF(8.0f);
        const float icCodeY = (m_btnAddCode.top + m_btnAddCode.bottom) / 2.0f;
        // <
        pRT->DrawLine(D2D1::Point2F(icCodeX + config.ScaleF(3.5f), icCodeY - config.ScaleF(3.5f)),
                      D2D1::Point2F(icCodeX, icCodeY), codeTextBrush, 1.3f);
        pRT->DrawLine(D2D1::Point2F(icCodeX, icCodeY),
                      D2D1::Point2F(icCodeX + config.ScaleF(3.5f), icCodeY + config.ScaleF(3.5f)), codeTextBrush, 1.3f);
        // >
        pRT->DrawLine(D2D1::Point2F(icCodeX + config.ScaleF(6.5f), icCodeY - config.ScaleF(3.5f)),
                      D2D1::Point2F(icCodeX + config.ScaleF(10.0f), icCodeY), codeTextBrush, 1.3f);
        pRT->DrawLine(D2D1::Point2F(icCodeX + config.ScaleF(10.0f), icCodeY),
                      D2D1::Point2F(icCodeX + config.ScaleF(6.5f), icCodeY + config.ScaleF(3.5f)), codeTextBrush, 1.3f);

        D2D1_RECT_F rcCodeLabel = D2D1::RectF(icCodeX + config.ScaleF(13.0f), btnY, m_btnAddCode.right - config.ScaleF(4.0f), btnY + btnSize);
        pRT->DrawText(L"+ Code", 6, theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontMenuItem.Get(),
                      rcCodeLabel, codeTextBrush);
    }

    curBtnX += btnCodeW + config.ScaleF(6.0f);

    // [+ Text] Toolbar Button (Crisp vector Text icon T and label)
    const float btnTextW = config.ScaleF(68.0f);
    m_btnAddText = D2D1::RectF(curBtnX, btnY, curBtnX + btnTextW, btnY + btnSize);
    D2D1_ROUNDED_RECT rrcAddText = D2D1::RoundedRect(m_btnAddText, config.ScaleF(4.0f), config.ScaleF(4.0f));
    if (m_hoveredToolbarBtn == 4) {
        pRT->FillRoundedRectangle(rrcAddText, theme.brushItemHover.Get());
    }
    pRT->DrawRoundedRectangle(rrcAddText, theme.brushBorderSubtle.Get(), 1.0f);

    ID2D1Brush* textIconBrush = (m_hoveredToolbarBtn == 4)
        ? (theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushBrandAccent.Get())
        : (theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushTextPrimary.Get());
    if (textIconBrush) {
        const float icTextX = m_btnAddText.left + config.ScaleF(8.0f);
        const float icTextY = (m_btnAddText.top + m_btnAddText.bottom) / 2.0f;
        // T horizontal bar
        pRT->DrawLine(D2D1::Point2F(icTextX, icTextY - config.ScaleF(4.0f)),
                      D2D1::Point2F(icTextX + config.ScaleF(8.0f), icTextY - config.ScaleF(4.0f)), textIconBrush, 1.4f);
        // T vertical stem
        pRT->DrawLine(D2D1::Point2F(icTextX + config.ScaleF(4.0f), icTextY - config.ScaleF(4.0f)),
                      D2D1::Point2F(icTextX + config.ScaleF(4.0f), icTextY + config.ScaleF(4.5f)), textIconBrush, 1.4f);

        D2D1_RECT_F rcTextLabel = D2D1::RectF(icTextX + config.ScaleF(11.0f), btnY, m_btnAddText.right - config.ScaleF(4.0f), btnY + btnSize);
        pRT->DrawText(L"+ Text", 6, theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontMenuItem.Get(),
                      rcTextLabel, textIconBrush);
    }

    // 3. Render Notebook Cells Area with Scrolling
    const float contentTop = top + toolbarH;
    const float contentH = height - toolbarH;

    // Viewport scroll clamping
    if (m_totalHeight > 0.0f) {
        float maxScroll = (std::max)(0.0f, m_totalHeight - contentH + config.ScaleF(40.0f));
        if (m_scrollY > maxScroll) {
            m_scrollY = maxScroll;
            doc.notebookDoc->scrollY = m_scrollY;
        }
    }
    if (m_scrollY < 0.0f) {
        m_scrollY = 0.0f;
        doc.notebookDoc->scrollY = 0.0f;
    }

    pRT->PushAxisAlignedClip(D2D1::RectF(left, contentTop, left + width, top + height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const float padX = config.ScaleF(24.0f);
    const float cardLeft = left + padX;
    const float cardRight = left + width - padX - (m_totalHeight > contentH ? config.ScaleF(14.0f) : 0.0f);
    const float lineH = config.ScaleF(20.0f);

    float curY = contentTop + config.ScaleF(16.0f) - m_scrollY;

    for (size_t ci = 0; ci < nb->cells.size(); ++ci) {
        auto& cell = nb->cells[ci];
        bool isActive = (static_cast<int>(ci) == nb->activeCellIdx);
        bool isHovered = (static_cast<int>(ci) == m_hoveredCellIdx);

        size_t lineCount = (std::max)(size_t(1), cell.inputBuffer.GetLineCount());
        float codeH = static_cast<float>(lineCount) * lineH + config.ScaleF(16.0f);

        // Calculate dynamic output height (snug fit for small outputs, capped at 260px with scrolling for large outputs)
        float outputH = 0.0f;
        if (!cell.outputs.empty()) {
            size_t outLines = 0;
            for (const auto& out : cell.outputs) {
                outLines += std::count(out.text.begin(), out.text.end(), L'\n') + 1;
            }
            float rawOutH = static_cast<float>(outLines) * config.ScaleF(18.0f) + config.ScaleF(14.0f);
            outputH = (std::min)(config.ScaleF(260.0f), (std::max)(config.ScaleF(36.0f), rawOutH));
        }

        float cardHeaderH = config.ScaleF(28.0f);
        float extraGutterH = 0.0f;
        if (cell.type == editor::NotebookCellType::Code) {
            if (cell.hasError || (cell.executionCount >= 1 && !cell.isExecuting && !cell.isQueued)) {
                extraGutterH = config.ScaleF(20.0f);
            }
        }
        float cardH = cardHeaderH + (std::max)(codeH, config.ScaleF(36.0f) + extraGutterH) + (outputH > 0.0f ? (outputH + config.ScaleF(8.0f)) : 0.0f) + config.ScaleF(10.0f);

        cell.bounds = D2D1::RectF(cardLeft, curY, cardRight, curY + cardH);

        // Only draw if visible in viewport
        if (cell.bounds.bottom >= contentTop && cell.bounds.top <= top + height) {
            D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(cell.bounds, 6.0f, 6.0f);

            // Card background: elevated dark card
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBg;
            if (theme.IsDarkMode()) {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.125f, 0.125f, 0.145f, 1.0f), &brushCardBg);
            } else {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.97f, 0.97f, 0.985f, 1.0f), &brushCardBg);
            }
            if (brushCardBg) {
                pRT->FillRoundedRectangle(rrcCard, brushCardBg.Get());
            }

            // Card border: highlighted if active or dragging
            if (m_isDraggingCell && m_draggedCellIdx == static_cast<int>(ci)) {
                ID2D1Brush* borderBrush = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushBorderHover.Get();
                pRT->DrawRoundedRectangle(rrcCard, borderBrush ? borderBrush : theme.brushTextPrimary.Get(), 2.0f);
            } else if (isActive) {
                ID2D1Brush* borderBrush = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushBorderHover.Get();
                pRT->DrawRoundedRectangle(rrcCard, borderBrush ? borderBrush : theme.brushTextPrimary.Get(), 1.5f);
            } else {
                pRT->DrawRoundedRectangle(rrcCard, theme.brushBorderSubtle.Get(), 0.8f);
            }

            // 3.1 Gutter on Left (Execution count [1], warning (!), and circular Run button)
            // Text (Markdown) cells do NOT have execution counts [ ] or Run buttons (Image 2)
            const bool isCode = (cell.type == editor::NotebookCellType::Code);
            const float gutterW = isCode ? config.ScaleF(68.0f) : config.ScaleF(14.0f);
            const float gutterX = cell.bounds.left;
            const float gutterY = cell.bounds.top + config.ScaleF(8.0f);

            if (isCode) {
                // [ 1 ] execution badge
                std::wstring inStr;
                if (cell.isExecuting || cell.isQueued || cell.executionCount == -2) {
                    inStr = L"[*]";
                } else if (cell.executionCount >= 1) {
                    inStr = L"[" + std::to_wstring(cell.executionCount) + L"]";
                } else {
                    inStr = L"[ ]";
                }

                D2D1_RECT_F rcIn = D2D1::RectF(gutterX + config.ScaleF(8.0f), gutterY, gutterX + gutterW, gutterY + config.ScaleF(16.0f));
                ID2D1Brush* inBrush = (cell.executionCount >= 1 || cell.isExecuting || cell.isQueued)
                    ? (theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushBrandAccent.Get())
                    : (theme.brushLineNumber ? theme.brushLineNumber.Get() : theme.brushTextSecondary.Get());
                pRT->DrawText(inStr.c_str(), static_cast<UINT32>(inStr.length()),
                              theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontCode.Get(), rcIn, inBrush);

                // Error badge: (!) 0s (Image 5) or Success badge: ✓ 0s (Image 2)
                float runBtnY = gutterY + config.ScaleF(20.0f);
                if (cell.hasError) {
                    float badgeY = gutterY + config.ScaleF(18.0f);
                    float dotX = gutterX + config.ScaleF(18.0f);
                    float dotY = badgeY + config.ScaleF(7.0f);
                    float dotR = config.ScaleF(6.0f);

                    // Red circle with white !
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushErrCircle;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.32f, 0.32f, 1.0f), &brushErrCircle);
                    if (brushErrCircle) {
                        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), dotR, dotR), brushErrCircle.Get());
                    }
                    D2D1_RECT_F rcExclam = D2D1::RectF(dotX - dotR, dotY - dotR - 1.0f, dotX + dotR, dotY + dotR);
                    pRT->DrawText(L"!", 1, theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontCode.Get(),
                                  rcExclam, theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());

                    // Elapsed time: e.g. "0s"
                    std::wstring elapsedStr = std::to_wstring(cell.lastElapsedMs / 1000) + L"s";
                    D2D1_RECT_F rcElapsed = D2D1::RectF(dotX + dotR + config.ScaleF(4.0f), badgeY, gutterX + gutterW, badgeY + config.ScaleF(16.0f));
                    pRT->DrawText(elapsedStr.c_str(), static_cast<UINT32>(elapsedStr.length()),
                                  theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontCode.Get(), rcElapsed,
                                  theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushLineNumber.Get());

                    runBtnY += config.ScaleF(20.0f);
                } else if (cell.executionCount >= 1 && !cell.isExecuting && !cell.isQueued) {
                    // Success checkmark ✓ and elapsed time (Image 2)
                    float badgeY = gutterY + config.ScaleF(18.0f);
                    float chkX = gutterX + config.ScaleF(12.0f);

                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushGreen;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.82f, 0.48f, 1.0f), &brushGreen);
                    D2D1_RECT_F rcCheck = D2D1::RectF(chkX, badgeY, chkX + config.ScaleF(12.0f), badgeY + config.ScaleF(16.0f));
                    pRT->DrawText(L"\x2713", 1, theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontCode.Get(),
                                  rcCheck, brushGreen ? brushGreen.Get() : theme.brushBrandAccent.Get());

                    std::wstring elapsedStr = std::to_wstring(cell.lastElapsedMs / 1000) + L"s";
                    D2D1_RECT_F rcElapsed = D2D1::RectF(chkX + config.ScaleF(14.0f), badgeY, gutterX + gutterW, badgeY + config.ScaleF(16.0f));
                    pRT->DrawText(elapsedStr.c_str(), static_cast<UINT32>(elapsedStr.length()),
                                  theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontCode.Get(), rcElapsed,
                                  theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushLineNumber.Get());

                    runBtnY += config.ScaleF(20.0f);
                }

                // Circular Run Button (Morphing into stop button with solid or dashed spinner)
                const float runBtnSize = config.ScaleF(26.0f);
                cell.runBtnBounds = D2D1::RectF(
                    gutterX + config.ScaleF(12.0f),
                    runBtnY,
                    gutterX + config.ScaleF(12.0f) + runBtnSize,
                    runBtnY + runBtnSize
                );

                bool isRunHover = (isHovered && m_hoveredBtn == 1);
                DrawRunMorphButton(pRT, theme, config, cell.runBtnBounds, cell.isExecuting, cell.isQueued, cell.hasError, isRunHover);
            } else {
                cell.runBtnBounds = D2D1::RectF(0, 0, 0, 0);
            }

            // 3.2 Top-Right Cell Header Bar (Clean minimal actions: Drag Handle, Move Up/Down, Delete)
            float rightX = cell.bounds.right - config.ScaleF(12.0f);
            const float barY = cell.bounds.top + config.ScaleF(6.0f);
            const float actBtnSize = config.ScaleF(20.0f);

            // Delete [ ✕ ]
            cell.deleteBtnBounds = D2D1::RectF(rightX - actBtnSize, barY, rightX, barY + actBtnSize);
            if (isHovered && m_hoveredBtn == 2) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(cell.deleteBtnBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
            }
            pRT->DrawText(L"✕", 1, theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontMenuItem.Get(),
                          cell.deleteBtnBounds, theme.brushTextSecondary.Get());
            rightX -= actBtnSize + config.ScaleF(6.0f);

            // Move Down [ ▼ ]
            cell.moveDownBtnBounds = D2D1::RectF(rightX - actBtnSize, barY, rightX, barY + actBtnSize);
            if (isHovered && m_hoveredBtn == 4) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(cell.moveDownBtnBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
            }
            pRT->DrawText(L"▼", 1, theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontMenuItem.Get(),
                          cell.moveDownBtnBounds, theme.brushTextSecondary.Get());
            rightX -= actBtnSize + config.ScaleF(4.0f);

            // Move Up [ ▲ ]
            cell.moveUpBtnBounds = D2D1::RectF(rightX - actBtnSize, barY, rightX, barY + actBtnSize);
            if (isHovered && m_hoveredBtn == 3) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(cell.moveUpBtnBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
            }
            pRT->DrawText(L"▲", 1, theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontMenuItem.Get(),
                          cell.moveUpBtnBounds, theme.brushTextSecondary.Get());
            rightX -= actBtnSize + config.ScaleF(6.0f);

            // Drag Grip [ ⠿ ] (Cell drag & drop reordering handle)
            cell.dragHandleBounds = D2D1::RectF(rightX - actBtnSize, barY, rightX, barY + actBtnSize);
            bool isDragHover = (isHovered && m_hoveredBtn == 6);
            if (isDragHover || (m_isDraggingCell && m_draggedCellIdx == static_cast<int>(ci))) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(cell.dragHandleBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
            }
            pRT->DrawText(L"⠿", 1, theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontMenuItem.Get(),
                          cell.dragHandleBounds, (isDragHover || (m_isDraggingCell && m_draggedCellIdx == static_cast<int>(ci)))
                              ? (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextPrimary.Get())
                              : theme.brushTextSecondary.Get());

            // 3.3 Code Area
            cell.codeAreaBounds = D2D1::RectF(
                gutterX + gutterW,
                cell.bounds.top + cardHeaderH,
                cell.bounds.right - config.ScaleF(12.0f),
                cell.bounds.top + cardHeaderH + codeH
            );

            // Code box inset background
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCodeBox;
            if (theme.IsDarkMode()) {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.09f, 0.105f, 1.0f), &brushCodeBox);
            } else {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.93f, 0.93f, 0.95f, 1.0f), &brushCodeBox);
            }
            if (brushCodeBox) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(cell.codeAreaBounds, 4.0f, 4.0f), brushCodeBox.Get());
            }

            // Render line numbers and code text lines
            const float lineNumW = config.ScaleF(28.0f);
            const auto& lines = cell.inputBuffer.GetLines();
            auto curPos = cell.inputBuffer.GetCursor();
            const float charW = EnsureCharWidth(theme, config);

            for (size_t li = 0; li < lines.size(); ++li) {
                float lineY = cell.codeAreaBounds.top + config.ScaleF(6.0f) + static_cast<float>(li) * lineH;
                D2D1_RECT_F rcNum = D2D1::RectF(cell.codeAreaBounds.left + config.ScaleF(6.0f), lineY,
                                               cell.codeAreaBounds.left + lineNumW, lineY + lineH);
                std::wstring numStr = std::to_wstring(li + 1);
                pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()),
                              theme.fontCode.Get(), rcNum, theme.brushLineNumber.Get());

                float textX = cell.codeAreaBounds.left + lineNumW + config.ScaleF(8.0f);
                const auto& codeLine = lines[li];

                // 1. Render active text selection background on code line
                if (cell.HasSelection()) {
                    size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                    cell.GetSelectionRange(sLine, sCol, eLine, eCol);
                    if (li >= sLine && li <= eLine) {
                        float selX1 = textX;
                        float selX2 = textX;
                        if (li == sLine && li == eLine) {
                            selX1 = textX + static_cast<float>(sCol) * charW;
                            selX2 = textX + static_cast<float>(eCol) * charW;
                        } else if (li == sLine) {
                            selX1 = textX + static_cast<float>(sCol) * charW;
                            selX2 = textX + static_cast<float>(codeLine.length()) * charW + config.ScaleF(8.0f);
                        } else if (li == eLine) {
                            selX1 = textX;
                            selX2 = textX + static_cast<float>(eCol) * charW;
                        } else {
                            selX1 = textX;
                            selX2 = (std::max)(textX + static_cast<float>(codeLine.length()) * charW + config.ScaleF(8.0f),
                                               textX + config.ScaleF(24.0f));
                        }

                        if (selX2 > selX1) {
                            D2D1_RECT_F rcSel = D2D1::RectF(selX1, lineY + 0.5f, selX2, lineY + lineH - 0.5f);
                            D2D1_ROUNDED_RECT rrcSel = D2D1::RoundedRect(rcSel, 3.0f, 3.0f);
                            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
                            pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.42f, 0.75f, 0.45f), &selBrush);
                            if (selBrush) {
                                pRT->FillRoundedRectangle(&rrcSel, selBrush.Get());
                            }
                        }
                    }
                }

                // Matching word occurrences highlight across lines
                if (!cell.selectedWord.empty() && cell.selectedWord.length() > 1 && !codeLine.empty()) {
                    size_t wpos = 0;
                    while ((wpos = codeLine.find(cell.selectedWord, wpos)) != std::wstring::npos) {
                        bool isCurrentSelection = false;
                        if (cell.HasSelection()) {
                            size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                            cell.GetSelectionRange(sLine, sCol, eLine, eCol);
                            if (li == sLine && li == eLine && wpos == sCol) {
                                isCurrentSelection = true;
                            }
                        }
                        if (!isCurrentSelection) {
                            float wx = textX + static_cast<float>(wpos) * charW;
                            float ww = static_cast<float>(cell.selectedWord.length()) * charW;
                            D2D1_RECT_F rcWord = D2D1::RectF(wx - 1.0f, lineY + 1.0f, wx + ww + 1.0f, lineY + lineH - 1.0f);
                            D2D1_ROUNDED_RECT rrcWord = D2D1::RoundedRect(rcWord, 2.0f, 2.0f);
                            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> wordBrush;
                            pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.45f, 0.60f, 0.25f), &wordBrush);
                            if (wordBrush) {
                                pRT->FillRoundedRectangle(&rrcWord, wordBrush.Get());
                            }
                        }
                        wpos += cell.selectedWord.length();
                    }
                }

                // Code text line with syntax tokenization via DirectWrite TextLayout
                auto tokens = cell.inputBuffer.GetTokensForLine(li);
                Microsoft::WRL::ComPtr<IDWriteTextLayout> pLineLayout;
                if (!codeLine.empty() && theme.dwriteFactory && theme.fontCode &&
                    SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                        codeLine.c_str(), static_cast<UINT32>(codeLine.length()),
                        theme.fontCode.Get(),
                        (std::max)(10.0f, cell.codeAreaBounds.right - textX),
                        lineH,
                        &pLineLayout))) {
                    for (const auto& tok : tokens) {
                        if (tok.startCol + tok.length <= codeLine.length()) {
                            DWRITE_TEXT_RANGE rng = { static_cast<UINT32>(tok.startCol), static_cast<UINT32>(tok.length) };
                            ID2D1Brush* tokBrush = nullptr;
                            switch (tok.type) {
                                case editor::TokenType::Keyword: tokBrush = theme.brushSyntaxKeyword.Get(); break;
                                case editor::TokenType::String:  tokBrush = theme.brushSyntaxString.Get(); break;
                                case editor::TokenType::Number:  tokBrush = theme.brushSyntaxNumber.Get(); break;
                                case editor::TokenType::Comment: tokBrush = theme.brushSyntaxComment.Get(); break;
                                default: break;
                            }
                            if (tokBrush) {
                                pLineLayout->SetDrawingEffect(tokBrush, rng);
                            }
                        }
                    }
                    pRT->DrawTextLayout(D2D1::Point2F(textX, lineY), pLineLayout.Get(), theme.brushTextPrimary.Get());
                } else if (!codeLine.empty()) {
                    D2D1_RECT_F rcLineText = D2D1::RectF(textX, lineY, cell.codeAreaBounds.right, lineY + lineH);
                    pRT->DrawText(codeLine.c_str(), static_cast<UINT32>(codeLine.length()),
                                  theme.fontCode.Get(), rcLineText, theme.brushTextPrimary.Get());
                }

                // 2. Syntax Error Squiggly Underlines (~~~~) on the error line
                if (cell.hasError && cell.errorLine >= 0 && li == static_cast<size_t>(cell.errorLine)) {
                    float errX1 = textX;
                    size_t firstNonSpace = 0;
                    while (firstNonSpace < codeLine.length() && iswspace(codeLine[firstNonSpace])) {
                        firstNonSpace++;
                    }
                    errX1 += static_cast<float>(firstNonSpace) * charW;
                    float errX2 = textX + static_cast<float>(codeLine.length()) * charW;
                    if (errX2 <= errX1) {
                        errX2 = errX1 + config.ScaleF(24.0f);
                    }
                    errX2 = (std::min)(errX2, cell.codeAreaBounds.right - config.ScaleF(8.0f));

                    float waveY = lineY + lineH - config.ScaleF(2.0f);
                    float waveH = config.ScaleF(2.2f);
                    float step = config.ScaleF(2.5f);

                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pErrBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.25f, 0.25f, 1.0f), &pErrBrush);
                    ID2D1Brush* brushToUse = pErrBrush ? pErrBrush.Get() : theme.brushIconRed.Get();

                    for (float wx = errX1; wx < errX2; wx += step * 2.0f) {
                        pRT->DrawLine(D2D1::Point2F(wx, waveY), D2D1::Point2F(wx + step, waveY - waveH), brushToUse, 1.6f);
                        pRT->DrawLine(D2D1::Point2F(wx + step, waveY - waveH), D2D1::Point2F((std::min)(wx + step * 2.0f, errX2), waveY), brushToUse, 1.6f);
                    }
                }

                // 3. Render Caret in active cell with precise charW alignment
                if (isActive && li == curPos.line && m_caretVisible) {
                    float caretX = textX + static_cast<float>(curPos.col) * charW;
                    pRT->DrawLine(
                        D2D1::Point2F(caretX, lineY + 1.0f),
                        D2D1::Point2F(caretX, lineY + lineH - 2.0f),
                        theme.brushTextPrimary.Get(),
                        1.5f
                    );
                }
            }

            // 3.4 Output Area (Colab/Jupyter standard with dynamic height, overflow clipping, and X/Y scroll)
            if (!cell.outputs.empty()) {
                float outTop = cell.codeAreaBounds.bottom + config.ScaleF(6.0f);
                cell.outputAreaBounds = D2D1::RectF(cell.codeAreaBounds.left, outTop, cell.codeAreaBounds.right, outTop + outputH);

                D2D1_ROUNDED_RECT rrcOut = D2D1::RoundedRect(cell.outputAreaBounds, 4.0f, 4.0f);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushOutBg;
                if (cell.hasError) {
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.06f, 0.06f, 1.0f), &brushOutBg); // Red tinted dark box
                } else if (theme.IsDarkMode()) {
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.06f, 0.08f, 1.0f), &brushOutBg);
                } else {
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.91f, 0.91f, 0.93f, 1.0f), &brushOutBg);
                }
                if (brushOutBg) pRT->FillRoundedRectangle(rrcOut, brushOutBg.Get());

                // Left red accent stripe on error
                if (cell.hasError) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushStripe;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.32f, 0.32f, 1.0f), &brushStripe);
                    if (brushStripe) {
                        pRT->DrawLine(
                            D2D1::Point2F(cell.outputAreaBounds.left + 1.5f, cell.outputAreaBounds.top + 3.0f),
                            D2D1::Point2F(cell.outputAreaBounds.left + 1.5f, cell.outputAreaBounds.bottom - 3.0f),
                            brushStripe.Get(), 3.0f
                        );
                    }
                }

                // Strict Direct2D axis-aligned clipping prevents any text bleeding/overlapping adjacent cells
                pRT->PushAxisAlignedClip(cell.outputAreaBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                float textPadX = config.ScaleF(12.0f);
                float textPadY = config.ScaleF(6.0f);
                float outLineH = config.ScaleF(18.0f);
                float curLineY = outTop + textPadY - cell.outputScrollY;

                float maxLineWidth = 0.0f;
                size_t totalLines = 0;

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushErrText;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.38f, 0.38f, 1.0f), &brushErrText);

                for (const auto& out : cell.outputs) {
                    ID2D1Brush* outTextBrush = out.isError ? (brushErrText ? brushErrText.Get() : theme.brushIconRed.Get())
                                                          : (theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get());

                    std::wstringstream wss(out.text);
                    std::wstring line;
                    while (std::getline(wss, line)) {
                        float lineW = static_cast<float>(line.length()) * charW;
                        if (lineW > maxLineWidth) maxLineWidth = lineW;
                        totalLines++;

                        if (curLineY + outLineH >= outTop && curLineY <= outTop + outputH) {
                            float drawX = cell.outputAreaBounds.left + textPadX - cell.outputScrollX;
                            D2D1_RECT_F rcLine = D2D1::RectF(
                                drawX,
                                curLineY,
                                drawX + (std::max)(120.0f, lineW),
                                curLineY + outLineH
                            );
                            pRT->DrawText(line.c_str(), static_cast<UINT32>(line.length()),
                                          theme.fontCode.Get(), rcLine, outTextBrush);
                        }
                        curLineY += outLineH;
                    }
                }

                pRT->PopAxisAlignedClip();

                float totalContentH = static_cast<float>(totalLines) * outLineH + textPadY * 2.0f;
                float availW = (cell.outputAreaBounds.right - cell.outputAreaBounds.left) - textPadX * 2.0f;

                // 1. Sleek Vertical Scrollbar when output exceeds clamped height (Thicker, easy-to-drag, with visual track)
                if (totalContentH > outputH && outputH > config.ScaleF(30.0f)) {
                    float sbW = config.ScaleF(8.0f);
                    float sbX = cell.outputAreaBounds.right - sbW - config.ScaleF(3.0f);
                    float trackH = outputH - config.ScaleF(8.0f);
                    float thumbH = (std::max)(config.ScaleF(24.0f), trackH * (outputH / totalContentH));
                    float maxScrollY = totalContentH - outputH;
                    float clampedScrollY = (std::max)(0.0f, (std::min)(cell.outputScrollY, maxScrollY));
                    float thumbY = outTop + config.ScaleF(4.0f) + (clampedScrollY / (maxScrollY > 0.0f ? maxScrollY : 1.0f)) * (trackH - thumbH);

                    cell.outputVScrollTrackBounds = D2D1::RectF(sbX - config.ScaleF(2.0f), outTop + config.ScaleF(4.0f), sbX + sbW + config.ScaleF(2.0f), outTop + config.ScaleF(4.0f) + trackH);
                    cell.outputVScrollThumbBounds = D2D1::RectF(sbX, thumbY, sbX + sbW, thumbY + thumbH);
                    cell.outputMaxScrollY = maxScrollY;

                    // Subtle track background
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTrackY;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f), &brushTrackY);
                    if (brushTrackY) {
                        D2D1_ROUNDED_RECT rrcTrack = D2D1::RoundedRect(cell.outputVScrollTrackBounds, sbW / 2.0f, sbW / 2.0f);
                        pRT->FillRoundedRectangle(rrcTrack, brushTrackY.Get());
                    }

                    bool isDraggingThis = (m_isDraggingOutputVScroll && m_dragOutputCellIdx == static_cast<int>(ci));
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSbY;
                    pRT->CreateSolidColorBrush(isDraggingThis ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.65f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f), &brushSbY);
                    if (brushSbY) {
                        D2D1_ROUNDED_RECT rrcSb = D2D1::RoundedRect(cell.outputVScrollThumbBounds, sbW / 2.0f, sbW / 2.0f);
                        pRT->FillRoundedRectangle(rrcSb, brushSbY.Get());
                    }
                } else {
                    cell.outputVScrollTrackBounds = D2D1::RectF(0, 0, 0, 0);
                    cell.outputVScrollThumbBounds = D2D1::RectF(0, 0, 0, 0);
                    cell.outputMaxScrollY = 0.0f;
                }

                // 2. Sleek Horizontal Scrollbar when long lines exceed width (Thicker, easy-to-drag, with visual track)
                if (maxLineWidth > availW && availW > config.ScaleF(30.0f)) {
                    float sbH = config.ScaleF(8.0f);
                    float sbY = cell.outputAreaBounds.bottom - sbH - config.ScaleF(3.0f);
                    float trackW = availW;
                    float thumbW = (std::max)(config.ScaleF(28.0f), trackW * (availW / maxLineWidth));
                    float maxScrollX = maxLineWidth - availW;
                    float clampedScrollX = (std::max)(0.0f, (std::min)(cell.outputScrollX, maxScrollX));
                    float thumbX = cell.outputAreaBounds.left + textPadX + (clampedScrollX / (maxScrollX > 0.0f ? maxScrollX : 1.0f)) * (trackW - thumbW);

                    cell.outputHScrollTrackBounds = D2D1::RectF(cell.outputAreaBounds.left + textPadX, sbY - config.ScaleF(2.0f), cell.outputAreaBounds.left + textPadX + trackW, sbY + sbH + config.ScaleF(2.0f));
                    cell.outputHScrollThumbBounds = D2D1::RectF(thumbX, sbY, thumbX + thumbW, sbY + sbH);
                    cell.outputMaxScrollX = maxScrollX;

                    // Subtle track background
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTrackX;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f), &brushTrackX);
                    if (brushTrackX) {
                        D2D1_ROUNDED_RECT rrcTrackX = D2D1::RoundedRect(cell.outputHScrollTrackBounds, sbH / 2.0f, sbH / 2.0f);
                        pRT->FillRoundedRectangle(rrcTrackX, brushTrackX.Get());
                    }

                    bool isDraggingThis = (m_isDraggingOutputHScroll && m_dragOutputCellIdx == static_cast<int>(ci));
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSbX;
                    pRT->CreateSolidColorBrush(isDraggingThis ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.65f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f), &brushSbX);
                    if (brushSbX) {
                        D2D1_ROUNDED_RECT rrcSbX = D2D1::RoundedRect(cell.outputHScrollThumbBounds, sbH / 2.0f, sbH / 2.0f);
                        pRT->FillRoundedRectangle(rrcSbX, brushSbX.Get());
                    }
                } else {
                    cell.outputHScrollTrackBounds = D2D1::RectF(0, 0, 0, 0);
                    cell.outputHScrollThumbBounds = D2D1::RectF(0, 0, 0, 0);
                    cell.outputMaxScrollX = 0.0f;
                }
            } else {
                cell.outputAreaBounds = D2D1::RectF(0, 0, 0, 0);
                cell.outputVScrollTrackBounds = D2D1::RectF(0, 0, 0, 0);
                cell.outputVScrollThumbBounds = D2D1::RectF(0, 0, 0, 0);
                cell.outputHScrollTrackBounds = D2D1::RectF(0, 0, 0, 0);
                cell.outputHScrollThumbBounds = D2D1::RectF(0, 0, 0, 0);
                cell.outputMaxScrollY = 0.0f;
                cell.outputMaxScrollX = 0.0f;
            }

            // 3.5 Bottom border hover: horizontal divider & centered [ + Code ] [ + Text ] pills (Image 3)
            float bottomY = cell.bounds.bottom;
            cell.bottomDividerBounds = D2D1::RectF(cardLeft, bottomY - config.ScaleF(1.0f), cardRight, bottomY + config.ScaleF(1.0f));

            if (m_hoveredBottomCellIdx == static_cast<int>(ci)) {
                // Subtle horizontal line
                pRT->DrawLine(
                    D2D1::Point2F(cardLeft, bottomY),
                    D2D1::Point2F(cardRight, bottomY),
                    theme.brushBorderSubtle ? theme.brushBorderSubtle.Get() : theme.brushTextSecondary.Get(),
                    1.0f
                );

                // Centered pills
                float cardMidX = (cardLeft + cardRight) / 2.0f;
                float pillH = config.ScaleF(22.0f);
                float pillRadius = pillH / 2.0f;
                float codeW = config.ScaleF(72.0f);
                float textW = config.ScaleF(68.0f);
                float gap = config.ScaleF(8.0f);
                float totalW = codeW + textW + gap;
                float pillY = bottomY - pillH / 2.0f;

                cell.bottomAddCodeBounds = D2D1::RectF(cardMidX - totalW / 2.0f, pillY, cardMidX - totalW / 2.0f + codeW, pillY + pillH);
                cell.bottomAddTextBounds = D2D1::RectF(cell.bottomAddCodeBounds.right + gap, pillY, cell.bottomAddCodeBounds.right + gap + textW, pillY + pillH);

                // [+ Code] pill
                D2D1_ROUNDED_RECT rrcCode = D2D1::RoundedRect(cell.bottomAddCodeBounds, pillRadius, pillRadius);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillBg;
                if (theme.IsDarkMode()) {
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f), &brushPillBg);
                } else {
                    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brushPillBg);
                }
                if (brushPillBg) pRT->FillRoundedRectangle(rrcCode, brushPillBg.Get());
                if (m_hoveredBottomBtn == 1 && theme.brushItemHover) {
                    pRT->FillRoundedRectangle(rrcCode, theme.brushItemHover.Get());
                }
                pRT->DrawRoundedRectangle(rrcCode, theme.brushBorderSubtle.Get(), 1.0f);

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBlueText;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.60f, 0.95f, 1.0f), &brushBlueText);
                pRT->DrawText(L"+ Code", 6, theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontMenuItem.Get(),
                              cell.bottomAddCodeBounds, brushBlueText ? brushBlueText.Get() : theme.brushBrandAccent.Get());

                // [+ Text] pill
                D2D1_ROUNDED_RECT rrcText = D2D1::RoundedRect(cell.bottomAddTextBounds, pillRadius, pillRadius);
                if (brushPillBg) pRT->FillRoundedRectangle(rrcText, brushPillBg.Get());
                if (m_hoveredBottomBtn == 2 && theme.brushItemHover) {
                    pRT->FillRoundedRectangle(rrcText, theme.brushItemHover.Get());
                }
                pRT->DrawRoundedRectangle(rrcText, theme.brushBorderSubtle.Get(), 1.0f);
                pRT->DrawText(L"+ Text", 6, theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontMenuItem.Get(),
                              cell.bottomAddTextBounds, brushBlueText ? brushBlueText.Get() : theme.brushBrandAccent.Get());
            } else {
                cell.bottomAddCodeBounds = D2D1::RectF(0, 0, 0, 0);
                cell.bottomAddTextBounds = D2D1::RectF(0, 0, 0, 0);
            }
        }

        curY += cardH + config.ScaleF(16.0f);
    }

    m_totalHeight = (curY + m_scrollY) - (contentTop + config.ScaleF(16.0f));

    // 3.4 Render drag-and-drop cell reordering indicator line
    if (m_isDraggingCell && nb && m_dropTargetCellIdx >= 0 &&
        m_dropTargetCellIdx < static_cast<int>(nb->cells.size()) &&
        m_dropTargetCellIdx != m_draggedCellIdx) {
        float lineY = 0.0f;
        if (m_dropTargetCellIdx < m_draggedCellIdx) {
            lineY = nb->cells[m_dropTargetCellIdx].bounds.top - config.ScaleF(4.0f);
        } else {
            lineY = nb->cells[m_dropTargetCellIdx].bounds.bottom + config.ScaleF(4.0f);
        }

        ID2D1Brush* brushDrop = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextPrimary.Get();
        if (brushDrop) {
            pRT->DrawLine(D2D1::Point2F(cardLeft, lineY), D2D1::Point2F(cardRight, lineY), brushDrop, 3.0f);
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cardLeft, lineY), 4.0f, 4.0f), brushDrop);
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cardRight, lineY), 4.0f, 4.0f), brushDrop);
        }
    }

    pRT->PopAxisAlignedClip();

    // 4. Custom Slim Scrollbar if content exceeds viewport
    if (m_totalHeight > contentH && contentH > 0.0f) {
        const float barW = config.ScaleF(8.0f);
        const float barX = left + width - barW - config.ScaleF(3.0f);
        m_scrollTrack = D2D1::RectF(barX, contentTop, barX + barW, top + height);

        float ratio = contentH / (m_totalHeight + contentH);
        float thumbH = (std::max)(config.ScaleF(28.0f), contentH * ratio);
        float maxScroll = (std::max)(0.0f, m_totalHeight - contentH + config.ScaleF(40.0f));
        float scrollPct = (maxScroll > 0.0f) ? (m_scrollY / maxScroll) : 0.0f;
        scrollPct = (std::clamp)(scrollPct, 0.0f, 1.0f);
        float thumbY = contentTop + scrollPct * (contentH - thumbH);

        m_scrollThumb = D2D1::RectF(barX, thumbY, barX + barW, thumbY + thumbH);

        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(m_scrollThumb, barW / 2.0f, barW / 2.0f);
        ID2D1Brush* thumbBrush = m_isDraggingScrollbar
            ? (theme.brushBorderHover ? theme.brushBorderHover.Get() : theme.brushBorderSubtle.Get())
            : (theme.brushBorderSubtle ? theme.brushBorderSubtle.Get() : theme.brushTextSecondary.Get());
        pRT->FillRoundedRectangle(rrcThumb, thumbBrush);
    } else {
        m_scrollTrack = D2D1::RectF(0, 0, 0, 0);
        m_scrollThumb = D2D1::RectF(0, 0, 0, 0);
    }

    // 5. Floating Tooltips for Toolbar Buttons (Image 3)
    if (m_hoveredToolbarBtn == 1) {
        std::wstring tip = m_isExecuting.load() ? L"Stop Execution" : L"Run All Cells";
        RenderTooltip(pRT, theme, config, m_btnRunAll, tip);
    } else if (m_hoveredToolbarBtn == 2) {
        RenderTooltip(pRT, theme, config, m_btnClearAll, L"Clear All Outputs");
    } else if (m_hoveredToolbarBtn == 3) {
        RenderTooltip(pRT, theme, config, m_btnAddCode, L"Add Code Cell (+ Code)");
    } else if (m_hoveredToolbarBtn == 4) {
        RenderTooltip(pRT, theme, config, m_btnAddText, L"Add Text / Markdown Cell (+ Text)");
    }
}

bool NotebookView::HandleLButtonDown(POINT clientPt, editor::OpenDocument& doc, const app::AppConfig& config, HWND hwnd) {
    if (!doc.notebookDoc) return false;
    auto nb = doc.notebookDoc;

    // Check scrollbar thumb
    if (clientPt.x >= m_scrollThumb.left && clientPt.x <= m_scrollThumb.right &&
        clientPt.y >= m_scrollThumb.top && clientPt.y <= m_scrollThumb.bottom) {
        m_isDraggingScrollbar = true;
        m_dragScrollStartY = static_cast<float>(clientPt.y);
        m_dragScrollStartOffset = m_scrollY;
        if (hwnd) SetCapture(hwnd);
        return true;
    }

    auto PointInRect = [](POINT pt, const D2D1_RECT_F& r) -> bool {
        return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
    };

    // Check cell output scrollbars (Vertical & Horizontal dragging)
    for (size_t ci = 0; ci < nb->cells.size(); ++ci) {
        auto& cell = nb->cells[ci];
        if (cell.outputMaxScrollY > 0.0f) {
            if (PointInRect(clientPt, cell.outputVScrollThumbBounds)) {
                m_isDraggingOutputVScroll = true;
                m_dragOutputCellIdx = static_cast<int>(ci);
                m_dragOutputStartPos = static_cast<float>(clientPt.y);
                m_dragOutputStartOffset = cell.outputScrollY;
                if (hwnd) SetCapture(hwnd);
                return true;
            } else if (PointInRect(clientPt, cell.outputVScrollTrackBounds)) {
                float step = config.ScaleF(80.0f);
                if (clientPt.y < cell.outputVScrollThumbBounds.top) {
                    cell.outputScrollY = (std::max)(0.0f, cell.outputScrollY - step);
                } else if (clientPt.y > cell.outputVScrollThumbBounds.bottom) {
                    cell.outputScrollY = (std::min)(cell.outputMaxScrollY, cell.outputScrollY + step);
                }
                return true;
            }
        }
        if (cell.outputMaxScrollX > 0.0f) {
            if (PointInRect(clientPt, cell.outputHScrollThumbBounds)) {
                m_isDraggingOutputHScroll = true;
                m_dragOutputCellIdx = static_cast<int>(ci);
                m_dragOutputStartPos = static_cast<float>(clientPt.x);
                m_dragOutputStartOffset = cell.outputScrollX;
                if (hwnd) SetCapture(hwnd);
                return true;
            } else if (PointInRect(clientPt, cell.outputHScrollTrackBounds)) {
                float step = config.ScaleF(80.0f);
                if (clientPt.x < cell.outputHScrollThumbBounds.left) {
                    cell.outputScrollX = (std::max)(0.0f, cell.outputScrollX - step);
                } else if (clientPt.x > cell.outputHScrollThumbBounds.right) {
                    cell.outputScrollX = (std::min)(cell.outputMaxScrollX, cell.outputScrollX + step);
                }
                return true;
            }
        }
    }

    // Check Top Toolbar buttons

    if (PointInRect(clientPt, m_btnRunAll)) {
        if (m_isExecuting.load()) {
            StopExecution(doc);
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            RunAllCells(doc, hwnd);
        }
        return true;
    }
    if (PointInRect(clientPt, m_btnClearAll)) {
        nb->ClearAllOutputs();
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (PointInRect(clientPt, m_btnAddCode)) {
        int targetIdx = nb->activeCellIdx >= 0 ? nb->activeCellIdx : static_cast<int>(nb->cells.size()) - 1;
        nb->AddCell(editor::NotebookCellType::Code, targetIdx);
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (PointInRect(clientPt, m_btnAddText)) {
        int targetIdx = nb->activeCellIdx >= 0 ? nb->activeCellIdx : static_cast<int>(nb->cells.size()) - 1;
        nb->AddCell(editor::NotebookCellType::Markdown, targetIdx);
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }

    // Check Bottom hover pill buttons (+ Code, + Text)
    for (size_t ci = 0; ci < nb->cells.size(); ++ci) {
        auto& cell = nb->cells[ci];
        if (PointInRect(clientPt, cell.bottomAddCodeBounds)) {
            nb->AddCell(editor::NotebookCellType::Code, static_cast<int>(ci));
            m_hoveredBottomCellIdx = -1;
            m_hoveredBottomBtn = 0;
            return true;
        }
        if (PointInRect(clientPt, cell.bottomAddTextBounds)) {
            nb->AddCell(editor::NotebookCellType::Markdown, static_cast<int>(ci));
            m_hoveredBottomCellIdx = -1;
            m_hoveredBottomBtn = 0;
            return true;
        }
    }

    // Check cell buttons and cards
    for (size_t ci = 0; ci < nb->cells.size(); ++ci) {
        auto& cell = nb->cells[ci];
        if (PointInRect(clientPt, cell.bounds)) {
            nb->activeCellIdx = static_cast<int>(ci);

            // Run button: morphs into stop if executing, or cancels queue if queued
            if (PointInRect(clientPt, cell.runBtnBounds)) {
                if (cell.isExecuting) {
                    StopExecution(doc);
                    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                } else if (cell.isQueued) {
                    {
                        std::lock_guard<std::mutex> qLock(m_queueMutex);
                        m_cellQueue.erase(std::remove(m_cellQueue.begin(), m_cellQueue.end(), static_cast<int>(ci)), m_cellQueue.end());
                    }
                    cell.isQueued = false;
                    cell.executionCount = -1;
                    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                } else {
                    RunCell(static_cast<int>(ci), doc, hwnd);
                }
                return true;
            }
            // Delete button
            if (PointInRect(clientPt, cell.deleteBtnBounds)) {
                nb->DeleteCell(static_cast<int>(ci));
                return true;
            }
            // Move Up
            if (PointInRect(clientPt, cell.moveUpBtnBounds)) {
                if (ci > 0) nb->MoveCell(static_cast<int>(ci), static_cast<int>(ci - 1));
                return true;
            }
            // Move Down
            if (PointInRect(clientPt, cell.moveDownBtnBounds)) {
                if (ci + 1 < nb->cells.size()) nb->MoveCell(static_cast<int>(ci), static_cast<int>(ci + 1));
                return true;
            }

            // Click inside code area: calculate line and column with sub-pixel charW accuracy & double-click word selection
            if (PointInRect(clientPt, cell.codeAreaBounds)) {
                size_t targetLine = 0, targetCol = 0;
                HitTestCodeArea(cell, clientPt, config, targetLine, targetCol);

                ULONGLONG now = GetTickCount64();
                int dblTime = static_cast<int>(GetDoubleClickTime());
                int ddx = abs(clientPt.x - m_lastLDownPt.x);
                int ddy = abs(clientPt.y - m_lastLDownPt.y);
                bool isDoubleClick = (now - m_lastLDownTime <= static_cast<ULONGLONG>(dblTime)) && (ddx <= 6 && ddy <= 6);
                m_lastLDownTime = now;
                m_lastLDownPt = clientPt;

                if (isDoubleClick) {
                    cell.SelectWordAt(targetLine, targetCol);
                    m_caretVisible = true;
                    m_lastBlinkTime = GetTickCount64();
                    return true;
                }

                bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (isShift) {
                    if (!cell.hasSelection) {
                        cell.selAnchor = cell.inputBuffer.GetCursor();
                    }
                    cell.selEnd = { targetLine, targetCol };
                    cell.hasSelection = (cell.selAnchor.line != cell.selEnd.line || cell.selAnchor.col != cell.selEnd.col);
                } else {
                    cell.ClearSelection();
                    cell.selAnchor = { targetLine, targetCol };
                    cell.selEnd = { targetLine, targetCol };
                }
                cell.inputBuffer.SetCursor(targetLine, targetCol);
                m_isSelectingText = true;
                if (hwnd) SetCapture(hwnd);
                m_caretVisible = true;
                m_lastBlinkTime = GetTickCount64();
                return true;
            }

            // Drag handle or card header/margin click initiates drag candidate
            if (PointInRect(clientPt, cell.dragHandleBounds) ||
                (clientPt.y >= cell.bounds.top && clientPt.y <= cell.bounds.top + config.ScaleF(28.0f)) ||
                (clientPt.x >= cell.bounds.left && clientPt.x < cell.codeAreaBounds.left)) {
                m_dragCandidateCellIdx = static_cast<int>(ci);
                m_dragStartPt = clientPt;
                if (hwnd) SetCapture(hwnd);
                return true;
            }

            return true;
        }
    }

    return false;
}

bool NotebookView::HandleMouseMove(POINT clientPt, editor::OpenDocument& doc, const app::AppConfig& config) {
    if (m_isDraggingScrollbar) {
        float deltaY = static_cast<float>(clientPt.y) - m_dragScrollStartY;
        float contentH = (std::max)(1.0f, m_viewHeight - config.ScaleF(40.0f));
        float maxScroll = (std::max)(0.0f, m_totalHeight - contentH + config.ScaleF(40.0f));
        float scrollDelta = (deltaY / contentH) * m_totalHeight;
        m_scrollY = (std::clamp)(m_dragScrollStartOffset + scrollDelta, 0.0f, maxScroll);
        if (doc.notebookDoc) doc.notebookDoc->scrollY = m_scrollY;
        return true;
    }

    if (m_isDraggingOutputVScroll && doc.notebookDoc && m_dragOutputCellIdx >= 0 && m_dragOutputCellIdx < static_cast<int>(doc.notebookDoc->cells.size())) {
        auto& cell = doc.notebookDoc->cells[m_dragOutputCellIdx];
        float deltaY = static_cast<float>(clientPt.y) - m_dragOutputStartPos;
        float trackH = cell.outputVScrollTrackBounds.bottom - cell.outputVScrollTrackBounds.top;
        float thumbH = cell.outputVScrollThumbBounds.bottom - cell.outputVScrollThumbBounds.top;
        float scrollable = (std::max)(1.0f, trackH - thumbH);
        float scrollDelta = (deltaY / scrollable) * cell.outputMaxScrollY;
        cell.outputScrollY = (std::clamp)(m_dragOutputStartOffset + scrollDelta, 0.0f, cell.outputMaxScrollY);
        return true;
    }

    if (m_isDraggingOutputHScroll && doc.notebookDoc && m_dragOutputCellIdx >= 0 && m_dragOutputCellIdx < static_cast<int>(doc.notebookDoc->cells.size())) {
        auto& cell = doc.notebookDoc->cells[m_dragOutputCellIdx];
        float deltaX = static_cast<float>(clientPt.x) - m_dragOutputStartPos;
        float trackW = cell.outputHScrollTrackBounds.right - cell.outputHScrollTrackBounds.left;
        float thumbW = cell.outputHScrollThumbBounds.right - cell.outputHScrollThumbBounds.left;
        float scrollable = (std::max)(1.0f, trackW - thumbW);
        float scrollDelta = (deltaX / scrollable) * cell.outputMaxScrollX;
        cell.outputScrollX = (std::clamp)(m_dragOutputStartOffset + scrollDelta, 0.0f, cell.outputMaxScrollX);
        return true;
    }

    // Drag-selection of text across lines and columns
    if (m_isSelectingText && doc.notebookDoc) {
        int activeIdx = doc.notebookDoc->activeCellIdx;
        if (activeIdx >= 0 && activeIdx < static_cast<int>(doc.notebookDoc->cells.size())) {
            auto& cell = doc.notebookDoc->cells[activeIdx];
            size_t targetLine = 0, targetCol = 0;
            HitTestCodeArea(cell, clientPt, config, targetLine, targetCol);
            cell.selEnd = { targetLine, targetCol };
            cell.hasSelection = (cell.selAnchor.line != cell.selEnd.line || cell.selAnchor.col != cell.selEnd.col);
            cell.inputBuffer.SetCursor(targetLine, targetCol);
            return true;
        }
    }

    // Check if dragging cell should start (> 4px movement)
    if (m_dragCandidateCellIdx >= 0 && !m_isDraggingCell) {
        int dx = abs(clientPt.x - m_dragStartPt.x);
        int dy = abs(clientPt.y - m_dragStartPt.y);
        if (dx > 4 || dy > 4) {
            m_isDraggingCell = true;
            m_draggedCellIdx = m_dragCandidateCellIdx;
            m_dropTargetCellIdx = m_draggedCellIdx;
            m_dragCandidateCellIdx = -1;
        }
    }

    if (m_isDraggingCell && doc.notebookDoc) {
        const auto& cells = doc.notebookDoc->cells;
        int targetIdx = m_draggedCellIdx;
        float bestDist = 1e9f;

        for (size_t ci = 0; ci < cells.size(); ++ci) {
            float midY = (cells[ci].bounds.top + cells[ci].bounds.bottom) * 0.5f;
            float dist = std::abs(static_cast<float>(clientPt.y) - midY);
            if (dist < bestDist) {
                bestDist = dist;
                targetIdx = static_cast<int>(ci);
            }
        }
        if (targetIdx != m_dropTargetCellIdx) {
            m_dropTargetCellIdx = targetIdx;
            return true;
        }
        return true;
    }

    auto PointInRect = [](POINT pt, const D2D1_RECT_F& r) -> bool {
        return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
    };

    int oldToolbarBtn = m_hoveredToolbarBtn;
    if (PointInRect(clientPt, m_btnRunAll)) m_hoveredToolbarBtn = 1;
    else if (PointInRect(clientPt, m_btnClearAll)) m_hoveredToolbarBtn = 2;
    else if (PointInRect(clientPt, m_btnAddCode)) m_hoveredToolbarBtn = 3;
    else if (PointInRect(clientPt, m_btnAddText)) m_hoveredToolbarBtn = 4;
    else m_hoveredToolbarBtn = 0;

    int oldCellIdx = m_hoveredCellIdx;
    int oldBtn = m_hoveredBtn;
    int oldBottomCell = m_hoveredBottomCellIdx;
    int oldBottomBtn = m_hoveredBottomBtn;
    m_hoveredCellIdx = -1;
    m_hoveredBtn = 0;
    m_hoveredBottomCellIdx = -1;
    m_hoveredBottomBtn = 0;

    if (doc.notebookDoc) {
        // 1. Check bottom border hover (+ Code, + Text)
        for (size_t ci = 0; ci < doc.notebookDoc->cells.size(); ++ci) {
            const auto& cell = doc.notebookDoc->cells[ci];
            if (clientPt.x >= cell.bounds.left && clientPt.x <= cell.bounds.right &&
                clientPt.y >= cell.bounds.bottom - config.ScaleF(12.0f) &&
                clientPt.y <= cell.bounds.bottom + config.ScaleF(12.0f)) {
                m_hoveredBottomCellIdx = static_cast<int>(ci);
                if (PointInRect(clientPt, cell.bottomAddCodeBounds)) m_hoveredBottomBtn = 1;
                else if (PointInRect(clientPt, cell.bottomAddTextBounds)) m_hoveredBottomBtn = 2;
                break;
            }
        }

        // 2. Check cell buttons and cards
        for (size_t ci = 0; ci < doc.notebookDoc->cells.size(); ++ci) {
            const auto& cell = doc.notebookDoc->cells[ci];
            if (PointInRect(clientPt, cell.bounds)) {
                m_hoveredCellIdx = static_cast<int>(ci);
                if (PointInRect(clientPt, cell.runBtnBounds)) m_hoveredBtn = 1;
                else if (PointInRect(clientPt, cell.deleteBtnBounds)) m_hoveredBtn = 2;
                else if (PointInRect(clientPt, cell.moveUpBtnBounds)) m_hoveredBtn = 3;
                else if (PointInRect(clientPt, cell.moveDownBtnBounds)) m_hoveredBtn = 4;
                else if (PointInRect(clientPt, cell.dragHandleBounds)) m_hoveredBtn = 6;
                break;
            }
        }
    }

    return (oldToolbarBtn != m_hoveredToolbarBtn || oldCellIdx != m_hoveredCellIdx ||
            oldBtn != m_hoveredBtn || oldBottomCell != m_hoveredBottomCellIdx ||
            oldBottomBtn != m_hoveredBottomBtn);
}

bool NotebookView::HandleLButtonUp(POINT clientPt, editor::OpenDocument& doc, const app::AppConfig& config) {
    if (m_isDraggingScrollbar) {
        m_isDraggingScrollbar = false;
        ReleaseCapture();
        return true;
    }
    if (m_isDraggingOutputVScroll || m_isDraggingOutputHScroll) {
        m_isDraggingOutputVScroll = false;
        m_isDraggingOutputHScroll = false;
        m_dragOutputCellIdx = -1;
        ReleaseCapture();
        return true;
    }
    if (m_isSelectingText) {
        m_isSelectingText = false;
        ReleaseCapture();
        if (doc.notebookDoc && doc.notebookDoc->activeCellIdx >= 0 &&
            doc.notebookDoc->activeCellIdx < static_cast<int>(doc.notebookDoc->cells.size())) {
            auto& cell = doc.notebookDoc->cells[doc.notebookDoc->activeCellIdx];
            if (cell.selAnchor.line == cell.selEnd.line && cell.selAnchor.col == cell.selEnd.col) {
                cell.ClearSelection();
            }
        }
        return true;
    }
    if (m_isDraggingCell) {
        m_isDraggingCell = false;
        ReleaseCapture();
        int from = m_draggedCellIdx;
        int to = m_dropTargetCellIdx;
        m_draggedCellIdx = -1;
        m_dropTargetCellIdx = -1;
        m_dragCandidateCellIdx = -1;
        if (doc.notebookDoc && from >= 0 && to >= 0 && from != to) {
            doc.notebookDoc->MoveCell(from, to);
            return true;
        }
        return true;
    }
    if (m_dragCandidateCellIdx >= 0) {
        m_dragCandidateCellIdx = -1;
        ReleaseCapture();
    }
    return false;
}

bool NotebookView::HandleMouseWheel(short delta, POINT clientPt, editor::OpenDocument& doc, const app::AppConfig& config) {
    // 1. If mouse is over a cell output area, scroll that output's X or Y
    if (doc.notebookDoc) {
        for (auto& cell : doc.notebookDoc->cells) {
            if (!cell.outputs.empty() &&
                clientPt.x >= cell.outputAreaBounds.left && clientPt.x <= cell.outputAreaBounds.right &&
                clientPt.y >= cell.outputAreaBounds.top && clientPt.y <= cell.outputAreaBounds.bottom) {

                size_t outLines = 0;
                float charW = (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.5f);
                float maxLineW = 0.0f;
                for (const auto& out : cell.outputs) {
                    std::wstringstream ss(out.text);
                    std::wstring l;
                    while (std::getline(ss, l)) {
                        outLines++;
                        float lw = static_cast<float>(l.length()) * charW;
                        if (lw > maxLineW) maxLineW = lw;
                    }
                }
                float outLineH = config.ScaleF(18.0f);
                float totalContentH = static_cast<float>(outLines) * outLineH + config.ScaleF(12.0f);
                float outputH = cell.outputAreaBounds.bottom - cell.outputAreaBounds.top;
                float availW = (cell.outputAreaBounds.right - cell.outputAreaBounds.left) - config.ScaleF(24.0f);

                bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (isShift) {
                    // Horizontal scroll
                    float maxScrollX = (std::max)(0.0f, maxLineW - availW);
                    if (maxScrollX > 0.0f) {
                        float step = config.ScaleF(40.0f);
                        if (delta > 0) {
                            if (cell.outputScrollX > 0.0f) {
                                cell.outputScrollX = (std::max)(0.0f, cell.outputScrollX - step);
                                return true;
                            }
                        } else {
                            if (cell.outputScrollX < maxScrollX) {
                                cell.outputScrollX = (std::min)(maxScrollX, cell.outputScrollX + step);
                                return true;
                            }
                        }
                    }
                } else {
                    // Vertical scroll with scroll chaining:
                    // If output is already at the top and scrolling UP, or already at the bottom and scrolling DOWN,
                    // propagate scroll to the whole notebook view!
                    float maxScrollY = (std::max)(0.0f, totalContentH - outputH);
                    if (maxScrollY > 0.0f) {
                        float step = config.ScaleF(40.0f);
                        if (delta > 0) {
                            if (cell.outputScrollY > 0.0f) {
                                cell.outputScrollY = (std::max)(0.0f, cell.outputScrollY - step);
                                return true;
                            }
                            // At top (outputScrollY <= 0) -> let scroll propagate to the notebook!
                        } else {
                            if (cell.outputScrollY < maxScrollY) {
                                cell.outputScrollY = (std::min)(maxScrollY, cell.outputScrollY + step);
                                return true;
                            }
                            // At bottom (outputScrollY >= maxScrollY) -> let scroll propagate to the notebook!
                        }
                    }
                }
            }
        }
    }

    // 2. Otherwise scroll notebook view vertically
    float contentH = (std::max)(1.0f, m_viewHeight - config.ScaleF(40.0f));
    float maxScroll = (std::max)(0.0f, m_totalHeight - contentH + config.ScaleF(40.0f));
    float scrollStep = config.ScaleF(60.0f);
    if (delta > 0) {
        m_scrollY = (std::max)(0.0f, m_scrollY - scrollStep);
    } else {
        m_scrollY = (std::min)(maxScroll, m_scrollY + scrollStep);
    }
    if (doc.notebookDoc) doc.notebookDoc->scrollY = m_scrollY;
    return true;
}

bool NotebookView::HandleKeyDown(WPARAM wParam, editor::OpenDocument& doc, HWND hwnd) {
    if (wParam == VK_ESCAPE && m_isDraggingCell) {
        m_isDraggingCell = false;
        m_draggedCellIdx = -1;
        m_dropTargetCellIdx = -1;
        m_dragCandidateCellIdx = -1;
        ReleaseCapture();
        return true;
    }

    if (!doc.notebookDoc || doc.notebookDoc->cells.empty()) return false;
    auto nb = doc.notebookDoc;
    if (nb->activeCellIdx < 0 || nb->activeCellIdx >= static_cast<int>(nb->cells.size())) return false;

    auto& activeCell = nb->cells[nb->activeCellIdx];
    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool isCtrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    // Shift + Enter: Run active cell and advance to next (classic Jupyter shortcut)
    if (wParam == VK_RETURN && isShift) {
        int nextIdx = nb->activeCellIdx + 1;
        if (nextIdx >= static_cast<int>(nb->cells.size())) {
            nb->AddCell(editor::NotebookCellType::Code, nb->activeCellIdx);
        } else {
            nb->activeCellIdx = nextIdx;
        }
        RunCell(nb->activeCellIdx - 1, doc, hwnd);
        return true;
    }

    // Ctrl + Enter: Run active cell and stay focused
    if (wParam == VK_RETURN && isCtrl) {
        RunCell(nb->activeCellIdx, doc, hwnd);
        return true;
    }

    // Ctrl + Z: Undo / Ctrl + Shift + Z: Redo
    if (isCtrl && (wParam == 'Z' || wParam == 'z')) {
        if (isShift) {
            activeCell.ClearSelection();
            activeCell.inputBuffer.Redo();
        } else {
            activeCell.ClearSelection();
            activeCell.inputBuffer.Undo();
        }
        nb->isDirty = true;
        return true;
    }

    // Ctrl + Y: Redo
    if (isCtrl && (wParam == 'Y' || wParam == 'y')) {
        activeCell.ClearSelection();
        activeCell.inputBuffer.Redo();
        nb->isDirty = true;
        return true;
    }

    // Ctrl + A: Select All
    if (isCtrl && (wParam == 'A' || wParam == 'a')) {
        size_t lineCount = activeCell.inputBuffer.GetLineCount();
        if (lineCount > 0) {
            activeCell.selAnchor = { 0, 0 };
            size_t lastL = lineCount - 1;
            size_t lastC = activeCell.inputBuffer.GetLine(lastL).length();
            activeCell.selEnd = { lastL, lastC };
            activeCell.hasSelection = true;
            activeCell.inputBuffer.SetCursor(lastL, lastC);
        }
        return true;
    }

    // Ctrl + C: Copy
    if (isCtrl && (wParam == 'C' || wParam == 'c')) {
        if (activeCell.HasSelection()) {
            SetClipboardText(activeCell.GetSelectedText(), hwnd);
        }
        return true;
    }

    // Ctrl + X: Cut
    if (isCtrl && (wParam == 'X' || wParam == 'x')) {
        if (activeCell.HasSelection()) {
            SetClipboardText(activeCell.GetSelectedText(), hwnd);
            activeCell.DeleteSelection();
            nb->isDirty = true;
        }
        return true;
    }

    // Ctrl + V: Paste
    if (isCtrl && (wParam == 'V' || wParam == 'v')) {
        std::wstring clip = GetClipboardText(hwnd);
        if (!clip.empty()) {
            if (activeCell.HasSelection()) {
                activeCell.DeleteSelection();
            }
            activeCell.inputBuffer.InsertText(clip);
            nb->isDirty = true;
        }
        return true;
    }

    m_caretVisible = true;
    m_lastBlinkTime = GetTickCount64();

    switch (wParam) {
        case VK_RETURN: {
            if (activeCell.HasSelection()) {
                activeCell.DeleteSelection();
            }
            auto cur = activeCell.inputBuffer.GetCursor();
            const auto& lines = activeCell.inputBuffer.GetLines();
            std::wstring baseIndent;
            bool shouldIndent = false;
            if (cur.line < lines.size()) {
                const auto& line = lines[cur.line];
                for (size_t col = 0; col < line.length() && (line[col] == L' ' || line[col] == L'\t'); ++col) {
                    baseIndent += line[col];
                }

                // Check if between braces: {|}
                if (cur.col > 0 && cur.col < line.length() && line[cur.col - 1] == L'{' && line[cur.col] == L'}') {
                    std::wstring midIndent = baseIndent + L"  ";
                    std::wstring expandStr = L"\n" + midIndent + L"\n" + baseIndent;
                    activeCell.inputBuffer.InsertText(expandStr);
                    activeCell.inputBuffer.SetCursor(cur.line + 1, midIndent.length());
                    nb->isDirty = true;
                    return true;
                }

                // Strictly inspect text before cursor on the CURRENT line (cur.line)
                std::wstring textBeforeCursor = (cur.col <= line.length()) ? line.substr(0, cur.col) : line;
                size_t endPos = textBeforeCursor.find_last_not_of(L" \t");
                if (endPos != std::wstring::npos && (textBeforeCursor[endPos] == L':' || textBeforeCursor[endPos] == L'{')) {
                    shouldIndent = true;
                }
            }
            if (shouldIndent) {
                // Auto-indent next line by 2 spaces relative to base indent (Colab/Jupyter standard)
                std::wstring indentStr = L"\n" + baseIndent + L"  ";
                activeCell.inputBuffer.InsertText(indentStr);
            } else if (!baseIndent.empty()) {
                std::wstring indentStr = L"\n" + baseIndent;
                activeCell.inputBuffer.InsertText(indentStr);
            } else {
                activeCell.inputBuffer.InsertChar(L'\n');
            }
            nb->isDirty = true;
            return true;
        }
        case VK_BACK: {
            if (activeCell.HasSelection()) {
                activeCell.DeleteSelection();
            } else {
                auto cur = activeCell.inputBuffer.GetCursor();
                const auto& lines = activeCell.inputBuffer.GetLines();
                bool isPair = false;
                if (cur.line < lines.size()) {
                    const auto& line = lines[cur.line];
                    if (cur.col > 0 && cur.col < line.length()) {
                        wchar_t leftCh = line[cur.col - 1];
                        wchar_t rightCh = line[cur.col];
                        if ((leftCh == L'(' && rightCh == L')') ||
                            (leftCh == L'[' && rightCh == L']') ||
                            (leftCh == L'{' && rightCh == L'}') ||
                            (leftCh == L'"' && rightCh == L'"') ||
                            (leftCh == L'\'' && rightCh == L'\'') ||
                            (leftCh == L'`' && rightCh == L'`')) {
                            isPair = true;
                        }
                    }
                }
                if (isPair) {
                    activeCell.inputBuffer.MoveCursorRight();
                    activeCell.inputBuffer.Backspace();
                    activeCell.inputBuffer.Backspace();
                } else {
                    activeCell.inputBuffer.Backspace();
                }
            }
            nb->isDirty = true;
            return true;
        }
        case VK_DELETE: {
            if (activeCell.HasSelection()) {
                activeCell.DeleteSelection();
            } else {
                auto cur = activeCell.inputBuffer.GetCursor();
                const auto& lines = activeCell.inputBuffer.GetLines();
                if (cur.line < lines.size()) {
                    if (cur.col < lines[cur.line].length() || cur.line + 1 < lines.size()) {
                        activeCell.inputBuffer.MoveCursorRight();
                        activeCell.inputBuffer.Backspace();
                    }
                }
            }
            nb->isDirty = true;
            return true;
        }
        case VK_TAB:
            if (activeCell.HasSelection()) {
                activeCell.DeleteSelection();
            }
            activeCell.inputBuffer.InsertText(L"  "); // 2 spaces (Colab / Jupyter standard)
            nb->isDirty = true;
            return true;
        case VK_LEFT:
            if (activeCell.HasSelection() && !isShift) {
                size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                activeCell.GetSelectionRange(sLine, sCol, eLine, eCol);
                activeCell.ClearSelection();
                activeCell.inputBuffer.SetCursor(sLine, sCol);
            } else {
                activeCell.inputBuffer.MoveCursorLeft();
            }
            return true;
        case VK_RIGHT:
            if (activeCell.HasSelection() && !isShift) {
                size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                activeCell.GetSelectionRange(sLine, sCol, eLine, eCol);
                activeCell.ClearSelection();
                activeCell.inputBuffer.SetCursor(eLine, eCol);
            } else {
                activeCell.inputBuffer.MoveCursorRight();
            }
            return true;
        case VK_UP:
            if (activeCell.HasSelection() && !isShift) activeCell.ClearSelection();
            activeCell.inputBuffer.MoveCursorUp();
            return true;
        case VK_DOWN:
            if (activeCell.HasSelection() && !isShift) activeCell.ClearSelection();
            activeCell.inputBuffer.MoveCursorDown();
            return true;
        case VK_HOME:
            if (activeCell.HasSelection() && !isShift) activeCell.ClearSelection();
            activeCell.inputBuffer.SetCursor(activeCell.inputBuffer.GetCursor().line, 0);
            return true;
        case VK_END: {
            if (activeCell.HasSelection() && !isShift) activeCell.ClearSelection();
            auto cur = activeCell.inputBuffer.GetCursor();
            if (cur.line < activeCell.inputBuffer.GetLineCount()) {
                activeCell.inputBuffer.SetCursor(cur.line, activeCell.inputBuffer.GetLine(cur.line).length());
            }
            return true;
        }
        default:
            break;
    }
    return false;
}

bool NotebookView::HandleChar(wchar_t ch, editor::OpenDocument& doc, HWND hwnd) {
    if (!doc.notebookDoc || doc.notebookDoc->cells.empty()) return false;
    auto nb = doc.notebookDoc;
    if (nb->activeCellIdx < 0 || nb->activeCellIdx >= static_cast<int>(nb->cells.size())) return false;

    // Filter control characters handled in HandleKeyDown
    if (ch < 32 && ch != L'\t') return false;

    auto& activeCell = nb->cells[nb->activeCellIdx];

    // Auto-closing bracket/quote pair mapping
    wchar_t pairClose = 0;
    if (ch == L'(') pairClose = L')';
    else if (ch == L'[') pairClose = L']';
    else if (ch == L'{') pairClose = L'}';
    else if (ch == L'"') pairClose = L'"';
    else if (ch == L'\'') pairClose = L'\'';
    else if (ch == L'`') pairClose = L'`';

    // 1. Wrap active selection when typing bracket/quote opener
    if (activeCell.HasSelection()) {
        if (pairClose != 0) {
            std::wstring sel = activeCell.GetSelectedText();
            activeCell.DeleteSelection();
            std::wstring wrapped = std::wstring(1, ch) + sel + pairClose;
            activeCell.inputBuffer.InsertText(wrapped);
            nb->isDirty = true;
            m_caretVisible = true;
            m_lastBlinkTime = GetTickCount64();
            return true;
        } else {
            activeCell.DeleteSelection();
        }
    }

    // 2. Overtype closing character if cursor is immediately before it
    auto cur = activeCell.inputBuffer.GetCursor();
    const auto& lines = activeCell.inputBuffer.GetLines();
    if (cur.line < lines.size()) {
        const auto& line = lines[cur.line];
        if (cur.col < line.length() && line[cur.col] == ch) {
            if (ch == L')' || ch == L']' || ch == L'}' || ch == L'"' || ch == L'\'' || ch == L'`') {
                // Step over closing character
                activeCell.inputBuffer.MoveCursorRight();
                nb->isDirty = true;
                m_caretVisible = true;
                m_lastBlinkTime = GetTickCount64();
                return true;
            }
        }
    }

    // 3. Auto-insert pair and position cursor inside
    if (pairClose != 0) {
        std::wstring pairStr;
        pairStr += ch;
        pairStr += pairClose;
        activeCell.inputBuffer.InsertText(pairStr);
        activeCell.inputBuffer.MoveCursorLeft();
        nb->isDirty = true;
        m_caretVisible = true;
        m_lastBlinkTime = GetTickCount64();
        return true;
    }

    // Normal character insertion
    activeCell.inputBuffer.InsertChar(ch);
    nb->isDirty = true;
    m_caretVisible = true;
    m_lastBlinkTime = GetTickCount64();
    return true;
}

LPCWSTR NotebookView::GetCursorForPoint(POINT pt, const editor::OpenDocument& doc) const {
    auto PointInRect = [](POINT p, const D2D1_RECT_F& r) -> bool {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    };

    if (m_isDraggingCell) return IDC_SIZEALL;

    // Output and main scrollbar dragging cursors (Vertical ↕, Horizontal ↔)
    if (m_isDraggingScrollbar || m_isDraggingOutputVScroll) return IDC_SIZENS;
    if (m_isDraggingOutputHScroll) return IDC_SIZEWE;

    // Main notebook scrollbar thumb & track -> IDC_SIZENS (Vertical double-arrow ↕, Image 4)
    if (PointInRect(pt, m_scrollThumb) || PointInRect(pt, m_scrollTrack)) {
        return IDC_SIZENS;
    }

    if (PointInRect(pt, m_btnRunAll) || PointInRect(pt, m_btnClearAll) ||
        PointInRect(pt, m_btnAddCode) || PointInRect(pt, m_btnAddText)) {
        return IDC_HAND;
    }

    if (doc.notebookDoc) {
        for (const auto& cell : doc.notebookDoc->cells) {
            // Cell output vertical scrollbar -> IDC_SIZENS (Vertical double-arrow ↕, Image 4)
            if (cell.outputMaxScrollY > 0.0f) {
                if (PointInRect(pt, cell.outputVScrollThumbBounds) || PointInRect(pt, cell.outputVScrollTrackBounds)) {
                    return IDC_SIZENS;
                }
            }
            // Cell output horizontal scrollbar -> IDC_SIZEWE (Horizontal double-arrow ↔, Image 4)
            if (cell.outputMaxScrollX > 0.0f) {
                if (PointInRect(pt, cell.outputHScrollThumbBounds) || PointInRect(pt, cell.outputHScrollTrackBounds)) {
                    return IDC_SIZEWE;
                }
            }

            if (PointInRect(pt, cell.dragHandleBounds)) {
                return IDC_SIZEALL;
            }
            if (PointInRect(pt, cell.runBtnBounds) || PointInRect(pt, cell.deleteBtnBounds) ||
                PointInRect(pt, cell.moveUpBtnBounds) || PointInRect(pt, cell.moveDownBtnBounds) ||
                PointInRect(pt, cell.bottomAddCodeBounds) || PointInRect(pt, cell.bottomAddTextBounds)) {
                return IDC_HAND;
            }
            if (PointInRect(pt, cell.codeAreaBounds)) {
                return IDC_IBEAM;
            }
        }
    }
    return IDC_ARROW;
}

std::wstring NotebookView::FindPythonExecutable(const std::wstring& targetFilePath) const {
    // 1. Check local virtual environments in the notebook's directory or parent directories
    std::vector<std::wstring> candidateDirs;

    if (!targetFilePath.empty()) {
        try {
            std::filesystem::path p(targetFilePath);
            auto cur = p.has_filename() ? p.parent_path() : p;
            for (int depth = 0; depth < 3 && !cur.empty(); ++depth) {
                candidateDirs.push_back(cur.wstring());
                auto parent = cur.parent_path();
                if (parent == cur) break;
                cur = parent;
            }
        } catch (...) {}
    }

    if (!m_workspacePath.empty()) {
        candidateDirs.push_back(m_workspacePath);
    }

    // Check candidate directories for common virtual environments (.venv, venv, env, .env)
    const wchar_t* venvNames[] = { L".venv", L"venv", L"env", L".env" };
    for (const auto& dir : candidateDirs) {
        for (const auto* vname : venvNames) {
            std::wstring vpy = dir + L"\\" + vname + L"\\Scripts\\python.exe";
            if (std::filesystem::exists(vpy)) {
                return vpy;
            }
        }
    }

    // 2. Check for Global Python installations (where user pip install packages live)
    // 2a. Check python.exe on PATH (skipping emsdk if present)
    wchar_t buf[MAX_PATH]{};
    if (SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, buf, nullptr) > 0) {
        std::wstring pathPy = buf;
        if (pathPy.find(L"emsdk") == std::wstring::npos && std::filesystem::exists(pathPy)) {
            return pathPy;
        }
    }

    // 2b. Check User Local AppData Python installations (default python.org installer location)
    wchar_t localApp[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH) > 0) {
        std::wstring pyBase = std::wstring(localApp) + L"\\Programs\\Python";
        if (std::filesystem::exists(pyBase)) {
            try {
                std::wstring highestPy;
                for (const auto& entry : std::filesystem::directory_iterator(pyBase)) {
                    if (entry.is_directory()) {
                        std::wstring cand = entry.path().wstring() + L"\\python.exe";
                        if (std::filesystem::exists(cand)) {
                            if (highestPy.empty() || cand > highestPy) {
                                highestPy = cand;
                            }
                        }
                    }
                }
                if (!highestPy.empty()) return highestPy;
            } catch (...) {}
        }
    }

    // 2c. Check C:\Program Files\Python3*\python.exe and C:\Python3*\python.exe
    const wchar_t* rootCandidates[] = {
        L"C:\\Program Files\\Python315\\python.exe",
        L"C:\\Program Files\\Python314\\python.exe",
        L"C:\\Program Files\\Python313\\python.exe",
        L"C:\\Program Files\\Python312\\python.exe",
        L"C:\\Program Files\\Python311\\python.exe",
        L"C:\\Program Files\\Python310\\python.exe",
        L"C:\\Python315\\python.exe",
        L"C:\\Python313\\python.exe",
        L"C:\\Python312\\python.exe",
        L"C:\\Python311\\python.exe"
    };
    for (const auto* rc : rootCandidates) {
        if (std::filesystem::exists(rc)) return rc;
    }

    // 2d. Check py.exe on PATH
    if (SearchPathW(nullptr, L"py.exe", nullptr, MAX_PATH, buf, nullptr) > 0) {
        return buf;
    }

    // 2e. Check PATH python even if it was emsdk
    if (SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, buf, nullptr) > 0) {
        return buf;
    }

    // 3. Fallback: dedicated EMSDK Python if present
    std::wstring emsdkPy = L"C:\\Users\\Administrator\\Downloads\\emsdk\\python\\3.13.3_64bit\\python.exe";
    if (std::filesystem::exists(emsdkPy)) return emsdkPy;

    return L"python";
}

void NotebookView::RunCell(int cellIdx, editor::OpenDocument& doc, HWND hwnd) {
    if (!doc.notebookDoc || cellIdx < 0 || cellIdx >= static_cast<int>(doc.notebookDoc->cells.size())) return;
    auto nb = doc.notebookDoc;
    auto& cell = nb->cells[cellIdx];

    // Markdown cells don't execute; they just remain formatted
    if (cell.type == editor::NotebookCellType::Markdown) {
        cell.executionCount = -1;
        return;
    }

    // If another cell is already running, enqueue this cell into the execution queue
    if (m_isExecuting.load()) {
        std::lock_guard<std::mutex> qLock(m_queueMutex);
        if (m_executingCellIdx.load() == cellIdx) {
            return; // Already actively executing
        }
        bool alreadyInQueue = false;
        for (int q : m_cellQueue) {
            if (q == cellIdx) {
                alreadyInQueue = true;
                break;
            }
        }
        if (!alreadyInQueue) {
            m_cellQueue.push_back(cellIdx);
            cell.isQueued = true;
            cell.isExecuting = false;
            cell.executionCount = -2; // shows [*] while waiting in queue
        }
        nb->isDirty = true;
        if (hwnd) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }

    std::wstring code = cell.inputBuffer.GetText();
    std::wstring lang = cell.language;
    auto nbDocPtr = doc.notebookDoc;

    // Fast-path: empty or whitespace-only code cells complete immediately in 0 ms (no freeze)
    bool isWhitespaceOnly = true;
    for (wchar_t ch : code) {
        if (!iswspace(ch)) {
            isWhitespaceOnly = false;
            break;
        }
    }
    if (isWhitespaceOnly) {
        cell.isExecuting = false;
        cell.isQueued = false;
        cell.outputs.clear();
        cell.hasError = false;
        cell.errorLine = -1;
        cell.lastElapsedMs = 0;
        if (cell.executionCount <= 0) {
            cell.executionCount = 1;
        } else {
            cell.executionCount++;
        }
        nb->isDirty = true;
        if (hwnd) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        // Trigger next queued cell if any
        int nextCellIdx = -1;
        {
            std::lock_guard<std::mutex> qLock(m_queueMutex);
            while (!m_cellQueue.empty()) {
                int cand = m_cellQueue.front();
                m_cellQueue.erase(m_cellQueue.begin());
                if (cand >= 0 && cand < static_cast<int>(nb->cells.size())) {
                    nextCellIdx = cand;
                    break;
                }
            }
        }
        if (nextCellIdx >= 0) {
            RunCell(nextCellIdx, doc, hwnd);
        }
        return;
    }

    cell.isExecuting = true;
    cell.isQueued = false;
    cell.outputs.clear();
    cell.hasError = false;
    cell.errorLine = -1;
    m_isExecuting.store(true);
    m_executingCellIdx.store(cellIdx);
    m_shouldStop.store(false);

    if (m_execThread.joinable()) {
        m_execThread.detach(); // Detach old thread so UI thread NEVER blocks!
    }

    m_execThread = std::thread([this, cellIdx, code, lang, nbDocPtr, hwnd]() {
        ExecuteScriptAsync(cellIdx, code, lang, nbDocPtr, hwnd);
    });
}

void NotebookView::RunAllCells(editor::OpenDocument& doc, HWND hwnd) {
    if (!doc.notebookDoc || doc.notebookDoc->cells.empty()) return;
    m_shouldStop.store(false);
    m_isQueueRunning.store(true);

    // Find first Code cell from the top (cell 0 or lowest index)
    int firstCodeIdx = -1;
    for (size_t i = 0; i < doc.notebookDoc->cells.size(); ++i) {
        if (doc.notebookDoc->cells[i].type == editor::NotebookCellType::Code) {
            firstCodeIdx = static_cast<int>(i);
            break;
        }
    }

    if (firstCodeIdx >= 0) {
        RunCell(firstCodeIdx, doc, hwnd);
    } else {
        m_isQueueRunning.store(false);
    }
}

void NotebookView::StopExecution(editor::OpenDocument& doc) {
    StopExecution_Internal();
    if (doc.notebookDoc) {
        for (auto& c : doc.notebookDoc->cells) {
            c.isExecuting = false;
            c.isQueued = false;
        }
    }
}

void NotebookView::RestartKernel(editor::OpenDocument& doc) {
    StopExecution(doc);
    m_kernel.Restart();
    if (doc.notebookDoc) {
        doc.notebookDoc->ClearAllOutputs();
    }
}

void NotebookView::StopExecution_Internal() {
    m_shouldStop.store(true);
    m_isQueueRunning.store(false);
    m_queueNextCellIdx.store(-1);
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_cellQueue.clear();
    }
    m_kernel.Interrupt();
    m_isExecuting.store(false);
    m_executingCellIdx.store(-1);
    std::lock_guard<std::mutex> lock(m_execMutex);
    if (m_hChildProcess) {
        TerminateProcess(m_hChildProcess, 1);
        CloseHandle(m_hChildProcess);
        m_hChildProcess = NULL;
    }
}

void NotebookView::ExecuteScriptAsync(int cellIdx, const std::wstring& script, const std::wstring& lang,
                                      std::shared_ptr<editor::NotebookDocument> nbDoc, HWND hwnd) {
    std::wstring docPath = nbDoc ? nbDoc->filePath : L"";
    std::wstring pyExe = FindPythonExecutable(docPath);
    std::wstring workingDir;
    if (!docPath.empty()) {
        try {
            workingDir = std::filesystem::path(docPath).parent_path().wstring();
        } catch (...) {}
    }
    if (workingDir.empty()) {
        workingDir = m_workspacePath;
    }

    auto kernelResult = m_kernel.Execute(script, pyExe, workingDir);

    bool hasErr = kernelResult.hasError;
    std::wstring capturedOutput = kernelResult.output;
    uint64_t elapsedMs = kernelResult.elapsedMs;

    // 3. Update notebook document cell outputs & error state
    if (nbDoc && cellIdx >= 0 && cellIdx < static_cast<int>(nbDoc->cells.size())) {
        auto& cell = nbDoc->cells[cellIdx];
        cell.isExecuting = false;
        cell.isQueued = false;
        cell.hasError = hasErr;
        cell.lastElapsedMs = elapsedMs;
        cell.outputs.clear();

        int errLine = -1;
        if (hasErr) {
            size_t pos = capturedOutput.find(L"File \"<cell>\", line ");
            if (pos != std::wstring::npos) {
                size_t numStart = pos + wcslen(L"File \"<cell>\", line ");
                size_t numEnd = numStart;
                while (numEnd < capturedOutput.length() && iswdigit(capturedOutput[numEnd])) {
                    numEnd++;
                }
                if (numEnd > numStart) {
                    try {
                        int line1 = std::stoi(capturedOutput.substr(numStart, numEnd - numStart));
                        errLine = line1 - 1;
                    } catch (...) {}
                }
            }
            if (errLine < 0) {
                size_t pos2 = capturedOutput.find(L", line ");
                if (pos2 != std::wstring::npos) {
                    size_t numStart = pos2 + wcslen(L", line ");
                    size_t numEnd = numStart;
                    while (numEnd < capturedOutput.length() && iswdigit(capturedOutput[numEnd])) {
                        numEnd++;
                    }
                    if (numEnd > numStart) {
                        try {
                            int line1 = std::stoi(capturedOutput.substr(numStart, numEnd - numStart));
                            errLine = line1 - 1;
                        } catch (...) {}
                    }
                }
            }
        }
        cell.errorLine = errLine;

        // Only add output if there is actual stdout/stderr produced (Jupyter / Colab standard)
        // Assignment cells like s = "hello" produce no output, so the output box is suppressed
        if (!capturedOutput.empty()) {
            editor::CellOutput out;
            out.streamName = hasErr ? L"stderr" : L"stdout";
            out.text = capturedOutput;
            out.isError = hasErr;
            out.elapsedMs = elapsedMs;
            cell.outputs.push_back(std::move(out));
        }

        // Execution count increment: Each cell increments its own run count by 1 independently
        if (cell.executionCount <= 0) {
            cell.executionCount = 1;
        } else {
            cell.executionCount++;
        }
        nbDoc->isDirty = true;
    }

    m_isExecuting.store(false);
    m_executingCellIdx.store(-1);

    if (hwnd) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    // 4. Check if there are user-queued cells waiting to run (dashed spinner queue)
    int queuedCellIdx = -1;
    {
        std::lock_guard<std::mutex> qLock(m_queueMutex);
        while (!m_cellQueue.empty()) {
            int cand = m_cellQueue.front();
            m_cellQueue.erase(m_cellQueue.begin());
            if (nbDoc && cand >= 0 && cand < static_cast<int>(nbDoc->cells.size())) {
                queuedCellIdx = cand;
                break;
            }
        }
    }

    if (queuedCellIdx >= 0 && !m_shouldStop.load() && nbDoc) {
        auto& nextCell = nbDoc->cells[queuedCellIdx];
        nextCell.isQueued = false;
        nextCell.isExecuting = true;
        nextCell.outputs.clear();
        nextCell.hasError = false;
        nextCell.errorLine = -1;
        m_isExecuting.store(true);
        m_executingCellIdx.store(queuedCellIdx);

        std::wstring nextCode = nextCell.inputBuffer.GetText();
        std::wstring nextLang = nextCell.language;
        std::thread([this, queuedCellIdx, nextCode, nextLang, nbDoc, hwnd]() {
            ExecuteScriptAsync(queuedCellIdx, nextCode, nextLang, nbDoc, hwnd);
        }).detach();
        if (hwnd) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }

    // 5. Sequential execution for RunAll: run 1-by-1 in order from top to bottom
    // If execution fails, halt sequential queue immediately (Colab/Jupyter standard)
    if (m_isQueueRunning.load() && !m_shouldStop.load() && !hasErr && nbDoc) {
        int nextIdx = cellIdx + 1;
        while (nextIdx < static_cast<int>(nbDoc->cells.size()) &&
               nbDoc->cells[nextIdx].type != editor::NotebookCellType::Code) {
            nextIdx++;
        }
        if (nextIdx < static_cast<int>(nbDoc->cells.size())) {
            auto& nextCell = nbDoc->cells[nextIdx];
            nextCell.isExecuting = true;
            nextCell.outputs.clear();
            nextCell.hasError = false;
            m_isExecuting.store(true);
            m_executingCellIdx.store(nextIdx);

            std::wstring nextCode = nextCell.inputBuffer.GetText();
            std::wstring nextLang = nextCell.language;
            std::thread([this, nextIdx, nextCode, nextLang, nbDoc, hwnd]() {
                ExecuteScriptAsync(nextIdx, nextCode, nextLang, nbDoc, hwnd);
            }).detach();
            return;
        } else {
            m_isQueueRunning.store(false);
        }
    } else {
        m_isQueueRunning.store(false);
    }
}

} // namespace phide::ui
