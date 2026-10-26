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
    Supabase = 1,
    NginxServer = 2
};

enum class DatabaseEditField {
    None,
    Host,
    Port,
    User,
    Pass,
    Database,
    Charset,
    NginxPort,
    NginxHost,
    SupabaseUrl,
    SupabaseAnonKey,
    SupabasePort
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
    ToggleSupabaseRun,
    ToggleServerRun,
    OpenBrowserPma,
    OpenBrowserSupabase,
    OpenBrowserNginx,
    EditPmaConfig,
    EditNginxConfig,
    OpenSupabaseFolder,
    CopySupabaseUrl,
    CopySupabaseAnonKey,
    CopySupabasePort,
    CopySupabaseJsSnippet,
    CopySupabasePySnippet,
    ImportSql,
    SwitchTabPma,
    SwitchTabSupabase,
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

    void SetActiveTab(DatabaseModalTab tab) noexcept {
        m_activeTab = tab;
        if (tab == DatabaseModalTab::Supabase) m_isSupabaseSelected = true;
        else if (tab == DatabaseModalTab::PhpMyAdmin) m_isSupabaseSelected = false;
    }
    [[nodiscard]] DatabaseModalTab GetActiveTab() const noexcept { return m_activeTab; }

    void SetSupabaseSelected(bool selected) noexcept { m_isSupabaseSelected = selected; }
    [[nodiscard]] bool IsSupabaseSelected() const noexcept { return m_isSupabaseSelected; }

    void SetRuntimeStatus(bool pmaRunning, bool nginxRunning, bool supabaseRunning) noexcept {
        m_isPmaRunning = pmaRunning;
        m_isNginxRunning = nginxRunning;
        m_isSupabaseRunning = supabaseRunning;
    }
    void SetPmaRunning(bool running) noexcept { m_isPmaRunning = running; }
    void SetNginxRunning(bool running) noexcept { m_isNginxRunning = running; }
    void SetSupabaseRunning(bool running) noexcept { m_isSupabaseRunning = running; }
    [[nodiscard]] bool IsPmaRunning() const noexcept { return m_isPmaRunning; }
    [[nodiscard]] bool IsNginxRunning() const noexcept { return m_isNginxRunning; }
    [[nodiscard]] bool IsSupabaseRunning() const noexcept { return m_isSupabaseRunning; }

    void SetSqlFileAvailable(const std::wstring& sqlFileName) noexcept { m_sqlFileName = sqlFileName; }
    [[nodiscard]] const std::wstring& GetSqlFileName() const noexcept { return m_sqlFileName; }

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
    void HandleClick(POINT pt) noexcept;
    void OnMouseMove(POINT pt) noexcept;
    [[nodiscard]] bool HitTestBounds(POINT pt) const noexcept;

    bool OnChar(wchar_t ch);
    bool OnKeyDown(WPARAM wParam);
    void SetFocusedField(DatabaseEditField field) noexcept { m_focusedField = field; }
    [[nodiscard]] DatabaseEditField GetFocusedField() const noexcept { return m_focusedField; }
    void ToggleCaret() noexcept { m_caretVisible = !m_caretVisible; }
    void UpdateSnippets();

    [[nodiscard]] const std::wstring& GetHost() const noexcept { return m_host; }
    [[nodiscard]] const std::wstring& GetPort() const noexcept { return m_port; }
    [[nodiscard]] const std::wstring& GetUser() const noexcept { return m_user; }
    [[nodiscard]] const std::wstring& GetPass() const noexcept { return m_pass; }
    [[nodiscard]] const std::wstring& GetDefaultDb() const noexcept { return m_defaultDb; }
    [[nodiscard]] const std::wstring& GetCharset() const noexcept { return m_charset; }
    [[nodiscard]] const std::wstring& GetPdoSnippet() const noexcept { return m_pdoSnippet; }
    [[nodiscard]] const std::wstring& GetMysqliSnippet() const noexcept { return m_mysqliSnippet; }
    [[nodiscard]] const std::wstring& GetPmaUrl() const noexcept { return m_pmaUrl; }
    [[nodiscard]] const std::wstring& GetPmaWebPort() const noexcept { return m_pmaWebPort; }
    [[nodiscard]] const std::wstring& GetNginxUrl() const noexcept { return m_nginxUrl; }
    [[nodiscard]] const std::wstring& GetNginxPort() const noexcept { return m_nginxPort; }
    [[nodiscard]] const std::wstring& GetNginxHost() const noexcept { return m_nginxHost; }
    [[nodiscard]] const std::wstring& GetPmaConfigPath() const noexcept { return m_pmaConfigFile; }
    [[nodiscard]] const std::wstring& GetNginxConfigPath() const noexcept { return m_nginxConfigFile; }

