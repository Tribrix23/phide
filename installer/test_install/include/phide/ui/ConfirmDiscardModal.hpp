/**
 * @file ConfirmDiscardModal.hpp
 * @brief Modern flat visual confirmation and alert modal dialog (Images 2, 3, 4, 5).
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <filesystem>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @enum ConfirmModalType
 * @brief Type of confirmation or alert modal being presented.
 */
enum class ConfirmModalType {
    DiscardFile,
    DiscardAll,
    RestoreFile,
    Notice,
    DeleteFile,
    DeleteFolder
};

/**
 * @class ConfirmDiscardModal
 * @brief Manages modern flat confirmation and notice dialogs with sleek vector icons and pointer cursors.
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
        m_type = (status == L"D") ? ConfirmModalType::RestoreFile :
                 (status == L"ALL" ? ConfirmModalType::DiscardAll : ConfirmModalType::DiscardFile);
        m_title.clear();
        m_message.clear();
        m_subtext.clear();
        m_isOpen = true;
        m_isLoading = false;
        m_spinnerAngle = 0.0f;
        m_isConfirmHovered = false;
        m_isCancelHovered = false;
        m_isCloseHovered = false;
    }

    /**
     * @brief Opens a centered alert notice dialog (e.g. Commit Message Required).
     */
    void OpenNotice(const std::wstring& title, const std::wstring& message, const std::wstring& subtext) {
        m_type = ConfirmModalType::Notice;
        m_title = title;
        m_message = message;
        m_subtext = subtext;
        m_filePath.clear();
        m_fileName.clear();
        m_status.clear();
        m_changeCount = 0;
        m_isOpen = true;
        m_isLoading = false;
        m_spinnerAngle = 0.0f;
        m_isConfirmHovered = false;
        m_isCancelHovered = false;
        m_isCloseHovered = false;
    }

    /**
     * @brief Opens the custom flat design delete confirmation dialog for files or folders.
     */
    void OpenDelete(const std::wstring& targetPath, bool isFolder) {
        m_type = isFolder ? ConfirmModalType::DeleteFolder : ConfirmModalType::DeleteFile;
        m_filePath = targetPath;
        m_fileName = std::filesystem::path(targetPath).filename().wstring();
        m_status.clear();
        m_changeCount = 0;
        m_title.clear();
        m_message.clear();
        m_subtext.clear();
        m_isOpen = true;
        m_isLoading = false;
        m_spinnerAngle = 0.0f;
        m_isConfirmHovered = false;
        m_isCancelHovered = false;
        m_isCloseHovered = false;
    }

    /**
     * @brief Closes the confirmation dialog.
     */
    void Close() noexcept {
        m_isOpen = false;
        m_isLoading = false;
        m_isConfirmHovered = false;
        m_isCancelHovered = false;
        m_isCloseHovered = false;
    }

    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }
    [[nodiscard]] bool IsLoading() const noexcept { return m_isLoading; }
    void SetLoading(bool loading) noexcept { m_isLoading = loading; }
    void UpdateSpinner(float delta = 14.0f) noexcept { m_spinnerAngle += delta; }

    [[nodiscard]] ConfirmModalType GetType() const noexcept { return m_type; }
    [[nodiscard]] const std::wstring& GetTargetFilePath() const noexcept { return m_filePath; }
    [[nodiscard]] const std::wstring& GetTargetFileName() const noexcept { return m_fileName; }
    [[nodiscard]] const std::wstring& GetTargetStatus() const noexcept { return m_status; }
    [[nodiscard]] const D2D1_RECT_F& GetConfirmBtnBounds() const noexcept { return m_confirmBtnBounds; }
    [[nodiscard]] const D2D1_RECT_F& GetCancelBtnBounds() const noexcept { return m_cancelBtnBounds; }
    [[nodiscard]] const D2D1_RECT_F& GetCardBounds() const noexcept { return m_cardBounds; }

    /**
     * @brief Checks if a client point is over any clickable buttons on the modal (Confirm, Cancel, Close).
     */
    [[nodiscard]] bool HitTestButtons(POINT pt) const noexcept {
        if (!m_isOpen || m_isLoading) return false;
        auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
            return (p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom);
        };
        return inRect(pt, m_confirmBtnBounds) || inRect(pt, m_cancelBtnBounds) || inRect(pt, m_closeBtnBounds);
    }

    /**
     * @brief Checks if a client point is inside the modal card.
     */
    [[nodiscard]] bool HitTestCard(POINT pt) const noexcept {
        if (!m_isOpen) return false;
        return (pt.x >= m_cardBounds.left && pt.x <= m_cardBounds.right &&
                pt.y >= m_cardBounds.top && pt.y <= m_cardBounds.bottom);
    }

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
    bool m_isLoading = false;
    float m_spinnerAngle = 0.0f;
    ConfirmModalType m_type = ConfirmModalType::DiscardFile;
    std::wstring m_filePath;
    std::wstring m_fileName;
    std::wstring m_status; // "D", "M", "U", "A", or "ALL"
    size_t m_changeCount = 1;

    std::wstring m_title;
    std::wstring m_message;
    std::wstring m_subtext;

    D2D1_RECT_F m_cardBounds{};
    D2D1_RECT_F m_confirmBtnBounds{};
    D2D1_RECT_F m_cancelBtnBounds{};
    D2D1_RECT_F m_closeBtnBounds{};

    bool m_isConfirmHovered = false;
    bool m_isCancelHovered = false;
    bool m_isCloseHovered = false;
};

} // namespace phide::ui
