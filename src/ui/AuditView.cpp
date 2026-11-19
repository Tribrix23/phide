/**
 * @file AuditView.cpp
 * @brief Implementation of the Workspace Dead Code, Unused CSS, Dependency & .env Drift Audit UI (#7/#8).
 */

#include "phide/ui/AuditView.hpp"

#include <algorithm>
#include <cwctype>

namespace phide::ui {

using Microsoft::WRL::ComPtr;
using phide::editor::AuditCategory;
using phide::editor::AuditFinding;
using phide::editor::EnvAuditEngine;
using phide::editor::WorkspaceAuditEngine;
using phide::editor::WorkspaceAuditReport;

namespace {

static bool PtInRectF(POINT pt, const D2D1_RECT_F& r) noexcept {
    return static_cast<float>(pt.x) >= r.left && static_cast<float>(pt.x) <= r.right &&
           static_cast<float>(pt.y) >= r.top  && static_cast<float>(pt.y) <= r.bottom;
}

static std::wstring ToLowerW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

static float MeasureAuditBtnW(Theme& theme, const app::AppConfig& config, const std::wstring& lbl, float minW) {
    IDWriteTextFormat* pFmt = theme.fontButtonCenter ? theme.fontButtonCenter.Get()
                            : (theme.fontMenu ? theme.fontMenu.Get() : theme.fontBody.Get());
    if (theme.dwriteFactory && pFmt && !lbl.empty()) {
        ComPtr<IDWriteTextLayout> lyt;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(lbl.c_str(), static_cast<UINT32>(lbl.size()), pFmt, 1200.0f, 80.0f, &lyt)) && lyt) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(lyt->GetMetrics(&tm))) {
                return (std::max)(config.ScaleF(minW), tm.widthIncludingTrailingWhitespace + config.ScaleF(22.0f));
            }
        }
    }
    return (std::max)(config.ScaleF(minW), static_cast<float>(lbl.size()) * config.ScaleF(7.6f) + config.ScaleF(22.0f));
}

static void DrawBtn(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                    const D2D1_RECT_F& rc, const std::wstring& lbl, bool active, bool hovered,
                    ID2D1Brush* brNormal, ID2D1Brush* brHover, ID2D1Brush* brBorder) {
    if (rc.right <= rc.left + 2.0f) return;
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rc, config.ScaleF(5.0f), config.ScaleF(5.0f));
    ID2D1Brush* fill = active ? theme.brushBrandAccent.Get() : (hovered ? brHover : brNormal);
    if (fill) pRT->FillRoundedRectangle(rrc, fill);
    if (brBorder) pRT->DrawRoundedRectangle(rrc, active ? theme.brushBrandAccent.Get() : brBorder, hovered ? 1.3f : 1.0f);

    IDWriteTextFormat* pFmt = theme.fontButtonCenter ? theme.fontButtonCenter.Get()
                            : (theme.fontMenu ? theme.fontMenu.Get() : theme.fontBody.Get());
    ID2D1Brush* txt = active
        ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
        : theme.brushTextPrimary.Get();
    if (pFmt && txt) {
        DWRITE_TEXT_ALIGNMENT old = pFmt->GetTextAlignment();
        DWRITE_PARAGRAPH_ALIGNMENT oldP = pFmt->GetParagraphAlignment();
        pFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        pRT->DrawText(lbl.c_str(), static_cast<UINT32>(lbl.size()), pFmt, rc, txt, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        pFmt->SetTextAlignment(old);
        pFmt->SetParagraphAlignment(oldP);
    }
}

static void DrawClipText(ID2D1RenderTarget* pRT, IDWriteTextFormat* pFmt, const std::wstring& txt,
                         const D2D1_RECT_F& rc, ID2D1Brush* br,
                         DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
    if (!pFmt || !br || txt.empty() || rc.right <= rc.left + 1.0f) return;
    DWRITE_TEXT_ALIGNMENT prev = pFmt->GetTextAlignment();
    DWRITE_PARAGRAPH_ALIGNMENT prevP = pFmt->GetParagraphAlignment();
    pFmt->SetTextAlignment(align);
    pFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    pRT->DrawText(txt.c_str(), static_cast<UINT32>(txt.size()), pFmt, rc, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    pFmt->SetTextAlignment(prev);
    pFmt->SetParagraphAlignment(prevP);
}

} // namespace

