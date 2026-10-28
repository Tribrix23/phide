/**
 * @file ConfirmDiscardModal.cpp
 * @brief Implementation of the visual confirmation modal for Git discard, restore, and delete actions.
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
    const bool isDelete = (m_type == ConfirmModalType::DeleteFile || m_type == ConfirmModalType::DeleteFolder);
    const bool isRestore = (m_type == ConfirmModalType::RestoreFile || m_status == L"D");
    const bool isDiscardAll = (m_type == ConfirmModalType::DiscardAll || m_status == L"ALL");

    const float cardW = config.ScaleF(540.0f);
    const float cardH = config.ScaleF(190.0f);
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
    if (m_isCloseHovered && !m_isLoading) {
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
        ID2D1Brush* closeTextBrush = m_isLoading
            ? (theme.brushTextMuted ? theme.brushTextMuted.Get() : theme.brushTextPrimary.Get())
            : (m_isCloseHovered
                ? (theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get())
                : (theme.brushTextMuted ? theme.brushTextMuted.Get() : theme.brushTextPrimary.Get()));
        if (closeTextBrush) {
            pRT->DrawText(L"✕", 1, pFmtHint, m_closeBtnBounds, closeTextBrush);
        }
        pFmtHint->SetTextAlignment(prevAlign);
        pFmtHint->SetParagraphAlignment(prevPAlign);
    }

    // 4. Accent Icon and Title Setup
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
    } else if (isDelete) {
        // Red Theme for Deleting files or folders
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.20f, 0.20f, 0.18f), &iconBgBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.32f, 0.32f, 1.0f), &iconFgBrush);
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
        const wchar_t* glyph = isNotice ? L"ℹ" : (isDelete ? L"⚠" : (isRestore ? L"↺" : L"⚠"));
        pRT->DrawText(glyph, 1, pFmtHint, rcIcon, iconFgBrush.Get());
        pFmtHint->SetTextAlignment(prevAlign);
        pFmtHint->SetParagraphAlignment(prevPAlign);
    }

    // Modal Title
    std::wstring titleText;
    if (isNotice) {
        titleText = m_title.empty() ? L"Commit Message Required" : m_title;
    } else if (isDelete) {
        titleText = (m_type == ConfirmModalType::DeleteFolder) ? L"Delete Folder?" : L"Delete File?";
    } else if (isRestore) {
        titleText = L"Restore File?";
    } else if (isDiscardAll) {
        titleText = L"Discard All Changes?";
    } else {
        titleText = L"Discard Changes?";
    }

    const float textLeft = iconX + iconSize + config.ScaleF(14.0f);
    const float textRight = cardX + cardW - config.ScaleF(24.0f);
    const float titleRight = m_closeBtnBounds.left - config.ScaleF(10.0f);

    D2D1_RECT_F rcTitle = D2D1::RectF(textLeft, iconY + config.ScaleF(3.0f), titleRight, iconY + config.ScaleF(26.0f));
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
    } else if (isDelete) {
        mainMsg = L"Are you sure you want to delete '" + m_fileName + L"'?";
        if (m_type == ConfirmModalType::DeleteFolder) {
            subMsg = L"This folder and its contents will be permanently deleted. This action cannot be undone.";
        } else {
            subMsg = L"This file will be permanently deleted from the project. This action cannot be undone.";
        }
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
    D2D1_RECT_F rcMainMsg = D2D1::RectF(textLeft, iconY + config.ScaleF(30.0f), textRight, iconY + config.ScaleF(58.0f));
    if (pFmtBody && textPrimary) {
        DWRITE_WORD_WRAPPING prevWrapBody = pFmtBody->GetWordWrapping();
        pFmtBody->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        pRT->DrawText(mainMsg.c_str(), static_cast<UINT32>(mainMsg.length()), pFmtBody, rcMainMsg, textPrimary);
        pFmtBody->SetWordWrapping(prevWrapBody);
    }

    ID2D1Brush* textMuted = theme.brushTextMuted ? theme.brushTextMuted.Get() : textPrimary;
    D2D1_RECT_F rcSubMsg = D2D1::RectF(textLeft, iconY + config.ScaleF(58.0f), textRight, iconY + config.ScaleF(124.0f));
    if (pFmtHint && textMuted) {
        DWRITE_WORD_WRAPPING prevWrapHint = pFmtHint->GetWordWrapping();
        pFmtHint->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        pRT->DrawText(subMsg.c_str(), static_cast<UINT32>(subMsg.length()), pFmtHint, rcSubMsg, textMuted);
        pFmtHint->SetWordWrapping(prevWrapHint);
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
        // Cancel + Primary Action (Discard, Restore, or Delete)
        const float cancelBtnW = config.ScaleF(75.0f);
        float confirmBtnW = isDelete ? config.ScaleF(105.0f) :
                            (isRestore ? config.ScaleF(122.0f) :
                            (isDiscardAll ? config.ScaleF(138.0f) : config.ScaleF(122.0f)));

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
        if (m_isCancelHovered && !m_isLoading) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cancelHoverBrush;
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.06f),
                &cancelHoverBrush
            );
            if (cancelHoverBrush) pRT->FillRoundedRectangle(rrcCancel, cancelHoverBrush.Get());
        }
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cancelBorderBrush;
        pRT->CreateSolidColorBrush(
            m_isLoading
                ? (theme.IsDarkMode() ? D2D1::ColorF(0.20f, 0.22f, 0.25f, 0.6f) : D2D1::ColorF(0.85f, 0.86f, 0.88f, 0.6f))
                : (theme.IsDarkMode() ? D2D1::ColorF(0.28f, 0.30f, 0.34f, 1.0f) : D2D1::ColorF(0.78f, 0.80f, 0.84f, 1.0f)),
            &cancelBorderBrush
        );
        if (cancelBorderBrush) pRT->DrawRoundedRectangle(rrcCancel, cancelBorderBrush.Get(), 1.0f);

        ID2D1Brush* cancelTextBrush = m_isLoading ? theme.brushTextMuted.Get() : textPrimary;
        if (pFmtBody && cancelTextBrush) {
            DWRITE_TEXT_ALIGNMENT prevAlign = pFmtBody->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtBody->GetParagraphAlignment();
            pFmtBody->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pFmtBody->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(L"Cancel", 6, pFmtBody, m_cancelBtnBounds, cancelTextBrush);
            pFmtBody->SetTextAlignment(prevAlign);
            pFmtBody->SetParagraphAlignment(prevPAlign);
        }

        // Render Primary Action Button (Restore, Delete, or Discard)
        D2D1_ROUNDED_RECT rrcConfirm = D2D1::RoundedRect(m_confirmBtnBounds, config.ScaleF(4.0f), config.ScaleF(4.0f));
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> confirmBgBrush;
        if (isRestore) {
            pRT->CreateSolidColorBrush(
                m_isLoading ? D2D1::ColorF(0.09f, 0.34f, 0.65f, 1.0f) :
                (m_isConfirmHovered ? D2D1::ColorF(0.20f, 0.56f, 0.98f, 1.0f) : D2D1::ColorF(0.12f, 0.46f, 0.88f, 1.0f)),
                &confirmBgBrush
            );
        } else if (isDelete) {
            pRT->CreateSolidColorBrush(
                m_isLoading ? D2D1::ColorF(0.60f, 0.14f, 0.14f, 1.0f) :
                (m_isConfirmHovered ? D2D1::ColorF(0.94f, 0.24f, 0.22f, 1.0f) : D2D1::ColorF(0.82f, 0.18f, 0.18f, 1.0f)),
                &confirmBgBrush
            );
        } else {
            // Discard
            pRT->CreateSolidColorBrush(
                m_isLoading ? D2D1::ColorF(0.60f, 0.14f, 0.14f, 1.0f) :
                (m_isConfirmHovered ? D2D1::ColorF(0.94f, 0.24f, 0.22f, 1.0f) : D2D1::ColorF(0.82f, 0.18f, 0.18f, 1.0f)),
                &confirmBgBrush
            );
        }
        if (confirmBgBrush) pRT->FillRoundedRectangle(rrcConfirm, confirmBgBrush.Get());

        std::wstring confirmBtnText;
        if (m_isLoading) {
            if (isRestore) {
                confirmBtnText = L"Restoring...";
            } else if (isDelete) {
                confirmBtnText = L"Deleting...";
            } else {
                confirmBtnText = L"Discarding...";
            }
        } else {
            if (isRestore) {
                confirmBtnText = L"Restore File";
            } else if (isDelete) {
                confirmBtnText = L"Delete";
            } else if (isDiscardAll) {
                confirmBtnText = L"Discard All Files";
            } else {
                confirmBtnText = L"Discard File";
            }
        }

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> whiteTextBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &whiteTextBrush);

        if (m_isLoading) {
            // Render animated Direct2D arc spinner inside button
            const float spinnerRadius = config.ScaleF(5.0f);
            const float spinnerCenterX = m_confirmBtnBounds.left + config.ScaleF(16.0f);
            const float spinnerCenterY = (m_confirmBtnBounds.top + m_confirmBtnBounds.bottom) * 0.5f;

            // Track circle
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBrush;
            pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f), &trackBrush);
            if (trackBrush) {
                pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(spinnerCenterX, spinnerCenterY), spinnerRadius, spinnerRadius), trackBrush.Get(), 1.5f);
            }

            // Rotating arc
            D2D1_MATRIX_3X2_F oldTrans;
            pRT->GetTransform(&oldTrans);
            pRT->SetTransform(D2D1::Matrix3x2F::Rotation(m_spinnerAngle, D2D1::Point2F(spinnerCenterX, spinnerCenterY)) * oldTrans);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> arcBrush;
            pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f), &arcBrush);
            if (arcBrush) {
                Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
                pRT->GetFactory(&pFactory);
                if (pFactory) {
                    Microsoft::WRL::ComPtr<ID2D1PathGeometry> pArcGeo;
                    if (SUCCEEDED(pFactory->CreatePathGeometry(&pArcGeo))) {
                        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                        if (SUCCEEDED(pArcGeo->Open(&sink))) {
                            sink->BeginFigure(D2D1::Point2F(spinnerCenterX - spinnerRadius, spinnerCenterY), D2D1_FIGURE_BEGIN_HOLLOW);
                            D2D1_ARC_SEGMENT arc{};
                            arc.point = D2D1::Point2F(spinnerCenterX, spinnerCenterY - spinnerRadius);
                            arc.size = D2D1::SizeF(spinnerRadius, spinnerRadius);
                            arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                            sink->AddArc(arc);
                            sink->EndFigure(D2D1_FIGURE_END_OPEN);
                            sink->Close();
                            pRT->DrawGeometry(pArcGeo.Get(), arcBrush.Get(), 2.0f);
                        }
                    }
                }
            }
            pRT->SetTransform(oldTrans);

            // Text offset to right of spinner
            D2D1_RECT_F rcText = D2D1::RectF(
                spinnerCenterX + spinnerRadius + config.ScaleF(6.0f),
                m_confirmBtnBounds.top,
                m_confirmBtnBounds.right - config.ScaleF(4.0f),
                m_confirmBtnBounds.bottom
            );
            if (pFmtBody && whiteTextBrush) {
                DWRITE_TEXT_ALIGNMENT prevAlign = pFmtBody->GetTextAlignment();
                DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtBody->GetParagraphAlignment();
                pFmtBody->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                pFmtBody->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                pRT->DrawText(confirmBtnText.c_str(), static_cast<UINT32>(confirmBtnText.length()), pFmtBody, rcText, whiteTextBrush.Get());
                pFmtBody->SetTextAlignment(prevAlign);
                pFmtBody->SetParagraphAlignment(prevPAlign);
            }
        } else {
            // Normal centered text
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
}

bool ConfirmDiscardModal::HandleMouseMove(POINT pt) noexcept {
    if (!m_isOpen) return false;
    if (m_isLoading) {
        if (m_isConfirmHovered || m_isCancelHovered || m_isCloseHovered) {
            m_isConfirmHovered = false;
            m_isCancelHovered = false;
            m_isCloseHovered = false;
            return true;
        }
        return false;
    }

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

    // Strict idempotency: when loading, completely ignore any clicks to prevent spamming
    if (m_isLoading) {
        return true;
    }

    // Check Primary Confirm Button
    if (pt.x >= m_confirmBtnBounds.left && pt.x <= m_confirmBtnBounds.right &&
        pt.y >= m_confirmBtnBounds.top && pt.y <= m_confirmBtnBounds.bottom) {
        outConfirmed = true;
        outPath = m_filePath;
        outFileName = m_fileName;
        outStatus = m_status;
        if (m_type == ConfirmModalType::Notice) {
            Close();
        } else {
            // Enter loading state and disable button immediately!
            m_isLoading = true;
            m_spinnerAngle = 0.0f;
        }
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

    // Strict idempotency: consume all keys while loading
    if (m_isLoading) {
        return true;
    }

    if (wParam == VK_RETURN) {
        outConfirmed = true;
        outPath = m_filePath;
        outFileName = m_fileName;
        outStatus = m_status;
        if (m_type == ConfirmModalType::Notice) {
            Close();
        } else {
            m_isLoading = true;
            m_spinnerAngle = 0.0f;
        }
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
