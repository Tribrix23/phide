/**
 * @file ConfirmDiscardModal.hpp
 * @brief Modal visual confirmation dialog for discarding or restoring Git changes (Images 2 & 3).
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
 * @class ConfirmDiscardModal
 * @brief Manages the visual confirmation dialog matching user reference Images 2 & 3.
 */
class ConfirmDiscardModal {
public:
    ConfirmDiscardModal() = default;
    ~ConfirmDiscardModal() = default;

    /**
     * @brief Opens the confirmation dialog for a specific modified or deleted file.
     * @param filePath Relative repository path of the target file (or "*" for all changes).
     * @param fileName File name to display in the confirmation message.
     * @param status Git status ("D" for deleted, "M" for modified, "U" for untracked, "ALL" for all).
     * @param changeCount Total count of changes if discarding all.
     */
    void Open(const std::wstring& filePath, const std::wstring& fileName, const std::wstring& status, size_t changeCount = 1) {
        m_filePath = filePath;
        m_fileName = fileName.empty() ? filePath : fileName;
        m_status = status;
        m_changeCount = changeCount;
        m_isOpen = true;
        m_isConfirmHovered = false;
        m_isCancelHovered = false;
        m_isCloseHovered = false;
    }

    /**
     * @brief Closes the confirmation dialog.
     */
    void Close() noexcept {
        m_isOpen = false;
        m_isConfirmHovered = false;
        m_isCancelHovered = false;
        m_isCloseHovered = false;
    }

    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }
    [[nodiscard]] const std::wstring& GetTargetFilePath() const noexcept { return m_filePath; }
    [[nodiscard]] const std::wstring& GetTargetFileName() const noexcept { return m_fileName; }
    [[nodiscard]] const std::wstring& GetTargetStatus() const noexcept { return m_status; }

    /**
     * @brief Renders the confirmation modal with dimmed backdrop and flat styled card.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientW, float clientH);

    /**
     * @brief Handles mouse movements and button hover states.
     */
    bool HandleMouseMove(POINT pt) noexcept;

    /**
     * @brief Handles mouse clicks on Confirm, Cancel, Close, or backdrop.
     */
    bool HandleMouseDown(POINT pt, bool& outConfirmed, std::wstring& outPath, std::wstring& outFileName, std::wstring& outStatus) noexcept;

    /**
     * @brief Handles keyboard input (Enter to confirm, Escape to cancel).
     */
    bool HandleKeyDown(WPARAM wParam, bool& outConfirmed, std::wstring& outPath, std::wstring& outFileName, std::wstring& outStatus) noexcept;

private:
    bool m_isOpen = false;
    std::wstring m_filePath;
    std::wstring m_fileName;
    std::wstring m_status; // "D", "M", "U", "A", or "ALL"
    size_t m_changeCount = 1;

    D2D1_RECT_F m_cardBounds{};
    D2D1_RECT_F m_confirmBtnBounds{};
    D2D1_RECT_F m_cancelBtnBounds{};
    D2D1_RECT_F m_closeBtnBounds{};

    bool m_isConfirmHovered = false;
    bool m_isCancelHovered = false;
    bool m_isCloseHovered = false;
};

} // namespace phide::ui