AuditView::~AuditView() {
    StopWorker();
}

void AuditView::StopWorker() {
    m_cancelFlag.store(true, std::memory_order_relaxed);
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_isRunning.store(false, std::memory_order_relaxed);
}

void AuditView::SetStatusNotice(const std::wstring& msg, ULONGLONG durationMs) {
    m_statusNotice = msg;
    m_statusExpireTick = msg.empty() ? 0ull : (GetTickCount64() + durationMs);
}

void AuditView::StartAuditAsync(const std::wstring& workspaceRoot) {
    StopWorker();
    m_workspaceRoot = workspaceRoot;
    m_scrollY = 0.0f;
    m_hoveredRowIdx = -1;
    m_cancelFlag.store(false, std::memory_order_relaxed);
    m_isRunning.store(true, std::memory_order_relaxed);

    if (workspaceRoot.empty()) {
        std::lock_guard<std::mutex> lk(m_reportMutex);
        m_report = WorkspaceAuditReport{};
        m_report.completed = true;
        m_isRunning.store(false, std::memory_order_relaxed);
        return;
    }

    m_worker = std::thread([this, root = workspaceRoot]() {
        WorkspaceAuditReport res = WorkspaceAuditEngine::RunFullAudit(root, &m_cancelFlag);
        if (!m_cancelFlag.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lk(m_reportMutex);
            m_report = std::move(res);
        }
        m_isRunning.store(false, std::memory_order_relaxed);
        if (m_callbacks.onRequestRepaint && !m_cancelFlag.load(std::memory_order_relaxed)) {
            m_callbacks.onRequestRepaint();
        }
    });
}

