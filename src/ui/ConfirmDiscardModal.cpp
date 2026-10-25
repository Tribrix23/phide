/**
 * @file ConfirmDiscardModal.cpp
 * @brief Modern flat visual confirmation and alert modal dialog implementation.
 */

#include "phide/ui/ConfirmDiscardModal.hpp"
#include <algorithm>
#include <cmath>
#include <wrl/client.h>

namespace phide::ui {

static bool PtInRectF(POINT pt, const D2D1_RECT_F& rc) {
    return (pt.x >= rc.left && pt.x <= rc.right && pt.y >= rc.top && pt.y <= rc.bottom);
}

void ConfirmDiscardModal::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientW, float clientH) {
    if (!m_isOpen || !pRT) return;

    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);

    // 1. Modern Dimmed Backdrop Overlay
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> backdropBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.65f), &backdropBrush);
    if (backdropBrush) {
        pRT->FillRectangle(D2D1::RectF(0.0f, 0.0f, clientW, clientH), backdropBrush.Get());
    }

    // 2. Card Dimensions and Center Placement
    const bool isNotice = (m_type == ConfirmModalType::Notice);
    const float cardW = config.ScaleF(isNotice ? 460.0f : 500.0f);
    const float cardH = config.ScaleF(isNotice ? 180.0f : 205.0f);
    const float cardX = (std::max)(0.0f, (clientW - cardW) * 0.5f);
    const float cardY = (std::max)(0.0f, (clientH - cardH) * 0.5f);
    m_cardBounds = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);

    D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_cardBounds, config.ScaleF(10.0f), config.ScaleF(10.0f));

    // Card Flat Background
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBgBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode()
            ? D2D1::ColorF(0.095f, 0.105f, 0.125f, 0.99f) // Sleek slate-dark #181B20
            : D2D1::ColorF(0.99f, 0.99f, 1.0f, 0.99f),
        &cardBgBrush
    );
    if (cardBgBrush) pRT->FillRoundedRectangle(rrcCard, cardBgBrush.Get());

    // Card Flat Subtle 1px Border
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBorderBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode()
            ? D2D1::ColorF(0.20f, 0.22f, 0.26f, 1.0f)
            : D2D1::ColorF(0.82f, 0.85f, 0.90f, 1.0f),
        &cardBorderBrush
    );
    if (cardBorderBrush) pRT->DrawRoundedRectangle(rrcCard, cardBorderBrush.Get(), 1.0f);

    // 3. Top-Right Close '✕' Button
    const float closeBtnSize = config.ScaleF(26.0f);
    m_closeBtnBounds = D2D1::RectF(
        cardX + cardW - closeBtnSize - config.ScaleF(14.0f),
        cardY + config.ScaleF(14.0f),
        cardX + cardW - config.ScaleF(14.0f),
        cardY + config.ScaleF(14.0f) + closeBtnSize
    );
    if (m_isCloseHovered) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> closeHoverBrush;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f),
            &closeHoverBrush
        );
        if (closeHoverBrush) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_closeBtnBounds, 5.0f, 5.0f), closeHoverBrush.Get());
        }
    }
    IDWriteTextFormat* pFmtHint = theme.fontHint ? theme.fontHint.Get() : theme.fontCode.Get();
    if (pFmtHint) {
        DWRITE_TEXT_ALIGNMENT prevAlign = pFmtHint->GetTextAlignment();
        DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtHint->GetParagraphAlignment();
        pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pFmtHint->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        ID2D1Brush* closeTextBrush = m_isCloseHovered
            ? (theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get())
            : (theme.brushTextMuted ? theme.brushTextMuted.Get() : theme.brushTextPrimary.Get());
        if (closeTextBrush) {
            pRT->DrawText(L"✕", 1, pFmtHint, m_closeBtnBounds, closeTextBrush);
        }
        pFmtHint->SetTextAlignment(prevAlign);
        pFmtHint->SetParagraphAlignment(prevPAlign);
    }

    // 4. Modern Vector Icon & Typography
    const float iconSize = config.ScaleF(34.0f);
    const float iconX = cardX + config.ScaleF(20.0f);
    const float iconY = cardY + config.ScaleF(20.0f);
    D2D1_RECT_F rcIcon = D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> iconBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> iconStrokeBrush;

    if (isNotice) {
        // Brand Azure for Notices (e.g. Commit Message Required)
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.44f, 0.95f, 0.16f), &iconBgBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.58f, 1.0f, 1.0f), &iconStrokeBrush);
    } else if (m_type == ConfirmModalType::RestoreFile) {
        // Emerald / Cyan for Restoring Deleted Files
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.65f, 0.45f, 0.16f), &iconBgBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.16f, 0.78f, 0.55f, 1.0f), &iconStrokeBrush);
    } else {
        // Crimson / Coral for Discarding Edits
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.23f, 0.23f, 0.16f), &iconBgBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.32f, 0.32f, 1.0f), &iconStrokeBrush);
    }

    if (iconBgBrush) {
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcIcon, config.ScaleF(8.0f), config.ScaleF(8.0f)), iconBgBrush.Get());
    }

    // Draw Modern Crisp Direct2D Vector Icon
    const float cx = iconX + iconSize * 0.5f;
    const float cy = iconY + iconSize * 0.5f;

    if (iconStrokeBrush) {
        if (isNotice) {
            // Sleek Commit Graph / Info Icon: Commit Circle with branching stems
            pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), config.ScaleF(5.0f), config.ScaleF(5.0f)), iconStrokeBrush.Get(), 1.8f);
            pRT->DrawLine(D2D1::Point2F(cx, cy - config.ScaleF(9.0f)), D2D1::Point2F(cx, cy - config.ScaleF(5.0f)), iconStrokeBrush.Get(), 1.8f);
            pRT->DrawLine(D2D1::Point2F(cx, cy + config.ScaleF(5.0f)), D2D1::Point2F(cx, cy + config.ScaleF(9.0f)), iconStrokeBrush.Get(), 1.8f);
        } else if (m_type == ConfirmModalType::RestoreFile) {
            // Sleek Vector Restore / Revert Arrow (counter-clockwise arc with arrowhead)
            float r = config.ScaleF(6.5f);
            // Arc using lines
            const int segs = 14;
            const float startAng = 0.5f;
            const float endAng = 4.8f;
            for (int s = 0; s < segs; ++s) {
                float a1 = startAng + (endAng - startAng) * (s / static_cast<float>(segs));
                float a2 = startAng + (endAng - startAng) * ((s + 1) / static_cast<float>(segs));
                pRT->DrawLine(
                    D2D1::Point2F(cx + r * std::cos(a1), cy + r * std::sin(a1)),
                    D2D1::Point2F(cx + r * std::cos(a2), cy + r * std::sin(a2)),
                    iconStrokeBrush.Get(), 1.8f
                );
            }
            // Arrowhead at start
            float ax = cx + r * std::cos(startAng);
            float ay = cy + r * std::sin(startAng);
            pRT->DrawLine(D2D1::Point2F(ax, ay), D2D1::Point2F(ax + config.ScaleF(4.0f), ay - config.ScaleF(2.0f)), iconStrokeBrush.Get(), 1.8f);
            pRT->DrawLine(D2D1::Point2F(ax, ay), D2D1::Point2F(ax + config.ScaleF(3.5f), ay + config.ScaleF(4.0f)), iconStrokeBrush.Get(), 1.8f);
        } else {
            // Sleek Vector Discard Arrow (Undo circular curve with sharp arrowhead)
            float r = config.ScaleF(6.5f);
            const int segs = 14;
            const float startAng = 3.6f;
            const float endAng = 1.6f + 6.28318f;
            for (int s = 0; s < segs; ++s) {
                float a1 = startAng + (endAng - startAng) * (s / static_cast<float>(segs));
                float a2 = startAng + (endAng - startAng) * ((s + 1) / static_cast<float>(segs));
                pRT->DrawLine(
                    D2D1::Point2F(cx + r * std::cos(a1), cy + r * std::sin(a1)),
                    D2D1::Point2F(cx + r * std::cos(a2), cy + r * std::sin(a2)),
                    iconStrokeBrush.Get(), 1.8f
                );
            }
            float ax = cx + r * std::cos(startAng);
            float ay = cy + r * std::sin(startAng);
            pRT->DrawLine(D2D1::Point2F(ax, ay), D2D1::Point2F(ax - config.ScaleF(4.0f), ay - config.ScaleF(2.0f)), iconStrokeBrush.Get(), 1.8f);
            pRT->DrawLine(D2D1::Point2F(ax, ay), D2D1::Point2F(ax - config.ScaleF(1.0f), ay + config.ScaleF(4.5f)), iconStrokeBrush.Get(), 1.8f);
        }
    }

    // Modal Header Title
    std::wstring headerTitle;
    if (isNotice) {
        headerTitle = m_title.empty() ? L"Commit Message Required" : m_title;
    } else if (m_type == ConfirmModalType::RestoreFile) {
        headerTitle = L"Restore Deleted File";
    } else if (m_type == ConfirmModalType::DiscardAll) {
        headerTitle = L"Discard All Changes";
    } else {
        headerTitle = L"Discard Changes";
    }

    const float textLeft = iconX + iconSize + config.ScaleF(14.0f);
    const float textRight = m_closeBtnBounds.left - config.ScaleF(10.0f);

    D2D1_RECT_F rcHeader = D2D1::RectF(textLeft, iconY, textRight, iconY + config.ScaleF(22.0f));
    IDWriteTextFormat* pFmtTitle = theme.fontTitle ? theme.fontTitle.Get() : theme.fontMenuItem.Get();
    ID2D1Brush* textPrimary = theme.IsDarkMode()
        ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
        : theme.brushTextPrimary.Get();
    if (pFmtTitle && textPrimary) {
        pRT->DrawText(headerTitle.c_str(), static_cast<UINT32>(headerTitle.length()), pFmtTitle, rcHeader, textPrimary);
    }

    float curContentY = iconY + config.ScaleF(26.0f);

    // 5. File Pill Badge (when operating on files)
    if (!isNotice && !m_fileName.empty()) {
        const float pillH = config.ScaleF(24.0f);
        D2D1_RECT_F rcPill = D2D1::RectF(textLeft, curContentY, textRight, curContentY + pillH);
        D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(rcPill, config.ScaleF(4.0f), config.ScaleF(4.0f));

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBgBrush;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.15f, 0.18f, 1.0f) : D2D1::ColorF(0.92f, 0.94f, 0.96f, 1.0f),
            &pillBgBrush
        );
        if (pillBgBrush) pRT->FillRoundedRectangle(rrcPill, pillBgBrush.Get());

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBorderBrush;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.26f, 0.30f, 1.0f) : D2D1::ColorF(0.82f, 0.85f, 0.88f, 1.0f),
            &pillBorderBrush
        );
        if (pillBorderBrush) pRT->DrawRoundedRectangle(rrcPill, pillBorderBrush.Get(), 0.8f);

        // File name text inside pill
        std::wstring pillText = (m_type == ConfirmModalType::DiscardAll)
            ? (L"All unstaged changes in " + std::to_wstring(m_changeCount) + L" files")
            : (m_fileName);

        D2D1_RECT_F rcPillText = D2D1::RectF(textLeft + config.ScaleF(8.0f), curContentY, textRight - config.ScaleF(70.0f), curContentY + pillH);
        IDWriteTextFormat* pFmtCode = theme.fontCode ? theme.fontCode.Get() : pFmtHint;
        if (pFmtCode && textPrimary) {
            DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtCode->GetParagraphAlignment();
            pFmtCode->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(pillText.c_str(), static_cast<UINT32>(pillText.length()), pFmtCode, rcPillText, textPrimary);
            pFmtCode->SetParagraphAlignment(prevPAlign);
        }

        // Status tag badge inside pill on right
        std::wstring tagText = (m_type == ConfirmModalType::RestoreFile) ? L"DELETED" :
                               ((m_type == ConfirmModalType::DiscardAll) ? L"ALL" : L"MODIFIED");
        const float tagW = config.ScaleF((m_type == ConfirmModalType::DiscardAll) ? 38.0f : 62.0f);
        D2D1_RECT_F rcTag = D2D1::RectF(textRight - tagW - config.ScaleF(4.0f), curContentY + config.ScaleF(3.0f), textRight - config.ScaleF(4.0f), curContentY + pillH - config.ScaleF(3.0f));
        D2D1_ROUNDED_RECT rrcTag = D2D1::RoundedRect(rcTag, 3.0f, 3.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tagBgBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tagFgBrush;
        if (m_type == ConfirmModalType::RestoreFile) {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.25f, 0.25f, 0.20f), &tagBgBrush);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.40f, 0.40f, 1.0f), &tagFgBrush);
        } else if (m_type == ConfirmModalType::DiscardAll) {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.55f, 0.95f, 0.20f), &tagBgBrush);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.70f, 1.0f, 1.0f), &tagFgBrush);
        } else {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.90f, 0.65f, 0.15f, 0.20f), &tagBgBrush);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.72f, 0.25f, 1.0f), &tagFgBrush);
        }
        if (tagBgBrush) pRT->FillRoundedRectangle(rrcTag, tagBgBrush.Get());
        if (pFmtHint && tagFgBrush) {
            DWRITE_TEXT_ALIGNMENT prevAlign = pFmtHint->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtHint->GetParagraphAlignment();
            pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pFmtHint->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(tagText.c_str(), static_cast<UINT32>(tagText.length()), pFmtHint, rcTag, tagFgBrush.Get());
            pFmtHint->SetTextAlignment(prevAlign);
            pFmtHint->SetParagraphAlignment(prevPAlign);
        }

        curContentY += pillH + config.ScaleF(10.0f);
    }

    // 6. Descriptive Developer Message
    std::wstring mainMsg;
    std::wstring subMsg;
    if (isNotice) {
        mainMsg = m_message.empty() ? L"Please enter a commit message before committing your changes." : m_message;
        subMsg = m_subtext.empty() ? L"Git requires a descriptive message to record changes in the repository history." : m_subtext;
    } else if (m_type == ConfirmModalType::RestoreFile) {
        mainMsg = L"This will restore the deleted file from the latest Git commit (HEAD).";
        subMsg = L"The working tree file will be restored and recreated on disk.";
    } else if (m_type == ConfirmModalType::DiscardAll) {
        mainMsg = L"All unstaged changes across all files will be permanently reverted back to HEAD.";
        subMsg = L"This action will overwrite your local edits on disk and cannot be undone.";
    } else {
        mainMsg = L"All unstaged edits in this file will be permanently reverted back to HEAD.";
        subMsg = L"This action will overwrite your local changes on disk. You cannot undo this action.";
    }

    IDWriteTextFormat* pFmtBody = theme.fontBody ? theme.fontBody.Get() : pFmtTitle;
    D2D1_RECT_F rcMainMsg = D2D1::RectF(textLeft, curContentY, textRight, curContentY + config.ScaleF(24.0f));
    if (pFmtBody && textPrimary) {
        pRT->DrawText(mainMsg.c_str(), static_cast<UINT32>(mainMsg.length()), pFmtBody, rcMainMsg, textPrimary);
    }
    curContentY += config.ScaleF(22.0f);

    ID2D1Brush* textMuted = theme.brushTextMuted ? theme.brushTextMuted.Get() : textPrimary;
    D2D1_RECT_F rcSubMsg = D2D1::RectF(textLeft, curContentY, textRight, curContentY + config.ScaleF(22.0f));
    if (pFmtHint && textMuted) {
        pRT->DrawText(subMsg.c_str(), static_cast<UINT32>(subMsg.length()), pFmtHint, rcSubMsg, textMuted);
    }

    // 7. Modern Flat Action Buttons at Bottom Right
    const float btnH = config.ScaleF(32.0f);
    const float btnY = cardY + cardH - btnH - config.ScaleF(18.0f);
    const float btnGap = config.ScaleF(10.0f);

    if (isNotice) {
        // Single Flat [ OK ] Button
        const float okBtnW = config.ScaleF(92.0f);
        m_confirmBtnBounds = D2D1::RectF(
            cardX + cardW - config.ScaleF(20.0f) - okBtnW,
            btnY,
            cardX + cardW - config.ScaleF(20.0f),
            btnY + btnH
        );
        m_cancelBtnBounds = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);

        D2D1_ROUNDED_RECT rrcOk = D2D1::RoundedRect(m_confirmBtnBounds, config.ScaleF(6.0f), config.ScaleF(6.0f));
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> okBgBrush;
        pRT->CreateSolidColorBrush(
            m_isConfirmHovered ? D2D1::ColorF(0.20f, 0.56f, 0.98f, 1.0f) : D2D1::ColorF(0.12f, 0.46f, 0.88f, 1.0f),
            &okBgBrush
        );
        if (okBgBrush) pRT->FillRoundedRectangle(rrcOk, okBgBrush.Get());

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> whiteTextBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &whiteTextBrush);
        if (pFmtBody && whiteTextBrush) {
            DWRITE_TEXT_ALIGNMENT prevAlign = pFmtBody->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtBody->GetParagraphAlignment();
            pFmtBody->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pFmtBody->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(L"OK", 2, pFmtBody, m_confirmBtnBounds, whiteTextBrush.Get());
            pFmtBody->SetTextAlignment(prevAlign);
            pFmtBody->SetParagraphAlignment(prevPAlign);
        }
    } else {
        // Cancel + Primary Action Buttons
        const float cancelBtnW = config.ScaleF(85.0f);
        float confirmBtnW = (m_type == ConfirmModalType::RestoreFile)
            ? config.ScaleF(120.0f)
            : (m_type == ConfirmModalType::DiscardAll ? config.ScaleF(150.0f) : config.ScaleF(125.0f));

        m_confirmBtnBounds = D2D1::RectF(
            cardX + cardW - config.ScaleF(20.0f) - confirmBtnW,
            btnY,
            cardX + cardW - config.ScaleF(20.0f),
            btnY + btnH
        );

        m_cancelBtnBounds = D2D1::RectF(
            m_confirmBtnBounds.left - btnGap - cancelBtnW,
            btnY,
            m_confirmBtnBounds.left - btnGap,
            btnY + btnH
        );

        // Cancel Button (Flat Neutral Surface with subtle border)
        D2D1_ROUNDED_RECT rrcCancel = D2D1::RoundedRect(m_cancelBtnBounds, config.ScaleF(6.0f), config.ScaleF(6.0f));
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cancelBgBrush;
        pRT->CreateSolidColorBrush(
            m_isCancelHovered
                ? (theme.IsDarkMode() ? D2D1::ColorF(0.20f, 0.22f, 0.26f, 1.0f) : D2D1::ColorF(0.88f, 0.90f, 0.93f, 1.0f))
                : (theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.15f, 0.18f, 1.0f) : D2D1::ColorF(0.93f, 0.94f, 0.96f, 1.0f)),
            &cancelBgBrush
        );
        if (cancelBgBrush) pRT->FillRoundedRectangle(rrcCancel, cancelBgBrush.Get());

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cancelBorderBrush;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.26f, 0.28f, 0.33f, 1.0f) : D2D1::ColorF(0.78f, 0.81f, 0.86f, 1.0f),
            &cancelBorderBrush
        );
        if (cancelBorderBrush) pRT->DrawRoundedRectangle(rrcCancel, cancelBorderBrush.Get(), 1.0f);

        if (pFmtBody && textPrimary) {
            DWRITE_TEXT_ALIGNMENT prevAlign = pFmtBody->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtBody->GetParagraphAlignment();
            pFmtBody->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pFmtBody->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(L"Cancel", 6, pFmtBody, m_cancelBtnBounds, textPrimary);
            pFmtBody->SetTextAlignment(prevAlign);
            pFmtBody->SetParagraphAlignment(prevPAlign);
        }

        // Primary Action Button (Restore: Emerald #059669; Discard: Crimson #DC2626)
        D2D1_ROUNDED_RECT rrcConfirm = D2D1::RoundedRect(m_confirmBtnBounds, config.ScaleF(6.0f), config.ScaleF(6.0f));
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> confirmBgBrush;
        if (m_type == ConfirmModalType::RestoreFile) {
            pRT->CreateSolidColorBrush(
                m_isConfirmHovered ? D2D1::ColorF(0.06f, 0.65f, 0.45f, 1.0f) : D2D1::ColorF(0.02f, 0.58f, 0.38f, 1.0f),
                &confirmBgBrush
            );
        } else {
            pRT->CreateSolidColorBrush(
                m_isConfirmHovered ? D2D1::ColorF(0.93f, 0.20f, 0.20f, 1.0f) : D2D1::ColorF(0.82f, 0.15f, 0.15f, 1.0f),
                &confirmBgBrush
            );
        }
        if (confirmBgBrush) pRT->FillRoundedRectangle(rrcConfirm, confirmBgBrush.Get());

        std::wstring confirmBtnText = (m_type == ConfirmModalType::RestoreFile)
            ? L"Restore File"
            : (m_type == ConfirmModalType::DiscardAll ? L"Discard All Files" : L"Discard Changes");

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> whiteTextBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &whiteTextBrush);
        if (pFmtBody && whiteTextBrush) {
            DWRITE_TEXT_ALIGNMENT prevAlign = pFmtBody->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtBody->GetParagraphAlignment();
            pFmtBody->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pFmtBody->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(confirmBtnText.c_str(), static_cast<UINT32>(confirmBtnText.length()), pFmtBody, m_confirmBtnBounds, whiteTextBrush.Get());
            pFmtBody->SetTextAlignment(prevAlign);
            pFmtBody->SetParagraphAlignment(prevPAlign);
        }
    }
}

