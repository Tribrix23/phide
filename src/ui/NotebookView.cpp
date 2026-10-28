/**
 * @file NotebookView.cpp
 * @brief Implementation of the interactive Jupyter / Colab-style notebook interface.
 */

#include "phide/ui/NotebookView.hpp"
#include <filesystem>
#include <chrono>
#include <sstream>
#include <algorithm>

namespace phide::ui {

NotebookView::NotebookView() {
    m_lastBlinkTime = GetTickCount64();
    m_lastAnimTick = GetTickCount64();
}

NotebookView::~NotebookView() {
    StopExecution_Internal();
    if (m_execThread.joinable()) {
        m_execThread.join();
    }
}

void NotebookView::DrawRunMorphButton(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     const D2D1_RECT_F& bounds, bool isExecuting, bool hasError, bool isHovered) {
    if (!pRT) return;
    const float cx = (bounds.left + bounds.right) / 2.0f;
    const float cy = (bounds.top + bounds.bottom) / 2.0f;
    const float r = (std::min)(bounds.right - bounds.left, bounds.bottom - bounds.top) / 2.0f;

    if (isExecuting) {
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

        // Rotating dashed spinner ring (Image 4)
        const float ringR = r * 0.90f;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSpin;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.65f, 0.98f, 1.0f), &brushSpin); // Electric cyan/blue spinner
        if (brushSpin) {
            const int dashCount = 10;
            const float arcSweep = 360.0f / static_cast<float>(dashCount);
            const float dashSweep = arcSweep * 0.55f;
            for (int i = 0; i < dashCount; ++i) {
                float deg1 = m_spinAngle + static_cast<float>(i) * arcSweep;
                float deg2 = deg1 + dashSweep;
                float rad1 = deg1 * 3.14159265f / 180.0f;
                float rad2 = deg2 * 3.14159265f / 180.0f;
                D2D1_POINT_2F p1 = D2D1::Point2F(cx + ringR * cosf(rad1), cy + ringR * sinf(rad1));
                D2D1_POINT_2F p2 = D2D1::Point2F(cx + ringR * cosf(rad2), cy + ringR * sinf(rad2));
                pRT->DrawLine(p1, p2, brushSpin.Get(), config.ScaleF(1.8f));
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

    auto nb = doc.notebookDoc;
    m_viewHeight = height;

    // Blink timer update
    ULONGLONG now = GetTickCount64();
    if (now - m_lastBlinkTime >= 500) {
        m_caretVisible = !m_caretVisible;
        m_lastBlinkTime = now;
    }

    // Animation tick update for spinner
    if (m_isExecuting.load()) {
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
    DrawRunMorphButton(pRT, theme, config, m_btnRunAll, m_isExecuting.load(), false, m_hoveredToolbarBtn == 1);
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

    // 3. Render Notebook Cells Area with Scrolling
    const float contentTop = top + toolbarH;
    const float contentH = height - toolbarH;

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

        // Calculate output height
        float outputH = 0.0f;
        if (cell.isExecuting) {
            outputH = config.ScaleF(34.0f);
        } else if (!cell.outputs.empty()) {
            size_t outLines = 0;
            for (const auto& out : cell.outputs) {
                outLines += std::count(out.text.begin(), out.text.end(), L'\n') + 1;
            }
            float rawOutH = static_cast<float>(outLines) * config.ScaleF(18.0f) + config.ScaleF(12.0f);
            outputH = (std::min)(config.ScaleF(300.0f), rawOutH);
        }

        float cardHeaderH = config.ScaleF(28.0f);
        float cardH = cardHeaderH + codeH + (outputH > 0.0f ? (outputH + config.ScaleF(8.0f)) : 0.0f) + config.ScaleF(10.0f);

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
                if (cell.isExecuting || cell.executionCount == -2) {
                    inStr = L"[*]";
                } else if (cell.executionCount >= 1) {
                    inStr = L"[" + std::to_wstring(cell.executionCount) + L"]";
                } else {
                    inStr = L"[ ]";
                }

                D2D1_RECT_F rcIn = D2D1::RectF(gutterX + config.ScaleF(8.0f), gutterY, gutterX + gutterW, gutterY + config.ScaleF(16.0f));
                ID2D1Brush* inBrush = (cell.executionCount >= 1 || cell.isExecuting)
                    ? (theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushBrandAccent.Get())
                    : (theme.brushLineNumber ? theme.brushLineNumber.Get() : theme.brushTextSecondary.Get());
                pRT->DrawText(inStr.c_str(), static_cast<UINT32>(inStr.length()),
                              theme.fontBadge.Get() ? theme.fontBadge.Get() : theme.fontCode.Get(), rcIn, inBrush);

                // Error badge: (!) 0s (Image 5)
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
                }

                // Circular Run Button (Morphing into stop button with dashed ring)
                const float runBtnSize = config.ScaleF(26.0f);
                cell.runBtnBounds = D2D1::RectF(
                    gutterX + config.ScaleF(12.0f),
                    runBtnY,
                    gutterX + config.ScaleF(12.0f) + runBtnSize,
                    runBtnY + runBtnSize
                );

                bool isRunHover = (isHovered && m_hoveredBtn == 1);
                DrawRunMorphButton(pRT, theme, config, cell.runBtnBounds, cell.isExecuting, cell.hasError, isRunHover);
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

            for (size_t li = 0; li < lines.size(); ++li) {
                float lineY = cell.codeAreaBounds.top + config.ScaleF(6.0f) + static_cast<float>(li) * lineH;
                D2D1_RECT_F rcNum = D2D1::RectF(cell.codeAreaBounds.left + config.ScaleF(6.0f), lineY,
                                               cell.codeAreaBounds.left + lineNumW, lineY + lineH);
                std::wstring numStr = std::to_wstring(li + 1);
                pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()),
                              theme.fontCode.Get(), rcNum, theme.brushLineNumber.Get());

                // Code text line with syntax tokenization via DirectWrite TextLayout
                float textX = cell.codeAreaBounds.left + lineNumW + config.ScaleF(8.0f);
                const auto& codeLine = lines[li];
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

                // Render Caret in active cell
                if (isActive && li == curPos.line && m_caretVisible) {
                    float charW = config.ScaleF(7.8f);
                    if (pLineLayout) {
                        DWRITE_HIT_TEST_METRICS htm{};
                        float hitX = 0.0f, hitY = 0.0f;
                        if (SUCCEEDED(pLineLayout->HitTestTextPosition(static_cast<UINT32>(curPos.col), FALSE, &hitX, &hitY, &htm))) {
                            charW = hitX / (std::max)(1.0f, static_cast<float>(curPos.col));
                        }
                    }
                    float caretX = textX + static_cast<float>(curPos.col) * charW;
                    pRT->DrawLine(
                        D2D1::Point2F(caretX, lineY + 1.0f),
                        D2D1::Point2F(caretX, lineY + lineH - 2.0f),
                        theme.brushTextPrimary.Get(),
                        1.5f
                    );
                }
            }