void AuditView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                       float canvasLeft, float canvasWidth, float topY, float clientHeight) {
    if (!pRT || canvasWidth <= 20.0f || clientHeight <= topY + 20.0f) return;

    const bool dark = theme.IsDarkMode();
    ComPtr<ID2D1SolidColorBrush> brBg, brCard, brHdr, brBorder, brBtnBg, brBtnHover,
                                  brImport, brExport, brCss, brDep, brEnv;
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x111318) : D2D1::ColorF(0xF6F8FB), &brBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x181B22) : D2D1::ColorF(0xFFFFFF), &brCard);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x1C2029) : D2D1::ColorF(0xEAEEF5), &brHdr);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x2D333F) : D2D1::ColorF(0xCBD5E1), &brBorder);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x232834) : D2D1::ColorF(0xFFFFFF), &brBtnBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x2E3545) : D2D1::ColorF(0xDCE3EE), &brBtnHover);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x14B8A6) : D2D1::ColorF(0x0F766E), &brImport);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0xF59E0B) : D2D1::ColorF(0xB45309), &brExport);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x38BDF8) : D2D1::ColorF(0x0369A1), &brCss);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0xA855F7) : D2D1::ColorF(0x7E22CE), &brDep);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0xEF4444) : D2D1::ColorF(0xB91C1C), &brEnv);

    if (brBg) pRT->FillRectangle(D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, clientHeight), brBg.Get());

    WorkspaceAuditReport snap;
    {
        std::lock_guard<std::mutex> lk(m_reportMutex);
        snap = m_report;
    }
    const bool running = m_isRunning.load(std::memory_order_relaxed);

    IDWriteTextFormat* pFmt = theme.fontBody ? theme.fontBody.Get() : theme.fontMenu.Get();
    IDWriteTextFormat* pCode = theme.fontCode ? theme.fontCode.Get() : pFmt;
    ID2D1Brush* txtPri = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get();
    ID2D1Brush* txtSec = theme.brushTextSecondary ? theme.brushTextSecondary.Get() : txtPri;

    const float pad = config.ScaleF(16.0f);
    const float maxR = canvasLeft + canvasWidth - pad;
    const float btnH = config.ScaleF(30.0f);
    float curY = topY + pad;

    // 1. Header Row
    if (theme.fontTitle && txtPri) {
        std::wstring title = L"Workspace Audit & .env Drift Detector";
        DrawClipText(pRT, theme.fontTitle.Get(), title,
                     D2D1::RectF(canvasLeft + pad, curY, (std::max)(canvasLeft + pad, maxR - config.ScaleF(340.0f)), curY + btnH),
                     txtPri);
    }

    float rx = maxR;
    const float syncW = MeasureAuditBtnW(theme, config, L"Sync Keys to .env.example", 190.0f);
    if (rx - syncW > canvasLeft + pad + config.ScaleF(140.0f)) {
        m_rcBtnSyncEnv = D2D1::RectF(rx - syncW, curY, rx, curY + btnH);
        DrawBtn(pRT, theme, config, m_rcBtnSyncEnv, L"Sync Keys to .env.example", false,
                m_hoveredBtnId == 2, brBtnBg.Get(), brBtnHover.Get(), brBorder.Get());
        rx -= syncW + config.ScaleF(8.0f);
    } else {
        m_rcBtnSyncEnv = {};
    }
    const std::wstring rerunLbl = running ? L"Scanning..." : L"Re-run Audit";
    const float rerunW = MeasureAuditBtnW(theme, config, rerunLbl, 108.0f);
    if (rx - rerunW > canvasLeft + pad + config.ScaleF(100.0f)) {
        m_rcBtnRerun = D2D1::RectF(rx - rerunW, curY, rx, curY + btnH);
        DrawBtn(pRT, theme, config, m_rcBtnRerun, rerunLbl, running,
                m_hoveredBtnId == 1, brBtnBg.Get(), brBtnHover.Get(), brBorder.Get());
    } else {
        m_rcBtnRerun = {};
    }
    curY += btnH + config.ScaleF(6.0f);

    // Subtitle / Status Notice
    std::wstring sub = running
        ? L"Scanning workspace imports, exports, CSS selectors, package dependencies, and .env keys..."
        : (std::to_wstring(snap.filesScanned) + L" files scanned in " + std::to_wstring(snap.elapsedMs) +
           L" ms  |  " + std::to_wstring(snap.findings.size()) + L" findings");
    if (!m_statusNotice.empty() && GetTickCount64() < m_statusExpireTick) sub = m_statusNotice;
    DrawClipText(pRT, pFmt, sub, D2D1::RectF(canvasLeft + pad, curY, maxR, curY + config.ScaleF(20.0f)), txtSec);
    curY += config.ScaleF(26.0f);

    // 2. Category Filter Tabs + Search Box
    float tx = canvasLeft + pad;
    m_rcTabAll = m_rcTabImports = m_rcTabExports = m_rcTabCss = m_rcTabDeps = m_rcTabEnv = {};
    auto addTab = [&](D2D1_RECT_F& outRc, const std::wstring& lbl, float minW, int idx, int hid) {
        const float w = MeasureAuditBtnW(theme, config, lbl, minW);
        if (tx + w > maxR) { outRc = {}; return; }
        outRc = D2D1::RectF(tx, curY, tx + w, curY + btnH);
        DrawBtn(pRT, theme, config, outRc, lbl, m_filterTab == idx, m_hoveredBtnId == hid,
                brBtnBg.Get(), brBtnHover.Get(), brBorder.Get());
        tx += w + config.ScaleF(7.0f);
    };

    addTab(m_rcTabAll,     L"All (" + std::to_wstring(snap.findings.size()) + L")",        72.0f, 0, 10);
    addTab(m_rcTabImports, L"Imports (" + std::to_wstring(snap.unusedImportCount) + L")",  88.0f, 5, 15);
    addTab(m_rcTabExports, L"Exports (" + std::to_wstring(snap.unusedExportCount) + L")",  88.0f, 1, 11);
    addTab(m_rcTabCss,     L"CSS (" + std::to_wstring(snap.unusedCssCount) + L")",         72.0f, 2, 12);
    addTab(m_rcTabDeps,    L"Deps (" + std::to_wstring(snap.unusedDepCount) + L")",        80.0f, 3, 13);
    addTab(m_rcTabEnv,     L".env (" + std::to_wstring(snap.envDriftCount) + L")",         78.0f, 4, 14);

    const float searchW = (std::min)(config.ScaleF(180.0f), maxR - tx);
    if (searchW >= config.ScaleF(70.0f)) {
        m_rcSearchBox = D2D1::RectF(maxR - searchW, curY, maxR, curY + btnH);
        D2D1_ROUNDED_RECT rrcS = D2D1::RoundedRect(m_rcSearchBox, 4.0f, 4.0f);
        if (brBtnBg) pRT->FillRoundedRectangle(rrcS, brBtnBg.Get());
        pRT->DrawRoundedRectangle(rrcS, m_searchFocused ? theme.brushBrandAccent.Get() : brBorder.Get(),
                                  m_searchFocused ? 1.4f : 1.0f);
        DrawClipText(pRT, pFmt, m_searchText.empty() ? L"Filter findings..." : m_searchText,
                     D2D1::RectF(m_rcSearchBox.left + config.ScaleF(8.0f), curY,
                                 m_rcSearchBox.right - config.ScaleF(6.0f), curY + btnH),
                     m_searchText.empty() ? theme.brushTextMuted.Get() : txtPri);
    } else {
        m_rcSearchBox = {};
    }
    curY += btnH + config.ScaleF(10.0f);

    // 3. Findings List Viewport
    m_rcViewport = D2D1::RectF(canvasLeft + pad, curY, maxR, clientHeight - pad);
    m_rowHits.clear();
    if (m_rcViewport.bottom <= m_rcViewport.top + 10.0f) return;

    std::wstring qLow = ToLowerW(m_searchText);
    std::vector<size_t> visibleIndices;
    for (size_t i = 0; i < snap.findings.size(); ++i) {
        const auto& f = snap.findings[i];
        if (m_filterTab == 1 && f.category != AuditCategory::UnusedExport) continue;
        if (m_filterTab == 2 && f.category != AuditCategory::UnusedCssSelector) continue;
        if (m_filterTab == 3 && f.category != AuditCategory::UnusedDependency) continue;
        if (m_filterTab == 4 && f.category != AuditCategory::EnvDrift) continue;
        if (m_filterTab == 5 && f.category != AuditCategory::UnusedImport) continue;
        if (!qLow.empty()) {
            if (ToLowerW(f.symbol).find(qLow) == std::wstring::npos &&
                ToLowerW(f.displayPath).find(qLow) == std::wstring::npos &&
                ToLowerW(f.message).find(qLow) == std::wstring::npos) {
                continue;
            }
        }
        visibleIndices.push_back(i);
    }

    pRT->PushAxisAlignedClip(m_rcViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (visibleIndices.empty()) {
        std::wstring emptyTxt = running ? L"Audit in progress..." : L"No audit findings match the current filter.";
        DrawClipText(pRT, pFmt, emptyTxt,
                     D2D1::RectF(m_rcViewport.left, m_rcViewport.top + config.ScaleF(14.0f), m_rcViewport.right, m_rcViewport.top + config.ScaleF(36.0f)),
                     txtPri);
        if (!running) {
            std::wstring helpTxt =
                L"Audit checks dead imports/functions, unreferenced exports, unused CSS selectors, unused package dependencies, and missing .env variables.  "
                L"'Sync Keys to .env.example' copies only key names from .env (never secret values) into .env.example.";
            DrawClipText(pRT, pFmt, helpTxt,
                         D2D1::RectF(m_rcViewport.left, m_rcViewport.top + config.ScaleF(40.0f), m_rcViewport.right, m_rcViewport.top + config.ScaleF(64.0f)),
                         txtSec);
        }
        m_maxScrollY = 0.0f;
    } else {
        const float rowH = config.ScaleF(46.0f);
        const float gap  = config.ScaleF(6.0f);
        float ry = m_rcViewport.top - m_scrollY;
        float totalH = 0.0f;

        for (size_t vi = 0; vi < visibleIndices.size(); ++vi) {
            size_t fi = visibleIndices[vi];
            const auto& f = snap.findings[fi];
            if (ry + rowH >= m_rcViewport.top && ry <= m_rcViewport.bottom) {
                D2D1_RECT_F rcRow = D2D1::RectF(m_rcViewport.left, ry, m_rcViewport.right, ry + rowH);
                m_rowHits.push_back({ fi, rcRow });
                bool isHov = (m_hoveredRowIdx == static_cast<int>(m_rowHits.size()) - 1);
                D2D1_ROUNDED_RECT rrcRow = D2D1::RoundedRect(rcRow, 5.0f, 5.0f);
                if (isHov && brHdr) pRT->FillRoundedRectangle(rrcRow, brHdr.Get());
                else if (brCard)    pRT->FillRoundedRectangle(rrcRow, brCard.Get());
                if (brBorder) pRT->DrawRoundedRectangle(rrcRow, isHov ? theme.brushBrandAccent.Get() : brBorder.Get(), 0.9f);

                const wchar_t* tagTxt = L"EXPORT";
                ID2D1Brush* tagBr = brExport.Get();
                if (f.category == AuditCategory::UnusedImport) { tagTxt = L"IMPORT"; tagBr = brImport.Get(); }
                else if (f.category == AuditCategory::UnusedCssSelector) { tagTxt = L"CSS"; tagBr = brCss.Get(); }
                else if (f.category == AuditCategory::UnusedDependency) { tagTxt = L"DEP"; tagBr = brDep.Get(); }
                else if (f.category == AuditCategory::EnvDrift) { tagTxt = L".ENV"; tagBr = brEnv.Get(); }

                const float badgeW = config.ScaleF(54.0f);
                D2D1_RECT_F rcBadge = D2D1::RectF(rcRow.left + config.ScaleF(10.0f), rcRow.top + config.ScaleF(10.0f),
                                                  rcRow.left + config.ScaleF(10.0f) + badgeW, rcRow.top + config.ScaleF(28.0f));
                if (tagBr) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcBadge, 3.5f, 3.5f), tagBr);
                DrawClipText(pRT, pFmt, tagTxt, rcBadge,
                             theme.brushTextWhite ? theme.brushTextWhite.Get() : txtPri, DWRITE_TEXT_ALIGNMENT_CENTER);

                const float textLeft = rcBadge.right + config.ScaleF(10.0f);
                const float locW = (std::min)(config.ScaleF(240.0f), (rcRow.right - textLeft) * 0.42f);
                std::wstring loc = f.displayPath + L":" + std::to_wstring(f.line0 + 1);
                DrawClipText(pRT, pCode, f.symbol,
                             D2D1::RectF(textLeft, rcRow.top + config.ScaleF(4.0f), rcRow.right - locW - config.ScaleF(12.0f), rcRow.top + config.ScaleF(23.0f)),
                             txtPri);
                DrawClipText(pRT, pCode, loc,
                             D2D1::RectF(rcRow.right - locW - config.ScaleF(10.0f), rcRow.top + config.ScaleF(4.0f), rcRow.right - config.ScaleF(10.0f), rcRow.top + config.ScaleF(23.0f)),
                             theme.brushBrandAccent.Get(), DWRITE_TEXT_ALIGNMENT_TRAILING);
                DrawClipText(pRT, pFmt, f.message,
                             D2D1::RectF(textLeft, rcRow.top + config.ScaleF(23.0f), rcRow.right - config.ScaleF(10.0f), rcRow.bottom - config.ScaleF(3.0f)),
                             txtSec);
            }
            ry += rowH + gap;
            totalH += rowH + gap;
        }
        m_maxScrollY = (std::max)(0.0f, totalH - (m_rcViewport.bottom - m_rcViewport.top));
        if (m_scrollY > m_maxScrollY) m_scrollY = m_maxScrollY;
    }

    pRT->PopAxisAlignedClip();
}