    [[nodiscard]] const std::wstring& GetSupabaseUrl() const noexcept { return m_supabaseUrl; }
    [[nodiscard]] const std::wstring& GetSupabaseAnonKey() const noexcept { return m_supabaseAnonKey; }
    [[nodiscard]] const std::wstring& GetSupabasePort() const noexcept { return m_supabasePort; }
    [[nodiscard]] std::wstring GetSupabaseStudioPort() const {
        size_t colonPos = m_supabaseUrl.rfind(L':');
        if (colonPos != std::wstring::npos && colonPos > 5) {
            std::wstring portPart = m_supabaseUrl.substr(colonPos + 1);
            size_t slashPos = portPart.find(L'/');
            if (slashPos != std::wstring::npos) portPart = portPart.substr(0, slashPos);
            if (!portPart.empty()) return portPart;
        }
        return m_supabaseStudioPort;
    }
    [[nodiscard]] const std::wstring& GetSupabaseHost() const noexcept { return m_supabaseHost; }
    [[nodiscard]] const std::wstring& GetSupabaseUser() const noexcept { return m_supabaseUser; }
    [[nodiscard]] const std::wstring& GetSupabasePass() const noexcept { return m_supabasePass; }
    [[nodiscard]] const std::wstring& GetSupabaseDb() const noexcept { return m_supabaseDb; }
    [[nodiscard]] const std::wstring& GetSupabaseDir() const noexcept { return m_supabaseDir; }
    [[nodiscard]] const std::wstring& GetSupabaseJsSnippet() const noexcept { return m_supabaseJsSnippet; }
    [[nodiscard]] const std::wstring& GetSupabasePySnippet() const noexcept { return m_supabasePySnippet; }

private:
    bool m_isOpen = false;
    bool m_initialized = false;
    bool m_isSupabaseSelected = false;
    DatabaseModalTab m_activeTab = DatabaseModalTab::PhpMyAdmin;

    // Database parameters (matches config.inc.php and config.php)
    std::wstring m_host = L"localhost";
    std::wstring m_port = L"3306";
    std::wstring m_user = L"root";
    std::wstring m_pass = L"";
    std::wstring m_defaultDb = L"student_crud";
    std::wstring m_charset = L"utf8mb4";
    std::wstring m_authType = L"config (Auto-Login)";
    std::wstring m_blowfishSecret = L"phide_pma_aes256_blowfish_secret_key_32b!";
    std::wstring m_pmaWebPort = L"8081";
    std::wstring m_pmaUrl = L"http://localhost:8081";
    std::wstring m_pmaConfigFile = L"include\\phide\\db\\phpMyAdmin\\config.inc.php";

    // Snippets for PHP
    std::wstring m_pdoSnippet = L"$pdo = new PDO(\"mysql:host=localhost;port=3306;dbname=student_crud;charset=utf8mb4\", \"root\", \"\");";
    std::wstring m_mysqliSnippet = L"$conn = new mysqli(\"localhost\", \"root\", \"\", \"student_crud\", 3306);";

