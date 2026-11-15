/**
 * @file GitErrorModal.hpp
 * @brief Modal dialog displayed when a Git operation fails (matching Image 2).
 *
 * Provides actions to "Open Git Log" in integrated terminal tab, "Show Command Output"
 * in a temporary editor document tab, or "Cancel" while preserving commit message.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

enum class GitErrorModalAction {
    None,
    OpenGitLog,
    ShowCommandOutput,
    Cancel
};

class GitErrorModal {
public:
    GitErrorModal() = default;
    ~GitErrorModal() = default;

    void Open(const std::wstring& command, const std::wstring& output, const std::wstring& repoDir) {
        m_command = command;
        m_output = output;
        m_repoDir = repoDir;
        m_isOpen = true;

        m_isGitLogHovered = false;
        m_isShowOutputHovered = false;
        m_isCancelHovered = false;
        m_isCloseHovered = false;

        // Parse primary error message and secondary details, skipping trailing `git status` noise
        m_title = L"Git: ";
        m_details.clear();

        auto trimLine = [](std::wstring s) {
            while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ' || s.back() == L'\t')) s.pop_back();
            size_t start = 0;
            while (start < s.size() && (s[start] == L' ' || s[start] == L'\t')) ++start;
            return s.substr(start);
        };
        auto startsWith = [](const std::wstring& s, const wchar_t* prefix) {
            return s.rfind(prefix, 0) == 0;
        };

        if (output.empty()) {
            m_title += L"Command failed: " + command;
        } else {
            std::wstringstream ss(output);
            std::wstring rawLine;
            bool foundPrimary = false;
            bool inStatusTrailer = false;
            int detailLineCount = 0;
            while (std::getline(ss, rawLine)) {
                std::wstring line = trimLine(rawLine);
                if (line.empty()) continue;

                if (startsWith(line, L"On branch ") ||
                    startsWith(line, L"Your branch is ") ||
                    startsWith(line, L"Changes not staged for commit:") ||
                    startsWith(line, L"Changes to be committed:") ||
                    startsWith(line, L"Untracked files:") ||
                    startsWith(line, L"no changes added to commit") ||
                    startsWith(line, L"nothing to commit")) {
                    inStatusTrailer = true;
                    continue;
                }
                if (inStatusTrailer) {
                    continue;
                }
                if (startsWith(line, L"(use \"git ") ||
                    startsWith(line, L"modified:") ||
                    startsWith(line, L"deleted:") ||
                    startsWith(line, L"new file:")) {
                    continue;
                }

                if (!foundPrimary) {
                    if (line.find(L"fatal:") != std::wstring::npos || line.find(L"error:") != std::wstring::npos) {
                        m_title += line;
                        foundPrimary = true;
                    } else if (line.find(L">") == std::wstring::npos) {
                        m_title += line;
                        foundPrimary = true;
                    }
                } else if (detailLineCount < 6) {
                    if (!m_details.empty()) m_details += L"\n";
                    m_details += line;
                    ++detailLineCount;
                }
            }
            if (!foundPrimary) {
                m_title += output;
            }
        }

        if (m_details.empty()) {
            if (output.find(L"index.lock") != std::wstring::npos || output.find(L"Another git process") != std::wstring::npos) {
                m_details = L"Another git process seems to be running in this repository, e.g. an editor opened by 'git commit'. Please make sure all processes are terminated then try again. If it still fails, a git process may have crashed in this repository earlier: remove the file manually to continue.";
            } else {
                m_details = L"The Git operation failed. Click 'Show Command Output' or 'Open Git Log' to review the full command output.";
            }
        }

        // Initialize default card bounds centered on standard 1280x800 display
        const float cardW = 620.0f;
        const float cardH = 220.0f;
        const float cardX = (1280.0f - cardW) * 0.5f;
        const float cardY = (800.0f - cardH) * 0.5f;
        m_cardBounds = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);

        const float btnH = 28.0f;
        const float btnY = cardY + cardH - btnH - 18.0f;
        const float gap = 8.0f;
        float curRight = cardX + cardW - 20.0f;

        const float cancelBtnW = 72.0f;
        m_cancelBtnBounds = D2D1::RectF(curRight - cancelBtnW, btnY, curRight, btnY + btnH);
        curRight -= (cancelBtnW + gap);

        const float showOutputBtnW = 164.0f;
        m_showOutputBtnBounds = D2D1::RectF(curRight - showOutputBtnW, btnY, curRight, btnY + btnH);
        curRight -= (showOutputBtnW + gap);

        const float gitLogBtnW = 124.0f;
        m_gitLogBtnBounds = D2D1::RectF(curRight - gitLogBtnW, btnY, curRight, btnY + btnH);

        const float closeBtnSize = 24.0f;
        m_closeBtnBounds = D2D1::RectF(cardX + cardW - closeBtnSize - 10.0f, cardY + 10.0f, cardX + cardW - 10.0f, cardY + 10.0f + closeBtnSize);
    }

    void Close() noexcept {
        m_isOpen = false;
        m_isGitLogHovered = false;
        m_isShowOutputHovered = false;
        m_isCancelHovered = false;
        m_isCloseHovered = false;
    }

    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }
    [[nodiscard]] const std::wstring& GetCommand() const noexcept { return m_command; }
    [[nodiscard]] const std::wstring& GetOutput() const noexcept { return m_output; }
    [[nodiscard]] const std::wstring& GetRepoDir() const noexcept { return m_repoDir; }
    [[nodiscard]] const std::wstring& GetTitle() const noexcept { return m_title; }
    [[nodiscard]] const std::wstring& GetDetails() const noexcept { return m_details; }
    [[nodiscard]] D2D1_RECT_F GetCardBounds() const noexcept { return m_cardBounds; }
    [[nodiscard]] D2D1_RECT_F GetTitleBounds() const noexcept { return m_titleBounds; }
    [[nodiscard]] D2D1_RECT_F GetDetailsBounds() const noexcept { return m_detailsBounds; }
    [[nodiscard]] D2D1_RECT_F GetGitLogBtnBounds() const noexcept { return m_gitLogBtnBounds; }
    [[nodiscard]] D2D1_RECT_F GetShowOutputBtnBounds() const noexcept { return m_showOutputBtnBounds; }
    [[nodiscard]] D2D1_RECT_F GetCancelBtnBounds() const noexcept { return m_cancelBtnBounds; }

    [[nodiscard]] bool HitTestButtons(POINT pt) const noexcept {
        if (!m_isOpen) return false;
        auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
            return (p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom);
        };
        return inRect(pt, m_gitLogBtnBounds) || inRect(pt, m_showOutputBtnBounds) ||
               inRect(pt, m_cancelBtnBounds) || inRect(pt, m_closeBtnBounds);
    }

    [[nodiscard]] bool HitTestCard(POINT pt) const noexcept {
        if (!m_isOpen) return false;
        return (pt.x >= m_cardBounds.left && pt.x <= m_cardBounds.right &&
                pt.y >= m_cardBounds.top && pt.y <= m_cardBounds.bottom);
    }

    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientW, float clientH) {
        if (!m_isOpen || !pRT) return;

        // 1. Dimmed Backdrop
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> backdropBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f), &backdropBrush);
        if (backdropBrush) {
            pRT->FillRectangle(D2D1::RectF(0.0f, 0.0f, clientW, clientH), backdropBrush.Get());
        }

        auto measureTextBlockHeight = [&](IDWriteTextFormat* fmt, const std::wstring& text, float maxW, float fallbackH) -> float {
            if (!fmt || text.empty() || !theme.dwriteFactory) return fallbackH;
            DWRITE_WORD_WRAPPING prevWrap = fmt->GetWordWrapping();
            DWRITE_TEXT_ALIGNMENT prevAlign = fmt->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = fmt->GetParagraphAlignment();
            fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            float measuredH = fallbackH;
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt, maxW, 2000.0f, &layout)) && layout) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(layout->GetMetrics(&tm))) {
                    measuredH = (std::max)(fallbackH, tm.height + 2.0f);
                }
            }
            fmt->SetWordWrapping(prevWrap);
            fmt->SetTextAlignment(prevAlign);
            fmt->SetParagraphAlignment(prevPAlign);
            return measuredH;
        };
        auto measureSingleLineWidth = [&](IDWriteTextFormat* fmt, const wchar_t* text, float fallbackW) -> float {
            if (!fmt || !text || !theme.dwriteFactory) return fallbackW;
            float measuredW = fallbackW;
            Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            const UINT32 len = static_cast<UINT32>(wcslen(text));
            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(text, len, fmt, 2000.0f, 100.0f, &layout)) && layout) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(layout->GetMetrics(&tm))) {
                    measuredW = (std::max)(fallbackW, tm.widthIncludingTrailingWhitespace);
                }
            }
            return measuredW;
        };

        IDWriteTextFormat* pFmtTitle = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get();
        IDWriteTextFormat* pFmtHint = theme.fontHint ? theme.fontHint.Get() : theme.fontCode.Get();
        IDWriteTextFormat* pFmtBody = theme.fontBody ? theme.fontBody.Get() : (theme.fontHint ? theme.fontHint.Get() : pFmtHint);
        IDWriteTextFormat* pFmtBtn = theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get();

        // 2. Adaptive Card Placement (prevents title/body/button overlap)
        const float cardW = (std::min)(config.ScaleF(640.0f), (std::max)(config.ScaleF(420.0f), clientW - config.ScaleF(40.0f)));
        const float closeBtnSize = config.ScaleF(24.0f);
        const float textLeftOffset = config.ScaleF(62.0f);
        const float textRightInset = closeBtnSize + config.ScaleF(22.0f);
        const float textMaxW = (std::max)(config.ScaleF(160.0f), cardW - textLeftOffset - textRightInset);

        const float topPad = config.ScaleF(20.0f);
        const float midGap = config.ScaleF(10.0f);
        const float botGap = config.ScaleF(14.0f);
        const float botPad = config.ScaleF(18.0f);
        const float btnH = config.ScaleF(28.0f);

        const float rawTitleH = measureTextBlockHeight(pFmtTitle, m_title, textMaxW, config.ScaleF(22.0f));
        const float titleH = (std::min)(config.ScaleF(96.0f), (std::max)(config.ScaleF(24.0f), rawTitleH));
        const float rawDetailsH = measureTextBlockHeight(pFmtBody, m_details, textMaxW, config.ScaleF(36.0f));
        const float desiredCardH = topPad + titleH + midGap + rawDetailsH + botGap + btnH + botPad;
        const float maxCardH = (std::min)(config.ScaleF(360.0f), (std::max)(config.ScaleF(210.0f), clientH - config.ScaleF(40.0f)));
        const float cardH = std::clamp(desiredCardH, config.ScaleF(210.0f), (std::max)(config.ScaleF(210.0f), maxCardH));

        const float cardX = (std::max)(0.0f, (clientW - cardW) * 0.5f);
        const float cardY = (std::max)(0.0f, (clientH - cardH) * 0.5f);
        m_cardBounds = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);

        pRT->PushAxisAlignedClip(m_cardBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_cardBounds, config.ScaleF(8.0f), config.ScaleF(8.0f));

        // Background & border
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBorder;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.13f, 0.16f, 0.98f) : D2D1::ColorF(0.96f, 0.97f, 0.98f, 0.98f),
            &cardBg
        );
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.26f, 0.31f, 1.0f) : D2D1::ColorF(0.80f, 0.82f, 0.86f, 1.0f),
            &cardBorder
        );

        if (cardBg) pRT->FillRoundedRectangle(rrcCard, cardBg.Get());
        if (cardBorder) pRT->DrawRoundedRectangle(rrcCard, cardBorder.Get(), 1.0f);

        // 3. Top-Right Close Button
        m_closeBtnBounds = D2D1::RectF(
            cardX + cardW - closeBtnSize - config.ScaleF(10.0f),
            cardY + config.ScaleF(10.0f),
            cardX + cardW - config.ScaleF(10.0f),
            cardY + config.ScaleF(10.0f) + closeBtnSize
        );
        if (m_isCloseHovered) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> closeHover;
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f),
                &closeHover
            );
            if (closeHover) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(m_closeBtnBounds, 4.0f, 4.0f), closeHover.Get());
            }
        }
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

        // 4. Red Circular (X) Badge Icon on Left (Image 2)
        const float iconCx = cardX + config.ScaleF(36.0f);
        const float iconCy = cardY + config.ScaleF(34.0f);
        const float iconR = config.ScaleF(13.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> redCircleBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> whiteCrossBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.26f, 0.26f, 1.0f), &redCircleBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &whiteCrossBrush);

        if (redCircleBrush) {
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(iconCx, iconCy), iconR, iconR), redCircleBrush.Get());
        }
        if (whiteCrossBrush) {
            float crossR = config.ScaleF(4.0f);
            pRT->DrawLine(D2D1::Point2F(iconCx - crossR, iconCy - crossR), D2D1::Point2F(iconCx + crossR, iconCy + crossR), whiteCrossBrush.Get(), 1.8f);
            pRT->DrawLine(D2D1::Point2F(iconCx + crossR, iconCy - crossR), D2D1::Point2F(iconCx - crossR, iconCy + crossR), whiteCrossBrush.Get(), 1.8f);
        }

        // 5. Text Area (strictly top-aligned with DWRITE_PARAGRAPH_ALIGNMENT_NEAR + clipped rects)
        const float btnY = cardY + cardH - btnH - botPad;
        const float textLeft = cardX + textLeftOffset;
        const float textRight = m_closeBtnBounds.left - config.ScaleF(12.0f);

        ID2D1Brush* textPrimary = theme.IsDarkMode()
            ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
            : theme.brushTextPrimary.Get();

        D2D1_RECT_F rcTitle = D2D1::RectF(textLeft, cardY + topPad, textRight, cardY + topPad + titleH);
        m_titleBounds = rcTitle;
        if (pFmtTitle && textPrimary) {
            DWRITE_TEXT_ALIGNMENT prevAlign = pFmtTitle->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtTitle->GetParagraphAlignment();
            DWRITE_WORD_WRAPPING prevWrap = pFmtTitle->GetWordWrapping();
            pFmtTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            pFmtTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            pFmtTitle->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            pRT->PushAxisAlignedClip(rcTitle, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            pRT->DrawText(m_title.c_str(), static_cast<UINT32>(m_title.length()), pFmtTitle, rcTitle, textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            pRT->PopAxisAlignedClip();
            pFmtTitle->SetTextAlignment(prevAlign);
            pFmtTitle->SetParagraphAlignment(prevPAlign);
            pFmtTitle->SetWordWrapping(prevWrap);
        }

        ID2D1Brush* textMuted = theme.brushTextMuted ? theme.brushTextMuted.Get() : textPrimary;
        const float detailsTop = rcTitle.bottom + midGap;
        const float detailsBottom = (std::max)(detailsTop + config.ScaleF(20.0f), btnY - botGap);
        D2D1_RECT_F rcDetails = D2D1::RectF(textLeft, detailsTop, textRight, detailsBottom);
        m_detailsBounds = rcDetails;
        if (pFmtBody && textMuted) {
            DWRITE_TEXT_ALIGNMENT prevAlign = pFmtBody->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtBody->GetParagraphAlignment();
            DWRITE_WORD_WRAPPING prevWrap = pFmtBody->GetWordWrapping();
            pFmtBody->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            pFmtBody->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            pFmtBody->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            pRT->PushAxisAlignedClip(rcDetails, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            pRT->DrawText(m_details.c_str(), static_cast<UINT32>(m_details.length()), pFmtBody, rcDetails, textMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            pRT->PopAxisAlignedClip();
            pFmtBody->SetTextAlignment(prevAlign);
            pFmtBody->SetParagraphAlignment(prevPAlign);
            pFmtBody->SetWordWrapping(prevWrap);
        }

        // 6. Action Buttons at Bottom Right (measured widths so labels never clip)
        // [ Open Git Log ]  [ Show Command Output ]  [ Cancel ]
        const float gap = config.ScaleF(8.0f);
        float curRight = cardX + cardW - config.ScaleF(20.0f);

        // Cancel Button (Rightmost)
        const float cancelBtnW = (std::max)(config.ScaleF(72.0f), measureSingleLineWidth(pFmtBtn, L"Cancel", config.ScaleF(44.0f)) + config.ScaleF(24.0f));
        m_cancelBtnBounds = D2D1::RectF(curRight - cancelBtnW, btnY, curRight, btnY + btnH);
        curRight -= (cancelBtnW + gap);

        // Show Command Output Button (Middle)
        const float showOutputBtnW = (std::max)(config.ScaleF(164.0f), measureSingleLineWidth(pFmtBtn, L"Show Command Output", config.ScaleF(140.0f)) + config.ScaleF(26.0f));
        m_showOutputBtnBounds = D2D1::RectF(curRight - showOutputBtnW, btnY, curRight, btnY + btnH);
        curRight -= (showOutputBtnW + gap);

        // Open Git Log Button (Left of middle, Primary Blue)
        const float gitLogBtnW = (std::max)(config.ScaleF(124.0f), measureSingleLineWidth(pFmtBtn, L"Open Git Log", config.ScaleF(96.0f)) + config.ScaleF(26.0f));
        m_gitLogBtnBounds = D2D1::RectF(curRight - gitLogBtnW, btnY, curRight, btnY + btnH);

        // Render [ Open Git Log ] (Primary Accent)
        {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_gitLogBtnBounds, 4.0f, 4.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> btnBg;
            pRT->CreateSolidColorBrush(
                m_isGitLogHovered ? D2D1::ColorF(0.00f, 0.54f, 0.90f, 1.0f) : D2D1::ColorF(0.00f, 0.478f, 0.80f, 1.0f),
                &btnBg
            );
            if (btnBg) pRT->FillRoundedRectangle(rrc, btnBg.Get());

            IDWriteTextFormat* pFmtBtn = theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get();
            if (pFmtBtn) {
                DWRITE_TEXT_ALIGNMENT prevAlign = pFmtBtn->GetTextAlignment();
                DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtBtn->GetParagraphAlignment();
                pFmtBtn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                pFmtBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                pRT->DrawText(L"Open Git Log", 12, pFmtBtn, m_gitLogBtnBounds, theme.brushTextWhite.Get());
                pFmtBtn->SetTextAlignment(prevAlign);
                pFmtBtn->SetParagraphAlignment(prevPAlign);
            }
        }

        // Render [ Show Command Output ] (Dark Secondary Button)
        {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_showOutputBtnBounds, 4.0f, 4.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> btnBg;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> btnBorder;
            pRT->CreateSolidColorBrush(
                m_isShowOutputHovered
                    ? (theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.25f, 0.28f, 1.0f) : D2D1::ColorF(0.88f, 0.90f, 0.92f, 1.0f))
                    : (theme.IsDarkMode() ? D2D1::ColorF(0.18f, 0.19f, 0.22f, 1.0f) : D2D1::ColorF(0.92f, 0.93f, 0.95f, 1.0f)),
                &btnBg
            );
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.32f, 0.34f, 0.38f, 1.0f) : D2D1::ColorF(0.75f, 0.77f, 0.80f, 1.0f),
                &btnBorder
            );
            if (btnBg) pRT->FillRoundedRectangle(rrc, btnBg.Get());
            if (btnBorder) pRT->DrawRoundedRectangle(rrc, btnBorder.Get(), 1.0f);

            IDWriteTextFormat* pFmtBtn = theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get();
            if (pFmtBtn && textPrimary) {
                DWRITE_TEXT_ALIGNMENT prevAlign = pFmtBtn->GetTextAlignment();
                DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtBtn->GetParagraphAlignment();
                pFmtBtn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                pFmtBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                pRT->DrawText(L"Show Command Output", 19, pFmtBtn, m_showOutputBtnBounds, textPrimary);
                pFmtBtn->SetTextAlignment(prevAlign);
                pFmtBtn->SetParagraphAlignment(prevPAlign);
            }
        }

        // Render [ Cancel ] (Dark Tertiary Button)
        {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_cancelBtnBounds, 4.0f, 4.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> btnBg;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> btnBorder;
            pRT->CreateSolidColorBrush(
                m_isCancelHovered
                    ? (theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.25f, 0.28f, 1.0f) : D2D1::ColorF(0.88f, 0.90f, 0.92f, 1.0f))
                    : (theme.IsDarkMode() ? D2D1::ColorF(0.18f, 0.19f, 0.22f, 1.0f) : D2D1::ColorF(0.92f, 0.93f, 0.95f, 1.0f)),
                &btnBg
            );
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.32f, 0.34f, 0.38f, 1.0f) : D2D1::ColorF(0.75f, 0.77f, 0.80f, 1.0f),
                &btnBorder
            );
            if (btnBg) pRT->FillRoundedRectangle(rrc, btnBg.Get());
            if (btnBorder) pRT->DrawRoundedRectangle(rrc, btnBorder.Get(), 1.0f);

            IDWriteTextFormat* pFmtBtn = theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get();
            if (pFmtBtn && textPrimary) {
                DWRITE_TEXT_ALIGNMENT prevAlign = pFmtBtn->GetTextAlignment();
                DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = pFmtBtn->GetParagraphAlignment();
                pFmtBtn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                pFmtBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                pRT->DrawText(L"Cancel", 6, pFmtBtn, m_cancelBtnBounds, textPrimary);
                pFmtBtn->SetTextAlignment(prevAlign);
                pFmtBtn->SetParagraphAlignment(prevPAlign);
            }
        }

        pRT->PopAxisAlignedClip();
    }

    bool HandleMouseMove(POINT pt) noexcept {
        if (!m_isOpen) return false;
        auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
            return (p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom);
        };
        bool hLog = inRect(pt, m_gitLogBtnBounds);
        bool hOut = inRect(pt, m_showOutputBtnBounds);
        bool hCan = inRect(pt, m_cancelBtnBounds);
        bool hClose = inRect(pt, m_closeBtnBounds);

        if (hLog != m_isGitLogHovered || hOut != m_isShowOutputHovered ||
            hCan != m_isCancelHovered || hClose != m_isCloseHovered) {
            m_isGitLogHovered = hLog;
            m_isShowOutputHovered = hOut;
            m_isCancelHovered = hCan;
            m_isCloseHovered = hClose;
            return true;
        }
        return false;
    }

    bool HandleMouseDown(POINT pt, GitErrorModalAction& outAction) noexcept {
        if (!m_isOpen) return false;
        auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
            return (p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom);
        };

        if (inRect(pt, m_gitLogBtnBounds)) {
            outAction = GitErrorModalAction::OpenGitLog;
            Close();
            return true;
        }
        if (inRect(pt, m_showOutputBtnBounds)) {
            outAction = GitErrorModalAction::ShowCommandOutput;
            Close();
            return true;
        }
        if (inRect(pt, m_cancelBtnBounds) || inRect(pt, m_closeBtnBounds)) {
            outAction = GitErrorModalAction::Cancel;
            Close();
            return true;
        }

        // Clicking outside card also cancels dialog
        if (!inRect(pt, m_cardBounds)) {
            outAction = GitErrorModalAction::Cancel;
            Close();
            return true;
        }

        return false;
    }

    bool HandleKeyDown(WPARAM wParam, GitErrorModalAction& outAction) noexcept {
        if (!m_isOpen) return false;
        if (wParam == VK_ESCAPE) {
            outAction = GitErrorModalAction::Cancel;
            Close();
            return true;
        }
        if (wParam == VK_RETURN) {
            outAction = GitErrorModalAction::OpenGitLog;
            Close();
            return true;
        }
        return false;
    }

private:
    bool m_isOpen = false;
    std::wstring m_command;
    std::wstring m_output;
    std::wstring m_repoDir;
    std::wstring m_title;
    std::wstring m_details;

    D2D1_RECT_F m_cardBounds{};
    D2D1_RECT_F m_titleBounds{};
    D2D1_RECT_F m_detailsBounds{};
    D2D1_RECT_F m_gitLogBtnBounds{};
    D2D1_RECT_F m_showOutputBtnBounds{};
    D2D1_RECT_F m_cancelBtnBounds{};
    D2D1_RECT_F m_closeBtnBounds{};

    bool m_isGitLogHovered = false;
    bool m_isShowOutputHovered = false;
    bool m_isCancelHovered = false;
    bool m_isCloseHovered = false;
};

} // namespace phide::ui
