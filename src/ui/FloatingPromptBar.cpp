/**
 * @file FloatingPromptBar.cpp
 * @brief Implementation of the floating prompt bar rendering and hit-testing.
 */

#include "phide/ui/FloatingPromptBar.hpp"

namespace phide::ui {

FloatingPromptBar::FloatingPromptBar() = default;

static bool PtInRectF(POINT pt, const D2D1_RECT_F& rc) {
    return (pt.x >= rc.left && pt.x < rc.right && pt.y >= rc.top && pt.y < rc.bottom);
}

void FloatingPromptBar::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              float canvasLeft, float canvasWidth, float clientHeight) {
    if (!pRT) return;

    const float pillW = config.ScaleF(static_cast<float>(config.promptBarWidth));
    const float pillH = config.ScaleF(static_cast<float>(config.promptBarHeight));
    const float marginB = config.ScaleF(static_cast<float>(config.promptBarBottomMargin));

    // Center the pill within the available canvas width
    const float pillX = canvasLeft + (canvasWidth - pillW) / 2.0f;
    const float pillY = clientHeight - marginB - pillH;

    m_rcPill = D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH);

    // 1. Draw Subtle Soft Drop Shadow
    D2D1_RECT_F rcShadow = D2D1::RectF(pillX - 2.0f, pillY + 3.0f, pillX + pillW + 2.0f, pillY + pillH + 4.0f);
    D2D1_ROUNDED_RECT rrcShadow = D2D1::RoundedRect(rcShadow, pillH / 2.0f, pillH / 2.0f);
    pRT->FillRoundedRectangle(rrcShadow, theme.brushPillShadow.Get());

    // 2. Draw Main White Pill Background
    D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(m_rcPill, pillH / 2.0f, pillH / 2.0f);
    pRT->FillRoundedRectangle(rrcPill, theme.brushPillBg.Get());
    pRT->DrawRoundedRectangle(rrcPill, theme.brushBorderSubtle.Get(), 1.0f);

    // 3. Left '+' Trigger Button
    float curX = pillX + config.ScaleF(14.0f);
    float plusW = config.ScaleF(24.0f);
    m_rcPlus = D2D1::RectF(curX, pillY, curX + plusW, pillY + pillH);
    pRT->DrawText(L"+", 1, theme.fontTitle.Get(), m_rcPlus, theme.brushTextSecondary.Get());
    curX += plusW + config.ScaleF(4.0f);

    // 4. Placeholder / Prompt Text
    float inputW = config.ScaleF(230.0f);
    m_rcInput = D2D1::RectF(curX, pillY, curX + inputW, pillY + pillH);
    const wchar_t* placeholder = L"Ask AI or generate code...";
    pRT->DrawText(
        placeholder,
        static_cast<UINT32>(wcslen(placeholder)),
        theme.fontBody.Get(),
        m_rcInput,
        theme.brushTextMuted.Get()
    );

    // 5. Right Controls: Model Selector, Mic, Submit Button
    float rightEdge = pillX + pillW - config.ScaleF(8.0f);

    // Submit circular button (Blue circle with up-arrow)
    const float submitSize = config.ScaleF(30.0f);
    const float submitY = pillY + (pillH - submitSize) / 2.0f;
    m_rcSubmit = D2D1::RectF(rightEdge - submitSize, submitY, rightEdge, submitY + submitSize);

    D2D1_ELLIPSE submitEllipse = D2D1::Ellipse(
        D2D1::Point2F(m_rcSubmit.left + submitSize / 2.0f, m_rcSubmit.top + submitSize / 2.0f),
        submitSize / 2.0f,
        submitSize / 2.0f
    );
    pRT->FillEllipse(submitEllipse, theme.brushBrandAccent.Get());
    pRT->DrawText(L"\u2191", 1, theme.fontTitle.Get(), m_rcSubmit, theme.brushWindowBg.Get());

    rightEdge -= (submitSize + config.ScaleF(8.0f));

    // Microphone voice trigger
    const float micW = config.ScaleF(24.0f);
    m_rcVoice = D2D1::RectF(rightEdge - micW, pillY, rightEdge, pillY + pillH);
    pRT->DrawText(L"\u2299", 1, theme.fontIcon.Get(), m_rcVoice, theme.brushTextSecondary.Get());
    rightEdge -= (micW + config.ScaleF(8.0f));

    // Model Selector Pill ("High v")
    const float modelW = config.ScaleF(68.0f);
    const float modelH = config.ScaleF(26.0f);
    const float modelY = pillY + (pillH - modelH) / 2.0f;
    m_rcModel = D2D1::RectF(rightEdge - modelW, modelY, rightEdge, modelY + modelH);

    if (m_hoveredButton == PromptButtonId::ModelSelector) {
        D2D1_ROUNDED_RECT rrcModel = D2D1::RoundedRect(m_rcModel, 6.0f, 6.0f);
        pRT->FillRoundedRectangle(rrcModel, theme.brushItemHover.Get());
    }

    pRT->DrawText(
        config.activeModelName.c_str(),
        static_cast<UINT32>(config.activeModelName.length()),
        theme.fontMenu.Get(),
        m_rcModel,
        theme.brushTextSecondary.Get()
    );
}

PromptButtonId FloatingPromptBar::HitTest(POINT pt) const {
    if (!PtInRectF(pt, m_rcPill)) return PromptButtonId::None;

    if (PtInRectF(pt, m_rcSubmit)) return PromptButtonId::Submit;
    if (PtInRectF(pt, m_rcVoice)) return PromptButtonId::VoiceInput;
    if (PtInRectF(pt, m_rcModel)) return PromptButtonId::ModelSelector;
    if (PtInRectF(pt, m_rcPlus)) return PromptButtonId::PlusIcon;
    if (PtInRectF(pt, m_rcInput)) return PromptButtonId::InputArea;

    return PromptButtonId::None;
}

bool FloatingPromptBar::SetHoveredButton(PromptButtonId id) {
    if (m_hoveredButton != id) {
        m_hoveredButton = id;
        return true;
    }
    return false;
}

} // namespace phide::ui