    // Supabase parameters
    std::wstring m_supabaseUrl = L"http://localhost:54323";
    std::wstring m_supabaseAnonKey = L"sb_publishable_nXkt2Y-i1brrrVYmF_mxzw_RMiAd6Ry";
    std::wstring m_supabaseHost = L"localhost";
    std::wstring m_supabasePort = L"5432";
    std::wstring m_supabaseUser = L"postgres";
    std::wstring m_supabasePass = L"postgres";
    std::wstring m_supabaseDb = L"postgres";
    std::wstring m_supabaseStudioPort = L"54323";
    std::wstring m_supabaseDir = L"include\\phide\\db\\supabase";
    std::wstring m_supabaseJsSnippet = L"import { createClient } from '@supabase/supabase-js';\nconst supabase = createClient('http://localhost:54323', 'sb_publishable_nXkt2Y-i1brrrVYmF_mxzw_RMiAd6Ry');";
    std::wstring m_supabasePySnippet = L"from supabase import create_client\nsupabase = create_client('http://localhost:54323', 'sb_publishable_nXkt2Y-i1brrrVYmF_mxzw_RMiAd6Ry')";
    bool m_isSupabaseRunning = false;

    // Nginx parameters
    std::wstring m_nginxUrl = L"http://localhost:80";
    std::wstring m_nginxPort = L"80";
    std::wstring m_nginxHost = L"localhost";
    std::wstring m_nginxConfigFile = L"include\\phide\\server\\nginx\\conf\\nginx.conf";

    bool m_isPmaRunning = false;
    bool m_isNginxRunning = false;

    DatabaseEditField m_focusedField = DatabaseEditField::None;
    bool m_caretVisible = true;

    std::wstring m_copiedField;
    int m_copiedTimerTicks = 0;

    LucideIcons m_icons;

    // Bounding rectangles for interactive elements
    D2D1_RECT_F m_rcModalCard{};
    D2D1_RECT_F m_rcCloseBtn{};
    D2D1_RECT_F m_rcTabPma{};
    D2D1_RECT_F m_rcTabSupabase{};
    D2D1_RECT_F m_rcTabNginx{};

    // Editable input fields in Section A
    D2D1_RECT_F m_rcInputHost{};
    D2D1_RECT_F m_rcInputPort{};
    D2D1_RECT_F m_rcInputUser{};
    D2D1_RECT_F m_rcInputPass{};
    D2D1_RECT_F m_rcInputDb{};
    D2D1_RECT_F m_rcInputCharset{};

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

    // Supabase interactive elements
    D2D1_RECT_F m_rcInputSupabaseUrl{};
    D2D1_RECT_F m_rcInputSupabaseAnonKey{};
    D2D1_RECT_F m_rcInputSupabasePort{};
    D2D1_RECT_F m_rcCopySupabaseUrl{};
    D2D1_RECT_F m_rcCopySupabaseAnonKey{};
    D2D1_RECT_F m_rcCopySupabasePort{};
    D2D1_RECT_F m_rcCopySupabaseJs{};
    D2D1_RECT_F m_rcCopySupabasePy{};
    D2D1_RECT_F m_rcBtnToggleSupabase{};
    D2D1_RECT_F m_rcBtnOpenSupabase{};
    D2D1_RECT_F m_rcBtnOpenSupabaseFolder{};

    D2D1_RECT_F m_rcCopyNginxUrl{};
    D2D1_RECT_F m_rcCopyNginxPort{};
    D2D1_RECT_F m_rcCopyNginxConfigPath{};
    D2D1_RECT_F m_rcInputNginxPort{};
    D2D1_RECT_F m_rcInputNginxHost{};
    D2D1_RECT_F m_rcBtnToggleNginx{};
    D2D1_RECT_F m_rcBtnOpenNginx{};
    D2D1_RECT_F m_rcBtnEditNginx{};

    D2D1_RECT_F m_rcBtnDone{};
    D2D1_RECT_F m_rcBtnImportSql{};
    std::wstring m_sqlFileName;

    DatabaseModalAction m_hoveredAction = DatabaseModalAction::None;
};

} // namespace phide::ui