bool ConfirmDiscardModal::HandleMouseMove(POINT pt) noexcept {
    if (!m_isOpen) return false;

    bool prevConf = m_isConfirmHovered;
    bool prevCanc = m_isCancelHovered;
    bool prevClos = m_isCloseHovered;

    m_isConfirmHovered = PtInRectF(pt, m_confirmBtnBounds);
    m_isCancelHovered = PtInRectF(pt, m_cancelBtnBounds);
    m_isCloseHovered = PtInRectF(pt, m_closeBtnBounds);

    return (prevConf != m_isConfirmHovered || prevCanc != m_isCancelHovered || prevClos != m_isCloseHovered);
}

bool ConfirmDiscardModal::HandleMouseDown(POINT pt, bool& outConfirmed, std::wstring& outPath, std::wstring& outFileName, std::wstring& outStatus) noexcept {
    if (!m_isOpen) return false;

    // Check Close '✕' button
    if (PtInRectF(pt, m_closeBtnBounds)) {
        Close();
        outConfirmed = false;
        return true;
    }

    // Check Confirm Button (OK or Discard/Restore)
    if (PtInRectF(pt, m_confirmBtnBounds)) {
        outConfirmed = (m_type != ConfirmModalType::Notice);
        outPath = m_filePath;
        outFileName = m_fileName;
        outStatus = m_status;
        Close();
        return true;
    }

    // Check Cancel Button
    if (PtInRectF(pt, m_cancelBtnBounds)) {
        Close();
        outConfirmed = false;
        return true;
    }

    // Click outside modal card dismisses notice or closes dialog
    if (!PtInRectF(pt, m_cardBounds)) {
        Close();
        outConfirmed = false;
        return true;
    }

    return true; // Click inside modal swallowed
}

bool ConfirmDiscardModal::HandleKeyDown(WPARAM wParam, bool& outConfirmed, std::wstring& outPath, std::wstring& outFileName, std::wstring& outStatus) noexcept {
    if (!m_isOpen) return false;

    if (wParam == VK_RETURN) {
        outConfirmed = (m_type != ConfirmModalType::Notice);
        outPath = m_filePath;
        outFileName = m_fileName;
        outStatus = m_status;
        Close();
        return true;
    } else if (wParam == VK_ESCAPE) {
        Close();
        outConfirmed = false;
        return true;
    }

    return true; // Swallowed inside modal
}

} // namespace phide::ui
