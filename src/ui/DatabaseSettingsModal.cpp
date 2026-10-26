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
        if (target->size() < 128) {
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

    if (m_rcTabPma.right > m_rcTabPma.left && PtInRectF(pt, m_rcTabPma)) return DatabaseModalAction::SwitchTabPma;
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
    } else {
        if (PtInRectF(pt, m_rcInputNginxPort)) { m_focusedField = DatabaseEditField::NginxPort; m_caretVisible = true; return; }
        if (PtInRectF(pt, m_rcInputNginxHost)) { m_focusedField = DatabaseEditField::NginxHost; m_caretVisible = true; return; }
    }

    if (PtInRectF(pt, m_rcModalCard)) {
        m_focusedField = DatabaseEditField::None;
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

    // 1. Semi-transparent backdrop overlay
    D2D1_RECT_F rcBackdrop = D2D1::RectF(0.0f, 0.0f, clientW, clientH);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bBackdrop;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.65f), &bBackdrop);
    if (bBackdrop) pRT->FillRectangle(rcBackdrop, bBackdrop.Get());

    // 2. Modal Window Dimensions (Bento card style)
    float cardW = (std::min)(config.ScaleF(740.0f), clientW - config.ScaleF(32.0f));
    float cardH = (std::min)(config.ScaleF(560.0f), clientH - config.ScaleF(32.0f));
    float cardX = (clientW - cardW) * 0.5f;
    float cardY = (clientH - cardH) * 0.5f;

    m_rcModalCard = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);
    D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_rcModalCard, 8.0f, 8.0f);

    // Card background & subtle border
    pRT->FillRoundedRectangle(rrcCard, theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
    pRT->DrawRoundedRectangle(rrcCard, theme.brushMenuBorder ? theme.brushMenuBorder.Get() : theme.brushBorderSubtle.Get(), 1.0f);

    ID2D1Brush* pWhiteBrush = theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
    ID2D1Brush* pMutedBrush = theme.brushTextMuted ? theme.brushTextMuted.Get() : theme.brushTextSecondary.Get();
    ID2D1Brush* pAccentBrush = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextPrimary.Get();
    ID2D1Brush* pBorderBrush = theme.brushBorderSubtle ? theme.brushBorderSubtle.Get() : theme.brushMenuBorder.Get();

    IDWriteTextFormat* pFmtTitle = theme.fontTitle ? theme.fontTitle.Get() : theme.fontBody.Get();
    IDWriteTextFormat* pFmtMain = theme.fontBody ? theme.fontBody.Get() : theme.fontHint.Get();
    IDWriteTextFormat* pFmtHint = theme.fontHint ? theme.fontHint.Get() : theme.fontBody.Get();

    float padX = config.ScaleF(20.0f);
    float topY = cardY + config.ScaleF(16.0f);

    // 3. Header: Icon + Title + Close Button [X]
    float icoS = config.ScaleF(20.0f);
    D2D1_RECT_F rcHeaderIcon = D2D1::RectF(cardX + padX, topY + config.ScaleF(2.0f), cardX + padX + icoS, topY + config.ScaleF(2.0f) + icoS);
    if (m_activeTab == DatabaseModalTab::PhpMyAdmin) {
        m_icons.DrawDatabase(pRT, rcHeaderIcon, pAccentBrush, 1.4f);
    } else {
        m_icons.DrawServer(pRT, rcHeaderIcon, pAccentBrush, 1.4f);
    }

    D2D1_RECT_F rcTitle = D2D1::RectF(cardX + padX + icoS + config.ScaleF(8.0f), topY, cardX + cardW - padX - config.ScaleF(40.0f), topY + config.ScaleF(24.0f));
    const wchar_t* titleStr = (m_activeTab == DatabaseModalTab::PhpMyAdmin) ? L"Database & phpMyAdmin Configuration" : L"Nginx Web Server Configuration";
    pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)), pFmtTitle, rcTitle, pWhiteBrush);

    // Close button [X]
    float closeS = config.ScaleF(24.0f);
    m_rcCloseBtn = D2D1::RectF(cardX + cardW - padX - closeS, topY, cardX + cardW - padX, topY + closeS);
    bool isCloseHov = (m_hoveredAction == DatabaseModalAction::CloseModal);
    if (isCloseHov && theme.brushMenuHover) {
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcCloseBtn, 4.0f, 4.0f), theme.brushMenuHover.Get());
    }
    m_icons.DrawClose(pRT, m_rcCloseBtn, isCloseHov ? pWhiteBrush : pMutedBrush, 1.3f);

    // 4. Segmented Tabs: [Database (MySQL / phpMyAdmin)] and [Web Server (Nginx)]
    float tabY = topY + config.ScaleF(48.0f);
    float tabW = (cardW - padX * 2.0f) / 2.0f;
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

    pRT->DrawText(L"phpMyAdmin (MySQL)", 19, pFmtMain,
                  D2D1::RectF(m_rcTabPma.left + config.ScaleF(10.0f), m_rcTabPma.top + config.ScaleF(6.0f), m_rcTabPma.right, m_rcTabPma.bottom),
                  (m_activeTab == DatabaseModalTab::PhpMyAdmin) ? pWhiteBrush : pMutedBrush);

    pRT->DrawText(L"Web Server (Nginx)", 18, pFmtMain,
                  D2D1::RectF(m_rcTabNginx.left + config.ScaleF(10.0f), m_rcTabNginx.top + config.ScaleF(6.0f), m_rcTabNginx.right, m_rcTabNginx.bottom),
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
        pRT->PushAxisAlignedClip(rcVal, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        std::wstring displayVal = value.empty() ? L"(none)" : value;
        if (isFocused && m_caretVisible) displayVal += L"|";
        pRT->DrawText(displayVal.c_str(), static_cast<UINT32>(displayVal.size()), pFmtMain, rcVal, isFocused ? pAccentBrush : pWhiteBrush);
        pRT->PopAxisAlignedClip();

        // Copy button on right side of field
        if (hasBtn) {
            float btnW = config.ScaleF(46.0f);
            outBtn = D2D1::RectF(rc.right - btnW - config.ScaleF(4.0f), rc.top + config.ScaleF(3.0f), rc.right - config.ScaleF(4.0f), rc.bottom - config.ScaleF(3.0f));
            bool isHov = (m_hoveredAction == action);
            if (isHov && theme.brushMenuHover) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(outBtn, 3.0f, 3.0f), theme.brushMenuHover.Get());
            }
            pRT->DrawRoundedRectangle(D2D1::RoundedRect(outBtn, 3.0f, 3.0f), pBorderBrush, 0.8f);
            pRT->DrawText(L"Copy", 4, pFmtHint, D2D1::RectF(outBtn.left + config.ScaleF(7.0f), outBtn.top + config.ScaleF(2.0f), outBtn.right, outBtn.bottom), isHov ? pWhiteBrush : pMutedBrush);
        } else {
            outBtn = D2D1::RectF(0, 0, 0, 0);
        }
    };

    if (m_activeTab == DatabaseModalTab::PhpMyAdmin) {
        // Section A: Parameters Card (Host, Port, User, Pass, Db, Charset)
        D2D1_RECT_F rcSecA = D2D1::RectF(cardX + padX, contentY, cardX + cardW - padX, contentY + config.ScaleF(144.0f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSecA, 6.0f, 6.0f), theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSecA, 6.0f, 6.0f), pBorderBrush, 1.0f);

        D2D1_RECT_F rcHeadA = D2D1::RectF(rcSecA.left + config.ScaleF(12.0f), rcSecA.top + config.ScaleF(7.0f), rcSecA.right, rcSecA.top + config.ScaleF(24.0f));
        pRT->DrawText(L"CONNECTION PARAMETERS (MYSQL / CONFIG.INC.PHP)", 44, pFmtHint, rcHeadA, pAccentBrush);

        float rowH = config.ScaleF(26.0f);
        float row1Y = rcSecA.top + config.ScaleF(28.0f);
        float halfW = (rcSecA.right - rcSecA.left - config.ScaleF(28.0f)) * 0.5f;

        // Row 1: Host & Port
        D2D1_RECT_F rcHost = D2D1::RectF(rcSecA.left + config.ScaleF(10.0f), row1Y, rcSecA.left + config.ScaleF(10.0f) + halfW, row1Y + rowH);
        D2D1_RECT_F rcPort = D2D1::RectF(rcSecA.left + config.ScaleF(18.0f) + halfW, row1Y, rcSecA.right - config.ScaleF(10.0f), row1Y + rowH);
        DrawFieldBadge(L"Host", m_host, rcHost, m_rcCopyHost, DatabaseModalAction::CopyHost, DatabaseEditField::Host, m_rcInputHost);
        DrawFieldBadge(L"Port", m_port, rcPort, m_rcCopyPort, DatabaseModalAction::CopyPort, DatabaseEditField::Port, m_rcInputPort);

        // Row 2: User & Password
        float row2Y = row1Y + rowH + config.ScaleF(5.0f);
        D2D1_RECT_F rcUser = D2D1::RectF(rcSecA.left + config.ScaleF(10.0f), row2Y, rcSecA.left + config.ScaleF(10.0f) + halfW, row2Y + rowH);
        D2D1_RECT_F rcPass = D2D1::RectF(rcSecA.left + config.ScaleF(18.0f) + halfW, row2Y, rcSecA.right - config.ScaleF(10.0f), row2Y + rowH);
        DrawFieldBadge(L"User", m_user, rcUser, m_rcCopyUser, DatabaseModalAction::CopyUser, DatabaseEditField::User, m_rcInputUser);
        DrawFieldBadge(L"Password", m_pass.empty() ? L"(empty)" : m_pass, rcPass, m_rcCopyPass, DatabaseModalAction::CopyPass, DatabaseEditField::Pass, m_rcInputPass);

        // Row 3: Database & Charset
        float row3Y = row2Y + rowH + config.ScaleF(5.0f);
        D2D1_RECT_F rcDb = D2D1::RectF(rcSecA.left + config.ScaleF(10.0f), row3Y, rcSecA.left + config.ScaleF(10.0f) + halfW, row3Y + rowH);
        D2D1_RECT_F rcCharset = D2D1::RectF(rcSecA.left + config.ScaleF(18.0f) + halfW, row3Y, rcSecA.right - config.ScaleF(10.0f), row3Y + rowH);
        DrawFieldBadge(L"Database", m_defaultDb, rcDb, m_rcCopyDb, DatabaseModalAction::CopyDb, DatabaseEditField::Database, m_rcInputDb);
        DrawFieldBadge(L"Charset", m_charset, rcCharset, dummyInput, DatabaseModalAction::None, DatabaseEditField::Charset, m_rcInputCharset);

        // Section B: Snippets Card (PDO & MySQLi)
        float secBY = rcSecA.bottom + config.ScaleF(10.0f);
        D2D1_RECT_F rcSecB = D2D1::RectF(cardX + padX, secBY, cardX + cardW - padX, secBY + config.ScaleF(96.0f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSecB, 6.0f, 6.0f), theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSecB, 6.0f, 6.0f), pBorderBrush, 1.0f);

        D2D1_RECT_F rcHeadB = D2D1::RectF(rcSecB.left + config.ScaleF(12.0f), rcSecB.top + config.ScaleF(7.0f), rcSecB.right, rcSecB.top + config.ScaleF(24.0f));
        pRT->DrawText(L"PHP DATABASE CONNECTION SNIPPETS", 32, pFmtHint, rcHeadB, pAccentBrush);

        float snipRow1Y = rcSecB.top + config.ScaleF(28.0f);
        D2D1_RECT_F rcPdo = D2D1::RectF(rcSecB.left + config.ScaleF(10.0f), snipRow1Y, rcSecB.right - config.ScaleF(10.0f), snipRow1Y + rowH);
        DrawFieldBadge(L"PDO Snippet", m_pdoSnippet, rcPdo, m_rcCopyPdoSnippet, DatabaseModalAction::CopyPdoSnippet, DatabaseEditField::None, dummyInput);

        float snipRow2Y = snipRow1Y + rowH + config.ScaleF(5.0f);
        D2D1_RECT_F rcMysqli = D2D1::RectF(rcSecB.left + config.ScaleF(10.0f), snipRow2Y, rcSecB.right - config.ScaleF(10.0f), snipRow2Y + rowH);
        DrawFieldBadge(L"MySQLi", m_mysqliSnippet, rcMysqli, m_rcCopyMysqliSnippet, DatabaseModalAction::CopyMysqliSnippet, DatabaseEditField::None, dummyInput);

        // Section C: phpMyAdmin Status & Action Buttons Card
        float secCY = rcSecB.bottom + config.ScaleF(10.0f);
        D2D1_RECT_F rcSecC = D2D1::RectF(cardX + padX, secCY, cardX + cardW - padX, secCY + config.ScaleF(74.0f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSecC, 6.0f, 6.0f), theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSecC, 6.0f, 6.0f), pBorderBrush, 1.0f);

        D2D1_RECT_F rcHeadC = D2D1::RectF(rcSecC.left + config.ScaleF(12.0f), rcSecC.top + config.ScaleF(7.0f), rcSecC.right, rcSecC.top + config.ScaleF(24.0f));
        pRT->DrawText(L"PHPMYADMIN STATUS & ACTIONS", 27, pFmtHint, rcHeadC, pAccentBrush);

        std::wstring pmaStatusStr = m_isPmaRunning ? (L"● Status: Running (" + m_pmaUrl + L")") : (L"● Status: Standby / Ready (Port " + m_pmaWebPort + L")");
        D2D1_RECT_F rcPmaStatus = D2D1::RectF(rcSecC.left + config.ScaleF(12.0f), rcSecC.top + config.ScaleF(32.0f), rcSecC.left + config.ScaleF(270.0f), rcSecC.bottom);
        pRT->PushAxisAlignedClip(rcPmaStatus, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(pmaStatusStr.c_str(), static_cast<UINT32>(pmaStatusStr.size()), pFmtMain, rcPmaStatus, pWhiteBrush);
        pRT->PopAxisAlignedClip();

        float actH = config.ScaleF(30.0f);
        float btnCY = rcSecC.top + config.ScaleF(28.0f);

        float actEditW = config.ScaleF(145.0f);
        m_rcBtnEditPma = D2D1::RectF(rcSecC.right - actEditW - config.ScaleF(6.0f), btnCY, rcSecC.right - config.ScaleF(6.0f), btnCY + actH);

        float actOpenW = config.ScaleF(150.0f);
        m_rcBtnOpenPma = D2D1::RectF(m_rcBtnEditPma.left - actOpenW - config.ScaleF(8.0f), btnCY, m_rcBtnEditPma.left - config.ScaleF(8.0f), btnCY + actH);

        float actRunW = config.ScaleF(130.0f);
        m_rcBtnTogglePma = D2D1::RectF(m_rcBtnOpenPma.left - actRunW - config.ScaleF(8.0f), btnCY, m_rcBtnOpenPma.left - config.ScaleF(8.0f), btnCY + actH);

        bool isToggleHov = (m_hoveredAction == DatabaseModalAction::TogglePmaRun);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bRunBg;
        if (m_isPmaRunning) {
            pRT->CreateSolidColorBrush(isToggleHov ? D2D1::ColorF(0.85f, 0.25f, 0.25f, 1.0f) : D2D1::ColorF(0.70f, 0.20f, 0.20f, 0.90f), &bRunBg);
        } else {
            pRT->CreateSolidColorBrush(isToggleHov ? D2D1::ColorF(0.20f, 0.70f, 0.35f, 1.0f) : D2D1::ColorF(0.15f, 0.58f, 0.28f, 0.90f), &bRunBg);
        }
        if (bRunBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnTogglePma, 4.0f, 4.0f), bRunBg.Get());
        const wchar_t* toggleText = m_isPmaRunning ? L"■ Stop PMA" : L"▶ Start PMA";
        pRT->DrawText(toggleText, static_cast<UINT32>(wcslen(toggleText)), pFmtMain,
                      D2D1::RectF(m_rcBtnTogglePma.left + config.ScaleF(18.0f), m_rcBtnTogglePma.top + config.ScaleF(6.0f), m_rcBtnTogglePma.right, m_rcBtnTogglePma.bottom),
                      pWhiteBrush);

        bool isOpenHov = (m_hoveredAction == DatabaseModalAction::OpenBrowserPma);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnOpenPma, 4.0f, 4.0f), isOpenHov ? pAccentBrush : (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : pWhiteBrush));
        pRT->DrawText(L"Open phpMyAdmin", 15, pFmtMain,
                      D2D1::RectF(m_rcBtnOpenPma.left + config.ScaleF(14.0f), m_rcBtnOpenPma.top + config.ScaleF(6.0f), m_rcBtnOpenPma.right, m_rcBtnOpenPma.bottom),
                      pWhiteBrush);

        bool isEditHov = (m_hoveredAction == DatabaseModalAction::EditPmaConfig);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnEditPma, 4.0f, 4.0f), isEditHov ? theme.brushMenuHover.Get() : pBorderBrush);
        pRT->DrawText(L"Edit config.inc.php", 18, pFmtMain,
                      D2D1::RectF(m_rcBtnEditPma.left + config.ScaleF(10.0f), m_rcBtnEditPma.top + config.ScaleF(6.0f), m_rcBtnEditPma.right, m_rcBtnEditPma.bottom),
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

        // Row 2: Host & Config File Path
        float row2Y = row1Y + rowH + config.ScaleF(5.0f);
        D2D1_RECT_F rcHost = D2D1::RectF(rcSecN.left + config.ScaleF(10.0f), row2Y, rcSecN.left + config.ScaleF(10.0f) + halfW, row2Y + rowH);
        D2D1_RECT_F rcCfg = D2D1::RectF(rcSecN.left + config.ScaleF(18.0f) + halfW, row2Y, rcSecN.right - config.ScaleF(10.0f), row2Y + rowH);
        DrawFieldBadge(L"Host", m_nginxHost, rcHost, dummyInput, DatabaseModalAction::None, DatabaseEditField::NginxHost, m_rcInputNginxHost);
        DrawFieldBadge(L"Config File", m_nginxConfigFile, rcCfg, m_rcCopyNginxConfigPath, DatabaseModalAction::CopyNginxConfigPath, DatabaseEditField::None, dummyInput);

        // Section B: Nginx Status & Actions
        float secCY = rcSecN.bottom + config.ScaleF(10.0f);
        D2D1_RECT_F rcSecB = D2D1::RectF(cardX + padX, secCY, cardX + cardW - padX, secCY + config.ScaleF(74.0f));
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSecB, 6.0f, 6.0f), theme.brushDrawerBg ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSecB, 6.0f, 6.0f), pBorderBrush, 1.0f);

        D2D1_RECT_F rcHeadB = D2D1::RectF(rcSecB.left + config.ScaleF(12.0f), rcSecB.top + config.ScaleF(7.0f), rcSecB.right, rcSecB.top + config.ScaleF(24.0f));
        pRT->DrawText(L"NGINX SERVER STATUS & ACTIONS", 29, pFmtHint, rcHeadB, pAccentBrush);

        std::wstring ngxStatusStr = m_isNginxRunning ? (L"● Status: Running (" + m_nginxUrl + L")") : L"● Status: Stopped / Ready";
        D2D1_RECT_F rcNgxStatus = D2D1::RectF(rcSecB.left + config.ScaleF(12.0f), rcSecB.top + config.ScaleF(32.0f), rcSecB.left + config.ScaleF(270.0f), rcSecB.bottom);
        pRT->PushAxisAlignedClip(rcNgxStatus, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(ngxStatusStr.c_str(), static_cast<UINT32>(ngxStatusStr.size()), pFmtMain, rcNgxStatus, pWhiteBrush);
        pRT->PopAxisAlignedClip();

        float actH = config.ScaleF(30.0f);
        float btnCY = rcSecB.top + config.ScaleF(28.0f);

        float actEditW = config.ScaleF(145.0f);
        m_rcBtnEditNginx = D2D1::RectF(rcSecB.right - actEditW - config.ScaleF(6.0f), btnCY, rcSecB.right - config.ScaleF(6.0f), btnCY + actH);

        float actOpenW = config.ScaleF(140.0f);
        m_rcBtnOpenNginx = D2D1::RectF(m_rcBtnEditNginx.left - actOpenW - config.ScaleF(8.0f), btnCY, m_rcBtnEditNginx.left - config.ScaleF(8.0f), btnCY + actH);

        float actRunW = config.ScaleF(130.0f);
        m_rcBtnToggleNginx = D2D1::RectF(m_rcBtnOpenNginx.left - actRunW - config.ScaleF(8.0f), btnCY, m_rcBtnOpenNginx.left - config.ScaleF(8.0f), btnCY + actH);

        bool isToggleHov = (m_hoveredAction == DatabaseModalAction::ToggleServerRun);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bRunBg;
        if (m_isNginxRunning) {
            pRT->CreateSolidColorBrush(isToggleHov ? D2D1::ColorF(0.85f, 0.25f, 0.25f, 1.0f) : D2D1::ColorF(0.70f, 0.20f, 0.20f, 0.90f), &bRunBg);
        } else {
            pRT->CreateSolidColorBrush(isToggleHov ? D2D1::ColorF(0.20f, 0.70f, 0.35f, 1.0f) : D2D1::ColorF(0.15f, 0.58f, 0.28f, 0.90f), &bRunBg);
        }
        if (bRunBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnToggleNginx, 4.0f, 4.0f), bRunBg.Get());
        const wchar_t* toggleText = m_isNginxRunning ? L"■ Stop Server" : L"▶ Start Server";
        pRT->DrawText(toggleText, static_cast<UINT32>(wcslen(toggleText)), pFmtMain,
                      D2D1::RectF(m_rcBtnToggleNginx.left + config.ScaleF(14.0f), m_rcBtnToggleNginx.top + config.ScaleF(6.0f), m_rcBtnToggleNginx.right, m_rcBtnToggleNginx.bottom),
                      pWhiteBrush);

        bool isOpenHov = (m_hoveredAction == DatabaseModalAction::OpenBrowserNginx);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnOpenNginx, 4.0f, 4.0f), isOpenHov ? pAccentBrush : (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : pWhiteBrush));
        pRT->DrawText(L"Open Localhost", 14, pFmtMain,
                      D2D1::RectF(m_rcBtnOpenNginx.left + config.ScaleF(14.0f), m_rcBtnOpenNginx.top + config.ScaleF(6.0f), m_rcBtnOpenNginx.right, m_rcBtnOpenNginx.bottom),
                      pWhiteBrush);

        bool isEditHov = (m_hoveredAction == DatabaseModalAction::EditNginxConfig);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcBtnEditNginx, 4.0f, 4.0f), isEditHov ? theme.brushMenuHover.Get() : pBorderBrush);
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
        const wchar_t* hintFooter = (m_activeTab == DatabaseModalTab::PhpMyAdmin)
            ? L"Paste these connection variables directly into your config.php PDO constructor."
            : L"Configure local Nginx server port, virtual host & reverse proxy parameters.";
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
