/**
 * @file FloatingPromptBar.hpp
 * @brief Modern floating AI prompt and command pill situated at the bottom of the workspace.
 * 
 * Directly recreates the floating prompt bar from the concept design mockup:
 * [ + Ask AI...                 High v  (mic)  (up-arrow submit) ]
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @enum PromptButtonId
 * @brief Identifies interactive targets within the floating prompt pill.
 */
enum class PromptButtonId {
    None,
    PlusIcon,       ///< Left '+' trigger
    InputArea,      ///< Main text entry field
    ModelSelector,  ///< 'High v' dropdown selector
    VoiceInput,     ///< Microphone voice trigger
    Submit          ///< Circular submit button (up arrow)
};

/**
 * @class FloatingPromptBar
 * @brief Renders the floating AI prompt pill and handles mouse interactions.
 */
class FloatingPromptBar {
public:
    FloatingPromptBar();
    ~FloatingPromptBar() = default;

    /**
     * @brief Paints the floating pill, drop shadow, text, model selector, and submit button.
     * @param pRT Active Direct2D render target.
     * @param theme Visual theme resources.
     * @param config Application configuration.
     * @param canvasLeft Left coordinate of the visible editor canvas.
     * @param canvasWidth Width of the visible editor canvas.
     * @param clientHeight Height of the window client area.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float canvasLeft, float canvasWidth, float clientHeight);

    /**
     * @brief Performs hit-testing on the floating prompt bar.
     * @param pt Mouse point in client coordinates.
     * @return PromptButtonId clicked/hovered, or PromptButtonId::None.
     */
    PromptButtonId HitTest(POINT pt) const;

    /**
     * @brief Sets the hovered button ID within the prompt bar.
     * @param id Hovered button.
     * @return True if state changed.
     */
    bool SetHoveredButton(PromptButtonId id);

private:
    PromptButtonId m_hoveredButton = PromptButtonId::None;

    D2D1_RECT_F m_rcPill{};
    D2D1_RECT_F m_rcPlus{};
    D2D1_RECT_F m_rcInput{};
    D2D1_RECT_F m_rcModel{};
    D2D1_RECT_F m_rcVoice{};
    D2D1_RECT_F m_rcSubmit{};
};

} // namespace phide::ui
