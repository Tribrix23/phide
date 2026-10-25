/**
 * @file DatabaseSettingsModal.hpp
 * @brief Modern visual configuration inspector and modal for Database (MySQL / phpMyAdmin) and Nginx Web Server.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"
#include "phide/ui/LucideIcons.hpp"

namespace phide::ui {

enum class DatabaseModalTab {
    PhpMyAdmin = 0,
    NginxServer = 1
};

enum class DatabaseModalAction {
    None,
    CopyHost,
    CopyPort,
    CopyUser,
    CopyPass,
    CopyDb,
    CopyPdoSnippet,
    CopyMysqliSnippet,
    CopyNginxUrl,
    CopyNginxPort,
    CopyNginxConfigPath,
    TogglePmaRun,
    ToggleServerRun,
    OpenBrowserPma,
    OpenBrowserNginx,
    EditPmaConfig,
    EditNginxConfig,
    SwitchTabPma,
    SwitchTabNginx,
    CloseModal
};

class DatabaseSettingsModal {
public:
    DatabaseSettingsModal() = default;
    ~DatabaseSettingsModal() = default;

    void Open(DatabaseModalTab initialTab = DatabaseModalTab::PhpMyAdmin);
    void Close() noexcept { m_isOpen = false; }
    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }

    void SetActiveTab(DatabaseModalTab tab) noexcept { m_activeTab = tab; }
    [[nodiscard]] DatabaseModalTab GetActiveTab() const noexcept { return m_activeTab; }

    void SetRuntimeStatus(bool pmaRunning, bool nginxRunning) noexcept {
        m_isPmaRunning = pmaRunning;
        m_isNginxRunning = nginxRunning;
    }
    [[nodiscard]] bool IsPmaRunning() const noexcept { return m_isPmaRunning; }
    [[nodiscard]] bool IsNginxRunning() const noexcept { return m_isNginxRunning; }

    void SetCopiedFeedback(const std::wstring& fieldName) {
        m_copiedField = fieldName;
        m_copiedTimerTicks = 120; // ~2 seconds at 60fps
    }

    void UpdateAnim(float deltaMs) {
        if (m_copiedTimerTicks > 0) {
            m_copiedTimerTicks--;
            if (m_copiedTimerTicks == 0) m_copiedField.clear();
        }
    }

    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientW, float clientH);

    DatabaseModalAction HitTestAction(POINT pt) const noexcept;
    void OnMouseMove(POINT pt) noexcept;
    [[nodiscard]] bool HitTestBounds(POINT pt) const noexcept;

    [[nodiscard]] const std::wstring& GetHost() const noexcept { return m_host; }
    [[nodiscard]] const std::wstring& GetPort() const noexcept { return m_port; }
    [[nodiscard]] const std::wstring& GetUser() const noexcept { return m_user; }
    [[nodiscard]] const std::wstring& GetPass() const noexcept { return m_pass; }
    [[nodiscard]] const std::wstring& GetDefaultDb() const noexcept { return m_defaultDb; }
    [[nodiscard]] const std::wstring& GetPdoSnippet() const noexcept { return m_pdoSnippet; }
    [[nodiscard]] const std::wstring& GetMysqliSnippet() const noexcept { return m_mysqliSnippet; }
    [[nodiscard]] const std::wstring& GetPmaUrl() const noexcept { return m_pmaUrl; }
    [[nodiscard]] const std::wstring& GetNginxUrl() const noexcept { return m_nginxUrl; }
    [[nodiscard]] const std::wstring& GetNginxPort() const noexcept { return m_nginxPort; }
    [[nodiscard]] const std::wstring& GetPmaConfigPath() const noexcept { return m_pmaConfigFile; }
    [[nodiscard]] const std::wstring& GetNginxConfigPath() const noexcept { return m_nginxConfigFile; }

private:
    bool m_isOpen = false;
    DatabaseModalTab m_activeTab = DatabaseModalTab::PhpMyAdmin;

    // Database parameters (matches config.inc.php and config.php)
    std::wstring m_host = L"127.0.0.1";
    std::wstring m_port = L"3306";
    std::wstring m_user = L"root";
    std::wstring m_pass = L"";
    std::wstring m_defaultDb = L"student_crud";
    std::wstring m_charset = L"utf8mb4";
    std::wstring m_authType = L"config (Auto-Login)";
    std::wstring m_blowfishSecret = L"phide_pma_aes256_blowfish_secret_key_32b!";
    std::wstring m_pmaUrl = L"http://127.0.0.1:8081";
    std::wstring m_pmaConfigFile = L"include\\phide\\db\\phpMyAdmin\\config.inc.php";

    // Snippets for PHP
    std::wstring m_pdoSnippet = L"$pdo = new PDO(\"mysql:host=127.0.0.1;port=3306;dbname=student_crud;charset=utf8mb4\", \"root\", \"\");";
    std::wstring m_mysqliSnippet = L"$conn = new mysqli(\"127.0.0.1\", \"root\", \"\", \"student_crud\", 3306);";

    // Nginx parameters
    std::wstring m_nginxUrl = L"http://localhost:80";
    std::wstring m_nginxPort = L"80";
    std::wstring m_nginxHost = L"localhost";
    std::wstring m_nginxConfigFile = L"include\\phide\\server\\nginx\\conf\\nginx.conf";

    bool m_isPmaRunning = false;
    bool m_isNginxRunning = false;

    std::wstring m_copiedField;
    int m_copiedTimerTicks = 0;

    LucideIcons m_icons;

    // Bounding rectangles for interactive elements
    D2D1_RECT_F m_rcModalCard{};
    D2D1_RECT_F m_rcCloseBtn{};
    D2D1_RECT_F m_rcTabPma{};
    D2D1_RECT_F m_rcTabNginx{};

    D2D1_RECT_F m_rcCopyHost{};
    D2D1_RECT_F m_rcCopyPort{};
    D2D1_RECT_F m_rcCopyUser{};
    D2D1_RECT_F m_rcCopyPass{};
    D2D1_RECT_F m_rcCopyDb{};
    D2D1_RECT_F m_rcCopyPdoSnippet{};
    D2D1_RECT_F m_rcCopyMysqliSnippet{};

    D2D1_RECT_F m_rcBtnTogglePma{};
    D2D1_RECT_F m_rcBtnOpenPma{};
    D2D1_RECT_F m_rcBtnEditPma{};

    D2D1_RECT_F m_rcCopyNginxUrl{};
    D2D1_RECT_F m_rcCopyNginxPort{};
    D2D1_RECT_F m_rcCopyNginxConfigPath{};
    D2D1_RECT_F m_rcBtnToggleNginx{};
    D2D1_RECT_F m_rcBtnOpenNginx{};
    D2D1_RECT_F m_rcBtnEditNginx{};

    D2D1_RECT_F m_rcBtnDone{};

    DatabaseModalAction m_hoveredAction = DatabaseModalAction::None;
};

} // namespace phide::ui
