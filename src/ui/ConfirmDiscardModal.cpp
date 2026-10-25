/**
 * @file ConfirmDiscardModal.cpp
 * @brief Implementation of the visual confirmation modal for Git discard and restore actions.
 */

#include "phide/ui/ConfirmDiscardModal.hpp"
#include <algorithm>
#include <wrl/client.h>

namespace phide::ui {

void ConfirmDiscardModal::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientW, float clientH) {
    if (!m_isOpen || !pRT) return;

    // 1. Dimmed Backdrop Overlay
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> backdropBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f), &backdropBrush);
    if (backdropBrush) {
        pRT->FillRectangle(D2D1::RectF(0.0f, 0.0f, clientW, clientH), backdropBrush.Get());
    }

    // 2. Card Dimensions and Center Placement
    const bool isNotice = (m_type == ConfirmModalType::Notice);
    const float cardW = config.ScaleF(470.0f);
    const float cardH = config.ScaleF(168.0f);
    const float cardX = (std::max)(0.0f, (clientW - cardW) * 0.5f);
    const float cardY = (std::max)(0.0f, (clientH - cardH) * 0.5f);
    m_cardBounds = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);

    D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_cardBounds, config.ScaleF(8.0f), config.ScaleF(8.0f));

    // Card Background
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBgBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode()
            ? D2D1::ColorF(0.12f, 0.13f, 0.16f, 0.98f)
            : D2D1::ColorF(0.96f, 0.97f, 0.98f, 0.98f),
        &cardBgBrush
    );
    if (cardBgBrush) pRT->FillRoundedRectangle(rrcCard, cardBgBrush.Get());

    // Card Flat Subtle Border (1px)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBorderBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode()
            ? D2D1::ColorF(0.24f, 0.26f, 0.31f, 1.0f)
            : D2D1::ColorF(0.80f, 0.82f, 0.86f, 1.0f),
        &cardBorderBrush
    );
    if (cardBorderBrush) pRT->DrawRoundedRectangle(rrcCard, cardBorderBrush.Get(), 1.0f);

    // 3. Top-Right Close '✕' Button
    const float closeBtnSize = config.ScaleF(24.0f);
    m_closeBtnBounds = D2D1::RectF(
        cardX + cardW - closeBtnSize - config.ScaleF(10.0f),
        cardY + config.ScaleF(10.0f),
        cardX + cardW - config.ScaleF(10.0f),
        cardY + config.ScaleF(10.0f) + closeBtnSize
    );
    if (m_isCloseHovered) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> closeHoverBrush;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f),
            &closeHoverBrush
        );
        if (closeHoverBrush) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_closeBtnBounds, 4.0f, 4.0f), closeHoverBrush.Get());
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

    // 4. Accent Icon and Title Setup
    bool isRestore = (m_type == ConfirmModalType::RestoreFile || m_status == L"D");
    bool isDiscardAll = (m_type == ConfirmModalType::DiscardAll || m_status == L"ALL");

    // Icon Circle Badge
    const float iconSize = config.ScaleF(28.0f);
    const float iconX = cardX + config.ScaleF(20.0f);
    const float iconY = cardY + config.ScaleF(20.0f);
    D2D1_RECT_F rcIcon = D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> iconBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> iconFgBrush;
    if (isNotice) {
        // Azure Theme for Notices
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.44f, 0.95f, 0.18f), &iconBgBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.70f, 1.0f, 1.0f), &iconFgBrush);
    } else if (isRestore) {
        // Cyan / Blue Theme for Restore
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.55f, 0.95f, 0.18f), &iconBgBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.70f, 1.0f, 1.0f), &iconFgBrush);
    } else {
        // Warning Amber / Orange Theme for Discard
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.65f, 0.15f, 0.18f), &iconBgBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.70f, 0.20f, 1.0f), &iconFgBrush);
    }

    if (iconBgBrush) {
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcIcon, config.ScaleF(6.0f), config.ScaleF(6.0f)), iconBgBrush.Get());
    }
    if (pFmtHint && iconFgBrush) {
        DWRITE_TEXT_ALIGNMENT prevAlign = pFmtHint->GetTextAlignment();
        DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtHint->GetParagraphAlignment();
        pFmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pFmtHint->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        const wchar_t* glyph = isNotice ? L"ℹ" : (isRestore ? L"↺" : L"⚠");
        pRT->DrawText(glyph, 1, pFmtHint, rcIcon, iconFgBrush.Get());
        pFmtHint->SetTextAlignment(prevAlign);
        pFmtHint->SetParagraphAlignment(prevPAlign);
    }

    // Modal Title
    std::wstring titleText;
    if (isNotice) {
        titleText = m_title.empty() ? L"Commit Message Required" : m_title;
    } else if (isRestore) {
        titleText = L"Restore File?";
    } else if (isDiscardAll) {
        titleText = L"Discard All Changes?";
    } else {
        titleText = L"Discard Changes?";
    }

    const float textLeft = iconX + iconSize + config.ScaleF(12.0f);
    const float textRight = m_closeBtnBounds.left - config.ScaleF(10.0f);

    D2D1_RECT_F rcTitle = D2D1::RectF(textLeft, iconY + config.ScaleF(3.0f), textRight, iconY + config.ScaleF(26.0f));
    IDWriteTextFormat* pFmtTitle = theme.fontTitle ? theme.fontTitle.Get() : theme.fontMenuItem.Get();
    ID2D1Brush* textPrimary = theme.IsDarkMode()
        ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
        : theme.brushTextPrimary.Get();
    if (pFmtTitle && textPrimary) {
        pRT->DrawText(titleText.c_str(), static_cast<UINT32>(titleText.length()), pFmtTitle, rcTitle, textPrimary);
    }

    // Main Confirmation Message
    std::wstring mainMsg;
    std::wstring subMsg;
    if (isNotice) {
        mainMsg = m_message.empty() ? L"Please enter a commit message before committing your changes." : m_message;
        subMsg = m_subtext.empty() ? L"Git requires a descriptive message to record changes in the repository history." : m_subtext;
    } else if (isRestore) {
        mainMsg = L"Are you sure you want to restore '" + m_fileName + L"'?";
        subMsg = L"The file will be restored from the latest Git commit (HEAD).";
    } else if (isDiscardAll) {
        mainMsg = L"Are you sure you want to discard ALL changes in " + std::to_wstring(m_changeCount) + L" files?";
        subMsg = L"This will permanently revert all unstaged changes back to HEAD. You cannot undo this action.";
    } else {
        mainMsg = L"Are you sure you want to discard changes in '" + m_fileName + L"'?";
        subMsg = L"Your changes will be lost if you don't save them. This action is irreversible.";
    }

    IDWriteTextFormat* pFmtBody = theme.fontBody ? theme.fontBody.Get() : pFmtTitle;
    D2D1_RECT_F rcMainMsg = D2D1::RectF(textLeft, iconY + config.ScaleF(32.0f), textRight, iconY + config.ScaleF(56.0f));
    if (pFmtBody && textPrimary) {
        pRT->DrawText(mainMsg.c_str(), static_cast<UINT32>(mainMsg.length()), pFmtBody, rcMainMsg, textPrimary);
    }

    ID2D1Brush* textMuted = theme.brushTextMuted ? theme.brushTextMuted.Get() : textPrimary;
    D2D1_RECT_F rcSubMsg = D2D1::RectF(textLeft, iconY + config.ScaleF(56.0f), textRight, iconY + config.ScaleF(78.0f));
    if (pFmtHint && textMuted) {
        pRT->DrawText(subMsg.c_str(), static_cast<UINT32>(subMsg.length()), pFmtHint, rcSubMsg, textMuted);
    }

    // 5. Action Buttons at Bottom Right
    const float btnH = config.ScaleF(30.0f);
    const float btnY = cardY + cardH - btnH - config.ScaleF(16.0f);

    if (isNotice) {
        // Single [ OK ] Button
        const float okBtnW = config.ScaleF(80.0f);
        m_confirmBtnBounds = D2D1::RectF(
            cardX + cardW - config.ScaleF(18.0f) - okBtnW,
            btnY,
            cardX + cardW - config.ScaleF(18.0f),
            btnY + btnH
        );
        m_cancelBtnBounds = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);

        D2D1_ROUNDED_RECT rrcOk = D2D1::RoundedRect(m_confirmBtnBounds, config.ScaleF(4.0f), config.ScaleF(4.0f));
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
        // Cancel + Primary Action (Discard or Restore)
        const float cancelBtnW = config.ScaleF(75.0f);
        float confirmBtnW = isRestore ? config.ScaleF(105.0f) : (isDiscardAll ? config.ScaleF(130.0f) : config.ScaleF(105.0f));

        m_confirmBtnBounds = D2D1::RectF(
            cardX + cardW - config.ScaleF(18.0f) - confirmBtnW,
            btnY,
            cardX + cardW - config.ScaleF(18.0f),
            btnY + btnH
        );

        m_cancelBtnBounds = D2D1::RectF(
            m_confirmBtnBounds.left - config.ScaleF(8.0f) - cancelBtnW,
            btnY,
            m_confirmBtnBounds.left - config.ScaleF(8.0f),
            btnY + btnH
        );

        // Render Cancel Button
        D2D1_ROUNDED_RECT rrcCancel = D2D1::RoundedRect(m_cancelBtnBounds, config.ScaleF(4.0f), config.ScaleF(4.0f));
        if (m_isCancelHovered) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cancelHoverBrush;
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.06f),
                &cancelHoverBrush
            );
            if (cancelHoverBrush) pRT->FillRoundedRectangle(rrcCancel, cancelHoverBrush.Get());
        }
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cancelBorderBrush;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.28f, 0.30f, 0.34f, 1.0f) : D2D1::ColorF(0.78f, 0.80f, 0.84f, 1.0f),
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

        // Render Primary Action Button
        D2D1_ROUNDED_RECT rrcConfirm = D2D1::RoundedRect(m_confirmBtnBounds, config.ScaleF(4.0f), config.ScaleF(4.0f));
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> confirmBgBrush;
        if (isRestore) {
            pRT->CreateSolidColorBrush(
                m_isConfirmHovered ? D2D1::ColorF(0.20f, 0.56f, 0.98f, 1.0f) : D2D1::ColorF(0.12f, 0.46f, 0.88f, 1.0f),
                &confirmBgBrush
            );
        } else {
            pRT->CreateSolidColorBrush(
                m_isConfirmHovered ? D2D1::ColorF(0.94f, 0.24f, 0.22f, 1.0f) : D2D1::ColorF(0.82f, 0.18f, 0.18f, 1.0f),
                &confirmBgBrush
            );
        }
        if (confirmBgBrush) pRT->FillRoundedRectangle(rrcConfirm, confirmBgBrush.Get());

        std::wstring confirmBtnText;
        if (isRestore) {
            confirmBtnText = L"Restore File";
        } else if (isDiscardAll) {
            confirmBtnText = L"Discard All Files";
        } else {
            confirmBtnText = L"Discard File";
        }

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

    bool oldConfirm = m_isConfirmHovered;
    bool oldCancel = m_isCancelHovered;
    bool oldClose = m_isCloseHovered;

    m_isConfirmHovered = (pt.x >= m_confirmBtnBounds.left && pt.x <= m_confirmBtnBounds.right &&
                          pt.y >= m_confirmBtnBounds.top && pt.y <= m_confirmBtnBounds.bottom);
    m_isCancelHovered = (pt.x >= m_cancelBtnBounds.left && pt.x <= m_cancelBtnBounds.right &&
                         pt.y >= m_cancelBtnBounds.top && pt.y <= m_cancelBtnBounds.bottom);
    m_isCloseHovered = (pt.x >= m_closeBtnBounds.left && pt.x <= m_closeBtnBounds.right &&
                        pt.y >= m_closeBtnBounds.top && pt.y <= m_closeBtnBounds.bottom);

    return (m_isConfirmHovered != oldConfirm) || (m_isCancelHovered != oldCancel) || (m_isCloseHovered != oldClose);
}

