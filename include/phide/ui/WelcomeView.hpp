/**
 * @file WelcomeView.hpp
 * @brief Welcome Walkthrough surface offering first-time IDE customization (Theme, Font, Feature guide).
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <functional>
#include <string>
#include <vector>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/ThemePickerModal.hpp"

namespace phide::ui {

/**
 * @class WelcomeView
 * @brief Interactive full-canvas walkthrough tab ("Get started with Antigravity IDE").
 * Allows new and returning users to pick their theme, choose editor fonts, and explore core features.
 */
class WelcomeView {
public:
    WelcomeView();
    ~WelcomeView() = default;

    /**
     * @brief Renders the complete Welcome Walkthrough surface.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float left, float width, float top, float height);

    /**
     * @brief Handles mouse movement and hover detection.
     */
    bool HandleMouseMove(POINT clientPt);

    /**
     * @brief Handles mouse clicks on theme cards, font options, or action buttons.
     */
    bool HandleLButtonDown(POINT clientPt);

    /**
     * @brief Handles mouse button release.
     */
    bool HandleLButtonUp(POINT clientPt);

    /**
     * @brief Handles mouse wheel scrolling with smooth boundary clamping.
     */
    bool HandleMouseWheel(short delta, float viewHeight);

    /**
     * @brief Determines the appropriate cursor icon for mouse coordinates.
     */
    [[nodiscard]] LPCWSTR GetCursorForPoint(POINT pt) const;

    /**
     * @brief Resets the vertical scroll position back to top.
     */
    void ResetScroll() noexcept { m_scrollY = 0.0f; }

    // --- Event Callbacks ---
    void SetOnThemeSelected(std::function<void(ThemeMode mode)> cb) { m_onThemeSelected = std::move(cb); }
    void SetOnFontSelected(std::function<void(const std::wstring& fontName)> cb) { m_onFontSelected = std::move(cb); }
    void SetOnOpenThemePicker(std::function<void()> cb) { m_onOpenThemePicker = std::move(cb); }
    void SetOnOpenInteractiveGuide(std::function<void()> cb) { m_onOpenInteractiveGuide = std::move(cb); }
    void SetOnClose(std::function<void()> cb) { m_onClose = std::move(cb); }

    // --- State Accessors (for tests) ---
    [[nodiscard]] float GetScrollY() const noexcept { return m_scrollY; }
    [[nodiscard]] int GetActiveStep() const noexcept { return m_activeStep; }
    void SetActiveStep(int s) noexcept { if (s >= 0 && s < 3) m_activeStep = s; }
    [[nodiscard]] bool IsStepCompleted(int s) const noexcept { return (s >= 0 && s < 3) ? m_stepCompleted[s] : false; }
    void SetStepCompleted(int s, bool c) noexcept { if (s >= 0 && s < 3) m_stepCompleted[s] = c; }
    [[nodiscard]] D2D1_RECT_F GetStepHeaderBounds(int s) const noexcept {
        return (s >= 0 && s < 3) ? m_stepHeaderBounds[s] : D2D1_RECT_F{};
    }
    [[nodiscard]] D2D1_RECT_F GetNextSectionBounds(int s) const noexcept {
        return (s >= 0 && s < 2) ? m_nextSectionBtnBounds[s] : D2D1_RECT_F{};
    }
    [[nodiscard]] int GetHoveredThemeCard() const noexcept { return m_hoveredThemeCard; }
    [[nodiscard]] int GetHoveredFontOption() const noexcept { return m_hoveredFontOption; }
    [[nodiscard]] D2D1_RECT_F GetBrowseThemeBounds() const noexcept { return m_browseThemeBtnBounds; }
    [[nodiscard]] D2D1_RECT_F GetSeeMoreThemesBounds() const noexcept { return m_seeMoreThemesBounds; }
    [[nodiscard]] D2D1_RECT_F GetThemeCardBounds(int idx) const noexcept {
        return (idx >= 0 && idx < 4) ? m_themeCardBounds[idx] : D2D1_RECT_F{};
    }
    [[nodiscard]] D2D1_RECT_F GetFontOptionBounds(int idx) const noexcept {
        return (idx >= 0 && idx < 5) ? m_fontOptionBounds[idx] : D2D1_RECT_F{};
    }

private:
    float m_scrollY = 0.0f;
    float m_maxScrollY = 0.0f;

    // Accordion State: 0 = Theme, 1 = Font, 2 = Guide
    int m_activeStep = 0;
    bool m_stepCompleted[3] = { false, false, false };

    // Interactive element bounds
    D2D1_RECT_F m_stepHeaderBounds[3]{};
    D2D1_RECT_F m_nextSectionBtnBounds[2]{};
    D2D1_RECT_F m_browseThemeBtnBounds{};
    D2D1_RECT_F m_seeMoreThemesBounds{};
    D2D1_RECT_F m_interactiveGuideBtnBounds{};
    D2D1_RECT_F m_markDoneBtnBounds{};
    D2D1_RECT_F m_themeCardBounds[4]{};
    D2D1_RECT_F m_fontOptionBounds[5]{};

    // Featured theme choices (Image 2)
    static constexpr ThemeMode s_themeCardModes[4] = {
        ThemeMode::Dark,
        ThemeMode::Light,
        ThemeMode::Purple,
        ThemeMode::Dracula
    };

    static constexpr const wchar_t* s_themeCardNames[4] = {
        L"Dark Modern",
        L"Light Modern",
        L"Deep Purple",
        L"Dracula"
    };

    // Supported fonts matching Settings (Image 1)
    static constexpr const wchar_t* s_fontNames[5] = {
        L"Koda Mono",
        L"Consolas",
        L"Cascadia Code",
        L"Courier New",
        L"Lucida Console"
    };

    // Hover states
    int m_hoveredStepHeader = -1;
    bool m_hoveredNextSection = false;
    int m_hoveredThemeCard = -1;
    int m_hoveredFontOption = -1;
    bool m_hoveredBrowseTheme = false;
    bool m_hoveredSeeMoreThemes = false;
    bool m_hoveredInteractiveGuide = false;
    bool m_hoveredMarkDone = false;

    // Callbacks
    std::function<void(ThemeMode mode)> m_onThemeSelected;
    std::function<void(const std::wstring& fontName)> m_onFontSelected;
    std::function<void()> m_onOpenThemePicker;
    std::function<void()> m_onOpenInteractiveGuide;
    std::function<void()> m_onClose;
};

} // namespace phide::ui
