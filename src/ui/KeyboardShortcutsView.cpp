/**
 * @file KeyboardShortcutsView.cpp
 * @brief Implementation of interactive VS Code-styled Keyboard Shortcuts view.
 */

#include "phide/ui/KeyboardShortcutsView.hpp"
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "phide/plugin/PluginManager.hpp"
#include <algorithm>

namespace phide::ui {

KeyboardShortcutsView::KeyboardShortcutsView() {
    InitializeShortcuts();
    PopulateDefaultIdsAndBackups();
    RecomputeConflicts();
}

void KeyboardShortcutsView::InitializeShortcuts() {
    // Verified genuine, operational shortcuts in phide (all fake/unimplemented entries removed)
    m_shortcuts = {
        // --- File Operations ---
        { L"File: New Text File", { L"Ctrl", L"N" }, L"global", L"Default" },
        { L"File: New File...", { L"Ctrl", L"Alt", L"N" }, L"global", L"Default" },
        { L"File: New Window", { L"Ctrl", L"Shift", L"N" }, L"global", L"Default" },
        { L"File: Open File...", { L"Ctrl", L"O" }, L"global", L"Default" },
        { L"File: Open Folder...", { L"Ctrl", L"K", L"Ctrl", L"O" }, L"global", L"Default" },
        { L"File: Save", { L"Ctrl", L"S" }, L"editorTextFocus", L"Default" },
        { L"File: Save As...", { L"Ctrl", L"Shift", L"S" }, L"editorTextFocus", L"Default" },
        { L"File: Save All", { L"Ctrl", L"K", L"S" }, L"global", L"Default" },
        { L"File: Close Editor", { L"Ctrl", L"W" }, L"editorTextFocus", L"Default" },
        { L"File: Close Folder", { L"Ctrl", L"K", L"F" }, L"global", L"Default" },
        { L"File: Reopen Closed Editor", { L"Ctrl", L"Shift", L"T" }, L"global", L"Default" },

        // --- Preferences ---
        { L"Preferences: Color Theme", { L"Ctrl", L"K", L"Ctrl", L"T" }, L"global", L"Default" },
        { L"Preferences: Open Keyboard Shortcuts", { L"Ctrl", L"K", L"Ctrl", L"S" }, L"global", L"Default" },
        { L"Preferences: Settings", { L"Ctrl", L"," }, L"global", L"Default" },

        // --- Edit & Refactoring ---
        { L"Edit: Undo", { L"Ctrl", L"Z" }, L"editorTextFocus", L"Default" },
        { L"Edit: Redo", { L"Ctrl", L"Y" }, L"editorTextFocus", L"Default" },
        { L"Edit: Cut", { L"Ctrl", L"X" }, L"editorTextFocus", L"Default" },
        { L"Edit: Copy", { L"Ctrl", L"C" }, L"editorTextFocus", L"Default" },
        { L"Edit: Paste", { L"Ctrl", L"V" }, L"editorTextFocus", L"Default" },
        { L"Edit: Select All", { L"Ctrl", L"A" }, L"editorTextFocus", L"Default" },
        { L"Edit: Find", { L"Ctrl", L"F" }, L"editorTextFocus", L"Default" },
        { L"Edit: Replace", { L"Ctrl", L"H" }, L"editorTextFocus", L"Default" },
        { L"Edit: Refactor...", { L"Ctrl", L"Shift", L"R" }, L"editorTextFocus", L"Default" },
        { L"Edit: Change All Occurrences", { L"Ctrl", L"F2" }, L"editorTextFocus", L"Default" },
        { L"Edit: Toggle Line Comment", { L"Ctrl", L"/" }, L"editorTextFocus", L"Default" },
        { L"Edit: Toggle Block Comment", { L"Shift", L"Alt", L"A" }, L"editorTextFocus", L"Default" },
        { L"Edit: Indent Line", { L"Tab" }, L"editorTextFocus", L"Default" },
        { L"Edit: Outdent Line", { L"Shift", L"Tab" }, L"editorTextFocus", L"Default" },

        // --- Explorer Operations ---
        { L"Explorer: Undo Last File Operation", { L"Ctrl", L"Z" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Redo Last File Operation", { L"Ctrl", L"Y" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Rename Item", { L"Enter" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Rename Item (Alternative)", { L"F2" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Delete Item", { L"Delete" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Cut Item", { L"Ctrl", L"X" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Copy Item", { L"Ctrl", L"C" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Paste Item", { L"Ctrl", L"V" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Copy Path", { L"Shift", L"Alt", L"C" }, L"filesExplorerFocus", L"Default" },
        { L"Explorer: Copy Relative Path", { L"Ctrl", L"K", L"Ctrl", L"Shift", L"C" }, L"filesExplorerFocus", L"Default" },

        // --- Search & Navigation ---
        { L"Navigation: Quick Open (Go to File)", { L"Ctrl", L"P" }, L"global", L"Default" },
        { L"Navigation: Quick Open / File Search", { L"Ctrl", L"K" }, L"global", L"Default" },
        { L"Navigation: Show All Commands", { L"Ctrl", L"Shift", L"P" }, L"global", L"Default" },
        { L"Navigation: Command Palette (Alternative)", { L"F1" }, L"global", L"Default" },
        { L"Navigation: Go to Definition", { L"F12" }, L"editorTextFocus", L"Default" },
        { L"Search: Find in Files", { L"Ctrl", L"Shift", L"F" }, L"global", L"Default" },
        { L"Search: Replace in Files", { L"Ctrl", L"Shift", L"H" }, L"global", L"Default" },

        // --- Debug & Execution Controls ---
        { L"Run: Run Without Debugging", { L"Ctrl", L"F5" }, L"global", L"Default" },
        { L"Debug: Start Debugging", { L"F5" }, L"global", L"Default" },
        { L"Debug: Stop Execution / Server", { L"Shift", L"F5" }, L"global", L"Default" },
        { L"Debug: Restart Execution / Server", { L"Ctrl", L"Shift", L"F5" }, L"global", L"Default" },
        { L"Debug: Toggle Breakpoint", { L"F9" }, L"editorTextFocus", L"Default" },
        { L"Debug: Step Over", { L"F10" }, L"debugFocus", L"Default" },
        { L"Debug: Step Into", { L"F11" }, L"debugFocus", L"Default" },
        { L"Debug: Step Out", { L"Shift", L"F11" }, L"debugFocus", L"Default" },
        { L"Debug: Continue Execution", { L"F5" }, L"debugFocus", L"Default" },
        { L"Debug: Toggle Debug Console", { L"Ctrl", L"Shift", L"Y" }, L"global", L"Default" },

        // --- Code Editing & IntelliSense ---
        { L"Code: Format Document", { L"Shift", L"Alt", L"F" }, L"editorTextFocus", L"Default" },
        { L"Code: Rename Symbol", { L"F2" }, L"editorTextFocus", L"Default" },
        { L"Code: Trigger IntelliSense Autocomplete", { L"Ctrl", L"Space" }, L"editorTextFocus", L"Default" },
        { L"Code: Emmet Expand Abbreviation", { L"Tab" }, L"editorTextFocus", L"Default" },

        // --- Source Control ---
        { L"Source Control: Commit Changes", { L"Ctrl", L"Enter" }, L"sourceControlFocus", L"Default" },
        { L"Source Control: Dismiss / Close Box", { L"Escape" }, L"sourceControlFocus", L"Default" },

        // --- View & Layout ---
        { L"View: Split Editor Right", { L"Ctrl", L"\\" }, L"global", L"Default" },
        { L"View: Toggle Primary Side Bar", { L"Ctrl", L"B" }, L"global", L"Default" },
        { L"View: Toggle Runtimes Panel (Extensions)", { L"Ctrl", L"Shift", L"X" }, L"global", L"Default" },
        { L"View: Toggle Full Screen", { L"F11" }, L"global", L"Default" },
        { L"View: Toggle Word Wrap", { L"Alt", L"Z" }, L"editorTextFocus", L"Default" },
        { L"View: Zoom In", { L"Ctrl", L"=" }, L"global", L"Default" },
        { L"View: Zoom Out", { L"Ctrl", L"-" }, L"global", L"Default" },
        { L"View: Reset Zoom", { L"Ctrl", L"0" }, L"global", L"Default" },
        { L"View: Next Tab", { L"Ctrl", L"Tab" }, L"global", L"Default" },
        { L"View: Previous Tab", { L"Ctrl", L"Shift", L"Tab" }, L"global", L"Default" },
        { L"View: Show Explorer", { L"Ctrl", L"Shift", L"E" }, L"global", L"Default" },
        { L"View: Show Search", { L"Ctrl", L"Shift", L"F" }, L"global", L"Default" },
        { L"View: Show Source Control", { L"Ctrl", L"Shift", L"G" }, L"global", L"Default" },

        // --- Terminal & Window ---
        { L"Terminal: Create New Terminal", { L"Ctrl", L"Shift", L"`" }, L"global", L"Default" },
        { L"Terminal: Toggle Terminal Panel", { L"Ctrl", L"`" }, L"global", L"Default" },
        { L"Terminal: Split Active Terminal Tab", { L"Ctrl", L"Shift", L"5" }, L"terminalFocus", L"Default" },
        { L"Terminal: Split Active Terminal (Alternative)", { L"Ctrl", L"Shift", L"\\" }, L"terminalFocus", L"Default" },
        { L"Window: Close Window", { L"Alt", L"F4" }, L"global", L"Default" },
    };

    // Append plugin/extension/mod keybindings
    for (const auto& pCmd : phide::plugin::PluginManager::Instance().GetCommands()) {
        if (!pCmd.keyTokens.empty()) {
            m_shortcuts.push_back({ pCmd.title, pCmd.keyTokens, pCmd.when.empty() ? L"global" : pCmd.when, L"Extension" });
        }
    }
}

std::vector<size_t> KeyboardShortcutsView::GetFilteredIndices() const {
    std::vector<size_t> indices;
    if (m_searchQuery.empty()) {
        for (size_t i = 0; i < m_shortcuts.size(); ++i) indices.push_back(i);
        return indices;
    }

    std::wstring q = m_searchQuery;
    for (auto& c : q) c = towlower(c);

    for (size_t i = 0; i < m_shortcuts.size(); ++i) {
        const auto& item = m_shortcuts[i];
        std::wstring cmd = item.command;
        for (auto& c : cmd) c = towlower(c);

        std::wstring keys;
        for (const auto& k : item.keyPills) keys += k + L" ";
        for (auto& c : keys) c = towlower(c);

        if (cmd.find(q) != std::wstring::npos || keys.find(q) != std::wstring::npos) {
            indices.push_back(i);
        }
    }
    return indices;
}

void KeyboardShortcutsView::DrawKeycapBadge(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                            const std::wstring& text, float& curX, float centerY, float badgeH,
                                            bool isConflict, bool isUserModified) {
    if (text.empty()) return;
    float textW = 0.0f;
    IDWriteTextFormat* pFmt = theme.fontKeycap ? theme.fontKeycap.Get() : theme.fontMenuShortcut.Get();
    if (theme.dwriteFactory && pFmt) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.length()),
                                                          pFmt, 200.0f, badgeH, &pLayout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                textW = tm.widthIncludingTrailingWhitespace;
            }
        }
    }

    const float padX = config.ScaleF(7.0f);
    const float badgeW = (std::max)(config.ScaleF(24.0f), textW + padX * 2.0f);
    D2D1_RECT_F rcBadge = D2D1::RectF(
        curX,
        centerY - badgeH / 2.0f,
        curX + badgeW,
        centerY + badgeH / 2.0f
    );

    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcBadge, 3.5f, 3.5f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderOverride;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgOverride;
    if (isConflict) {
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.35f, 0.25f, 0.20f), &bgOverride);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.45f, 0.30f, 0.90f), &borderOverride);
    } else if (isUserModified) {
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.60f, 0.95f, 0.18f), &bgOverride);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.70f, 1.0f, 0.85f), &borderOverride);
    }
    pRT->FillRoundedRectangle(rrc, bgOverride ? bgOverride.Get() : (theme.brushItemHover ? theme.brushItemHover.Get() : theme.brushMenuBg.Get()));
    pRT->DrawRoundedRectangle(rrc, borderOverride ? borderOverride.Get() : (theme.brushBorderHover ? theme.brushBorderHover.Get() : theme.brushBorderSubtle.Get()), 1.0f);

    pRT->PushAxisAlignedClip(rcBadge, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    pRT->DrawText(text.c_str(), static_cast<UINT32>(text.length()),
                  pFmt, rcBadge, theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    pRT->PopAxisAlignedClip();

    curX += badgeW + config.ScaleF(4.0f);
}

void KeyboardShortcutsView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                   float left, float width, float top, float height) {
    if (!pRT) return;
    m_viewBounds = D2D1::RectF(left, top, left + width, top + height);

    // Background
    pRT->FillRectangle(m_viewBounds, theme.brushCanvasBg.Get());

    const float marginX = config.ScaleF(24.0f);
    float curY = top + config.ScaleF(14.0f);

    // 1. Search Bar at Top: "Type to search in keybindings"
    const float searchH = config.ScaleF(32.0f);
    m_searchBoxBounds = D2D1::RectF(
        left + marginX,
        curY,
        left + width - marginX,
        curY + searchH
    );

    D2D1_ROUNDED_RECT rrcSearch = D2D1::RoundedRect(m_searchBoxBounds, 4.0f, 4.0f);
    pRT->FillRoundedRectangle(rrcSearch, theme.IsDarkMode() ? theme.brushDrawerBg.Get() : theme.brushCanvasBg.Get());

    // Focus / Active outline
    if (m_isSearchFocused) {
        pRT->DrawRoundedRectangle(rrcSearch, theme.brushBrandAccent.Get(), 1.5f);
    } else {
        pRT->DrawRoundedRectangle(rrcSearch, theme.brushBorderHover.Get(), 1.0f);
    }

    // Search Icon glyph (Magnifying Glass 🔍)
    const wchar_t* searchGlyph = L"\u26B2";
    D2D1_RECT_F rcIcon = D2D1::RectF(
        m_searchBoxBounds.left + config.ScaleF(8.0f),
        curY,
        m_searchBoxBounds.left + config.ScaleF(28.0f),
        curY + searchH
    );
    pRT->DrawText(searchGlyph, 1, theme.fontMenu.Get(), rcIcon, theme.brushTextMuted.Get());

    // Search Text / Placeholder
    D2D1_RECT_F rcSearchText = D2D1::RectF(
        m_searchBoxBounds.left + config.ScaleF(32.0f),
        curY,
        m_searchBoxBounds.right - config.ScaleF(10.0f),
        curY + searchH
    );

    ID2D1Brush* textBrush = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get();

    if (m_searchQuery.empty()) {
        const wchar_t* placeholder = L"Type to search in keybindings";
        pRT->DrawText(placeholder, static_cast<UINT32>(wcslen(placeholder)),
                      theme.fontBody.Get(), rcSearchText, theme.brushTextMuted.Get());
    } else {
        pRT->DrawText(m_searchQuery.c_str(), static_cast<UINT32>(m_searchQuery.length()),
                      theme.fontBody.Get(), rcSearchText, textBrush);

        // Blinking search cursor
        if (m_isSearchFocused) {
            float textW = 0.0f;
            if (theme.dwriteFactory && theme.fontBody) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(m_searchQuery.c_str(), static_cast<UINT32>(m_searchQuery.length()),
                                                                  theme.fontBody.Get(), 1000.0f, searchH, &pLayout))) {
                    DWRITE_TEXT_METRICS tm{};
                    if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                        textW = tm.widthIncludingTrailingWhitespace;
                    }
                }
            }
            float cursorX = rcSearchText.left + textW + 1.0f;
            float cy = curY + searchH / 2.0f;
            pRT->DrawLine(
                D2D1::Point2F(cursorX, cy - config.ScaleF(8.0f)),
                D2D1::Point2F(cursorX, cy + config.ScaleF(8.0f)),
                textBrush,
                1.5f
            );
        }
    }

    curY += searchH + config.ScaleF(14.0f);

    // 2. Table Column Headers
    const float headerH = config.ScaleF(26.0f);
    D2D1_RECT_F rcHeaderRow = D2D1::RectF(left + marginX, curY, left + width - marginX, curY + headerH);
    pRT->FillRectangle(rcHeaderRow, theme.brushDrawerBg.Get());

    const float tableW = rcHeaderRow.right - rcHeaderRow.left;
    const float colCommandW = tableW * 0.36f;
    const float colKeysW    = tableW * 0.28f;
    const float colWhenW    = tableW * 0.22f;

    float col0X = rcHeaderRow.left + config.ScaleF(8.0f);
    float col1X = rcHeaderRow.left + colCommandW;
    float col2X = col1X + colKeysW;
    float col3X = col2X + colWhenW;

    pRT->DrawText(L"Command", 7, theme.fontSectionHeader.Get(),
                  D2D1::RectF(col0X, curY, col1X, curY + headerH), theme.brushTextSecondary.Get());
    pRT->DrawText(L"Keybinding", 10, theme.fontSectionHeader.Get(),
                  D2D1::RectF(col1X, curY, col2X, curY + headerH), theme.brushTextSecondary.Get());
    pRT->DrawText(L"When", 4, theme.fontSectionHeader.Get(),
                  D2D1::RectF(col2X, curY, col3X, curY + headerH), theme.brushTextSecondary.Get());
    pRT->DrawText(L"Source", 6, theme.fontSectionHeader.Get(),
                  D2D1::RectF(col3X, curY, rcHeaderRow.right, curY + headerH), theme.brushTextSecondary.Get());

    // Divider line under header
    pRT->DrawLine(
        D2D1::Point2F(rcHeaderRow.left, curY + headerH),
        D2D1::Point2F(rcHeaderRow.right, curY + headerH),
        theme.brushBorderSubtle.Get(), 1.0f
    );

    curY += headerH + 1.0f;

    // Inline Keybinding Recorder Bar when active (above table list)
    if (m_isRecording && m_recordingItemIdx >= 0 && m_recordingItemIdx < static_cast<int>(m_shortcuts.size())) {
        const float recBarH = config.ScaleF(36.0f);
        D2D1_RECT_F rcRec = D2D1::RectF(left + marginX, curY, left + width - marginX, curY + recBarH);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> recBg, recBorder, warnBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.16f, 0.30f, 0.50f, 0.25f), &recBg);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.65f, 1.0f, 0.85f), &recBorder);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.55f, 0.22f, 1.0f), &warnBrush);
        D2D1_ROUNDED_RECT rrcRec = D2D1::RoundedRect(rcRec, 5.0f, 5.0f);
        if (recBg) pRT->FillRoundedRectangle(rrcRec, recBg.Get());
        if (recBorder) pRT->DrawRoundedRectangle(rrcRec, recBorder.Get(), 1.2f);

        std::wstring recPrompt = L"Press keys for \"" + m_shortcuts[m_recordingItemIdx].command + L"\" (Enter=Save, Esc=Cancel):";
        D2D1_RECT_F rcPrompt = D2D1::RectF(rcRec.left + config.ScaleF(10.0f), rcRec.top, rcRec.left + config.ScaleF(390.0f), rcRec.bottom);
        pRT->PushAxisAlignedClip(rcPrompt, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pRT->DrawText(recPrompt.c_str(), static_cast<UINT32>(recPrompt.length()), theme.fontMenuShortcut.Get(),
                      rcPrompt, theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        pRT->PopAxisAlignedClip();

        float bx = rcPrompt.right + config.ScaleF(8.0f);
        float cy = (rcRec.top + rcRec.bottom) * 0.5f;
        const auto& previewPills = m_recordedPills.empty() ? m_shortcuts[m_recordingItemIdx].keyPills : m_recordedPills;
        for (const auto& pill : previewPills) {
            if (bx + config.ScaleF(48.0f) > rcRec.right - config.ScaleF(140.0f)) break;
            DrawKeycapBadge(pRT, theme, config, pill, bx, cy, config.ScaleF(20.0f), !m_recordingConflictWarning.empty(), true);
        }

        if (!m_recordingConflictWarning.empty()) {
            D2D1_RECT_F rcWarn = D2D1::RectF(bx + config.ScaleF(4.0f), rcRec.top, rcRec.right - config.ScaleF(126.0f), rcRec.bottom);
            if (rcWarn.right > rcWarn.left + 20.0f) {
                pRT->PushAxisAlignedClip(rcWarn, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                pRT->DrawText(m_recordingConflictWarning.c_str(), static_cast<UINT32>(m_recordingConflictWarning.length()),
                              theme.fontMenuShortcut.Get(), rcWarn, warnBrush ? warnBrush.Get() : theme.brushTextMuted.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
                pRT->PopAxisAlignedClip();
            }
        }

        const float btnW = config.ScaleF(54.0f);
        const float btnH = config.ScaleF(22.0f);
        m_recordCancelBounds = D2D1::RectF(rcRec.right - btnW - config.ScaleF(8.0f), cy - btnH * 0.5f, rcRec.right - config.ScaleF(8.0f), cy + btnH * 0.5f);
        m_recordCommitBounds = D2D1::RectF(m_recordCancelBounds.left - btnW - config.ScaleF(6.0f), cy - btnH * 0.5f, m_recordCancelBounds.left - config.ScaleF(6.0f), cy + btnH * 0.5f);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(m_recordCommitBounds, 3.0f, 3.0f), theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushItemHover.Get());
        pRT->DrawText(L"Save", 4, theme.fontMenuShortcut.Get(), m_recordCommitBounds, theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(m_recordCancelBounds, 3.0f, 3.0f), theme.brushBorderSubtle.Get(), 1.0f);
        pRT->DrawText(L"Cancel", 6, theme.fontMenuShortcut.Get(), m_recordCancelBounds, theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

        curY += recBarH + config.ScaleF(6.0f);
    } else {
        m_recordCommitBounds = {};
        m_recordCancelBounds = {};
    }

    // 3. Table Rows with Clipping
    D2D1_RECT_F rcListClip = D2D1::RectF(left + marginX, curY, left + width - marginX, top + height - config.ScaleF(10.0f));
    pRT->PushAxisAlignedClip(rcListClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    m_rowActionRects.clear();
    float rowY = curY - m_scrollOffsetY;
    const float rowH = config.ScaleF(30.0f);
    const float badgeH = config.ScaleF(20.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> conflictTextBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.52f, 0.25f, 1.0f), &conflictTextBrush);

    auto filtered = GetFilteredIndices();
    for (size_t idx = 0; idx < filtered.size(); ++idx) {
        if (rowY + rowH >= rcListClip.top && rowY <= rcListClip.bottom) {
            size_t itemIdx = filtered[idx];
            const auto& item = m_shortcuts[itemIdx];

            D2D1_RECT_F rcRow = D2D1::RectF(rcListClip.left, rowY, rcListClip.right, rowY + rowH);
            bool isHovered = (m_hoveredRow == static_cast<int>(idx));
            bool isBeingRecorded = (m_isRecording && m_recordingItemIdx == static_cast<int>(itemIdx));

            if (isBeingRecorded) {
                D2D1_ROUNDED_RECT rrcRow = D2D1::RoundedRect(rcRow, 3.0f, 3.0f);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selRowBg;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.45f, 0.85f, 0.18f), &selRowBg);
                pRT->FillRoundedRectangle(rrcRow, selRowBg ? selRowBg.Get() : theme.brushItemHover.Get());
            } else if (isHovered) {
                D2D1_ROUNDED_RECT rrcRow = D2D1::RoundedRect(rcRow, 3.0f, 3.0f);
                pRT->FillRoundedRectangle(rrcRow, theme.brushItemHover.Get());
            }

            // Command Label
            D2D1_RECT_F rcCmd = D2D1::RectF(col0X, rowY, col1X - config.ScaleF(6.0f), rowY + rowH);
            pRT->PushAxisAlignedClip(rcCmd, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            pRT->DrawText(item.command.c_str(), static_cast<UINT32>(item.command.length()),
                          theme.fontBody.Get(), rcCmd, theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            pRT->PopAxisAlignedClip();

            // Keybinding Badges (VS Code Pill Keycaps)
            float curBadgeX = col1X;
            float rowCenterY = rowY + rowH / 2.0f;
            bool hasConflict = !item.conflictCommand.empty();
            bool isUserMod = (item.source == L"User");
            D2D1_RECT_F rcKeyCol = D2D1::RectF(col1X, rowY, col2X - config.ScaleF(6.0f), rowY + rowH);
            pRT->PushAxisAlignedClip(rcKeyCol, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            for (size_t p = 0; p < item.keyPills.size(); ++p) {
                DrawKeycapBadge(pRT, theme, config, item.keyPills[p], curBadgeX, rowCenterY, badgeH, hasConflict, isUserMod);
            }
            pRT->PopAxisAlignedClip();

            // When Label + optional Conflict badge
            D2D1_RECT_F rcWhen = D2D1::RectF(col2X, rowY, col3X - config.ScaleF(6.0f), rowY + rowH);
            pRT->PushAxisAlignedClip(rcWhen, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            if (hasConflict) {
                std::wstring warnStr = item.when + L"  [\u26A0 " + item.conflictCommand + L"]";
                pRT->DrawText(warnStr.c_str(), static_cast<UINT32>(warnStr.length()),
                              theme.fontMenuShortcut.Get(), rcWhen,
                              conflictTextBrush ? conflictTextBrush.Get() : theme.brushTextMuted.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
            } else {
                pRT->DrawText(item.when.c_str(), static_cast<UINT32>(item.when.length()),
                              theme.fontMenuShortcut.Get(), rcWhen, theme.brushTextMuted.Get(),
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            pRT->PopAxisAlignedClip();

            // Source + inline Edit/Reset pills
            const float actBtnH = config.ScaleF(20.0f);
            const float resetW = (item.source == L"User") ? config.ScaleF(46.0f) : 0.0f;
            const float editW = config.ScaleF(42.0f);
            const float rightEdge = rcListClip.right - config.ScaleF(6.0f);

            RowActionRect rar;
            rar.itemIdx = static_cast<int>(itemIdx);
            rar.editBounds = D2D1::RectF(rightEdge - editW, rowCenterY - actBtnH * 0.5f, rightEdge, rowCenterY + actBtnH * 0.5f);
            if (resetW > 0.0f) {
                rar.resetBounds = D2D1::RectF(rar.editBounds.left - resetW - config.ScaleF(4.0f), rowCenterY - actBtnH * 0.5f,
                                              rar.editBounds.left - config.ScaleF(4.0f), rowCenterY + actBtnH * 0.5f);
            }
            m_rowActionRects.push_back(rar);

            float srcRight = (resetW > 0.0f ? rar.resetBounds.left : rar.editBounds.left) - config.ScaleF(4.0f);
            D2D1_RECT_F rcSource = D2D1::RectF(col3X, rowY, (std::max)(col3X + 20.0f, srcRight), rowY + rowH);
            pRT->PushAxisAlignedClip(rcSource, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            pRT->DrawText(item.source.c_str(), static_cast<UINT32>(item.source.length()),
                          theme.fontMenuShortcut.Get(), rcSource, theme.brushTextMuted.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            pRT->PopAxisAlignedClip();

            if (isHovered || isBeingRecorded || item.source == L"User") {
                pRT->DrawRoundedRectangle(D2D1::RoundedRect(rar.editBounds, 3.0f, 3.0f), theme.brushBorderSubtle.Get(), 1.0f);
                pRT->DrawText(L"Edit", 4, theme.fontMenuShortcut.Get(), rar.editBounds, theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                if (resetW > 0.0f) {
                    pRT->DrawRoundedRectangle(D2D1::RoundedRect(rar.resetBounds, 3.0f, 3.0f), theme.brushBorderSubtle.Get(), 1.0f);
                    pRT->DrawText(L"Reset", 5, theme.fontMenuShortcut.Get(), rar.resetBounds, theme.brushTextMuted.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
            }

            // Subtle row separator
            pRT->DrawLine(
                D2D1::Point2F(rcRow.left, rowY + rowH - 0.5f),
                D2D1::Point2F(rcRow.right, rowY + rowH - 0.5f),
                theme.brushBorderSubtle.Get(), 0.5f
            );
        }
        rowY += rowH;
    }

    pRT->PopAxisAlignedClip();
}

bool KeyboardShortcutsView::HandleMouseMove(POINT clientPt) {
    int oldHover = m_hoveredRow;
    m_hoveredRow = -1;

    float curY = m_searchBoxBounds.bottom + 27.0f + (m_isRecording ? 42.0f : 0.0f);
    float rowY = curY - m_scrollOffsetY;
    const float rowH = 30.0f;

    auto filtered = GetFilteredIndices();
    for (size_t idx = 0; idx < filtered.size(); ++idx) {
        if (clientPt.x >= m_viewBounds.left && clientPt.x <= m_viewBounds.right &&
            clientPt.y >= rowY && clientPt.y < rowY + rowH) {
            m_hoveredRow = static_cast<int>(idx);
            break;
        }
        rowY += rowH;
    }

    return (m_hoveredRow != oldHover);
}

bool KeyboardShortcutsView::HandleLButtonDown(POINT clientPt) {
    if (m_isRecording) {
        if (clientPt.x >= m_recordCommitBounds.left && clientPt.x <= m_recordCommitBounds.right &&
            clientPt.y >= m_recordCommitBounds.top && clientPt.y <= m_recordCommitBounds.bottom) {
            return CommitRecordedKeybinding();
        }
        if (clientPt.x >= m_recordCancelBounds.left && clientPt.x <= m_recordCancelBounds.right &&
            clientPt.y >= m_recordCancelBounds.top && clientPt.y <= m_recordCancelBounds.bottom) {
            CancelRecording();
            return true;
        }
    }

    if (clientPt.x >= m_searchBoxBounds.left && clientPt.x <= m_searchBoxBounds.right &&
        clientPt.y >= m_searchBoxBounds.top && clientPt.y <= m_searchBoxBounds.bottom) {
        m_isSearchFocused = true;
        CancelRecording();
        return true;
    }
    m_isSearchFocused = false;

    for (const auto& rar : m_rowActionRects) {
        if (clientPt.x >= rar.editBounds.left && clientPt.x <= rar.editBounds.right &&
            clientPt.y >= rar.editBounds.top && clientPt.y <= rar.editBounds.bottom) {
            BeginRecordingItem(rar.itemIdx);
            return true;
        }
        if (rar.resetBounds.right > rar.resetBounds.left &&
            clientPt.x >= rar.resetBounds.left && clientPt.x <= rar.resetBounds.right &&
            clientPt.y >= rar.resetBounds.top && clientPt.y <= rar.resetBounds.bottom) {
            return ResetItemToDefault(rar.itemIdx);
        }
    }

    // Clicking a row also initiates recording for that command
    if (m_hoveredRow >= 0) {
        BeginRecordingRow(m_hoveredRow);
        return true;
    }
    return false;
}

bool KeyboardShortcutsView::HandleMouseWheel(short delta) {
    const float scrollSpeed = 40.0f;
    m_scrollOffsetY -= (delta > 0 ? scrollSpeed : -scrollSpeed);
    if (m_scrollOffsetY < 0.0f) m_scrollOffsetY = 0.0f;
    return true;
}

bool KeyboardShortcutsView::HandleChar(wchar_t ch) {
    if (m_isRecording) return true;
    if (!m_isSearchFocused) return false;
    if (ch >= 32) {
        m_searchQuery += ch;
        m_scrollOffsetY = 0.0f;
        return true;
    }
    return false;
}

bool KeyboardShortcutsView::HandleKeyDown(WPARAM key) {
    if (!m_isSearchFocused) return false;
    if (key == VK_BACK) {
        if (!m_searchQuery.empty()) {
            m_searchQuery.pop_back();
            m_scrollOffsetY = 0.0f;
            return true;
        }
    } else if (key == VK_ESCAPE) {
        m_searchQuery.clear();
        m_scrollOffsetY = 0.0f;
        return true;
    }
    return false;
}

namespace {

std::wstring ToLowerKbd(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(::towlower(c));
    return s;
}

std::wstring NormalizePillStr(std::wstring p) {
    std::wstring lp = ToLowerKbd(p);
    if (lp == L"ctrl" || lp == L"control") return L"Ctrl";
    if (lp == L"shift") return L"Shift";
    if (lp == L"alt") return L"Alt";
    if (lp == L"enter" || lp == L"return") return L"Enter";
    if (lp == L"esc" || lp == L"escape") return L"Esc";
    if (lp == L"space") return L"Space";
    if (lp == L"tab") return L"Tab";
    if (p.size() == 1 && p[0] >= L'a' && p[0] <= L'z') {
        p[0] = static_cast<wchar_t>(::towupper(p[0]));
    }
    return p;
}

std::wstring PillsCanonicalKey(const std::vector<std::wstring>& pills) {
    std::wstring out;
    for (size_t i = 0; i < pills.size(); ++i) {
        if (i > 0) out += L"+";
        out += ToLowerKbd(NormalizePillStr(pills[i]));
    }
    return out;
}

std::wstring EscapeJsonKbd(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c == L'\\') out += L"\\\\";
        else if (c == L'"') out += L"\\\"";
        else out += c;
    }
    return out;
}

} // namespace

void KeyboardShortcutsView::PopulateDefaultIdsAndBackups() {
    auto assignId = [](ShortcutItem& item) {
        std::wstring c = ToLowerKbd(item.command);
        if (c.find(L"command palette") != std::wstring::npos) item.id = L"cmd_command_palette";
        else if (c.find(L"quick open") != std::wstring::npos || c.find(L"go to file") != std::wstring::npos) item.id = L"cmd_quick_open";
        else if (c.find(L"new file") != std::wstring::npos) item.id = L"cmd_new_file";
        else if (c.find(L"new window") != std::wstring::npos) item.id = L"cmd_new_window";
        else if (c.find(L"open file") != std::wstring::npos) item.id = L"cmd_open_file";
        else if (c.find(L"open folder") != std::wstring::npos) item.id = L"cmd_open_folder";
        else if (c.find(L"save all") != std::wstring::npos) item.id = L"cmd_save_all";
        else if (c.find(L"save as") != std::wstring::npos) item.id = L"cmd_save_as";
        else if (c == L"file: save" || c == L"save") item.id = L"cmd_save";
        else if (c.find(L"close editor") != std::wstring::npos || c.find(L"close tab") != std::wstring::npos) item.id = L"cmd_close_editor";
        else if (c.find(L"undo") != std::wstring::npos) item.id = L"cmd_undo";
        else if (c.find(L"redo") != std::wstring::npos) item.id = L"cmd_redo";
        else if (c.find(L"toggle line comment") != std::wstring::npos || c == L"edit: toggle line comment") item.id = L"cmd_toggle_line_comment";
        else if (c.find(L"toggle block comment") != std::wstring::npos) item.id = L"cmd_toggle_block_comment";
        else if (c.find(L"find in files") != std::wstring::npos) item.id = L"cmd_find_in_files";
        else if (c.find(L"replace in files") != std::wstring::npos) item.id = L"cmd_replace_in_files";
        else if (c == L"edit: find" || c == L"find") item.id = L"cmd_find_inline";
        else if (c == L"edit: replace" || c == L"replace") item.id = L"cmd_replace_inline";
        else if (c.find(L"sidebar") != std::wstring::npos) item.id = L"cmd_toggle_sidebar";
        else if (c.find(L"terminal") != std::wstring::npos) item.id = L"cmd_toggle_terminal";
        else if (c.find(L"format document") != std::wstring::npos) item.id = L"cmd_format_document";
        else if (c.find(L"preferences: open settings") != std::wstring::npos || c == L"settings") item.id = L"cmd_open_settings";
        else if (c.find(L"keyboard shortcuts") != std::wstring::npos) item.id = L"cmd_shortcuts";
        else if (c.find(L"delete line") != std::wstring::npos) item.id = L"cmd_delete_line";
        else if (c.find(L"join lines") != std::wstring::npos) item.id = L"cmd_join_lines";
        else if (c.find(L"add selection to next find match") != std::wstring::npos) item.id = L"cmd_select_all_occurrences";
        else if (c.find(L"sql") != std::wstring::npos) item.id = L"cmd_sql_console";
        else if (c.find(L"snippet") != std::wstring::npos) item.id = L"cmd_snippet_editor";
        else {
            std::wstring slug = L"cmd_";
            for (wchar_t ch : c) {
                if ((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9')) slug += ch;
                else if (slug.back() != L'_') slug += L'_';
            }
            item.id = slug;
        }
    };

    bool hasSql = false, hasSnippet = false, hasSave = false;
    for (auto& it : m_shortcuts) {
        if (it.id.empty()) assignId(it);
        if (it.defaultKeyPills.empty()) it.defaultKeyPills = it.keyPills;
        if (it.id == L"cmd_sql_console") hasSql = true;
        if (it.id == L"cmd_snippet_editor") hasSnippet = true;
        if (it.id == L"cmd_save") hasSave = true;
    }
    if (!hasSave) {
        ShortcutItem s;
        s.id = L"cmd_save";
        s.command = L"File: Save";
        s.keyPills = { L"Ctrl", L"S" };
        s.defaultKeyPills = s.keyPills;
        s.when = L"global";
        s.source = L"Default";
        m_shortcuts.push_back(std::move(s));
    }
    if (!hasSql) {
        ShortcutItem s;
        s.id = L"cmd_sql_console";
        s.command = L"View: SQL Query Console";
        s.keyPills = { L"Ctrl", L"Shift", L"Q" };
        s.defaultKeyPills = s.keyPills;
        s.when = L"global";
        s.source = L"Default";
        m_shortcuts.push_back(std::move(s));
    }
    if (!hasSnippet) {
        ShortcutItem s;
        s.id = L"cmd_snippet_editor";
        s.command = L"Preferences: Open Snippet Editor (GUI)";
        s.keyPills = {};
        s.defaultKeyPills = {};
        s.when = L"global";
        s.source = L"Default";
        m_shortcuts.push_back(std::move(s));
    }
}

void KeyboardShortcutsView::RecomputeConflicts() {
    for (auto& it : m_shortcuts) it.conflictCommand.clear();
    for (size_t i = 0; i < m_shortcuts.size(); ++i) {
        if (m_shortcuts[i].keyPills.empty()) continue;
        std::wstring keyI = PillsCanonicalKey(m_shortcuts[i].keyPills);
        for (size_t j = i + 1; j < m_shortcuts.size(); ++j) {
            if (m_shortcuts[j].keyPills.empty()) continue;
            std::wstring keyJ = PillsCanonicalKey(m_shortcuts[j].keyPills);
            if (keyI == keyJ) {
                bool sameScope = (m_shortcuts[i].when == m_shortcuts[j].when ||
                                  m_shortcuts[i].when == L"global" || m_shortcuts[j].when == L"global");
                if (sameScope) {
                    if (m_shortcuts[i].conflictCommand.empty()) m_shortcuts[i].conflictCommand = m_shortcuts[j].command;
                    if (m_shortcuts[j].conflictCommand.empty()) m_shortcuts[j].conflictCommand = m_shortcuts[i].command;
                }
            }
        }
    }
}

std::vector<std::wstring> KeyboardShortcutsView::FindConflictsForPills(
    const std::vector<std::wstring>& pills,
    int excludeItemIdx,
    const std::wstring& whenScope) const {
    std::vector<std::wstring> conflicts;
    if (pills.empty()) return conflicts;
    std::wstring target = PillsCanonicalKey(pills);
    for (size_t i = 0; i < m_shortcuts.size(); ++i) {
        if (static_cast<int>(i) == excludeItemIdx) continue;
        if (m_shortcuts[i].keyPills.empty()) continue;
        if (PillsCanonicalKey(m_shortcuts[i].keyPills) == target) {
            if (whenScope.empty() || m_shortcuts[i].when == whenScope ||
                m_shortcuts[i].when == L"global" || whenScope == L"global") {
                conflicts.push_back(m_shortcuts[i].command);
            }
        }
    }
    return conflicts;
}

void KeyboardShortcutsView::BeginRecordingRow(int filteredRowIdx) {
    auto filtered = GetFilteredIndices();
    if (filteredRowIdx < 0 || filteredRowIdx >= static_cast<int>(filtered.size())) return;
    BeginRecordingItem(static_cast<int>(filtered[filteredRowIdx]));
}

void KeyboardShortcutsView::BeginRecordingItem(int itemIdx) {
    if (itemIdx < 0 || itemIdx >= static_cast<int>(m_shortcuts.size())) return;
    m_isRecording = true;
    m_recordingItemIdx = itemIdx;
    m_recordedPills.clear();
    m_recordingConflictWarning.clear();
    m_isSearchFocused = false;
}

void KeyboardShortcutsView::CancelRecording() noexcept {
    m_isRecording = false;
    m_recordingItemIdx = -1;
    m_recordedPills.clear();
    m_recordingConflictWarning.clear();
}

bool KeyboardShortcutsView::CommitRecordedKeybinding() {
    if (!m_isRecording || m_recordingItemIdx < 0 || m_recordingItemIdx >= static_cast<int>(m_shortcuts.size())) {
        return false;
    }
    if (m_recordedPills.empty()) {
        CancelRecording();
        return false;
    }
    auto& item = m_shortcuts[m_recordingItemIdx];
    item.keyPills = m_recordedPills;
    item.source = (PillsCanonicalKey(item.keyPills) == PillsCanonicalKey(item.defaultKeyPills)) ? L"Default" : L"User";
    CancelRecording();
    RecomputeConflicts();
    if (!m_userKeybindingsPath.empty()) {
        SaveUserKeybindings(m_userKeybindingsPath);
    }
    return true;
}

bool KeyboardShortcutsView::ResetItemToDefault(int itemIdx) {
    if (itemIdx < 0 || itemIdx >= static_cast<int>(m_shortcuts.size())) return false;
    auto& item = m_shortcuts[itemIdx];
    item.keyPills = item.defaultKeyPills;
    item.source = L"Default";
    if (m_isRecording && m_recordingItemIdx == itemIdx) {
        CancelRecording();
    }
    RecomputeConflicts();
    if (!m_userKeybindingsPath.empty()) {
        SaveUserKeybindings(m_userKeybindingsPath);
    }
    return true;
}

bool KeyboardShortcutsView::SetShortcutKeys(const std::wstring& idOrCommand, const std::vector<std::wstring>& newKeyPills) {
    for (size_t i = 0; i < m_shortcuts.size(); ++i) {
        if (m_shortcuts[i].id == idOrCommand || m_shortcuts[i].command == idOrCommand) {
            m_shortcuts[i].keyPills = newKeyPills;
            for (auto& p : m_shortcuts[i].keyPills) p = NormalizePillStr(p);
            m_shortcuts[i].source = (PillsCanonicalKey(m_shortcuts[i].keyPills) == PillsCanonicalKey(m_shortcuts[i].defaultKeyPills)) ? L"Default" : L"User";
            RecomputeConflicts();
            return true;
        }
    }
    return false;
}

std::vector<std::wstring> KeyboardShortcutsView::BuildKeyPillsFromEvent(bool isCtrl, bool isShift, bool isAlt, UINT vk) {
    if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
        vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
        vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
        vk == VK_LWIN || vk == VK_RWIN) {
        return {};
    }
    std::vector<std::wstring> pills;
    if (isCtrl) pills.push_back(L"Ctrl");
    if (isShift) pills.push_back(L"Shift");
    if (isAlt) pills.push_back(L"Alt");

    std::wstring keyName;
    if (vk >= 'A' && vk <= 'Z') {
        keyName.push_back(static_cast<wchar_t>(vk));
    } else if (vk >= '0' && vk <= '9') {
        keyName.push_back(static_cast<wchar_t>(vk));
    } else if (vk >= VK_F1 && vk <= VK_F12) {
        keyName = L"F" + std::to_wstring(vk - VK_F1 + 1);
    } else {
        switch (vk) {
            case VK_RETURN: keyName = L"Enter"; break;
            case VK_ESCAPE: keyName = L"Esc"; break;
            case VK_SPACE:  keyName = L"Space"; break;
            case VK_TAB:    keyName = L"Tab"; break;
            case VK_BACK:   keyName = L"Backspace"; break;
            case VK_DELETE: keyName = L"Delete"; break;
            case VK_LEFT:   keyName = L"Left"; break;
            case VK_RIGHT:  keyName = L"Right"; break;
            case VK_UP:     keyName = L"Up"; break;
            case VK_DOWN:   keyName = L"Down"; break;
            case VK_OEM_COMMA:  keyName = L","; break;
            case VK_OEM_PERIOD: keyName = L"."; break;
            case VK_OEM_2:      keyName = L"/"; break;
            case VK_OEM_3:      keyName = L"`"; break;
            case VK_OEM_5:      keyName = L"\\"; break;
            case VK_OEM_MINUS:  keyName = L"-"; break;
            case VK_OEM_PLUS:   keyName = L"="; break;
            case VK_OEM_4:      keyName = L"["; break;
            case VK_OEM_6:      keyName = L"]"; break;
            default: break;
        }
    }
    if (keyName.empty()) return {};
    pills.push_back(keyName);
    return pills;
}

bool KeyboardShortcutsView::HandleKeyRecording(WPARAM vk, bool isCtrl, bool isShift, bool isAlt) {
    if (!m_isRecording || m_recordingItemIdx < 0 || m_recordingItemIdx >= static_cast<int>(m_shortcuts.size())) {
        return false;
    }
    if (vk == VK_ESCAPE && !isCtrl && !isShift && !isAlt) {
        CancelRecording();
        return true;
    }
    if (vk == VK_RETURN && !isCtrl && !isShift && !isAlt && !m_recordedPills.empty()) {
        return CommitRecordedKeybinding();
    }
    auto pills = BuildKeyPillsFromEvent(isCtrl, isShift, isAlt, static_cast<UINT>(vk));
    if (pills.empty()) return true; // Consume bare modifier keys while recording

    // Allow 2-chord sequence (e.g. Ctrl+K then Ctrl+S -> cap at 6 pills total)
    if (!m_recordedPills.empty() && m_recordedPills.size() <= 3 && pills.size() <= 3 &&
        (isCtrl || isAlt)) {
        for (const auto& p : pills) m_recordedPills.push_back(p);
    } else {
        m_recordedPills = std::move(pills);
    }

    auto conflicts = FindConflictsForPills(m_recordedPills, m_recordingItemIdx, m_shortcuts[m_recordingItemIdx].when);
    if (!conflicts.empty()) {
        m_recordingConflictWarning = L"Conflicts with: " + conflicts[0];
        if (conflicts.size() > 1) m_recordingConflictWarning += L" (+" + std::to_wstring(conflicts.size() - 1) + L")";
    } else {
        m_recordingConflictWarning.clear();
    }
    return true;
}

bool KeyboardShortcutsView::LoadUserKeybindings(const std::wstring& jsonPath) {
    m_userKeybindingsPath = jsonPath;
    if (jsonPath.empty()) return false;
    std::ifstream in(std::filesystem::path(jsonPath), std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();
    if (raw.empty()) return false;

    int wlen = ::MultiByteToWideChar(CP_UTF8, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring content(static_cast<size_t>(wlen), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, raw.data(), static_cast<int>(raw.size()), &content[0], wlen);

    // Lightweight parser for {"bindings":[{"id":"...","command":"...","keys":["Ctrl","Alt","S"]}]}
    size_t pos = 0;
    auto parseStr = [&](size_t& p, std::wstring& out) -> bool {
        while (p < content.size() && content[p] != L'"') ++p;
        if (p >= content.size()) return false;
        ++p;
        out.clear();
        while (p < content.size()) {
            wchar_t c = content[p++];
            if (c == L'"') return true;
            if (c == L'\\' && p < content.size()) out += content[p++];
            else out += c;
        }
        return false;
    };

    bool anyLoaded = false;
    while (pos < content.size()) {
        size_t objStart = content.find(L'{', pos);
        if (objStart == std::wstring::npos) break;
        // Skip outer root object if it contains "bindings"
        size_t nextArr = content.find(L"\"bindings\"", objStart);
        if (nextArr != std::wstring::npos && nextArr < content.find(L'}', objStart)) {
            pos = nextArr + 10;
            continue;
        }
        size_t objEnd = content.find(L'}', objStart);
        if (objEnd == std::wstring::npos) break;
        std::wstring block = content.substr(objStart, objEnd - objStart + 1);
        pos = objEnd + 1;

        std::wstring idVal, cmdVal;
        std::vector<std::wstring> keysVal;
        size_t idPos = block.find(L"\"id\"");
        if (idPos != std::wstring::npos) {
            size_t colon = block.find(L':', idPos);
            if (colon != std::wstring::npos) {
                size_t sp = colon + 1;
                size_t bp = objStart + sp;
                parseStr(bp, idVal);
            }
        }
        size_t cmdPos = block.find(L"\"command\"");
        if (cmdPos != std::wstring::npos) {
            size_t colon = block.find(L':', cmdPos);
            if (colon != std::wstring::npos) {
                size_t bp = objStart + colon + 1;
                parseStr(bp, cmdVal);
            }
        }
        size_t keysPos = block.find(L"\"keys\"");
        if (keysPos != std::wstring::npos) {
            size_t arrS = block.find(L'[', keysPos);
            size_t arrE = block.find(L']', keysPos);
            if (arrS != std::wstring::npos && arrE != std::wstring::npos && arrE > arrS) {
                size_t kp = objStart + arrS + 1;
                size_t limit = objStart + arrE;
                while (kp < limit) {
                    size_t q = content.find(L'"', kp);
                    if (q == std::wstring::npos || q >= limit) break;
                    std::wstring pill;
                    if (parseStr(kp, pill)) {
                        keysVal.push_back(pill);
                    } else break;
                }
            }
        }

        if (!idVal.empty() || !cmdVal.empty()) {
            if (SetShortcutKeys(!idVal.empty() ? idVal : cmdVal, keysVal)) {
                anyLoaded = true;
            }
        }
    }

    RecomputeConflicts();
    return anyLoaded;
}

bool KeyboardShortcutsView::SaveUserKeybindings(const std::wstring& jsonPath) const {
    if (jsonPath.empty()) return false;
    std::error_code ec;
    std::filesystem::path p(jsonPath);
    if (!p.parent_path().empty()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    std::wostringstream out;
    out << L"{\n  \"bindings\": [\n";
    std::vector<const ShortcutItem*> userItems;
    for (const auto& it : m_shortcuts) {
        if (it.source == L"User") userItems.push_back(&it);
    }
    for (size_t i = 0; i < userItems.size(); ++i) {
        const auto* it = userItems[i];
        out << L"    {\n";
        out << L"      \"id\": \"" << EscapeJsonKbd(it->id) << L"\",\n";
        out << L"      \"command\": \"" << EscapeJsonKbd(it->command) << L"\",\n";
        out << L"      \"keys\": [";
        for (size_t k = 0; k < it->keyPills.size(); ++k) {
            if (k > 0) out << L", ";
            out << L"\"" << EscapeJsonKbd(it->keyPills[k]) << L"\"";
        }
        out << L"]\n    }" << (i + 1 < userItems.size() ? L"," : L"") << L"\n";
    }
    out << L"  ]\n}\n";

    std::wstring ws = out.str();
    int ulen = ::WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    std::string u8(ulen > 0 ? static_cast<size_t>(ulen) : 0, '\0');
    if (ulen > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), &u8[0], ulen, nullptr, nullptr);
    }
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs.write(u8.data(), static_cast<std::streamsize>(u8.size()));
    return ofs.good();
}

std::wstring KeyboardShortcutsView::ResolveOverrideActionForKey(
    bool isCtrl, bool isShift, bool isAlt, UINT vk, bool hasOpenEditor) {
    auto pills = BuildKeyPillsFromEvent(isCtrl, isShift, isAlt, vk);
    if (pills.empty()) return L"";

    // Expire pending first chord after 1.5s
    ULONGLONG now = ::GetTickCount64();
    if (!m_pendingChordPrefix.empty() && (now - m_pendingChordTick) > 1500) {
        m_pendingChordPrefix.clear();
    }

    if (!m_pendingChordPrefix.empty()) {
        std::vector<std::wstring> fullChord = m_pendingChordPrefix;
        for (const auto& p : pills) fullChord.push_back(p);
        std::wstring chordKey = PillsCanonicalKey(fullChord);
        m_pendingChordPrefix.clear();
        for (const auto& it : m_shortcuts) {
            if (it.source == L"User" && !it.keyPills.empty() && PillsCanonicalKey(it.keyPills) == chordKey) {
                if (it.when == L"editorTextFocus" && !hasOpenEditor) continue;
                return it.id;
            }
        }
    }

    std::wstring singleKey = PillsCanonicalKey(pills);
    // Check if this stroke starts any user-customized multi-chord sequence
    for (const auto& it : m_shortcuts) {
        if (it.source == L"User" && it.keyPills.size() > pills.size()) {
            std::vector<std::wstring> prefix(it.keyPills.begin(), it.keyPills.begin() + static_cast<ptrdiff_t>(pills.size()));
            if (PillsCanonicalKey(prefix) == singleKey) {
                m_pendingChordPrefix = pills;
                m_pendingChordTick = now;
                return L"__CHORD_PREFIX__";
            }
        }
    }

    for (const auto& it : m_shortcuts) {
        if (it.source == L"User" && !it.keyPills.empty() && PillsCanonicalKey(it.keyPills) == singleKey) {
            if (it.when == L"editorTextFocus" && !hasOpenEditor) continue;
            return it.id;
        }
    }
    return L"";
}

bool KeyboardShortcutsView::IsActionDefaultUnchanged(const std::wstring& actionId) const {
    for (const auto& it : m_shortcuts) {
        if (it.id == actionId) {
            return it.source != L"User";
        }
    }
    return true;
}

} // namespace phide::ui
