/**
 * @file DatabaseSettingsModal.cpp
 * @brief Implementation of Database & phpMyAdmin and Nginx Web Server configuration modal.
 */

#include "phide/ui/DatabaseSettingsModal.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace phide::ui {

static std::filesystem::path GetPhideRoot() {
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path dir = std::filesystem::path(exePath).parent_path();
    if (std::filesystem::exists(dir / L"include" / L"phide")) {
        return dir;
    }
    if (std::filesystem::exists(dir.parent_path() / L"include" / L"phide")) {
        return dir.parent_path();
    }
    if (std::filesystem::exists(std::filesystem::current_path() / L"include" / L"phide")) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(L"c:\\Users\\Administrator\\Downloads\\studio\\phide");
}

static inline bool PtInRectF(POINT pt, const D2D1_RECT_F& rc) {
    return (pt.x >= rc.left && pt.x <= rc.right && pt.y >= rc.top && pt.y <= rc.bottom);
}

void DatabaseSettingsModal::Open(DatabaseModalTab initialTab) {
    m_isOpen = true;
    m_activeTab = initialTab;
    m_copiedField.clear();
    m_copiedTimerTicks = 0;
    m_hoveredAction = DatabaseModalAction::None;
    m_focusedField = DatabaseEditField::None;
    m_caretVisible = true;

    // Check if config.inc.php exists and parse basic values on first open
    if (!m_initialized) {
        m_initialized = true;
        std::filesystem::path pmaCfgPath = GetPhideRoot() / L"include" / L"phide" / L"db" / L"phpMyAdmin" / L"config.inc.php";
        if (std::filesystem::exists(pmaCfgPath)) {
            std::wifstream in(pmaCfgPath);
            std::wstring line;
            while (std::getline(in, line)) {
                if (line.find(L"['host']") != std::wstring::npos && line.find(L"'") != std::wstring::npos) {
                    size_t firstQ = line.rfind(L"'");
                    if (firstQ != std::wstring::npos) {
                        size_t prevQ = line.rfind(L"'", firstQ - 1);
                        if (prevQ != std::wstring::npos && prevQ < firstQ) {
                            m_host = line.substr(prevQ + 1, firstQ - prevQ - 1);
                        }
                    }
                } else if (line.find(L"['port']") != std::wstring::npos && line.find(L"'") != std::wstring::npos) {
                    size_t firstQ = line.rfind(L"'");
                    if (firstQ != std::wstring::npos) {
                        size_t prevQ = line.rfind(L"'", firstQ - 1);
                        if (prevQ != std::wstring::npos && prevQ < firstQ) {
                            m_port = line.substr(prevQ + 1, firstQ - prevQ - 1);
                        }
                    }
                } else if (line.find(L"['user']") != std::wstring::npos && line.find(L"'") != std::wstring::npos) {
                    size_t firstQ = line.rfind(L"'");
                    if (firstQ != std::wstring::npos) {
                        size_t prevQ = line.rfind(L"'", firstQ - 1);
                        if (prevQ != std::wstring::npos && prevQ < firstQ) {
                            m_user = line.substr(prevQ + 1, firstQ - prevQ - 1);
                        }
                    }
                }
            }
        }
    }
    UpdateSnippets();
}

bool DatabaseSettingsModal::OnChar(wchar_t ch) {
    if (!m_isOpen || m_focusedField == DatabaseEditField::None) return false;
    std::wstring* target = nullptr;
    switch (m_focusedField) {
        case DatabaseEditField::Host: target = &m_host; break;
        case DatabaseEditField::Port: target = &m_port; break;
        case DatabaseEditField::User: target = &m_user; break;
        case DatabaseEditField::Pass: target = &m_pass; break;
        case DatabaseEditField::Database: target = &m_defaultDb; break;
        case DatabaseEditField::Charset: target = &m_charset; break;
        case DatabaseEditField::NginxPort: target = &m_nginxPort; break;
        case DatabaseEditField::NginxHost: target = &m_nginxHost; break;
        default: break;
    }
    if (!target) return false;

    if (ch == 8) { // Backspace
        if (!target->empty()) {
            target->pop_back();
            UpdateSnippets();
            return true;
        }
    } else if (ch >= 32) {
        if (target->size() < 64) {
            target->push_back(ch);
            UpdateSnippets();
            return true;
        }
    }
    return false;
}

bool DatabaseSettingsModal::OnKeyDown(WPARAM wParam) {
    if (!m_isOpen || m_focusedField == DatabaseEditField::None) return false;
    if (wParam == VK_ESCAPE || wParam == VK_RETURN) {
        m_focusedField = DatabaseEditField::None;
        return true;
    }
    if (wParam == VK_TAB) {
        switch (m_focusedField) {
            case DatabaseEditField::Host: m_focusedField = DatabaseEditField::Port; break;
            case DatabaseEditField::Port: m_focusedField = DatabaseEditField::User; break;
            case DatabaseEditField::User: m_focusedField = DatabaseEditField::Pass; break;
            case DatabaseEditField::Pass: m_focusedField = DatabaseEditField::Database; break;
            case DatabaseEditField::Database: m_focusedField = DatabaseEditField::Charset; break;
            case DatabaseEditField::Charset: m_focusedField = DatabaseEditField::Host; break;
            case DatabaseEditField::NginxPort: m_focusedField = DatabaseEditField::NginxHost; break;
            case DatabaseEditField::NginxHost: m_focusedField = DatabaseEditField::NginxPort; break;
            default: m_focusedField = DatabaseEditField::Host; break;
        }
        return true;
    }
    return false;
}

void DatabaseSettingsModal::UpdateSnippets() {
    m_pdoSnippet = L"$pdo = new PDO(\"mysql:host=" + m_host + L";port=" + m_port + L";dbname=" + m_defaultDb + L";charset=" + m_charset + L"\", \"" + m_user + L"\", \"" + m_pass + L"\");";
    m_mysqliSnippet = L"$conn = new mysqli(\"" + m_host + L"\", \"" + m_user + L"\", \"" + m_pass + L"\", \"" + m_defaultDb + L"\", " + m_port + L");";

    std::wstring effPmaHost = m_host.empty() ? L"localhost" : m_host;
    std::wstring effPmaPort = m_pmaWebPort.empty() ? L"8081" : m_pmaWebPort;
    m_pmaUrl = L"http://" + effPmaHost + L":" + effPmaPort;

    std::wstring effNgxHost = m_nginxHost.empty() ? L"localhost" : m_nginxHost;
    std::wstring effNgxPort = m_nginxPort.empty() ? L"80" : m_nginxPort;
    m_nginxUrl = L"http://" + effNgxHost + L":" + effNgxPort;
}