bool AuditView::HandleLButtonDown(POINT clientPt, HWND /*hwnd*/, const app::AppConfig& /*config*/) {
    m_searchFocused = PtInRectF(clientPt, m_rcSearchBox);
    if (m_searchFocused) return true;

    if (PtInRectF(clientPt, m_rcBtnRerun)) {
        StartAuditAsync(m_workspaceRoot);
        return true;
    }
    if (PtInRectF(clientPt, m_rcBtnSyncEnv)) {
        int added = 0;
        if (EnvAuditEngine::SyncEnvExampleKeys(m_workspaceRoot, added)) {
            m_statusNotice = (added > 0)
                ? (L"Synced " + std::to_wstring(added) + L" env key(s) to .env.example (values omitted)")
                : L".env.example is already in sync with .env";
            m_statusExpireTick = GetTickCount64() + 3200ull;
            if (m_callbacks.onEnvSynced) m_callbacks.onEnvSynced(added);
            StartAuditAsync(m_workspaceRoot);
        } else {
            m_statusNotice = L"No .env file found in workspace root";
            m_statusExpireTick = GetTickCount64() + 2600ull;
        }
        return true;
    }

    if (PtInRectF(clientPt, m_rcTabAll))     { m_filterTab = 0; m_scrollY = 0.0f; return true; }
    if (PtInRectF(clientPt, m_rcTabImports)) { m_filterTab = 5; m_scrollY = 0.0f; return true; }
    if (PtInRectF(clientPt, m_rcTabExports)) { m_filterTab = 1; m_scrollY = 0.0f; return true; }
    if (PtInRectF(clientPt, m_rcTabCss))     { m_filterTab = 2; m_scrollY = 0.0f; return true; }
    if (PtInRectF(clientPt, m_rcTabDeps))    { m_filterTab = 3; m_scrollY = 0.0f; return true; }
    if (PtInRectF(clientPt, m_rcTabEnv))     { m_filterTab = 4; m_scrollY = 0.0f; return true; }

    for (const auto& rh : m_rowHits) {
        if (PtInRectF(clientPt, rh.bounds)) {
            AuditFinding target;
            bool valid = false;
            {
                std::lock_guard<std::mutex> lk(m_reportMutex);
                if (rh.findingIndex < m_report.findings.size()) {
                    target = m_report.findings[rh.findingIndex];
                    valid = true;
                }
            }
            if (valid && m_callbacks.onOpenLocation) {
                m_callbacks.onOpenLocation(target.filePath, target.line0, target.col0);
            }
            return true;
        }
    }
    return false;
}