            // 3.4 Output Area (Colab style with red traceback styling on error - Image 5)
            if (cell.isExecuting) {
                float outTop = cell.codeAreaBounds.bottom + config.ScaleF(6.0f);
                cell.outputAreaBounds = D2D1::RectF(cell.codeAreaBounds.left, outTop, cell.codeAreaBounds.right, outTop + config.ScaleF(32.0f));

                D2D1_ROUNDED_RECT rrcOut = D2D1::RoundedRect(cell.outputAreaBounds, 4.0f, 4.0f);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushOutBg;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.06f, 0.08f, 1.0f), &brushOutBg);
                if (brushOutBg) pRT->FillRoundedRectangle(rrcOut, brushOutBg.Get());

                std::wstring execMsg = L"⚙ Running cell in background execution engine...";
                D2D1_RECT_F rcMsg = D2D1::RectF(cell.outputAreaBounds.left + config.ScaleF(12.0f), outTop + config.ScaleF(7.0f),
                                               cell.outputAreaBounds.right, cell.outputAreaBounds.bottom);
                pRT->DrawText(execMsg.c_str(), static_cast<UINT32>(execMsg.length()),
                              theme.fontMenuItem.Get(), rcMsg, theme.brushTextSecondary.Get());
            } else if (!cell.outputs.empty()) {
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

                // Output text (rendered cleanly without any "stdout" label, Colab/Jupyter standard)
                const auto& firstOut = cell.outputs[0];
                D2D1_RECT_F rcOutText = D2D1::RectF(cell.outputAreaBounds.left + config.ScaleF(12.0f),
                                                   outTop + config.ScaleF(6.0f),
                                                   cell.outputAreaBounds.right - config.ScaleF(10.0f),
                                                   cell.outputAreaBounds.bottom - config.ScaleF(4.0f));

                ID2D1Brush* outTextBrush = theme.brushTextPrimary.Get();
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushErrText;
                if (firstOut.isError) {
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.38f, 0.38f, 1.0f), &brushErrText);
                    if (brushErrText) outTextBrush = brushErrText.Get();
                }
                pRT->DrawText(firstOut.text.c_str(), static_cast<UINT32>(firstOut.text.length()),
                              theme.fontCode.Get(), rcOutText, outTextBrush ? outTextBrush : theme.brushTextPrimary.Get());
            } else {
                cell.outputAreaBounds = D2D1::RectF(0, 0, 0, 0);
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
        return true;
    }

    // Check Top Toolbar buttons
    auto PointInRect = [](POINT pt, const D2D1_RECT_F& r) -> bool {
        return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
    };

    if (PointInRect(clientPt, m_btnRunAll)) {
        if (m_isExecuting.load()) {
            StopExecution(doc);
        } else {
            RunAllCells(doc, hwnd);
        }
        return true;
    }
    if (PointInRect(clientPt, m_btnClearAll)) {
        nb->ClearAllOutputs();
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

            // Run button: morphs into stop if executing
            if (PointInRect(clientPt, cell.runBtnBounds)) {
                if (cell.isExecuting) {
                    StopExecution(doc);
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

            // Click inside code area: calculate line and column
            if (PointInRect(clientPt, cell.codeAreaBounds)) {
                const float lineH = config.ScaleF(20.0f);
                const float lineNumW = config.ScaleF(28.0f);
                float relY = static_cast<float>(clientPt.y) - cell.codeAreaBounds.top - config.ScaleF(6.0f);
                int targetLine = (std::max)(0, static_cast<int>(relY / lineH));
                float relX = static_cast<float>(clientPt.x) - cell.codeAreaBounds.left - lineNumW - config.ScaleF(8.0f);
                int targetCol = (std::max)(0, static_cast<int>(relX / config.ScaleF(7.8f)));

                cell.inputBuffer.SetCursor(static_cast<size_t>(targetLine), static_cast<size_t>(targetCol));
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
        return true;
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
    float contentH = (std::max)(1.0f, m_viewHeight - config.ScaleF(40.0f));
    float maxScroll = (std::max)(0.0f, m_totalHeight - contentH + config.ScaleF(40.0f));
    float scrollStep = config.ScaleF(60.0f);
    if (delta > 0) {
        m_scrollY = (std::max)(0.0f, m_scrollY - scrollStep);
    } else {
        m_scrollY = (std::min)(maxScroll, m_scrollY + scrollStep);
    }
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

    m_caretVisible = true;
    m_lastBlinkTime = GetTickCount64();

    switch (wParam) {
        case VK_RETURN:
            activeCell.inputBuffer.InsertChar(L'\n');
            nb->isDirty = true;
            return true;
        case VK_BACK:
            activeCell.inputBuffer.Backspace();
            nb->isDirty = true;
            return true;
        case VK_DELETE: {
            auto cur = activeCell.inputBuffer.GetCursor();
            const auto& lines = activeCell.inputBuffer.GetLines();
            if (cur.line < lines.size()) {
                if (cur.col < lines[cur.line].length() || cur.line + 1 < lines.size()) {
                    activeCell.inputBuffer.MoveCursorRight();
                    activeCell.inputBuffer.Backspace();
                    nb->isDirty = true;
                }
            }
            return true;
        }
        case VK_TAB:
            activeCell.inputBuffer.InsertText(L"    ");
            nb->isDirty = true;
            return true;
        case VK_LEFT:
            activeCell.inputBuffer.MoveCursorLeft();
            return true;
        case VK_RIGHT:
            activeCell.inputBuffer.MoveCursorRight();
            return true;
        case VK_UP:
            activeCell.inputBuffer.MoveCursorUp();
            return true;
        case VK_DOWN:
            activeCell.inputBuffer.MoveCursorDown();
            return true;
        case VK_HOME:
            activeCell.inputBuffer.SetCursor(activeCell.inputBuffer.GetCursor().line, 0);
            return true;
        case VK_END: {
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

    if (PointInRect(pt, m_btnRunAll) || PointInRect(pt, m_btnClearAll) || PointInRect(pt, m_scrollThumb)) {
        return IDC_HAND;
    }

    if (doc.notebookDoc) {
        for (const auto& cell : doc.notebookDoc->cells) {
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

std::wstring NotebookView::FindPythonExecutable() const {
    // 0. Dedicated EMSDK Python install check
    std::wstring emsdkPy = L"C:\\Users\\Administrator\\Downloads\\emsdk\\python\\3.13.3_64bit\\python.exe";
    if (std::filesystem::exists(emsdkPy)) return emsdkPy;

    // 1. Check workspace venv
    if (!m_workspacePath.empty()) {
        std::wstring venvPy1 = m_workspacePath + L"\\.venv\\Scripts\\python.exe";
        if (std::filesystem::exists(venvPy1)) return venvPy1;
        std::wstring venvPy2 = m_workspacePath + L"\\venv\\Scripts\\python.exe";
        if (std::filesystem::exists(venvPy2)) return venvPy2;
    }

    // 2. Search python.exe on PATH
    wchar_t buf[MAX_PATH]{};
    if (SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, buf, nullptr) > 0) {
        return buf;
    }

    // 3. Search py.exe on PATH
    if (SearchPathW(nullptr, L"py.exe", nullptr, MAX_PATH, buf, nullptr) > 0) {
        return buf;
    }

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

    cell.isExecuting = true;
    cell.outputs.clear();
    cell.hasError = false;
    m_isExecuting.store(true);
    m_executingCellIdx.store(cellIdx);
    m_shouldStop.store(false);

    std::wstring code = cell.inputBuffer.GetText();
    std::wstring lang = cell.language;
    auto nbDocPtr = doc.notebookDoc;

    if (m_execThread.joinable()) {
        m_execThread.join();
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
            if (c.isExecuting) {
                c.isExecuting = false;
                c.executionCount = -1;
            }
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
    m_kernel.Interrupt();
    std::lock_guard<std::mutex> lock(m_execMutex);
    if (m_hChildProcess) {
        TerminateProcess(m_hChildProcess, 1);
        CloseHandle(m_hChildProcess);
        m_hChildProcess = NULL;
    }
    m_isExecuting.store(false);
    m_executingCellIdx.store(-1);
}

void NotebookView::ExecuteScriptAsync(int cellIdx, const std::wstring& script, const std::wstring& lang,
                                      std::shared_ptr<editor::NotebookDocument> nbDoc, HWND hwnd) {
    std::wstring pyExe = FindPythonExecutable();
    auto kernelResult = m_kernel.Execute(script, pyExe);

    bool hasErr = kernelResult.hasError;
    std::wstring capturedOutput = kernelResult.output;
    uint64_t elapsedMs = kernelResult.elapsedMs;

    // 3. Update notebook document cell outputs & error state
    if (nbDoc && cellIdx >= 0 && cellIdx < static_cast<int>(nbDoc->cells.size())) {
        auto& cell = nbDoc->cells[cellIdx];
        cell.isExecuting = false;
        cell.hasError = hasErr;
        cell.lastElapsedMs = elapsedMs;
        cell.outputs.clear();

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

    // 4. Sequential execution: run 1-by-1 in order from top to bottom
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
