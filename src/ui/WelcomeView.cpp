/**
 * @file WelcomeView.cpp
 * @brief Implementation of the Welcome Walkthrough surface with collapsible accordion steps.
 */

#include "phide/ui/WelcomeView.hpp"
#include <algorithm>
#include <cmath>

namespace phide::ui {

static bool PtInRectF(POINT pt, const D2D1_RECT_F& rc) noexcept {
    return pt.x >= rc.left && pt.x <= rc.right && pt.y >= rc.top && pt.y <= rc.bottom;
}

static constexpr const wchar_t* s_fontDisplayNames[5] = {
    L"Koda Mono  (Recommended)",
    L"Consolas",
    L"Cascadia Code",
    L"Courier New",
    L"Lucida Console"
};

WelcomeView::WelcomeView() = default;

bool WelcomeView::HandleMouseMove(POINT clientPt) {
    int oldHoverHeader = m_hoveredStepHeader;
    int oldHoverTheme = m_hoveredThemeCard;
    int oldHoverFont = m_hoveredFontOption;
    bool oldHoverBrowse = m_hoveredBrowseTheme;
    bool oldHoverMore = m_hoveredSeeMoreThemes;
    bool oldHoverGuide = m_hoveredInteractiveGuide;
    bool oldHoverDone = m_hoveredMarkDone;
    bool oldHoverNext = m_hoveredNextSection;

    // Check step headers
    m_hoveredStepHeader = -1;
    for (int i = 0; i < 3; ++i) {
        if (PtInRectF(clientPt, m_stepHeaderBounds[i])) {
            m_hoveredStepHeader = i;
            break;
        }
    }

    // Step-specific hovers
    m_hoveredThemeCard = -1;
    m_hoveredBrowseTheme = false;
    m_hoveredSeeMoreThemes = false;
    m_hoveredFontOption = -1;
    m_hoveredInteractiveGuide = false;
    m_hoveredNextSection = false;

    if (m_activeStep == 0) {
        for (int i = 0; i < 4; ++i) {
            if (PtInRectF(clientPt, m_themeCardBounds[i])) {
                m_hoveredThemeCard = i;
                break;
            }
        }
        m_hoveredBrowseTheme = PtInRectF(clientPt, m_browseThemeBtnBounds);
        m_hoveredSeeMoreThemes = PtInRectF(clientPt, m_seeMoreThemesBounds);
        m_hoveredNextSection = PtInRectF(clientPt, m_nextSectionBtnBounds[0]);
    } else if (m_activeStep == 1) {
        for (int i = 0; i < 5; ++i) {
            if (PtInRectF(clientPt, m_fontOptionBounds[i])) {
                m_hoveredFontOption = i;
                break;
            }
        }
        m_hoveredNextSection = PtInRectF(clientPt, m_nextSectionBtnBounds[1]);
    } else if (m_activeStep == 2) {
        m_hoveredInteractiveGuide = PtInRectF(clientPt, m_interactiveGuideBtnBounds);
    }

    m_hoveredMarkDone = PtInRectF(clientPt, m_markDoneBtnBounds);

    return (m_hoveredStepHeader != oldHoverHeader ||
            m_hoveredThemeCard != oldHoverTheme ||
            m_hoveredFontOption != oldHoverFont ||
            m_hoveredBrowseTheme != oldHoverBrowse ||
            m_hoveredSeeMoreThemes != oldHoverMore ||
            m_hoveredInteractiveGuide != oldHoverGuide ||
            m_hoveredMarkDone != oldHoverDone ||
            m_hoveredNextSection != oldHoverNext);
}

bool WelcomeView::HandleLButtonDown(POINT clientPt) {
    // 1. Step Header clicks (expands the clicked step and collapses others)
    for (int i = 0; i < 3; ++i) {
        if (PtInRectF(clientPt, m_stepHeaderBounds[i])) {
            if (m_activeStep != i) {
                m_activeStep = i;
                return true;
            }
        }
    }

    // 2. Active Step 0: Theme Picker & Controls
    if (m_activeStep == 0) {
        for (int i = 0; i < 4; ++i) {
            if (PtInRectF(clientPt, m_themeCardBounds[i])) {
                m_stepCompleted[0] = true;
                if (m_onThemeSelected) {
                    m_onThemeSelected(s_themeCardModes[i]);
                }
                return true;
            }
        }

        if (PtInRectF(clientPt, m_browseThemeBtnBounds) || PtInRectF(clientPt, m_seeMoreThemesBounds)) {
            m_stepCompleted[0] = true;
            if (m_onOpenThemePicker) {
                m_onOpenThemePicker();
            }
            return true;
        }

        if (PtInRectF(clientPt, m_nextSectionBtnBounds[0])) {
            m_stepCompleted[0] = true;
            m_activeStep = 1;
            return true;
        }
    }

    // 3. Active Step 1: Font Picker & Controls
    if (m_activeStep == 1) {
        for (int i = 0; i < 5; ++i) {
            if (PtInRectF(clientPt, m_fontOptionBounds[i])) {
                m_stepCompleted[1] = true;
                if (m_onFontSelected) {
                    m_onFontSelected(s_fontNames[i]);
                }
                return true;
            }
        }

        if (PtInRectF(clientPt, m_nextSectionBtnBounds[1])) {
            m_stepCompleted[1] = true;
            m_activeStep = 2;
            return true;
        }
    }

    // 4. Active Step 2: Interactive Guide
    if (m_activeStep == 2) {
        if (PtInRectF(clientPt, m_interactiveGuideBtnBounds)) {
            m_stepCompleted[2] = true;
            if (m_onOpenInteractiveGuide) {
                m_onOpenInteractiveGuide();
            }
            return true;
        }
    }

    // 5. Mark Done -> Close Welcome Tab
    if (PtInRectF(clientPt, m_markDoneBtnBounds)) {
        if (m_onClose) {
            m_onClose();
        }
        return true;
    }

    return false;
}

bool WelcomeView::HandleLButtonUp(POINT /*clientPt*/) {
    return false;
}

bool WelcomeView::HandleMouseWheel(short delta, float /*viewHeight*/) {
    float prevScroll = m_scrollY;
    float scrollAmount = (delta > 0 ? -1.0f : 1.0f) * 60.0f;
    m_scrollY = (std::max)(0.0f, (std::min)(m_scrollY + scrollAmount, m_maxScrollY));
    return (m_scrollY != prevScroll);
}

LPCWSTR WelcomeView::GetCursorForPoint(POINT pt) const {
    if (m_hoveredStepHeader >= 0 ||
        m_hoveredThemeCard >= 0 ||
        m_hoveredFontOption >= 0 ||
        m_hoveredBrowseTheme ||
        m_hoveredSeeMoreThemes ||
        m_hoveredInteractiveGuide ||
        m_hoveredMarkDone ||
        m_hoveredNextSection) {
        return IDC_HAND;
    }
    return nullptr;
}

void WelcomeView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                         float left, float width, float top, float height) {
    if (!pRT || width <= 10.0f || height <= 10.0f) return;

    D2D1_RECT_F viewBounds = D2D1::RectF(left, top, left + width, top + height);
    pRT->PushAxisAlignedClip(viewBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // 1. Solid Canvas Background
    if (theme.brushCanvasBg) {
        pRT->FillRectangle(viewBounds, theme.brushCanvasBg.Get());
    }

    // Helper solid brushes
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushAccent;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushAccentHover;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardSelected;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBorderSubtle;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTextMuted;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBadgeBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLineGutterBg;

    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.478f, 0.800f, 1.0f), &brushAccent);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.54f, 0.88f, 1.0f), &brushAccentHover);
    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.035f), &brushCardBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.478f, 0.800f, 0.16f), &brushCardSelected);
    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f), &brushBorderSubtle);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.55f, 0.55f, 1.0f), &brushTextMuted);
    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.06f), &brushBadgeBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.08f, 0.85f), &brushLineGutterBg);

    // DirectWrite Text Formats - Dedicated to WelcomeView to strictly prevent text leaking or centering bugs
    IDWriteFactory* pDWriteFactory = theme.dwriteFactory.Get();
    if (!pDWriteFactory) {
        pRT->PopAxisAlignedClip();
        return;
    }

    const wchar_t* uiFontFamily = L"Segoe UI";

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtHeader;
    pDWriteFactory->CreateTextFormat(uiFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(20.0f), L"en-us", &fmtHeader);
    if (fmtHeader) {
        fmtHeader->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fmtHeader->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmtHeader->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtStepTitle;
    pDWriteFactory->CreateTextFormat(uiFontFamily, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(13.5f), L"en-us", &fmtStepTitle);
    if (fmtStepTitle) {
        fmtStepTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fmtStepTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmtStepTitle->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // MULTI-LINE DESCRIPTION FORMAT WITH WORD WRAP ENABLED
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtDesc;
    pDWriteFactory->CreateTextFormat(uiFontFamily, nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(12.0f), L"en-us", &fmtDesc);
    if (fmtDesc) {
        fmtDesc->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fmtDesc->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        fmtDesc->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtBtn;
    pDWriteFactory->CreateTextFormat(uiFontFamily, nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(12.0f), L"en-us", &fmtBtn);
    if (fmtBtn) {
        fmtBtn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmtBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmtBtn->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtFontItem;
    pDWriteFactory->CreateTextFormat(uiFontFamily, nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(12.5f), L"en-us", &fmtFontItem);
    if (fmtFontItem) {
        fmtFontItem->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fmtFontItem->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmtFontItem->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCode;
    pDWriteFactory->CreateTextFormat(config.fontFamily.c_str(), nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(12.0f), L"en-us", &fmtCode);
    if (fmtCode) {
        fmtCode->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fmtCode->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmtCode->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    // Responsive container calculation (max width 940px, centered)
    const float maxContentW = config.ScaleF(940.0f);
    const float contentW = (std::min)(width - config.ScaleF(48.0f), maxContentW);
    const float startX = left + (std::max)(config.ScaleF(24.0f), (width - contentW) / 2.0f);
    float curY = top + config.ScaleF(32.0f) - m_scrollY;

    // 2. Header Section
    // Title: "Get started with Phide"
    const wchar_t* titleStr = L"Get started with Phide";
    D2D1_RECT_F rcTitle = D2D1::RectF(startX, curY, startX + contentW, curY + config.ScaleF(32.0f));
    pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)), fmtHeader.Get(), rcTitle, theme.brushTextWhite.Get());
    curY += config.ScaleF(34.0f);

    // Subtitle: "Customize your editor, learn the basics, and start coding"
    const wchar_t* subStr = L"Customize your editor, learn the basics, and start coding";
    D2D1_RECT_F rcSub = D2D1::RectF(startX, curY, startX + contentW, curY + config.ScaleF(22.0f));
    pRT->DrawText(subStr, static_cast<UINT32>(wcslen(subStr)), fmtDesc.Get(), rcSub, brushTextMuted.Get());
    curY += config.ScaleF(32.0f);

    // 3. Two-Column Layout
    const float colGap = config.ScaleF(36.0f);
    const float leftColW = (contentW - colGap) * 0.48f;
    const float rightColW = contentW - colGap - leftColW;
    const float leftColX = startX;
    const float rightColX = startX + leftColW + colGap;

    float leftY = curY;
    float rightY = curY;

    // Reset bounds that may be hidden in inactive steps
    m_browseThemeBtnBounds = D2D1::RectF(0, 0, 0, 0);
    m_seeMoreThemesBounds = D2D1::RectF(0, 0, 0, 0);
    m_interactiveGuideBtnBounds = D2D1::RectF(0, 0, 0, 0);
    m_nextSectionBtnBounds[0] = D2D1::RectF(0, 0, 0, 0);
    m_nextSectionBtnBounds[1] = D2D1::RectF(0, 0, 0, 0);
    for (int i = 0; i < 4; ++i) m_themeCardBounds[i] = D2D1::RectF(0, 0, 0, 0);
    for (int i = 0; i < 5; ++i) m_fontOptionBounds[i] = D2D1::RectF(0, 0, 0, 0);

    // =========================================================================
    // LEFT COLUMN: Accordion Steps (Only 1 Active/Expanded, Others Collapsed)
    // =========================================================================

    const wchar_t* stepTitles[3] = {
        L"Choose your theme",
        L"Choose your font",
        L"Interactive Feature Guide"
    };

    for (int stepIdx = 0; stepIdx < 3; ++stepIdx) {
        bool isActive = (m_activeStep == stepIdx);
        bool isDone = m_stepCompleted[stepIdx];
        bool isHeaderHovered = (m_hoveredStepHeader == stepIdx);

        if (isActive) {
            // -------------------------------------------------------------
            // EXPANDED ACTIVE STEP CONTAINER
            // -------------------------------------------------------------
            float boxTop = leftY;
            float boxContentH = 0.0f;

            if (stepIdx == 0) {
                boxContentH = config.ScaleF(180.0f);
            } else if (stepIdx == 1) {
                boxContentH = config.ScaleF(265.0f);
            } else {
                boxContentH = config.ScaleF(135.0f);
            }

            D2D1_RECT_F rcBox = D2D1::RectF(leftColX, boxTop, leftColX + leftColW, boxTop + boxContentH);
            pRT->FillRoundedRectangle(D2D1::RoundedRect(rcBox, 6.0f, 6.0f), brushCardBg.Get());
            pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcBox, 6.0f, 6.0f), brushBorderSubtle.Get(), 1.0f);

            // Step Header clickable hit-test
            float headerH = config.ScaleF(34.0f);
            m_stepHeaderBounds[stepIdx] = D2D1::RectF(rcBox.left, rcBox.top, rcBox.right, rcBox.top + headerH);

            // Radio/Check Indicator
            float iconX = rcBox.left + config.ScaleF(20.0f);
            float iconY = rcBox.top + config.ScaleF(18.0f);
            float iconR = config.ScaleF(7.5f);
            D2D1_ELLIPSE elIcon = D2D1::Ellipse(D2D1::Point2F(iconX, iconY), iconR, iconR);

            if (isDone) {
                // Completed: blue filled circle with white checkmark
                pRT->FillEllipse(elIcon, brushAccent.Get());
                pRT->DrawLine(D2D1::Point2F(iconX - 3.5f, iconY), D2D1::Point2F(iconX - 1.0f, iconY + 2.8f), theme.brushTextWhite.Get(), 1.5f);
                pRT->DrawLine(D2D1::Point2F(iconX - 1.0f, iconY + 2.8f), D2D1::Point2F(iconX + 4.0f, iconY - 3.0f), theme.brushTextWhite.Get(), 1.5f);
            } else {
                // Active but not yet checked: blue accent outer ring + inner dot
                pRT->DrawEllipse(elIcon, brushAccent.Get(), 1.5f);
                D2D1_ELLIPSE elDot = D2D1::Ellipse(D2D1::Point2F(iconX, iconY), config.ScaleF(3.2f), config.ScaleF(3.2f));
                pRT->FillEllipse(elDot, brushAccent.Get());
            }

            // Title (Left-aligned right next to icon)
            D2D1_RECT_F rcStepTitle = D2D1::RectF(
                rcBox.left + config.ScaleF(36.0f),
                rcBox.top + config.ScaleF(7.0f),
                rcBox.right - config.ScaleF(16.0f),
                rcBox.top + config.ScaleF(29.0f)
            );
            pRT->DrawText(stepTitles[stepIdx], static_cast<UINT32>(wcslen(stepTitles[stepIdx])),
                          fmtStepTitle.Get(), rcStepTitle, theme.brushTextWhite.Get());

            // --- STEP-SPECIFIC BODY ---
            if (stepIdx == 0) {
                // Multi-line Description (Strictly wrapped, no leaking)
                const wchar_t* descTheme = L"The right theme helps you focus on your code, is easy on your eyes, and is simply more fun to use.";
                D2D1_RECT_F rcDesc = D2D1::RectF(
                    rcBox.left + config.ScaleF(24.0f),
                    rcBox.top + config.ScaleF(36.0f),
                    rcBox.right - config.ScaleF(24.0f),
                    rcBox.top + config.ScaleF(78.0f)
                );
                pRT->DrawText(descTheme, static_cast<UINT32>(wcslen(descTheme)), fmtDesc.Get(), rcDesc, brushTextMuted.Get());

                // Button: [ Browse Color Themes ]
                const float btnW = config.ScaleF(160.0f);
                const float btnH = config.ScaleF(30.0f);
                m_browseThemeBtnBounds = D2D1::RectF(
                    rcBox.left + config.ScaleF(24.0f),
                    rcBox.top + config.ScaleF(84.0f),
                    rcBox.left + config.ScaleF(24.0f) + btnW,
                    rcBox.top + config.ScaleF(84.0f) + btnH
                );
                pRT->FillRoundedRectangle(D2D1::RoundedRect(m_browseThemeBtnBounds, 4.0f, 4.0f),
                                          m_hoveredBrowseTheme ? brushAccentHover.Get() : brushAccent.Get());
                const wchar_t* btnBrowseText = L"Browse Color Themes";
                pRT->DrawText(btnBrowseText, static_cast<UINT32>(wcslen(btnBrowseText)), fmtBtn.Get(), m_browseThemeBtnBounds, theme.brushTextWhite.Get());

                // Tip + shortcut keycaps (Dynamically measured to prevent any layering/collision)
                float tipY = rcBox.top + config.ScaleF(128.0f);
                const wchar_t* tipPrefix = L"Tip: Use keyboard shortcut";

                float tipPrefixW = config.ScaleF(160.0f);
                Microsoft::WRL::ComPtr<IDWriteTextLayout> pTipLayout;
                if (SUCCEEDED(pDWriteFactory->CreateTextLayout(tipPrefix, static_cast<UINT32>(wcslen(tipPrefix)),
                                                             fmtDesc.Get(), 300.0f, 24.0f, &pTipLayout))) {
                    DWRITE_TEXT_METRICS tm{};
                    pTipLayout->GetMetrics(&tm);
                    tipPrefixW = tm.widthIncludingTrailingWhitespace;
                }

                D2D1_RECT_F rcTip = D2D1::RectF(rcBox.left + config.ScaleF(24.0f), tipY,
                                               rcBox.left + config.ScaleF(24.0f) + tipPrefixW, tipY + config.ScaleF(20.0f));
                pRT->DrawText(tipPrefix, static_cast<UINT32>(wcslen(tipPrefix)), fmtDesc.Get(), rcTip, brushTextMuted.Get());

                // Keycap [Ctrl+K]
                float k1X = rcTip.right + config.ScaleF(8.0f);
                D2D1_RECT_F rcK1 = D2D1::RectF(k1X, tipY - config.ScaleF(1.0f), k1X + config.ScaleF(48.0f), tipY + config.ScaleF(17.0f));
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcK1, 3.0f, 3.0f), brushBadgeBg.Get());
                pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcK1, 3.0f, 3.0f), brushBorderSubtle.Get(), 1.0f);
                pRT->DrawText(L"Ctrl+K", 6, fmtBtn.Get(), rcK1, theme.brushTextWhite.Get());

                // Keycap [Ctrl+T]
                float k2X = rcK1.right + config.ScaleF(6.0f);
                D2D1_RECT_F rcK2 = D2D1::RectF(k2X, tipY - config.ScaleF(1.0f), k2X + config.ScaleF(48.0f), tipY + config.ScaleF(17.0f));
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcK2, 3.0f, 3.0f), brushBadgeBg.Get());
                pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcK2, 3.0f, 3.0f), brushBorderSubtle.Get(), 1.0f);
                pRT->DrawText(L"Ctrl+T", 6, fmtBtn.Get(), rcK2, theme.brushTextWhite.Get());

                // Next Section link at bottom right
                const wchar_t* nextStr = L"Next Section \u2192";
                const float nextW = config.ScaleF(105.0f);
                const float nextH = config.ScaleF(20.0f);
                m_nextSectionBtnBounds[0] = D2D1::RectF(
                    rcBox.right - nextW - config.ScaleF(16.0f),
                    rcBox.bottom - nextH - config.ScaleF(10.0f),
                    rcBox.right - config.ScaleF(16.0f),
                    rcBox.bottom - config.ScaleF(10.0f)
                );
                pRT->DrawText(nextStr, static_cast<UINT32>(wcslen(nextStr)), fmtFontItem.Get(), m_nextSectionBtnBounds[0],
                              m_hoveredNextSection ? brushAccentHover.Get() : brushAccent.Get());
            } else if (stepIdx == 1) {
                // Multi-line Description (Strictly wrapped)
                const wchar_t* descFont = L"Select the typeface for your code editor. Synchronized directly with your settings.";
                D2D1_RECT_F rcDesc = D2D1::RectF(
                    rcBox.left + config.ScaleF(24.0f),
                    rcBox.top + config.ScaleF(34.0f),
                    rcBox.right - config.ScaleF(24.0f),
                    rcBox.top + config.ScaleF(68.0f)
                );
                pRT->DrawText(descFont, static_cast<UINT32>(wcslen(descFont)), fmtDesc.Get(), rcDesc, brushTextMuted.Get());

                // 5 Clean Font Choice Rows (User specifically removed the cluttered sample code from inside these rows)
                const float rowH = config.ScaleF(30.0f);
                const float rowSpacing = config.ScaleF(4.0f);
                float rowY = rcBox.top + config.ScaleF(72.0f);

                for (int f = 0; f < 5; ++f) {
                    bool isCurrent = (config.fontFamily == s_fontNames[f]);
                    bool isRowHovered = (m_hoveredFontOption == f);

                    D2D1_RECT_F rcRow = D2D1::RectF(
                        rcBox.left + config.ScaleF(14.0f),
                        rowY,
                        rcBox.right - config.ScaleF(14.0f),
                        rowY + rowH
                    );
                    m_fontOptionBounds[f] = rcRow;

                    if (isCurrent) {
                        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcRow, 4.0f, 4.0f), brushCardSelected.Get());
                        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcRow, 4.0f, 4.0f), brushAccent.Get(), 1.2f);
                    } else if (isRowHovered) {
                        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcRow, 4.0f, 4.0f), brushBadgeBg.Get());
                        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcRow, 4.0f, 4.0f),
                                                  theme.brushBorderHover ? theme.brushBorderHover.Get() : brushBorderSubtle.Get(), 1.0f);
                    } else {
                        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcRow, 4.0f, 4.0f), brushBorderSubtle.Get(), 0.7f);
                    }

                    // Radio circle
                    float rX = rcRow.left + config.ScaleF(16.0f);
                    float rY = rcRow.top + rowH / 2.0f;
                    float rR = config.ScaleF(5.0f);
                    D2D1_ELLIPSE elOptRadio = D2D1::Ellipse(D2D1::Point2F(rX, rY), rR, rR);

                    if (isCurrent) {
                        pRT->FillEllipse(elOptRadio, brushAccent.Get());
                        D2D1_ELLIPSE elOptDot = D2D1::Ellipse(D2D1::Point2F(rX, rY), config.ScaleF(2.0f), config.ScaleF(2.0f));
                        pRT->FillEllipse(elOptDot, theme.brushTextWhite.Get());
                    } else {
                        pRT->DrawEllipse(elOptRadio, brushTextMuted.Get(), 1.0f);
                    }

                    // Clean Font Name with ample room, no overflow
                    D2D1_RECT_F rcFontName = D2D1::RectF(
                        rcRow.left + config.ScaleF(32.0f),
                        rcRow.top + config.ScaleF(4.0f),
                        rcRow.right - config.ScaleF(12.0f),
                        rcRow.bottom - config.ScaleF(4.0f)
                    );
                    pRT->DrawText(s_fontDisplayNames[f], static_cast<UINT32>(wcslen(s_fontDisplayNames[f])),
                                  fmtFontItem.Get(), rcFontName,
                                  isCurrent ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());

                    rowY += rowH + rowSpacing;
                }

                // Next Section link at bottom right
                const wchar_t* nextStr = L"Next Section \u2192";
                const float nextW = config.ScaleF(105.0f);
                const float nextH = config.ScaleF(20.0f);
                m_nextSectionBtnBounds[1] = D2D1::RectF(
                    rcBox.right - nextW - config.ScaleF(16.0f),
                    rcBox.bottom - nextH - config.ScaleF(8.0f),
                    rcBox.right - config.ScaleF(16.0f),
                    rcBox.bottom - config.ScaleF(8.0f)
                );
                pRT->DrawText(nextStr, static_cast<UINT32>(wcslen(nextStr)), fmtFontItem.Get(), m_nextSectionBtnBounds[1],
                              m_hoveredNextSection ? brushAccentHover.Get() : brushAccent.Get());
            } else if (stepIdx == 2) {
                // Multi-line Description (Strictly wrapped)
                const wchar_t* descGuide = L"Explore multi-cursor editing, live CSS & Tailwind color pickers, and smart IntelliSense.";
                D2D1_RECT_F rcDesc = D2D1::RectF(
                    rcBox.left + config.ScaleF(24.0f),
                    rcBox.top + config.ScaleF(36.0f),
                    rcBox.right - config.ScaleF(24.0f),
                    rcBox.top + config.ScaleF(76.0f)
                );
                pRT->DrawText(descGuide, static_cast<UINT32>(wcslen(descGuide)), fmtDesc.Get(), rcDesc, brushTextMuted.Get());

                // Button: [ Open Interactive Guide ]
                const float btnW = config.ScaleF(175.0f);
                const float btnH = config.ScaleF(30.0f);
                m_interactiveGuideBtnBounds = D2D1::RectF(
                    rcBox.left + config.ScaleF(24.0f),
                    rcBox.top + config.ScaleF(82.0f),
                    rcBox.left + config.ScaleF(24.0f) + btnW,
                    rcBox.top + config.ScaleF(82.0f) + btnH
                );
                pRT->FillRoundedRectangle(D2D1::RoundedRect(m_interactiveGuideBtnBounds, 4.0f, 4.0f),
                                          m_hoveredInteractiveGuide ? brushAccentHover.Get() : brushAccent.Get());
                const wchar_t* btnGuideText = L"Open Interactive Guide";
                pRT->DrawText(btnGuideText, static_cast<UINT32>(wcslen(btnGuideText)), fmtBtn.Get(), m_interactiveGuideBtnBounds, theme.brushTextWhite.Get());
            }

            leftY += boxContentH + config.ScaleF(16.0f);
        } else {
            // -------------------------------------------------------------
            // COLLAPSED STEP ROW (Clean single line with empty circle)
            // -------------------------------------------------------------
            const float rowH = config.ScaleF(34.0f);
            D2D1_RECT_F rcRow = D2D1::RectF(leftColX, leftY, leftColX + leftColW, leftY + rowH);
            m_stepHeaderBounds[stepIdx] = rcRow;

            if (isHeaderHovered) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcRow, 4.0f, 4.0f), brushBadgeBg.Get());
            }

            // Circle icon
            float iconX = rcRow.left + config.ScaleF(20.0f);
            float iconY = rcRow.top + rowH / 2.0f;
            float iconR = config.ScaleF(6.5f);
            D2D1_ELLIPSE elIcon = D2D1::Ellipse(D2D1::Point2F(iconX, iconY), iconR, iconR);

            if (isDone) {
                // Completed: blue filled circle with white checkmark
                pRT->FillEllipse(elIcon, brushAccent.Get());
                pRT->DrawLine(D2D1::Point2F(iconX - 3.0f, iconY), D2D1::Point2F(iconX - 0.8f, iconY + 2.4f), theme.brushTextWhite.Get(), 1.4f);
                pRT->DrawLine(D2D1::Point2F(iconX - 0.8f, iconY + 2.4f), D2D1::Point2F(iconX + 3.5f, iconY - 2.5f), theme.brushTextWhite.Get(), 1.4f);
            } else {
                // Not completed & not active: empty subtle circle
                pRT->DrawEllipse(elIcon, brushTextMuted.Get(), 1.2f);
            }

            // Step Title (Left-aligned)
            D2D1_RECT_F rcTitle = D2D1::RectF(
                rcRow.left + config.ScaleF(36.0f),
                rcRow.top + config.ScaleF(6.0f),
                rcRow.right - config.ScaleF(12.0f),
                rcRow.bottom - config.ScaleF(6.0f)
            );
            pRT->DrawText(stepTitles[stepIdx], static_cast<UINT32>(wcslen(stepTitles[stepIdx])),
                          fmtStepTitle.Get(), rcTitle,
                          isHeaderHovered ? theme.brushTextWhite.Get() : theme.brushTextSecondary.Get());

            leftY += rowH + config.ScaleF(6.0f);
        }
    }

    // --- Bottom Action: [✓ Mark Done] ---
    leftY += config.ScaleF(10.0f);
    {
        const float doneBtnW = config.ScaleF(110.0f);
        const float doneBtnH = config.ScaleF(28.0f);
        m_markDoneBtnBounds = D2D1::RectF(leftColX, leftY, leftColX + doneBtnW, leftY + doneBtnH);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_markDoneBtnBounds, 4.0f, 4.0f),
                                  m_hoveredMarkDone ? brushBadgeBg.Get() : brushCardBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(m_markDoneBtnBounds, 4.0f, 4.0f), brushBorderSubtle.Get(), 0.9f);
        const wchar_t* doneText = L"\u2713 Mark Done";
        pRT->DrawText(doneText, static_cast<UINT32>(wcslen(doneText)), fmtBtn.Get(), m_markDoneBtnBounds, theme.brushTextWhite.Get());

        leftY += doneBtnH + config.ScaleF(36.0f);
    }

    // =========================================================================
    // RIGHT COLUMN: Dynamic Preview Corresponding to the Active Step
    // =========================================================================

    if (m_activeStep == 0) {
        // -------------------------------------------------------------
        // STEP 0 PREVIEW: 2x2 Grid of Theme Preview Cards + Catalog Link
        // -------------------------------------------------------------
        const float cardGap = config.ScaleF(14.0f);
        const float cardW = (rightColW - cardGap) / 2.0f;
        const float previewH = config.ScaleF(105.0f);
        const float labelH = config.ScaleF(30.0f);
        const float cardH = previewH + labelH;

        ThemeMode currentMode = theme.GetThemeMode();

        for (int i = 0; i < 4; ++i) {
            int row = i / 2;
            int col = i % 2;

            float cx = rightColX + col * (cardW + cardGap);
            float cy = rightY + row * (cardH + cardGap);

            D2D1_RECT_F rcCard = D2D1::RectF(cx, cy, cx + cardW, cy + cardH);
            m_themeCardBounds[i] = rcCard;

            bool isSelected = (currentMode == s_themeCardModes[i]);
            bool isHovered = (m_hoveredThemeCard == i);

            // Miniature IDE illustration
            D2D1_RECT_F rcPreviewBox = D2D1::RectF(cx, cy, cx + cardW, cy + previewH);
            ThemePickerModal::DrawThemePreviewForMode(pRT, config, s_themeCardModes[i], rcPreviewBox);

            // Bottom label area
            D2D1_RECT_F rcLabelRow = D2D1::RectF(cx, cy + previewH, cx + cardW, cy + cardH);
            pRT->FillRectangle(rcLabelRow, brushCardBg.Get());

            // Theme Radio Icon
            float radioX = cx + config.ScaleF(14.0f);
            float radioY = cy + previewH + labelH / 2.0f;
            float radioR = config.ScaleF(5.0f);
            D2D1_ELLIPSE elCardRadio = D2D1::Ellipse(D2D1::Point2F(radioX, radioY), radioR, radioR);

            if (isSelected) {
                pRT->FillEllipse(elCardRadio, brushAccent.Get());
                D2D1_ELLIPSE elCardDot = D2D1::Ellipse(D2D1::Point2F(radioX, radioY), config.ScaleF(2.0f), config.ScaleF(2.0f));
                pRT->FillEllipse(elCardDot, theme.brushTextWhite.Get());
            } else {
                pRT->DrawEllipse(elCardRadio, brushTextMuted.Get(), 1.0f);
            }

            // Theme Name (Left-aligned)
            D2D1_RECT_F rcThemeName = D2D1::RectF(
                cx + config.ScaleF(26.0f),
                cy + previewH + config.ScaleF(4.0f),
                cx + cardW - config.ScaleF(8.0f),
                cy + cardH - config.ScaleF(4.0f)
            );
            pRT->DrawText(s_themeCardNames[i], static_cast<UINT32>(wcslen(s_themeCardNames[i])),
                          fmtFontItem.Get(), rcThemeName,
                          isSelected ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());

            // Card Border
            D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, 5.0f, 5.0f);
            if (isSelected) {
                pRT->DrawRoundedRectangle(rrcCard, brushAccent.Get(), 2.0f);
            } else if (isHovered) {
                pRT->DrawRoundedRectangle(rrcCard, theme.brushBorderHover ? theme.brushBorderHover.Get() : brushBorderSubtle.Get(), 1.3f);
            } else {
                pRT->DrawRoundedRectangle(rrcCard, brushBorderSubtle.Get(), 1.0f);
            }
        }

        rightY += (cardH * 2.0f) + cardGap + config.ScaleF(12.0f);

        // Clickable link: "See More Themes..." -> opens full 20-Theme Catalog Modal
        const wchar_t* moreThemesStr = L"See More Themes...";
        const float linkW = config.ScaleF(135.0f);
        const float linkH = config.ScaleF(22.0f);
        m_seeMoreThemesBounds = D2D1::RectF(
            rightColX + rightColW - linkW,
            rightY,
            rightColX + rightColW,
            rightY + linkH
        );
        pRT->DrawText(moreThemesStr, static_cast<UINT32>(wcslen(moreThemesStr)), fmtFontItem.Get(),
                      m_seeMoreThemesBounds,
                      m_hoveredSeeMoreThemes ? brushAccentHover.Get() : brushAccent.Get());

        if (m_hoveredSeeMoreThemes) {
            pRT->DrawLine(
                D2D1::Point2F(m_seeMoreThemesBounds.left, m_seeMoreThemesBounds.bottom - 2.0f),
                D2D1::Point2F(m_seeMoreThemesBounds.right, m_seeMoreThemesBounds.bottom - 2.0f),
                brushAccentHover.Get(), 1.2f
            );
        }

        rightY += linkH + config.ScaleF(24.0f);
    } else if (m_activeStep == 1) {
        // -------------------------------------------------------------
        // STEP 1 PREVIEW: Live Code Editor Typography Preview Window
        // -------------------------------------------------------------
        const float previewBoxH = config.ScaleF(265.0f);
        D2D1_RECT_F rcWindow = D2D1::RectF(rightColX, rightY, rightColX + rightColW, rightY + previewBoxH);

        // Window background & border with clip so no text can EVER leak
        pRT->PushAxisAlignedClip(rcWindow, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcWindow, 6.0f, 6.0f), brushCardBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcWindow, 6.0f, 6.0f), brushBorderSubtle.Get(), 1.0f);

        // Window Title Bar (Header)
        float winHeaderH = config.ScaleF(32.0f);
        D2D1_RECT_F rcWinHeader = D2D1::RectF(rcWindow.left, rcWindow.top, rcWindow.right, rcWindow.top + winHeaderH);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcWinHeader, 6.0f, 6.0f), brushBadgeBg.Get());
        pRT->DrawLine(D2D1::Point2F(rcWindow.left, rcWindow.top + winHeaderH),
                      D2D1::Point2F(rcWindow.right, rcWindow.top + winHeaderH),
                      brushBorderSubtle.Get(), 1.0f);

        // Window Dots (red, yellow, green)
        float dotY = rcWindow.top + winHeaderH / 2.0f;
        float dotR = config.ScaleF(4.0f);
        D2D1_COLOR_F dotColors[3] = {
            D2D1::ColorF(0.95f, 0.35f, 0.35f, 0.9f),
            D2D1::ColorF(0.95f, 0.75f, 0.25f, 0.9f),
            D2D1::ColorF(0.35f, 0.85f, 0.45f, 0.9f)
        };
        for (int d = 0; d < 3; ++d) {
            float dx = rcWindow.left + config.ScaleF(14.0f) + d * config.ScaleF(12.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bDot;
            pRT->CreateSolidColorBrush(dotColors[d], &bDot);
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dx, dotY), dotR, dotR), bDot.Get());
        }

        // Active File Tab Label
        const wchar_t* tabTitle = L"TypographyPreview.ts";
        D2D1_RECT_F rcTab = D2D1::RectF(rcWindow.left + config.ScaleF(56.0f), rcWindow.top + config.ScaleF(6.0f),
                                       rcWindow.left + config.ScaleF(200.0f), rcWindow.top + winHeaderH - config.ScaleF(6.0f));
        pRT->DrawText(tabTitle, static_cast<UINT32>(wcslen(tabTitle)), fmtFontItem.Get(), rcTab, theme.brushTextWhite.Get());

        // Font Badge Pill on the Right (Measured dynamically to never overflow)
        std::wstring fontPill = L"Font: " + config.fontFamily + L" \u2022 15px";
        float pillW = config.ScaleF(170.0f);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pPillLayout;
        if (SUCCEEDED(pDWriteFactory->CreateTextLayout(fontPill.c_str(), static_cast<UINT32>(fontPill.length()),
                                                     fmtBtn.Get(), 300.0f, 24.0f, &pPillLayout))) {
            DWRITE_TEXT_METRICS tm{};
            pPillLayout->GetMetrics(&tm);
            pillW = tm.widthIncludingTrailingWhitespace + config.ScaleF(16.0f);
        }

        D2D1_RECT_F rcPill = D2D1::RectF(rcWindow.right - pillW - config.ScaleF(10.0f), rcWindow.top + config.ScaleF(5.0f),
                                        rcWindow.right - config.ScaleF(10.0f), rcWindow.top + winHeaderH - config.ScaleF(5.0f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcPill, 3.0f, 3.0f), brushBadgeBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcPill, 3.0f, 3.0f), brushBorderSubtle.Get(), 0.8f);
        pRT->DrawText(fontPill.c_str(), static_cast<UINT32>(fontPill.length()), fmtBtn.Get(), rcPill, brushTextMuted.Get());

        // Editor Surface with Gutter and Syntax Highlighted Lines (Carefully sized to never leak horizontally)
        float gutterW = config.ScaleF(32.0f);
        D2D1_RECT_F rcGutter = D2D1::RectF(rcWindow.left, rcWindow.top + winHeaderH, rcWindow.left + gutterW, rcWindow.bottom);
        pRT->FillRectangle(rcGutter, brushLineGutterBg.Get());
        pRT->DrawLine(D2D1::Point2F(rcWindow.left + gutterW, rcWindow.top + winHeaderH),
                      D2D1::Point2F(rcWindow.left + gutterW, rcWindow.bottom),
                      brushBorderSubtle.Get(), 1.0f);

        struct CodePreviewLine {
            const wchar_t* num;
            const wchar_t* code;
            bool isCurrent = false;
        };

        const CodePreviewLine codeLines[] = {
            { L"1", L"// Phide Typography Engine", false },
            { L"2", L"import { IDETheme } from '@phide';", false },
            { L"3", L"", false },
            { L"4", L"function startEngine(): boolean {", false },
            { L"5", L"    const active = true; // active", true },
            { L"6", L"    console.log('Ready to code');", false },
            { L"7", L"    return active;", false },
            { L"8", L"}", false },
            { L"9", L"export default startEngine;", false }
        };

        float lineH = config.ScaleF(23.0f);
        float lineY = rcWindow.top + winHeaderH + config.ScaleF(8.0f);

        for (const auto& cl : codeLines) {
            if (lineY + lineH > rcWindow.bottom - config.ScaleF(6.0f)) break;

            if (cl.isCurrent && theme.brushCurrentLineBg) {
                D2D1_RECT_F rcLineBg = D2D1::RectF(rcWindow.left + gutterW + 1.0f, lineY, rcWindow.right - 1.0f, lineY + lineH);
                pRT->FillRectangle(rcLineBg, theme.brushCurrentLineBg.Get());
            }

            // Gutter Line Number
            D2D1_RECT_F rcNum = D2D1::RectF(rcWindow.left, lineY, rcWindow.left + gutterW - config.ScaleF(6.0f), lineY + lineH);
            pRT->DrawText(cl.num, static_cast<UINT32>(wcslen(cl.num)), fmtCode.Get(), rcNum,
                          cl.isCurrent ? theme.brushTextWhite.Get() : theme.brushLineNumber.Get());

            // Code Line
            D2D1_RECT_F rcText = D2D1::RectF(rcWindow.left + gutterW + config.ScaleF(8.0f), lineY, rcWindow.right - config.ScaleF(8.0f), lineY + lineH);
            pRT->DrawText(cl.code, static_cast<UINT32>(wcslen(cl.code)), fmtCode.Get(), rcText,
                          cl.isCurrent ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());

            lineY += lineH;
        }

        pRT->PopAxisAlignedClip();
        rightY += previewBoxH + config.ScaleF(24.0f);
    } else if (m_activeStep == 2) {
        // -------------------------------------------------------------
        // STEP 2 PREVIEW: Feature Cards (Multi-Cursor, Color Pickers, IntelliSense)
        // -------------------------------------------------------------
        const float previewBoxH = config.ScaleF(230.0f);
        D2D1_RECT_F rcFeatureBox = D2D1::RectF(rightColX, rightY, rightColX + rightColW, rightY + previewBoxH);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcFeatureBox, 6.0f, 6.0f), brushCardBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcFeatureBox, 6.0f, 6.0f), brushBorderSubtle.Get(), 1.0f);

        struct FeatureHighlight {
            const wchar_t* title;
            const wchar_t* desc;
            D2D1_COLOR_F accent;
        };

        const FeatureHighlight highlights[3] = {
            { L"Multi-Cursor Editing", L"Ctrl+Alt+Up/Down box-select and simultaneous edits.", D2D1::ColorF(0.0f, 0.50f, 0.90f) },
            { L"Live CSS & Tailwind Color Picker", L"Inline color swatches with instant hex/rgb editing.", D2D1::ColorF(0.85f, 0.35f, 0.85f) },
            { L"Smart IntelliSense & Tasks", L"Real-time autocomplete, parameter hints, and TODO tasks.", D2D1::ColorF(0.20f, 0.80f, 0.50f) }
        };

        float cardY = rcFeatureBox.top + config.ScaleF(14.0f);
        float cardItemH = config.ScaleF(58.0f);
        for (int h = 0; h < 3; ++h) {
            D2D1_RECT_F rcCardItem = D2D1::RectF(
                rcFeatureBox.left + config.ScaleF(14.0f),
                cardY,
                rcFeatureBox.right - config.ScaleF(14.0f),
                cardY + cardItemH
            );
            pRT->FillRoundedRectangle(D2D1::RoundedRect(rcCardItem, 4.0f, 4.0f), brushBadgeBg.Get());
            pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcCardItem, 4.0f, 4.0f), brushBorderSubtle.Get(), 0.8f);

            // Colored Left Accent Bar
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bBar;
            pRT->CreateSolidColorBrush(highlights[h].accent, &bBar);
            D2D1_RECT_F rcBar = D2D1::RectF(rcCardItem.left, rcCardItem.top, rcCardItem.left + config.ScaleF(3.5f), rcCardItem.bottom);
            pRT->FillRectangle(rcBar, bBar.Get());

            // Title (Left-aligned)
            D2D1_RECT_F rcHTitle = D2D1::RectF(rcCardItem.left + config.ScaleF(14.0f), rcCardItem.top + config.ScaleF(7.0f),
                                              rcCardItem.right - config.ScaleF(10.0f), rcCardItem.top + config.ScaleF(25.0f));
            pRT->DrawText(highlights[h].title, static_cast<UINT32>(wcslen(highlights[h].title)), fmtStepTitle.Get(), rcHTitle, theme.brushTextWhite.Get());

            // Multi-line Description (Strictly wrapped inside card, no leaking)
            D2D1_RECT_F rcHDesc = D2D1::RectF(rcCardItem.left + config.ScaleF(14.0f), rcCardItem.top + config.ScaleF(27.0f),
                                             rcCardItem.right - config.ScaleF(10.0f), rcCardItem.bottom - config.ScaleF(4.0f));
            pRT->DrawText(highlights[h].desc, static_cast<UINT32>(wcslen(highlights[h].desc)), fmtDesc.Get(), rcHDesc, brushTextMuted.Get());

            cardY += cardItemH + config.ScaleF(8.0f);
        }

        rightY += previewBoxH + config.ScaleF(24.0f);
    }

    // Update max scroll boundary
    float totalH = (std::max)(leftY, rightY) + m_scrollY - top;
    m_maxScrollY = (std::max)(0.0f, totalH - height + config.ScaleF(40.0f));

    pRT->PopAxisAlignedClip();
}

} // namespace phide::ui