bool AuditView::HandleMouseMove(POINT clientPt) {
    int btn = -1;
    if (PtInRectF(clientPt, m_rcBtnRerun)) btn = 1;
    else if (PtInRectF(clientPt, m_rcBtnSyncEnv)) btn = 2;
    else if (PtInRectF(clientPt, m_rcTabAll)) btn = 10;
    else if (PtInRectF(clientPt, m_rcTabImports)) btn = 15;
    else if (PtInRectF(clientPt, m_rcTabExports)) btn = 11;
    else if (PtInRectF(clientPt, m_rcTabCss)) btn = 12;
    else if (PtInRectF(clientPt, m_rcTabDeps)) btn = 13;
    else if (PtInRectF(clientPt, m_rcTabEnv)) btn = 14;

    int row = -1;
    for (size_t i = 0; i < m_rowHits.size(); ++i) {
        if (PtInRectF(clientPt, m_rowHits[i].bounds)) { row = static_cast<int>(i); break; }
    }
    bool changed = (btn != m_hoveredBtnId || row != m_hoveredRowIdx);
    m_hoveredBtnId = btn;
    m_hoveredRowIdx = row;
    return changed;
}

bool AuditView::HandleMouseWheel(short delta, const app::AppConfig& config) {
    float step = config.ScaleF(44.0f) * (delta > 0 ? -1.0f : 1.0f);
    float next = std::clamp(m_scrollY + step, 0.0f, m_maxScrollY);
    bool changed = (next != m_scrollY);
    m_scrollY = next;
    return changed;
}