bool DatabaseSettingsModal::HitTestBounds(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    return PtInRectF(pt, m_rcModalCard);
}

DatabaseModalAction DatabaseSettingsModal::HitTestAction(POINT pt) const noexcept {
    if (!m_isOpen) return DatabaseModalAction::None;

    if (PtInRectF(pt, m_rcCloseBtn)) return DatabaseModalAction::CloseModal;
    if (PtInRectF(pt, m_rcBtnDone)) return DatabaseModalAction::CloseModal;
    if (!m_sqlFileName.empty() && PtInRectF(pt, m_rcBtnImportSql)) return DatabaseModalAction::ImportSql;

    if (PtInRectF(pt, m_rcTabPma)) return DatabaseModalAction::SwitchTabPma;
    if (PtInRectF(pt, m_rcTabNginx)) return DatabaseModalAction::SwitchTabNginx;

    if (m_activeTab == DatabaseModalTab::PhpMyAdmin) {
        if (PtInRectF(pt, m_rcCopyHost)) return DatabaseModalAction::CopyHost;
        if (PtInRectF(pt, m_rcCopyPort)) return DatabaseModalAction::CopyPort;
        if (PtInRectF(pt, m_rcCopyUser)) return DatabaseModalAction::CopyUser;
        if (PtInRectF(pt, m_rcCopyPass)) return DatabaseModalAction::CopyPass;
        if (PtInRectF(pt, m_rcCopyDb)) return DatabaseModalAction::CopyDb;
        if (PtInRectF(pt, m_rcCopyPdoSnippet)) return DatabaseModalAction::CopyPdoSnippet;
        if (PtInRectF(pt, m_rcCopyMysqliSnippet)) return DatabaseModalAction::CopyMysqliSnippet;
        if (PtInRectF(pt, m_rcBtnTogglePma)) return DatabaseModalAction::TogglePmaRun;
        if (PtInRectF(pt, m_rcBtnOpenPma)) return DatabaseModalAction::OpenBrowserPma;
        if (PtInRectF(pt, m_rcBtnEditPma)) return DatabaseModalAction::EditPmaConfig;
    } else {
        if (PtInRectF(pt, m_rcCopyNginxUrl)) return DatabaseModalAction::CopyNginxUrl;
        if (PtInRectF(pt, m_rcCopyNginxPort)) return DatabaseModalAction::CopyNginxPort;
        if (PtInRectF(pt, m_rcCopyNginxConfigPath)) return DatabaseModalAction::CopyNginxConfigPath;
        if (PtInRectF(pt, m_rcBtnToggleNginx)) return DatabaseModalAction::ToggleServerRun;
        if (PtInRectF(pt, m_rcBtnOpenNginx)) return DatabaseModalAction::OpenBrowserNginx;
        if (PtInRectF(pt, m_rcBtnEditNginx)) return DatabaseModalAction::EditNginxConfig;
    }

    return DatabaseModalAction::None;
}

void DatabaseSettingsModal::HandleClick(POINT pt) noexcept {
    if (!m_isOpen) return;

    if (m_activeTab == DatabaseModalTab::PhpMyAdmin) {
        if (PtInRectF(pt, m_rcInputHost)) { m_focusedField = DatabaseEditField::Host; m_caretVisible = true; return; }
        if (PtInRectF(pt, m_rcInputPort)) { m_focusedField = DatabaseEditField::Port; m_caretVisible = true; return; }
        if (PtInRectF(pt, m_rcInputUser)) { m_focusedField = DatabaseEditField::User; m_caretVisible = true; return; }
        if (PtInRectF(pt, m_rcInputPass)) { m_focusedField = DatabaseEditField::Pass; m_caretVisible = true; return; }
        if (PtInRectF(pt, m_rcInputDb)) { m_focusedField = DatabaseEditField::Database; m_caretVisible = true; return; }
        if (PtInRectF(pt, m_rcInputCharset)) { m_focusedField = DatabaseEditField::Charset; m_caretVisible = true; return; }

        if (PtInRectF(pt, m_rcModalCard)) {
            m_focusedField = DatabaseEditField::None;
        }
    } else {
        if (PtInRectF(pt, m_rcInputNginxPort)) { m_focusedField = DatabaseEditField::NginxPort; m_caretVisible = true; return; }
        if (PtInRectF(pt, m_rcInputNginxHost)) { m_focusedField = DatabaseEditField::NginxHost; m_caretVisible = true; return; }

        if (PtInRectF(pt, m_rcModalCard)) {
            m_focusedField = DatabaseEditField::None;
        }
    }
}

void DatabaseSettingsModal::OnMouseMove(POINT pt) noexcept {
    if (!m_isOpen) {
        m_hoveredAction = DatabaseModalAction::None;
        return;
    }
    m_hoveredAction = HitTestAction(pt);
}