bool ConfirmDiscardModal::HandleMouseDown(POINT pt, bool& outConfirmed, std::wstring& outPath, std::wstring& outFileName, std::wstring& outStatus) noexcept {
    if (!m_isOpen) return false;

    // Check Primary Confirm Button
    if (pt.x >= m_confirmBtnBounds.left && pt.x <= m_confirmBtnBounds.right &&
        pt.y >= m_confirmBtnBounds.top && pt.y <= m_confirmBtnBounds.bottom) {
        outConfirmed = true;
        outPath = m_filePath;
        outFileName = m_fileName;
        outStatus = m_status;
        Close();
        return true;
    }

    // Check Cancel Button
    if (pt.x >= m_cancelBtnBounds.left && pt.x <= m_cancelBtnBounds.right &&
        pt.y >= m_cancelBtnBounds.top && pt.y <= m_cancelBtnBounds.bottom) {
        outConfirmed = false;
        Close();
        return true;
    }

    // Check Close '✕' Button
    if (pt.x >= m_closeBtnBounds.left && pt.x <= m_closeBtnBounds.right &&
        pt.y >= m_closeBtnBounds.top && pt.y <= m_closeBtnBounds.bottom) {
        outConfirmed = false;
        Close();
        return true;
    }

    // Clicking anywhere outside the modal card cancels the dialog
    if (pt.x < m_cardBounds.left || pt.x > m_cardBounds.right ||
        pt.y < m_cardBounds.top || pt.y > m_cardBounds.bottom) {
        outConfirmed = false;
        Close();
        return true;
    }

    // Click inside modal card (swallow click to prevent click-through)
    return true;
}

bool ConfirmDiscardModal::HandleKeyDown(WPARAM wParam, bool& outConfirmed, std::wstring& outPath, std::wstring& outFileName, std::wstring& outStatus) noexcept {
    if (!m_isOpen) return false;

    if (wParam == VK_RETURN) {
        outConfirmed = true;
        outPath = m_filePath;
        outFileName = m_fileName;
        outStatus = m_status;
        Close();
        return true;
    }

    if (wParam == VK_ESCAPE) {
        outConfirmed = false;
        Close();
        return true;
    }

    return true; // Consume keys while modal is active
}

} // namespace phide::ui