bool AuditView::HandleChar(wchar_t ch) {
    if (!m_searchFocused) return false;
    if (ch >= 32 && ch != 127 && m_searchText.size() < 64) {
        m_searchText.push_back(ch);
        m_scrollY = 0.0f;
        return true;
    }
    return false;
}

bool AuditView::HandleKeyDown(WPARAM vk) {
    if (!m_searchFocused) return false;
    if (vk == VK_BACK && !m_searchText.empty()) {
        m_searchText.pop_back();
        m_scrollY = 0.0f;
        return true;
    }
    if (vk == VK_ESCAPE) {
        m_searchFocused = false;
        return true;
    }
    return false;
}

LPCWSTR AuditView::GetCursorForPoint(POINT clientPt) const noexcept {
    if (PtInRectF(clientPt, m_rcBtnRerun) || PtInRectF(clientPt, m_rcBtnSyncEnv) ||
        PtInRectF(clientPt, m_rcTabAll) || PtInRectF(clientPt, m_rcTabImports) ||
        PtInRectF(clientPt, m_rcTabExports) ||
        PtInRectF(clientPt, m_rcTabCss) || PtInRectF(clientPt, m_rcTabDeps) ||
        PtInRectF(clientPt, m_rcTabEnv)) {
        return IDC_HAND;
    }
    if (PtInRectF(clientPt, m_rcSearchBox)) return IDC_IBEAM;
    for (const auto& rh : m_rowHits) {
        if (PtInRectF(clientPt, rh.bounds)) return IDC_HAND;
    }
    return IDC_ARROW;
}

} // namespace phide::ui