void DatabaseSettingsModal::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientW, float clientH) {
    if (!m_isOpen || !pRT) return;

    // 1. Semi-translucent dark modal backdrop
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pBackdropBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.65f), &pBackdropBrush);
    if (pBackdropBrush) {
        pRT->FillRectangle(D2D1::RectF(0.0f, 0.0f, clientW, clientH), pBackdropBrush.Get());
    }

    // 2. Centered Modal Card
    const float cardW = config.ScaleF(780.0f);
    const float cardH = config.ScaleF(560.0f);
    const float cardX = (std::max)(10.0f, (clientW - cardW) * 0.5f);
    const float cardY = (std::max)(10.0f, (clientH - cardH) * 0.5f);

    m_rcModalCard = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);
    D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_rcModalCard, 8.0f, 8.0f);

    // Card background & subtle 1px border
    ID2D1Brush* pBgBrush = theme.brushMenuBg ? theme.brushMenuBg.Get() : theme.brushDrawerBg.Get();
    pRT->FillRoundedRectangle(rrcCard, pBgBrush);

    ID2D1Brush* pBorderBrush = theme.brushMenuBorder ? theme.brushMenuBorder.Get() : theme.brushBorderSubtle.Get();
    pRT->DrawRoundedRectangle(rrcCard, pBorderBrush, 1.0f);

    IDWriteTextFormat* pFmtTitle = theme.fontTitle ? theme.fontTitle.Get() : theme.fontBody.Get();
    IDWriteTextFormat* pFmtMain  = theme.fontBody ? theme.fontBody.Get() : theme.fontHint.Get();
    IDWriteTextFormat* pFmtHint  = theme.fontHint ? theme.fontHint.Get() : theme.fontBody.Get();
    IDWriteTextFormat* pFmtCode  = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();

    ID2D1Brush* pWhiteBrush = theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
    ID2D1Brush* pMutedBrush = theme.brushTextMuted ? theme.brushTextMuted.Get() : theme.brushTextSecondary.Get();
    ID2D1Brush* pAccentBrush = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextPrimary.Get();

    // 3. Header: Icon + Title + Subtitle + Close Button
    float padX = config.ScaleF(24.0f);
    float topY = cardY + config.ScaleF(20.0f);

    float iconS = config.ScaleF(22.0f);
    D2D1_RECT_F rcHeaderIcon = D2D1::RectF(cardX + padX, topY, cardX + padX + iconS, topY + iconS);
    if (m_activeTab == DatabaseModalTab::PhpMyAdmin) {
        m_icons.DrawDatabase(pRT, rcHeaderIcon, pWhiteBrush, 1.4f);
    } else {
        m_icons.DrawServer(pRT, rcHeaderIcon, pWhiteBrush, 1.4f);
    }

    D2D1_RECT_F rcTitle = D2D1::RectF(cardX + padX + iconS + config.ScaleF(10.0f), topY - config.ScaleF(2.0f),
                                     cardX + cardW - config.ScaleF(60.0f), topY + config.ScaleF(22.0f));
    const wchar_t* titleStr = (m_activeTab == DatabaseModalTab::PhpMyAdmin)
        ? L"Database & phpMyAdmin Configuration"
        : L"Nginx Web Server Configuration";
    pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)), pFmtTitle, rcTitle, pWhiteBrush);

    D2D1_RECT_F rcSubtitle = D2D1::RectF(cardX + padX + iconS + config.ScaleF(10.0f), topY + config.ScaleF(22.0f),
                                        cardX + cardW - config.ScaleF(60.0f), topY + config.ScaleF(40.0f));
    const wchar_t* subStr = (m_activeTab == DatabaseModalTab::PhpMyAdmin)
        ? L"MySQL / MariaDB local credentials, connection strings & phpMyAdmin runtime"
        : L"Local high-performance Nginx daemon, virtual hosts & reverse proxy parameters";
    pRT->DrawText(subStr, static_cast<UINT32>(wcslen(subStr)), pFmtHint, rcSubtitle, pMutedBrush);

    // Close button [X]
    float closeS = config.ScaleF(24.0f);
    m_rcCloseBtn = D2D1::RectF(cardX + cardW - padX - closeS, topY, cardX + cardW - padX, topY + closeS);
    bool isCloseHov = (m_hoveredAction == DatabaseModalAction::CloseModal);
    if (isCloseHov && theme.brushMenuHover) {
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcCloseBtn, 4.0f, 4.0f), theme.brushMenuHover.Get());
    }
    m_icons.DrawClose(pRT, m_rcCloseBtn, isCloseHov ? pWhiteBrush : pMutedBrush, 1.3f);

    // 4. Segmented Tabs: [Database & phpMyAdmin] [Nginx Web Server]
    float tabY = topY + config.ScaleF(48.0f);
    float tabW = (cardW - padX * 2.0f) * 0.5f;
    float tabH = config.ScaleF(32.0f);

    m_rcTabPma = D2D1::RectF(cardX + padX, tabY, cardX + padX + tabW, tabY + tabH);
    m_rcTabNginx = D2D1::RectF(cardX + padX + tabW, tabY, cardX + cardW - padX, tabY + tabH);

    D2D1_ROUNDED_RECT rrcTabPma = D2D1::RoundedRect(m_rcTabPma, 4.0f, 4.0f);
    D2D1_ROUNDED_RECT rrcTabNginx = D2D1::RoundedRect(m_rcTabNginx, 4.0f, 4.0f);

    if (m_activeTab == DatabaseModalTab::PhpMyAdmin) {
        if (theme.brushMenuHover) pRT->FillRoundedRectangle(rrcTabPma, theme.brushMenuHover.Get());
        pRT->DrawRoundedRectangle(rrcTabPma, pAccentBrush, 1.2f);
        pRT->DrawRoundedRectangle(rrcTabNginx, pBorderBrush, 1.0f);
    } else {
        if (theme.brushMenuHover) pRT->FillRoundedRectangle(rrcTabNginx, theme.brushMenuHover.Get());
        pRT->DrawRoundedRectangle(rrcTabNginx, pAccentBrush, 1.2f);
        pRT->DrawRoundedRectangle(rrcTabPma, pBorderBrush, 1.0f);
    }

    pRT->DrawText(L"Database & phpMyAdmin (MySQL)", 29, pFmtMain,
                  D2D1::RectF(m_rcTabPma.left + config.ScaleF(12.0f), m_rcTabPma.top + config.ScaleF(6.0f), m_rcTabPma.right, m_rcTabPma.bottom),
                  (m_activeTab == DatabaseModalTab::PhpMyAdmin) ? pWhiteBrush : pMutedBrush);

    pRT->DrawText(L"Web Server (Nginx)", 18, pFmtMain,
                  D2D1::RectF(m_rcTabNginx.left + config.ScaleF(12.0f), m_rcTabNginx.top + config.ScaleF(6.0f), m_rcTabNginx.right, m_rcTabNginx.bottom),
                  (m_activeTab == DatabaseModalTab::NginxServer) ? pWhiteBrush : pMutedBrush);

    float contentY = tabY + tabH + config.ScaleF(16.0f);

    D2D1_RECT_F dummyInput{};

    auto DrawFieldBadge = [&](const std::wstring& label, const std::wstring& value, const D2D1_RECT_F& rc, D2D1_RECT_F& outBtn, DatabaseModalAction action, DatabaseEditField editField, D2D1_RECT_F& outInput) {
        // Field card background
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rc, 4.0f, 4.0f), theme.brushCanvasBg ? theme.brushCanvasBg.Get() : theme.brushDrawerBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rc, 4.0f, 4.0f), pBorderBrush, 0.8f);

        // Label
        float lblW = config.ScaleF(92.0f);
        D2D1_RECT_F rcLbl = D2D1::RectF(rc.left + config.ScaleF(10.0f), rc.top + config.ScaleF(4.0f), rc.left + lblW, rc.bottom);
        pRT->DrawText(label.c_str(), static_cast<UINT32>(label.size()), pFmtHint, rcLbl, pMutedBrush);

        // Value with axis-aligned clipping so long strings never overflow
        bool hasBtn = (action != DatabaseModalAction::None);
        float valRight = hasBtn ? (rc.right - config.ScaleF(56.0f)) : (rc.right - config.ScaleF(10.0f));
        outInput = D2D1::RectF(rc.left + lblW + config.ScaleF(2.0f), rc.top + config.ScaleF(2.0f), valRight, rc.bottom - config.ScaleF(2.0f));

        bool isFocused = (editField != DatabaseEditField::None && m_focusedField == editField);
        if (isFocused) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bFocusBorder;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.52f, 0.95f, 1.0f), &bFocusBorder);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bFocusBg;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.52f, 0.95f, 0.15f), &bFocusBg);
            if (bFocusBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(outInput, 3.0f, 3.0f), bFocusBg.Get());
            if (bFocusBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(outInput, 3.0f, 3.0f), bFocusBorder.Get(), 1.2f);
        }

        D2D1_RECT_F rcVal = D2D1::RectF(outInput.left + config.ScaleF(4.0f), outInput.top + config.ScaleF(2.0f), outInput.right - config.ScaleF(4.0f), outInput.bottom);
        std::wstring dispVal = value.empty() ? (isFocused ? L"" : L"(empty / none)") : value;
        pRT->PushAxisAlignedClip(rcVal, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ID2D1Brush* textBrush = (value.empty() && !isFocused) ? pMutedBrush : pWhiteBrush;
        pRT->DrawText(dispVal.c_str(), static_cast<UINT32>(dispVal.size()), pFmtCode, rcVal, textBrush);

        // Blinking caret cursor if focused
        if (isFocused && m_caretVisible) {
            float textW = static_cast<float>(value.size()) * config.ScaleF(7.5f);
            float caretX = rcVal.left + textW;
            if (caretX < rcVal.right - config.ScaleF(2.0f)) {
                pRT->DrawLine(
                    D2D1::Point2F(caretX, rcVal.top + config.ScaleF(2.0f)),
                    D2D1::Point2F(caretX, rcVal.bottom - config.ScaleF(2.0f)),
                    pWhiteBrush,
                    1.4f
                );
            }
        }
        pRT->PopAxisAlignedClip();

        // Copy button
        if (hasBtn) {
            float btnW = config.ScaleF(44.0f);
            outBtn = D2D1::RectF(rc.right - btnW - config.ScaleF(6.0f), rc.top + config.ScaleF(3.0f), rc.right - config.ScaleF(6.0f), rc.bottom - config.ScaleF(3.0f));
            bool isHov = (m_hoveredAction == action);
            if (isHov && theme.brushMenuHover) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(outBtn, 3.0f, 3.0f), theme.brushMenuHover.Get());
            }
            pRT->DrawRoundedRectangle(D2D1::RoundedRect(outBtn, 3.0f, 3.0f), pBorderBrush, 0.8f);
            pRT->DrawText(L"Copy", 4, pFmtHint, D2D1::RectF(outBtn.left + config.ScaleF(7.0f), outBtn.top + config.ScaleF(2.0f), outBtn.right, outBtn.bottom),
                          isHov ? pWhiteBrush : pMutedBrush);
        } else {
            outBtn = D2D1::RectF(0, 0, 0, 0);
        }
    };

    if (m_activeTab == DatabaseModalTab::PhpMyAdmin) {
        // Section A: Connection Parameters Card
        D2D1_RECT_F rcSecA = D2D1::RectF(cardX + padX, contentY, cardX + cardW - padX, contentY + config.ScaleF(126.0f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSecA, 6.0f, 6.0f), theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSecA, 6.0f, 6.0f), pBorderBrush, 1.0f);

        D2D1_RECT_F rcHeadA = D2D1::RectF(rcSecA.left + config.ScaleF(12.0f), rcSecA.top + config.ScaleF(7.0f), rcSecA.right, rcSecA.top + config.ScaleF(24.0f));
        pRT->DrawText(L"CREDENTIALS (PHP / CONFIG.PHP)", 29, pFmtHint, rcHeadA, pAccentBrush);

        float rowH = config.ScaleF(26.0f);
        float row1Y = rcSecA.top + config.ScaleF(28.0f);
        float halfW = (rcSecA.right - rcSecA.left - config.ScaleF(28.0f)) * 0.5f;

        // Row 1: Host and Port
        D2D1_RECT_F rcHost = D2D1::RectF(rcSecA.left + config.ScaleF(10.0f), row1Y, rcSecA.left + config.ScaleF(10.0f) + halfW, row1Y + rowH);
        D2D1_RECT_F rcPort = D2D1::RectF(rcSecA.left + config.ScaleF(18.0f) + halfW, row1Y, rcSecA.right - config.ScaleF(10.0f), row1Y + rowH);
        DrawFieldBadge(L"Host ($host)", m_host, rcHost, m_rcCopyHost, DatabaseModalAction::CopyHost, DatabaseEditField::Host, m_rcInputHost);
        DrawFieldBadge(L"Port", m_port, rcPort, m_rcCopyPort, DatabaseModalAction::CopyPort, DatabaseEditField::Port, m_rcInputPort);

        // Row 2: User and Password
        float row2Y = row1Y + rowH + config.ScaleF(5.0f);
        D2D1_RECT_F rcUser = D2D1::RectF(rcSecA.left + config.ScaleF(10.0f), row2Y, rcSecA.left + config.ScaleF(10.0f) + halfW, row2Y + rowH);
        D2D1_RECT_F rcPass = D2D1::RectF(rcSecA.left + config.ScaleF(18.0f) + halfW, row2Y, rcSecA.right - config.ScaleF(10.0f), row2Y + rowH);
        DrawFieldBadge(L"User ($user)", m_user, rcUser, m_rcCopyUser, DatabaseModalAction::CopyUser, DatabaseEditField::User, m_rcInputUser);
        DrawFieldBadge(L"Pass ($pass)", m_pass, rcPass, m_rcCopyPass, DatabaseModalAction::CopyPass, DatabaseEditField::Pass, m_rcInputPass);

        // Row 3: Database and Charset
        float row3Y = row2Y + rowH + config.ScaleF(5.0f);
        D2D1_RECT_F rcDb = D2D1::RectF(rcSecA.left + config.ScaleF(10.0f), row3Y, rcSecA.left + config.ScaleF(10.0f) + halfW, row3Y + rowH);
        D2D1_RECT_F rcCharset = D2D1::RectF(rcSecA.left + config.ScaleF(18.0f) + halfW, row3Y, rcSecA.right - config.ScaleF(10.0f), row3Y + rowH);
        D2D1_RECT_F dummyBtn;
        DrawFieldBadge(L"Database ($db)", m_defaultDb, rcDb, m_rcCopyDb, DatabaseModalAction::CopyDb, DatabaseEditField::Database, m_rcInputDb);
        DrawFieldBadge(L"Charset", m_charset, rcCharset, dummyBtn, DatabaseModalAction::None, DatabaseEditField::Charset, m_rcInputCharset);

        // Section B: Quick PHP Connection Snippets (2-line layout with zero button collisions)
        float secBY = rcSecA.bottom + config.ScaleF(10.0f);
        D2D1_RECT_F rcSecB = D2D1::RectF(cardX + padX, secBY, cardX + cardW - padX, secBY + config.ScaleF(134.0f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSecB, 6.0f, 6.0f), theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSecB, 6.0f, 6.0f), pBorderBrush, 1.0f);

        D2D1_RECT_F rcHeadB = D2D1::RectF(rcSecB.left + config.ScaleF(12.0f), rcSecB.top + config.ScaleF(7.0f), rcSecB.right, rcSecB.top + config.ScaleF(24.0f));
        pRT->DrawText(L"READY-TO-USE PHP CODE SNIPPETS", 30, pFmtHint, rcHeadB, pAccentBrush);

        float snipH = config.ScaleF(45.0f);

        // Snippet 1: PDO
        float snip1Y = rcSecB.top + config.ScaleF(28.0f);
        D2D1_RECT_F rcSnip1 = D2D1::RectF(rcSecB.left + config.ScaleF(10.0f), snip1Y, rcSecB.right - config.ScaleF(10.0f), snip1Y + snipH);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSnip1, 4.0f, 4.0f), theme.brushCanvasBg ? theme.brushCanvasBg.Get() : theme.brushDrawerBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSnip1, 4.0f, 4.0f), pBorderBrush, 0.8f);

        D2D1_RECT_F rcSnip1Lbl = D2D1::RectF(rcSnip1.left + config.ScaleF(10.0f), rcSnip1.top + config.ScaleF(3.0f), rcSnip1.left + config.ScaleF(260.0f), rcSnip1.top + config.ScaleF(18.0f));
        pRT->DrawText(L"PDO (PHP Data Objects)", 22, pFmtHint, rcSnip1Lbl, pMutedBrush);

        m_rcCopyPdoSnippet = D2D1::RectF(rcSnip1.right - config.ScaleF(84.0f), rcSnip1.top + config.ScaleF(3.0f), rcSnip1.right - config.ScaleF(6.0f), rcSnip1.top + config.ScaleF(20.0f));
        bool isPdoHov = (m_hoveredAction == DatabaseModalAction::CopyPdoSnippet);
        if (isPdoHov && theme.brushMenuHover) pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcCopyPdoSnippet, 3.0f, 3.0f), theme.brushMenuHover.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(m_rcCopyPdoSnippet, 3.0f, 3.0f), pBorderBrush, 0.8f);
        pRT->DrawText(L"Copy PDO", 8, pFmtHint, D2D1::RectF(m_rcCopyPdoSnippet.left + config.ScaleF(10.0f), m_rcCopyPdoSnippet.top + config.ScaleF(1.0f), m_rcCopyPdoSnippet.right, m_rcCopyPdoSnippet.bottom),
                      isPdoHov ? pWhiteBrush : pMutedBrush);

        // Code line with AXIS-ALIGNED CLIPPING
        D2D1_RECT_F rcSnip1Code = D2D1::RectF(rcSnip1.left + config.ScaleF(10.0f), rcSnip1.top + config.ScaleF(21.0f), rcSnip1.right - config.ScaleF(10.0f), rcSnip1.bottom - config.ScaleF(2.0f));
        pRT->PushAxisAlignedClip(rcSnip1Code, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(m_pdoSnippet.c_str(), static_cast<UINT32>(m_pdoSnippet.size()), pFmtCode, rcSnip1Code, pWhiteBrush);
        pRT->PopAxisAlignedClip();

        // Snippet 2: MySQLi
        float snip2Y = snip1Y + snipH + config.ScaleF(6.0f);
        D2D1_RECT_F rcSnip2 = D2D1::RectF(rcSecB.left + config.ScaleF(10.0f), snip2Y, rcSecB.right - config.ScaleF(10.0f), snip2Y + snipH);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSnip2, 4.0f, 4.0f), theme.brushCanvasBg ? theme.brushCanvasBg.Get() : theme.brushDrawerBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSnip2, 4.0f, 4.0f), pBorderBrush, 0.8f);

        D2D1_RECT_F rcSnip2Lbl = D2D1::RectF(rcSnip2.left + config.ScaleF(10.0f), rcSnip2.top + config.ScaleF(3.0f), rcSnip2.left + config.ScaleF(260.0f), rcSnip2.top + config.ScaleF(18.0f));
        pRT->DrawText(L"MySQLi Object-Oriented", 22, pFmtHint, rcSnip2Lbl, pMutedBrush);

        m_rcCopyMysqliSnippet = D2D1::RectF(rcSnip2.right - config.ScaleF(96.0f), rcSnip2.top + config.ScaleF(3.0f), rcSnip2.right - config.ScaleF(6.0f), rcSnip2.top + config.ScaleF(20.0f));
        bool isMysqliHov = (m_hoveredAction == DatabaseModalAction::CopyMysqliSnippet);
        if (isMysqliHov && theme.brushMenuHover) pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcCopyMysqliSnippet, 3.0f, 3.0f), theme.brushMenuHover.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(m_rcCopyMysqliSnippet, 3.0f, 3.0f), pBorderBrush, 0.8f);
        pRT->DrawText(L"Copy MySQLi", 11, pFmtHint, D2D1::RectF(m_rcCopyMysqliSnippet.left + config.ScaleF(10.0f), m_rcCopyMysqliSnippet.top + config.ScaleF(1.0f), m_rcCopyMysqliSnippet.right, m_rcCopyMysqliSnippet.bottom),
                      isMysqliHov ? pWhiteBrush : pMutedBrush);

        // Code line with AXIS-ALIGNED CLIPPING
        D2D1_RECT_F rcSnip2Code = D2D1::RectF(rcSnip2.left + config.ScaleF(10.0f), rcSnip2.top + config.ScaleF(21.0f), rcSnip2.right - config.ScaleF(10.0f), rcSnip2.bottom - config.ScaleF(2.0f));
        pRT->PushAxisAlignedClip(rcSnip2Code, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(m_mysqliSnippet.c_str(), static_cast<UINT32>(m_mysqliSnippet.size()), pFmtCode, rcSnip2Code, pWhiteBrush);
        pRT->PopAxisAlignedClip();

        // Section C: phpMyAdmin Runtime & Action Buttons
        float secCY = rcSecB.bottom + config.ScaleF(10.0f);
        D2D1_RECT_F rcSecC = D2D1::RectF(cardX + padX, secCY, cardX + cardW - padX, secCY + config.ScaleF(74.0f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSecC, 6.0f, 6.0f), theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSecC, 6.0f, 6.0f), pBorderBrush, 1.0f);

        D2D1_RECT_F rcHeadC = D2D1::RectF(rcSecC.left + config.ScaleF(12.0f), rcSecC.top + config.ScaleF(7.0f), rcSecC.right, rcSecC.top + config.ScaleF(24.0f));
        pRT->DrawText(L"PHPMYADMIN STATUS & ACTIONS", 27, pFmtHint, rcHeadC, pAccentBrush);

        std::wstring pmaStatusStr = m_isPmaRunning ? (L"● Status: Running (" + m_pmaUrl + L")") : L"● Status: Ready for PHP Run (Auth: config)";
        D2D1_RECT_F rcStatus = D2D1::RectF(rcSecC.left + config.ScaleF(12.0f), rcSecC.top + config.ScaleF(32.0f), rcSecC.left + config.ScaleF(270.0f), rcSecC.bottom);
        pRT->PushAxisAlignedClip(rcStatus, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(pmaStatusStr.c_str(), static_cast<UINT32>(pmaStatusStr.size()), pFmtMain, rcStatus, pWhiteBrush);
        pRT->PopAxisAlignedClip();

        // 3 Action Buttons: [▶ Start / ■ Stop phpMyAdmin] [Open in Browser] [Edit config.inc.php]
        float actH = config.ScaleF(30.0f);
        float btnY = rcSecC.top + config.ScaleF(28.0f);

        float actEditW = config.ScaleF(145.0f);
        m_rcBtnEditPma = D2D1::RectF(rcSecC.right - actEditW - config.ScaleF(6.0f), btnY, rcSecC.right - config.ScaleF(6.0f), btnY + actH);

        float actOpenW = config.ScaleF(135.0f);
        m_rcBtnOpenPma = D2D1::RectF(m_rcBtnEditPma.left - actOpenW - config.ScaleF(8.0f), btnY, m_rcBtnEditPma.left - config.ScaleF(8.0f), btnY + actH);

        float actRunW = config.ScaleF(145.0f);
        m_rcBtnTogglePma = D2D1::RectF(m_rcBtnOpenPma.left - actRunW - config.ScaleF(8.0f), btnY, m_rcBtnOpenPma.left - config.ScaleF(8.0f), btnY + actH);

        bool isTogglePmaHov = (m_hoveredAction == DatabaseModalAction::TogglePmaRun);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bPmaRunBg;
        if (m_isPmaRunning) {
            pRT->CreateSolidColorBrush(isTogglePmaHov ? D2D1::ColorF(0.85f, 0.25f, 0.25f, 1.0f) : D2D1::ColorF(0.70f, 0.20f, 0.20f, 0.90f), &bPmaRunBg);
        } else {
            pRT->CreateSolidColorBrush(isTogglePmaHov ? D2D1::ColorF(0.20f, 0.70f, 0.35f, 1.0f) : D2D1::ColorF(0.15f, 0.58f, 0.28f, 0.90f), &bPmaRunBg);
        }
        if (bPmaRunBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnTogglePma, 4.0f, 4.0f), bPmaRunBg.Get());
        const wchar_t* pmaToggleText = m_isPmaRunning ? L"■ Stop phpMyAdmin" : L"▶ Start phpMyAdmin";
        pRT->DrawText(pmaToggleText, static_cast<UINT32>(wcslen(pmaToggleText)), pFmtMain,
                      D2D1::RectF(m_rcBtnTogglePma.left + config.ScaleF(12.0f), m_rcBtnTogglePma.top + config.ScaleF(6.0f), m_rcBtnTogglePma.right, m_rcBtnTogglePma.bottom),
                      pWhiteBrush);

        bool isOpenPmaHov = (m_hoveredAction == DatabaseModalAction::OpenBrowserPma);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnOpenPma, 4.0f, 4.0f), isOpenPmaHov ? pAccentBrush : (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : pWhiteBrush));
        pRT->DrawText(L"Open in Browser", 15, pFmtMain,
                      D2D1::RectF(m_rcBtnOpenPma.left + config.ScaleF(16.0f), m_rcBtnOpenPma.top + config.ScaleF(6.0f), m_rcBtnOpenPma.right, m_rcBtnOpenPma.bottom),
                      pWhiteBrush);

        bool isEditPmaHov = (m_hoveredAction == DatabaseModalAction::EditPmaConfig);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnEditPma, 4.0f, 4.0f), isEditPmaHov ? theme.brushMenuHover.Get() : pBorderBrush);
        pRT->DrawText(L"Edit config.inc.php", 19, pFmtMain,
                      D2D1::RectF(m_rcBtnEditPma.left + config.ScaleF(12.0f), m_rcBtnEditPma.top + config.ScaleF(6.0f), m_rcBtnEditPma.right, m_rcBtnEditPma.bottom),
                      pWhiteBrush);
    } else {
        // Nginx Tab
        // Section A: Parameters Card
        D2D1_RECT_F rcSecN = D2D1::RectF(cardX + padX, contentY, cardX + cardW - padX, contentY + config.ScaleF(134.0f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSecN, 6.0f, 6.0f), theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSecN, 6.0f, 6.0f), pBorderBrush, 1.0f);

        D2D1_RECT_F rcHeadN = D2D1::RectF(rcSecN.left + config.ScaleF(12.0f), rcSecN.top + config.ScaleF(7.0f), rcSecN.right, rcSecN.top + config.ScaleF(24.0f));
        pRT->DrawText(L"NGINX WEB SERVER PARAMETERS", 27, pFmtHint, rcHeadN, pAccentBrush);

        float rowH = config.ScaleF(26.0f);
        float row1Y = rcSecN.top + config.ScaleF(28.0f);
        float halfW = (rcSecN.right - rcSecN.left - config.ScaleF(28.0f)) * 0.5f;

        // Row 1: URL and Port
        D2D1_RECT_F rcUrl = D2D1::RectF(rcSecN.left + config.ScaleF(10.0f), row1Y, rcSecN.left + config.ScaleF(10.0f) + halfW, row1Y + rowH);
        D2D1_RECT_F rcPort = D2D1::RectF(rcSecN.left + config.ScaleF(18.0f) + halfW, row1Y, rcSecN.right - config.ScaleF(10.0f), row1Y + rowH);
        DrawFieldBadge(L"Local URL", m_nginxUrl, rcUrl, m_rcCopyNginxUrl, DatabaseModalAction::CopyNginxUrl, DatabaseEditField::None, dummyInput);
        DrawFieldBadge(L"Port", m_nginxPort, rcPort, m_rcCopyNginxPort, DatabaseModalAction::CopyNginxPort, DatabaseEditField::NginxPort, m_rcInputNginxPort);

        // Row 2: Server Name & FastCGI Upstream
        float row2Y = row1Y + rowH + config.ScaleF(5.0f);
        D2D1_RECT_F rcHost = D2D1::RectF(rcSecN.left + config.ScaleF(10.0f), row2Y, rcSecN.left + config.ScaleF(10.0f) + halfW, row2Y + rowH);
        D2D1_RECT_F rcFastCgi = D2D1::RectF(rcSecN.left + config.ScaleF(18.0f) + halfW, row2Y, rcSecN.right - config.ScaleF(10.0f), row2Y + rowH);
        D2D1_RECT_F dummyBtn;
        DrawFieldBadge(L"Server Name", m_nginxHost, rcHost, dummyBtn, DatabaseModalAction::None, DatabaseEditField::NginxHost, m_rcInputNginxHost);
        DrawFieldBadge(L"FastCGI PHP", L"127.0.0.1:9000", rcFastCgi, dummyBtn, DatabaseModalAction::None, DatabaseEditField::None, dummyInput);

        // Row 3: Config file with copy button
        float row3Y = row2Y + rowH + config.ScaleF(5.0f);
        D2D1_RECT_F rcCfg = D2D1::RectF(rcSecN.left + config.ScaleF(10.0f), row3Y, rcSecN.right - config.ScaleF(10.0f), row3Y + rowH);
        DrawFieldBadge(L"Config File", m_nginxConfigFile, rcCfg, m_rcCopyNginxConfigPath, DatabaseModalAction::CopyNginxConfigPath, DatabaseEditField::None, dummyInput);

        // Section B: Document Root & Binary Card
        float secNBY = rcSecN.bottom + config.ScaleF(10.0f);
        D2D1_RECT_F rcSecNB = D2D1::RectF(cardX + padX, secNBY, cardX + cardW - padX, secNBY + config.ScaleF(96.0f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSecNB, 6.0f, 6.0f), theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSecNB, 6.0f, 6.0f), pBorderBrush, 1.0f);

        D2D1_RECT_F rcHeadNB = D2D1::RectF(rcSecNB.left + config.ScaleF(12.0f), rcSecNB.top + config.ScaleF(7.0f), rcSecNB.right, rcSecNB.top + config.ScaleF(24.0f));
        pRT->DrawText(L"NGINX DOCUMENT ROOT & BINARY", 28, pFmtHint, rcHeadNB, pAccentBrush);

        float nbRow1Y = rcSecNB.top + config.ScaleF(28.0f);
        D2D1_RECT_F rcDocRoot = D2D1::RectF(rcSecNB.left + config.ScaleF(10.0f), nbRow1Y, rcSecNB.right - config.ScaleF(10.0f), nbRow1Y + rowH);
        DrawFieldBadge(L"Doc Root", L"include\\phide\\server\\nginx\\html", rcDocRoot, dummyBtn, DatabaseModalAction::None, DatabaseEditField::None, dummyInput);

        float nbRow2Y = nbRow1Y + rowH + config.ScaleF(5.0f);
        D2D1_RECT_F rcBin = D2D1::RectF(rcSecNB.left + config.ScaleF(10.0f), nbRow2Y, rcSecNB.right - config.ScaleF(10.0f), nbRow2Y + rowH);
        DrawFieldBadge(L"Executable", L"include\\phide\\server\\nginx\\nginx.exe", rcBin, dummyBtn, DatabaseModalAction::None, DatabaseEditField::None, dummyInput);

        // Section C: Nginx Actions & Tools Card
        float secNY = rcSecNB.bottom + config.ScaleF(10.0f);
        D2D1_RECT_F rcSecNAct = D2D1::RectF(cardX + padX, secNY, cardX + cardW - padX, secNY + config.ScaleF(74.0f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSecNAct, 6.0f, 6.0f), theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSecNAct, 6.0f, 6.0f), pBorderBrush, 1.0f);

        D2D1_RECT_F rcHeadNAct = D2D1::RectF(rcSecNAct.left + config.ScaleF(12.0f), rcSecNAct.top + config.ScaleF(7.0f), rcSecNAct.right, rcSecNAct.top + config.ScaleF(24.0f));
        pRT->DrawText(L"NGINX ACTIONS & TOOLS", 21, pFmtHint, rcHeadNAct, pAccentBrush);

        std::wstring nginxStatusStr = m_isNginxRunning ? (L"● Status: Running (" + m_nginxUrl + L")") : (L"● Status: Ready / Standby (Port " + m_nginxPort + L")");
        D2D1_RECT_F rcNginxStatus = D2D1::RectF(rcSecNAct.left + config.ScaleF(12.0f), rcSecNAct.top + config.ScaleF(32.0f), rcSecNAct.left + config.ScaleF(270.0f), rcSecNAct.bottom);
        pRT->PushAxisAlignedClip(rcNginxStatus, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(nginxStatusStr.c_str(), static_cast<UINT32>(nginxStatusStr.size()), pFmtMain, rcNginxStatus, pWhiteBrush);
        pRT->PopAxisAlignedClip();

        float actH = config.ScaleF(30.0f);
        float btnNY = rcSecNAct.top + config.ScaleF(28.0f);

        float actEditW = config.ScaleF(135.0f);
        m_rcBtnEditNginx = D2D1::RectF(rcSecNAct.right - actEditW - config.ScaleF(6.0f), btnNY, rcSecNAct.right - config.ScaleF(6.0f), btnNY + actH);

        float actOpenW = config.ScaleF(135.0f);
        m_rcBtnOpenNginx = D2D1::RectF(m_rcBtnEditNginx.left - actOpenW - config.ScaleF(8.0f), btnNY, m_rcBtnEditNginx.left - config.ScaleF(8.0f), btnNY + actH);

        float actRunW = config.ScaleF(130.0f);
        m_rcBtnToggleNginx = D2D1::RectF(m_rcBtnOpenNginx.left - actRunW - config.ScaleF(8.0f), btnNY, m_rcBtnOpenNginx.left - config.ScaleF(8.0f), btnNY + actH);

        bool isToggleNginxHov = (m_hoveredAction == DatabaseModalAction::ToggleServerRun);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bNginxRunBg;
        if (m_isNginxRunning) {
            pRT->CreateSolidColorBrush(isToggleNginxHov ? D2D1::ColorF(0.85f, 0.25f, 0.25f, 1.0f) : D2D1::ColorF(0.70f, 0.20f, 0.20f, 0.90f), &bNginxRunBg);
        } else {
            pRT->CreateSolidColorBrush(isToggleNginxHov ? D2D1::ColorF(0.20f, 0.70f, 0.35f, 1.0f) : D2D1::ColorF(0.15f, 0.58f, 0.28f, 0.90f), &bNginxRunBg);
        }
        if (bNginxRunBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnToggleNginx, 4.0f, 4.0f), bNginxRunBg.Get());
        const wchar_t* nginxToggleText = m_isNginxRunning ? L"■ Stop Nginx" : L"▶ Start Nginx";
        pRT->DrawText(nginxToggleText, static_cast<UINT32>(wcslen(nginxToggleText)), pFmtMain,
                      D2D1::RectF(m_rcBtnToggleNginx.left + config.ScaleF(16.0f), m_rcBtnToggleNginx.top + config.ScaleF(6.0f), m_rcBtnToggleNginx.right, m_rcBtnToggleNginx.bottom),
                      pWhiteBrush);

        bool isOpenNginxHov = (m_hoveredAction == DatabaseModalAction::OpenBrowserNginx);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnOpenNginx, 4.0f, 4.0f), isOpenNginxHov ? pAccentBrush : (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : pWhiteBrush));
        pRT->DrawText(L"Open in Browser", 15, pFmtMain,
                      D2D1::RectF(m_rcBtnOpenNginx.left + config.ScaleF(16.0f), m_rcBtnOpenNginx.top + config.ScaleF(6.0f), m_rcBtnOpenNginx.right, m_rcBtnOpenNginx.bottom),
                      pWhiteBrush);

        bool isEditNginxHov = (m_hoveredAction == DatabaseModalAction::EditNginxConfig);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnEditNginx, 4.0f, 4.0f), isEditNginxHov ? theme.brushMenuHover.Get() : pBorderBrush);
        pRT->DrawText(L"Edit nginx.conf", 15, pFmtMain,
                      D2D1::RectF(m_rcBtnEditNginx.left + config.ScaleF(18.0f), m_rcBtnEditNginx.top + config.ScaleF(6.0f), m_rcBtnEditNginx.right, m_rcBtnEditNginx.bottom),
                      pWhiteBrush);
    }

    // 5. Footer: Copied Feedback + Done Button
    float footY = cardY + cardH - config.ScaleF(46.0f);
    if (!m_copiedField.empty()) {
        std::wstring fb = L"✓ Copied " + m_copiedField + L" to clipboard!";
        pRT->DrawText(fb.c_str(), static_cast<UINT32>(fb.size()), pFmtMain,
                      D2D1::RectF(cardX + padX, footY + config.ScaleF(6.0f), cardX + cardW - config.ScaleF(120.0f), footY + config.ScaleF(30.0f)),
                      pAccentBrush);
    } else {
        const wchar_t* hintFooter = L"Paste these connection variables directly into your config.php PDO constructor.";
        pRT->DrawText(hintFooter, static_cast<UINT32>(wcslen(hintFooter)), pFmtHint,
                      D2D1::RectF(cardX + padX, footY + config.ScaleF(6.0f), cardX + cardW - config.ScaleF(120.0f), footY + config.ScaleF(30.0f)),
                      pMutedBrush);
    }

    float doneW = config.ScaleF(90.0f);
    float doneH = config.ScaleF(28.0f);
    m_rcBtnDone = D2D1::RectF(cardX + cardW - padX - doneW, footY, cardX + cardW - padX, footY + doneH);
    bool isDoneHov = (m_hoveredAction == DatabaseModalAction::CloseModal);
    pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnDone, 4.0f, 4.0f), isDoneHov ? theme.brushMenuHover.Get() : pBorderBrush);
    pRT->DrawText(L"Done", 4, pFmtMain,
                  D2D1::RectF(m_rcBtnDone.left + config.ScaleF(26.0f), m_rcBtnDone.top + config.ScaleF(4.0f), m_rcBtnDone.right, m_rcBtnDone.bottom),
                  pWhiteBrush);

    if (!m_sqlFileName.empty() && m_activeTab == DatabaseModalTab::PhpMyAdmin) {
        float importW = config.ScaleF(170.0f);
        m_rcBtnImportSql = D2D1::RectF(m_rcBtnDone.left - importW - config.ScaleF(10.0f), footY, m_rcBtnDone.left - config.ScaleF(10.0f), footY + doneH);
        bool isImportHov = (m_hoveredAction == DatabaseModalAction::ImportSql);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnImportSql, 4.0f, 4.0f), isImportHov ? pAccentBrush : pBorderBrush);
        std::wstring importLabel = L"⚡ Import " + m_sqlFileName;
        pRT->DrawText(importLabel.c_str(), static_cast<UINT32>(importLabel.size()), pFmtMain,
                      D2D1::RectF(m_rcBtnImportSql.left + config.ScaleF(12.0f), m_rcBtnImportSql.top + config.ScaleF(4.0f), m_rcBtnImportSql.right, m_rcBtnImportSql.bottom),
                      pWhiteBrush);
    } else {
        m_rcBtnImportSql = D2D1::RectF(0, 0, 0, 0);
    }
}

} // namespace phide::ui
