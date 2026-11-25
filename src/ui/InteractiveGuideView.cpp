/**
 * @file InteractiveGuideView.cpp
 * @brief Modern ErdView-inspired Interactive Feature Guide powered by real EditorCanvas code editors.
 */

#include "phide/ui/InteractiveGuideView.hpp"
#include "phide/ui/EditorCanvas.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace phide::ui {

namespace {

bool PtInRectF(POINT pt, const D2D1_RECT_F& r) noexcept {
    return r.right > r.left && r.bottom > r.top &&
           static_cast<float>(pt.x) >= r.left && static_cast<float>(pt.x) <= r.right &&
           static_cast<float>(pt.y) >= r.top  && static_cast<float>(pt.y) <= r.bottom;
}

float MeasureWrappedTextH(IDWriteFactory* pFactory, IDWriteTextFormat* pFmt,
                          const std::wstring& txt, float maxW, float fallbackH) {
    if (!pFactory || !pFmt || txt.empty() || maxW <= 8.0f) return fallbackH;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> lay;
    if (SUCCEEDED(pFactory->CreateTextLayout(txt.c_str(), static_cast<UINT32>(txt.size()),
                                             pFmt, maxW, 2000.0f, &lay)) && lay) {
        lay->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(lay->GetMetrics(&tm))) {
            return tm.height;
        }
    }
    return fallbackH;
}

float MeasureSingleLineW(IDWriteFactory* pFactory, IDWriteTextFormat* pFmt,
                         const std::wstring& txt, float fallbackW) {
    if (!pFactory || !pFmt || txt.empty()) return fallbackW;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> lay;
    if (SUCCEEDED(pFactory->CreateTextLayout(txt.c_str(), static_cast<UINT32>(txt.size()),
                                             pFmt, 1200.0f, 60.0f, &lay)) && lay) {
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(lay->GetMetrics(&tm))) {
            return tm.widthIncludingTrailingWhitespace;
        }
    }
    return fallbackW;
}

void DrawCenteredTxt(ID2D1RenderTarget* pRT, IDWriteTextFormat* pFmt,
                     const std::wstring& txt, const D2D1_RECT_F& r, ID2D1Brush* br) {
    if (!pRT || !pFmt || !br || txt.empty()) return;
    DWRITE_TEXT_ALIGNMENT oldA = pFmt->GetTextAlignment();
    DWRITE_PARAGRAPH_ALIGNMENT oldP = pFmt->GetParagraphAlignment();
    pFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    pFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    pRT->DrawText(txt.c_str(), static_cast<UINT32>(txt.size()), pFmt, r, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    pFmt->SetTextAlignment(oldA);
    pFmt->SetParagraphAlignment(oldP);
}

bool ContainsCi(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty() || hay.size() < needle.size()) return false;
    std::wstring h = hay, n = needle;
    for (wchar_t& c : h) c = static_cast<wchar_t>(towlower(c));
    for (wchar_t& c : n) c = static_cast<wchar_t>(towlower(c));
    return h.find(n) != std::wstring::npos;
}

} // namespace

InteractiveGuideView::InteractiveGuideView() {
    m_lastConfig.fontSize = 14.0f;
    m_lastConfig.lineHeightMultiplier = 1.55f;
    m_lastConfig.tabSize = 4;
    InitializeEditors();
    MarkSectionExplored(0);
}

void InteractiveGuideView::InitializeEditors() {
    m_editors.clear();
    m_editors.resize(7);
    for (int i = 0; i < 7; ++i) {
        ResetEditorBlock(i);
    }
    m_focusedEditorIdx = 0;
    m_editors[0].isFocused = true;
}

void InteractiveGuideView::ResetEditorBlock(int id) {
    if (id < 0 || id >= static_cast<int>(m_editors.size())) return;
    auto& ed = m_editors[static_cast<size_t>(id)];
    ed.id = id;
    ed.doc.secondarySelections.clear();
    ed.doc.ClearSelection();
    ed.doc.scrollLine = 0;
    ed.doc.scrollOffsetX = 0.0f;
    ed.showIntelliSense = false;
    ed.ghostText.clear();
    ed.readOnly = false;

    if (id == 0) {
        ed.languageExt = L".php";
        ed.doc.fileName = L"UserController.php";
        ed.doc.filePath = L"UserController.php";
        ed.doc.buffer.SetText(
            L"<?php\n"
            L"function fetchUserStats(int $userId): array {\n"
            L"    $pdo = Database::getConnection();\n"
            L"    $stmt = $pdo->prepare(\"SELECT * FROM stats WHERE user_id = :id\");\n"
            L"    $stmt->execute(['id' => $userId]);\n"
            L"    return $stmt->"
        );
        ed.doc.buffer.SetCursor(5, 18);
        ed.ghostText = L"fetchAll(PDO::FETCH_ASSOC);";
        ed.ghostLine = 5;
        ed.ghostCol = 18;
    } else if (id == 1) {
        ed.languageExt = L".js";
        ed.doc.fileName = L"canvas.js";
        ed.doc.filePath = L"canvas.js";
        ed.doc.buffer.SetText(
            L"const canvas = document.querySelector('canvas');\n"
            L"const context = canvas.getContext('2d');\n"
            L"\n"
            L"context.strokeStyle = '#38bdf8';\n"
            L"context.fillRect(0, 0, 150, 75);\n"
            L"context."
        );
        ed.doc.buffer.SetCursor(5, 8);
        UpdateIntelliSense(ed);
        ed.showIntelliSense = !ed.intelliSenseItems.empty();
    } else if (id == 2) {
        ed.languageExt = L".ts";
        ed.doc.fileName = L"sprint-tasks.ts";
        ed.doc.filePath = L"sprint-tasks.ts";
        ed.doc.buffer.SetText(
            L"// [x] TASK Configure database connection pool in config.php\n"
            L"// [x] TASK Add REST API router & middleware pipeline\n"
            L"// [ ] TASK Implement JWT session authentication & refresh\n"
            L"// [ ] TASK Add rate limiting & CSRF protection headers\n"
            L"// [ ] TASK Verify live SSE streaming output in API Tester"
        );
        ed.doc.buffer.SetCursor(2, 3);
    } else if (id == 3) {
        ed.languageExt = L".css";
        ed.doc.fileName = L"theme-tokens.css";
        ed.doc.filePath = L"theme-tokens.css";
        ed.doc.buffer.SetText(
            L":root {\n"
            L"    --brand-primary: #3b82f6;           /* Hex swatch */\n"
            L"    --accent-emerald: hsl(152, 69%, 45%); /* HSL swatch */\n"
            L"    --surface-glow: rgba(139, 92, 246, 0.72); /* RGBA swatch */\n"
            L"    --alert-amber: #f59e0b;              /* Click square to edit */\n"
            L"}"
        );
        ed.doc.buffer.SetCursor(1, 22);
    } else if (id == 4) {
        ed.languageExt = L".tsx";
        ed.doc.fileName = L"DashboardCard.tsx";
        ed.doc.filePath = L"DashboardCard.tsx";
        ed.doc.buffer.SetText(
            L"/*! Preserved license header - classified as Note, never Critical */\n"
            L"// TODO: Memoize computed revenue totals\n"
            L"// ^ Watch re-render depth inside nested callbacks\n"
            L"export function DashboardCard({ items }: { items: number[] }) {\n"
            L"    return items.map((n, idx) => ({ key: idx, val: (n * 2) }));\n"
            L"}"
        );
        ed.doc.buffer.SetCursor(4, 34);
    } else if (id == 5) {
        ed.languageExt = L".ts";
        ed.doc.fileName = L"api-config.ts";
        ed.doc.filePath = L"api-config.ts";
        ed.doc.buffer.SetText(
            L"export const endpoints = {\n"
            L"    users:   baseUrl + '/v1/users',\n"
            L"    orders:  baseUrl + '/v1/orders',\n"
            L"    metrics: baseUrl + '/v1/metrics',\n"
            L"    health:  baseUrl + '/v1/health',\n"
            L"};"
        );
        ed.doc.buffer.SetCursor(1, 13);
    } else if (id == 6) {
        ed.languageExt = L".json";
        ed.doc.fileName = L"response.json";
        ed.doc.filePath = L"response.json";
        ed.readOnly = false;
        UpdateApiSimulatorResponse();
    }
}

void InteractiveGuideView::UpdateIntelliSense(GuideEditorBlock& ed) {
    auto cursor = ed.doc.buffer.GetCursor();
    std::wstring line = ed.doc.buffer.GetLine(cursor.line);
    const auto& allLines = ed.doc.buffer.GetLines();
    ed.intelliSenseItems = m_intelliSense.QueryCompletions(ed.doc.fileName, line, cursor.col, &allLines);
    if (ed.intelliSenseItems.empty()) {
        ed.showIntelliSense = false;
        ed.intelliSenseSelectedIdx = 0;
    } else if (ed.intelliSenseSelectedIdx >= ed.intelliSenseItems.size()) {
        ed.intelliSenseSelectedIdx = 0;
    }
}

void InteractiveGuideView::ApplyCompletion(GuideEditorBlock& ed, size_t idx) {
    if (idx >= ed.intelliSenseItems.size()) return;
    const auto& item = ed.intelliSenseItems[idx];
    auto cursor = ed.doc.buffer.GetCursor();
    std::wstring line = ed.doc.buffer.GetLine(cursor.line);
    std::wstring prefix = editor::IntelliSenseEngine::ExtractQueryPrefix(line, cursor.col);
    for (size_t i = 0; i < prefix.length(); ++i) {
        ed.doc.buffer.Backspace();
    }
    std::wstring toInsert = !item.insertText.empty() ? item.insertText : item.label;
    if (item.kind == editor::CompletionKind::Method && toInsert.find(L'(') == std::wstring::npos) {
        toInsert += L"()";
    }
    for (wchar_t c : toInsert) {
        ed.doc.buffer.InsertChar(c);
    }
    ed.showIntelliSense = false;
    ed.intelliSenseItems.clear();
    MarkSectionExplored(1);
}

void InteractiveGuideView::UpdateApiSimulatorResponse() {
    if (m_editors.size() <= 6) return;
    auto& ed = m_editors[6];
    const wchar_t* methods[3] = { L"GET", L"POST", L"PUT" };
    const wchar_t* paths[3] = { L"/v1/chat/completions", L"/v1/users/profile", L"/api/health" };

    if (m_apiMethodIdx == 1 && m_apiPresetIdx == 0) {
        m_apiStatusCode = 200;
        m_apiLatencyMs = 142 + (m_apiSendCount % 5) * 19;
        m_apiPayloadBytes = 4820;
        ed.doc.buffer.SetText(
            L"{\n"
            L"  \"id\": \"chatcmpl-9f2a\",\n"
            L"  \"method\": \"POST\",\n"
            L"  \"endpoint\": \"/v1/chat/completions\",\n"
            L"  \"stream\": true,\n"
            L"  \"usage\": { \"prompt_tokens\": 48, \"completion_tokens\": 128 }\n"
            L"}"
        );
    } else if (m_apiPresetIdx == 1) {
        m_apiStatusCode = (m_apiMethodIdx == 2) ? 204 : 200;
        m_apiLatencyMs = 64 + (m_apiSendCount % 4) * 11;
        m_apiPayloadBytes = 1290;
        ed.doc.buffer.SetText(
            std::wstring(L"{\n  \"method\": \"") + methods[m_apiMethodIdx] + L"\",\n"
            L"  \"user\": { \"id\": 42, \"role\": \"maintainer\", \"active\": true },\n"
            L"  \"endpoint\": \"" + paths[m_apiPresetIdx] + L"\"\n"
            L"}"
        );
    } else {
        m_apiStatusCode = 200;
        m_apiLatencyMs = 18 + (m_apiSendCount % 3) * 4;
        m_apiPayloadBytes = 384;
        ed.doc.buffer.SetText(
            std::wstring(L"{\n  \"method\": \"") + methods[m_apiMethodIdx] + L"\",\n"
            L"  \"status\": \"healthy\",\n"
            L"  \"db\": \"connected\",\n"
            L"  \"uptimeSec\": 86412\n"
            L"}"
        );
    }
    ed.doc.buffer.SetCursor(1, 2);
}

void InteractiveGuideView::MarkSectionExplored(int secIdx) {
    if (secIdx >= 0 && secIdx < 8) {
        m_exploredSections.insert(secIdx);
    }
}

void InteractiveGuideView::ExecuteButtonAction(GuideButtonId id) {
    switch (id) {
    case GuideButtonId::ResetAll:
        InitializeEditors();
        m_activeCategory = 0;
        m_apiMethodIdx = 1;
        m_apiPresetIdx = 0;
        m_scrollY = 0.0f;
        UpdateApiSimulatorResponse();
        break;
    case GuideButtonId::AcceptGhost:
        if (!m_editors.empty()) {
            auto& ed = m_editors[0];
            if (!ed.ghostText.empty()) {
                ed.doc.buffer.SetCursor(ed.ghostLine, ed.ghostCol);
                for (wchar_t c : ed.ghostText) ed.doc.buffer.InsertChar(c);
                ed.ghostText.clear();
                m_focusedEditorIdx = 0;
                for (size_t i = 0; i < m_editors.size(); ++i) m_editors[i].isFocused = (i == 0);
                MarkSectionExplored(0);
            }
        }
        break;
    case GuideButtonId::ResetGhost:
        ResetEditorBlock(0);
        MarkSectionExplored(0);
        break;
    case GuideButtonId::TriggerIntelliSense:
        if (m_editors.size() > 1) {
            m_focusedEditorIdx = 1;
            for (size_t i = 0; i < m_editors.size(); ++i) m_editors[i].isFocused = (i == 1);
            UpdateIntelliSense(m_editors[1]);
            m_editors[1].showIntelliSense = !m_editors[1].intelliSenseItems.empty();
            MarkSectionExplored(1);
        }
        break;
    case GuideButtonId::ResetIntelliSense:
        ResetEditorBlock(1);
        MarkSectionExplored(1);
        break;
    case GuideButtonId::CompleteAllTasks:
    case GuideButtonId::ClearAllTasks:
        if (m_editors.size() > 2) {
            auto& buf = m_editors[2].doc.buffer;
            const bool markDone = (id == GuideButtonId::CompleteAllTasks);
            for (size_t l = 0; l < buf.GetLineCount(); ++l) {
                std::wstring line = buf.GetLine(l);
                size_t pEmpty = line.find(L"[ ]");
                size_t pDone = line.find(L"[x]");
                if (pDone == std::wstring::npos) pDone = line.find(L"[X]");
                if (markDone && pEmpty != std::wstring::npos) {
                    line.replace(pEmpty, 3, L"[x]");
                    buf.SetLine(l, line);
                } else if (!markDone && pDone != std::wstring::npos) {
                    line.replace(pDone, 3, L"[ ]");
                    buf.SetLine(l, line);
                }
            }
            MarkSectionExplored(2);
        }
        break;
    case GuideButtonId::ResetTasks:
        ResetEditorBlock(2);
        MarkSectionExplored(2);
        break;
    case GuideButtonId::AddAccentColor:
        if (m_editors.size() > 3) {
            auto& buf = m_editors[3].doc.buffer;
            size_t insertLine = (buf.GetLineCount() > 1) ? buf.GetLineCount() - 1 : 0;
            buf.SetCursor(insertLine, 0);
            const std::wstring newLine = L"    --neon-cyan: #06b6d4;             /* Added live token */\n";
            for (wchar_t c : newLine) buf.InsertChar(c);
            m_focusedEditorIdx = 3;
            for (size_t i = 0; i < m_editors.size(); ++i) m_editors[i].isFocused = (i == 3);
            MarkSectionExplored(3);
        }
        break;
    case GuideButtonId::ResetColors:
        ResetEditorBlock(3);
        MarkSectionExplored(3);
        break;
    case GuideButtonId::JumpMatchedBracket:
        if (m_editors.size() > 4) {
            m_editors[4].doc.buffer.SetCursor(4, 34);
            m_focusedEditorIdx = 4;
            for (size_t i = 0; i < m_editors.size(); ++i) m_editors[i].isFocused = (i == 4);
            MarkSectionExplored(4);
        }
        break;
    case GuideButtonId::ResetBrackets:
        ResetEditorBlock(4);
        MarkSectionExplored(4);
        break;
    case GuideButtonId::SelectAllOccurrences:
        if (m_editors.size() > 5) {
            auto& ed = m_editors[5];
            ed.doc.secondarySelections.clear();
            const std::wstring target = L"baseUrl";
            bool setPrimary = false;
            for (size_t l = 0; l < ed.doc.buffer.GetLineCount(); ++l) {
                const auto& ln = ed.doc.buffer.GetLine(l);
                size_t p = ln.find(target);
                if (p != std::wstring::npos) {
                    if (!setPrimary) {
                        ed.doc.selAnchor = { l, p };
                        ed.doc.selEnd = { l, p + target.size() };
                        ed.doc.hasSelection = true;
                        ed.doc.buffer.SetCursor(l, p + target.size());
                        setPrimary = true;
                    } else {
                        ed.doc.secondarySelections.push_back({ { l, p }, { l, p + target.size() } });
                    }
                }
            }
            m_focusedEditorIdx = 5;
            for (size_t i = 0; i < m_editors.size(); ++i) m_editors[i].isFocused = (i == 5);
            MarkSectionExplored(5);
        }
        break;
    case GuideButtonId::AddLineCursors:
        if (m_editors.size() > 5) {
            auto& ed = m_editors[5];
            ed.doc.ClearSelection();
            ed.doc.secondarySelections.clear();
            ed.doc.buffer.SetCursor(1, 13);
            for (size_t l = 2; l <= 4 && l < ed.doc.buffer.GetLineCount(); ++l) {
                ed.doc.secondarySelections.push_back({ { l, 13 }, { l, 13 } });
            }
            m_focusedEditorIdx = 5;
            for (size_t i = 0; i < m_editors.size(); ++i) m_editors[i].isFocused = (i == 5);
            MarkSectionExplored(5);
        }
        break;
    case GuideButtonId::ResetMultiCursor:
        ResetEditorBlock(5);
        MarkSectionExplored(5);
        break;
    case GuideButtonId::ApiMethodGet:  m_apiMethodIdx = 0; UpdateApiSimulatorResponse(); MarkSectionExplored(6); break;
    case GuideButtonId::ApiMethodPost: m_apiMethodIdx = 1; UpdateApiSimulatorResponse(); MarkSectionExplored(6); break;
    case GuideButtonId::ApiMethodPut:  m_apiMethodIdx = 2; UpdateApiSimulatorResponse(); MarkSectionExplored(6); break;
    case GuideButtonId::ApiPresetChat:   m_apiPresetIdx = 0; UpdateApiSimulatorResponse(); MarkSectionExplored(6); break;
    case GuideButtonId::ApiPresetUsers:  m_apiPresetIdx = 1; UpdateApiSimulatorResponse(); MarkSectionExplored(6); break;
    case GuideButtonId::ApiPresetHealth: m_apiPresetIdx = 2; UpdateApiSimulatorResponse(); MarkSectionExplored(6); break;
    case GuideButtonId::ApiSendRequest:  m_apiSendCount++; UpdateApiSimulatorResponse(); MarkSectionExplored(6); break;
    case GuideButtonId::OpenFullApiTester: if (onLaunchAction) onLaunchAction(L"api"); MarkSectionExplored(6); break;
    case GuideButtonId::OpenTaskRunner:    if (onLaunchAction) onLaunchAction(L"task_runner"); MarkSectionExplored(7); break;
    case GuideButtonId::OpenErd:           if (onLaunchAction) onLaunchAction(L"erd"); MarkSectionExplored(7); break;
    case GuideButtonId::OpenSqlConsole:    if (onLaunchAction) onLaunchAction(L"sql_console"); MarkSectionExplored(7); break;
    case GuideButtonId::OpenPackages:      if (onLaunchAction) onLaunchAction(L"packages"); MarkSectionExplored(7); break;
    case GuideButtonId::ToggleZenMode:     if (onLaunchAction) onLaunchAction(L"zen_mode"); MarkSectionExplored(7); break;
    default: break;
    }
}

void InteractiveGuideView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                  float left, float width, float top, float height,
                                  EditorCanvas* pHostEditor) {
    if (!pRT || width < 40.0f || height < 40.0f) return;
    if (pHostEditor) m_pHostEditor = pHostEditor;
    m_lastConfig = config;

    m_viewBounds = D2D1::RectF(left, top, left + width, top + height);
    m_actionHits.clear();
    m_filterHits.clear();
    m_bentoHits.clear();
    m_scrollTrackBounds = {};
    m_scrollThumbBounds = {};
    for (auto& ed : m_editors) {
        ed.cardBounds = {};
        ed.codeBoxBounds = {};
        ed.resetBtnBounds = {};
        ed.intelliSensePopupBounds = {};
    }

    const bool isDark = theme.IsDarkMode();
    auto makeBrush = [&](D2D1_COLOR_F col) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> br;
        pRT->CreateSolidColorBrush(col, &br);
        return br;
    };

    auto brCanvasBg   = makeBrush(isDark ? D2D1::ColorF(0.072f, 0.080f, 0.096f, 1.0f) : D2D1::ColorF(0.955f, 0.965f, 0.985f, 1.0f));
    auto brGridLine   = makeBrush(isDark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.028f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.032f));
    auto brTopBarBg   = makeBrush(isDark ? D2D1::ColorF(0.096f, 0.106f, 0.128f, 1.0f) : D2D1::ColorF(0.988f, 0.992f, 1.0f, 1.0f));
    auto brCardBg     = makeBrush(isDark ? D2D1::ColorF(0.104f, 0.115f, 0.138f, 1.0f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
    auto brCardHover  = makeBrush(isDark ? D2D1::ColorF(0.122f, 0.136f, 0.165f, 1.0f) : D2D1::ColorF(0.975f, 0.985f, 1.0f, 1.0f));
    auto brEditorHdr  = makeBrush(isDark ? D2D1::ColorF(0.082f, 0.090f, 0.110f, 1.0f) : D2D1::ColorF(0.935f, 0.946f, 0.970f, 1.0f));
    auto brBorder     = makeBrush(isDark ? D2D1::ColorF(0.205f, 0.225f, 0.270f, 1.0f) : D2D1::ColorF(0.805f, 0.830f, 0.880f, 1.0f));
    auto brPillBg     = makeBrush(isDark ? D2D1::ColorF(0.145f, 0.160f, 0.192f, 1.0f) : D2D1::ColorF(0.905f, 0.920f, 0.950f, 1.0f));
    auto brPillHov    = makeBrush(isDark ? D2D1::ColorF(0.190f, 0.210f, 0.255f, 1.0f) : D2D1::ColorF(0.840f, 0.865f, 0.915f, 1.0f));
    auto brAccentBlue = makeBrush(D2D1::ColorF(0.07f, 0.45f, 0.88f, 1.0f));
    auto brAccentHov  = makeBrush(D2D1::ColorF(0.12f, 0.52f, 0.96f, 1.0f));
    auto brBlueSoft   = makeBrush(isDark ? D2D1::ColorF(0.11f, 0.24f, 0.42f, 0.88f) : D2D1::ColorF(0.86f, 0.92f, 1.0f, 1.0f));
    auto brGreen      = makeBrush(D2D1::ColorF(0.16f, 0.68f, 0.39f, 1.0f));
    auto brGreenSoft  = makeBrush(isDark ? D2D1::ColorF(0.12f, 0.34f, 0.22f, 0.88f) : D2D1::ColorF(0.84f, 0.96f, 0.89f, 1.0f));
    auto brAmber      = makeBrush(D2D1::ColorF(0.86f, 0.58f, 0.12f, 1.0f));
    auto brPurple     = makeBrush(D2D1::ColorF(0.56f, 0.42f, 0.96f, 1.0f));

    IDWriteTextFormat* fTitle = theme.fontCourseTitle ? theme.fontCourseTitle.Get() : (theme.fontTitle ? theme.fontTitle.Get() : theme.fontBody.Get());
    IDWriteTextFormat* fHead  = theme.fontCourseHeading ? theme.fontCourseHeading.Get() : (theme.fontTitle ? theme.fontTitle.Get() : theme.fontBody.Get());
    IDWriteTextFormat* fBody  = theme.fontCourseBody ? theme.fontCourseBody.Get() : theme.fontBody.Get();
    IDWriteTextFormat* fSmall = theme.fontHint ? theme.fontHint.Get() : theme.fontMenuShortcut.Get();
    IDWriteTextFormat* fCode  = theme.fontCode ? theme.fontCode.Get() : fBody;

    ID2D1Brush* textPrimary = theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
    ID2D1Brush* textMuted   = theme.brushTextSecondary ? theme.brushTextSecondary.Get() : textPrimary;
    ID2D1Brush* textWhite   = theme.brushTextWhite ? theme.brushTextWhite.Get() : textPrimary;

    // 1. Blueprint Grid Canvas
    if (brCanvasBg) pRT->FillRectangle(m_viewBounds, brCanvasBg.Get());
    pRT->PushAxisAlignedClip(m_viewBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (brGridLine) {
        const float step = config.ScaleF(24.0f);
        for (float gx = left; gx <= left + width; gx += step) {
            pRT->DrawLine(D2D1::Point2F(gx, top), D2D1::Point2F(gx, top + height), brGridLine.Get(), 1.0f);
        }
        for (float gy = top; gy <= top + height; gy += step) {
            pRT->DrawLine(D2D1::Point2F(left, gy), D2D1::Point2F(left + width, gy), brGridLine.Get(), 1.0f);
        }
    }

    auto drawPillBtn = [&](GuideButtonId id, const D2D1_RECT_F& r, const std::wstring& lbl, bool primary) {
        const bool hov = (m_hoveredAction == id);
        ID2D1Brush* bg = primary ? (hov ? brAccentHov.Get() : brAccentBlue.Get()) : (hov ? brPillHov.Get() : brPillBg.Get());
        if (bg) pRT->FillRoundedRectangle(D2D1::RoundedRect(r, 5.0f, 5.0f), bg);
        if (!primary && brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(r, 5.0f, 5.0f), brBorder.Get(), 0.9f);
        DrawCenteredTxt(pRT, fSmall ? fSmall : fBody, lbl, r, primary ? textWhite : textPrimary);
        m_actionHits.push_back({ id, r });
    };

    // 2. Sticky Top Command Bar
    const float barH = config.ScaleF(46.0f);
    const float padX = config.ScaleF(20.0f);
    D2D1_RECT_F rcTopBar = D2D1::RectF(left, top, left + width, top + barH);
    if (brTopBarBg) pRT->FillRectangle(rcTopBar, brTopBarBg.Get());
    if (brBorder) pRT->DrawLine(D2D1::Point2F(left, rcTopBar.bottom), D2D1::Point2F(left + width, rcTopBar.bottom), brBorder.Get(), 1.0f);

    {
        float bx = left + padX;
        float midY = top + barH * 0.5f;
        const std::wstring barTitle = L"Interactive Guide";
        float tw = MeasureSingleLineW(theme.dwriteFactory.Get(), fHead, barTitle, config.ScaleF(140.0f)) + config.ScaleF(4.0f);
        if (fHead && textPrimary) {
            DWRITE_PARAGRAPH_ALIGNMENT oldP = fHead->GetParagraphAlignment();
            fHead->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(barTitle.c_str(), static_cast<UINT32>(barTitle.size()), fHead,
                          D2D1::RectF(bx, top, bx + tw, top + barH), textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            fHead->SetParagraphAlignment(oldP);
        }
        bx += tw + config.ScaleF(10.0f);

        // Progress badge
        std::wstring progLbl = L"Explored " + std::to_wstring(m_exploredSections.size()) + L" / 8";
        float pw = MeasureSingleLineW(theme.dwriteFactory.Get(), fSmall, progLbl, config.ScaleF(90.0f)) + config.ScaleF(18.0f);
        D2D1_RECT_F rcProg = D2D1::RectF(bx, midY - config.ScaleF(11.0f), bx + pw, midY + config.ScaleF(11.0f));
        if (brGreenSoft) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcProg, 11.0f, 11.0f), brGreenSoft.Get());
        DrawCenteredTxt(pRT, fSmall, progLbl, rcProg, brGreen.Get());
        bx = rcProg.right + config.ScaleF(12.0f);

        // Reset All button on right
        const std::wstring resetAllLbl = L"Reset All Demos";
        float rawW = MeasureSingleLineW(theme.dwriteFactory.Get(), fSmall, resetAllLbl, config.ScaleF(100.0f)) + config.ScaleF(20.0f);
        float rx = left + width - padX - config.ScaleF(12.0f);
        D2D1_RECT_F rcResetAll = D2D1::RectF(rx - rawW, midY - config.ScaleF(13.0f), rx, midY + config.ScaleF(13.0f));
        drawPillBtn(GuideButtonId::ResetAll, rcResetAll, resetAllLbl, false);
        float rightFloor = rcResetAll.left - config.ScaleF(10.0f);

        // Category filter pills
        const wchar_t* catLabels[5] = { L"All (8)", L"Editor & AI", L"Colors & CSS", L"Tasks & Tags", L"Tools" };
        for (int ci = 0; ci < 5; ++ci) {
            float cw = MeasureSingleLineW(theme.dwriteFactory.Get(), fSmall, catLabels[ci], config.ScaleF(64.0f)) + config.ScaleF(18.0f);
            if (bx + cw > rightFloor) break;
            D2D1_RECT_F rcCat = D2D1::RectF(bx, midY - config.ScaleF(12.5f), bx + cw, midY + config.ScaleF(12.5f));
            const bool active = (m_activeCategory == ci);
            const bool hov = (m_hoveredFilter == ci);
            ID2D1Brush* bg = active ? brAccentBlue.Get() : (hov ? brPillHov.Get() : brPillBg.Get());
            if (bg) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcCat, 4.5f, 4.5f), bg);
            if (!active && brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcCat, 4.5f, 4.5f), brBorder.Get(), 0.8f);
            DrawCenteredTxt(pRT, fSmall, catLabels[ci], rcCat, active ? textWhite : textPrimary);
            m_filterHits.push_back({ ci, rcCat });
            bx += cw + config.ScaleF(6.0f);
        }
    }

    // 3. Scrollable Content Area (below Top Bar)
    const float scrollTop = rcTopBar.bottom;
    const float scrollH = (std::max)(40.0f, (top + height) - scrollTop);
    const float sbW = config.ScaleF(10.0f);
    D2D1_RECT_F rcScrollArea = D2D1::RectF(left, scrollTop, left + width - sbW, top + height);
    pRT->PushAxisAlignedClip(rcScrollArea, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const float marginX = config.ScaleF(28.0f);
    const float maxContentW = config.ScaleF(1040.0f);
    const float availW = (std::max)(config.ScaleF(300.0f), (width - sbW) - marginX * 2.0f);
    const float contentW = (std::min)(maxContentW, availW);
    const float contentLeft = left + ((width - sbW) - contentW) * 0.5f;

    float curY = scrollTop + config.ScaleF(22.0f) - m_scrollY;

    // Hero Banner (in All view)
    if (m_activeCategory == 0) {
        const float heroPad = config.ScaleF(18.0f);
        const std::wstring heroTitle = L"Explore PhIDE With Real Embedded Code Editors";
        const std::wstring heroDesc =
            L"Every playground below runs PhIDE's real EditorCanvas engine — click inside any editor to type, accept ghost text, trigger IntelliSense, toggle checkboxes, edit CSS colors, or inspect matched brackets.";
        float descH = MeasureWrappedTextH(theme.dwriteFactory.Get(), fBody, heroDesc, contentW - heroPad * 2.0f, config.ScaleF(36.0f));
        float heroH = config.ScaleF(18.0f + 32.0f + 8.0f) + descH + config.ScaleF(18.0f);

        D2D1_RECT_F rcHero = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + heroH);
        if (brCardBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcHero, 10.0f, 10.0f), brCardBg.Get());
        if (brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcHero, 10.0f, 10.0f), brBorder.Get(), 1.0f);
        if (brAccentBlue) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rcHero.left + 1.0f, rcHero.top + 1.0f, rcHero.right - 1.0f, rcHero.top + config.ScaleF(4.0f)), 4.0f, 4.0f), brAccentBlue.Get());
        }
        if (fTitle && textPrimary) {
            pRT->DrawText(heroTitle.c_str(), static_cast<UINT32>(heroTitle.size()), fTitle,
                          D2D1::RectF(contentLeft + heroPad, curY + config.ScaleF(16.0f), contentLeft + contentW - heroPad, curY + config.ScaleF(50.0f)),
                          textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        if (fBody && textMuted) {
            pRT->DrawText(heroDesc.c_str(), static_cast<UINT32>(heroDesc.size()), fBody,
                          D2D1::RectF(contentLeft + heroPad, curY + config.ScaleF(54.0f), contentLeft + contentW - heroPad, curY + heroH - config.ScaleF(12.0f)),
                          textMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        curY += heroH + config.ScaleF(18.0f);

        // Quick Jump Bento Overview Grid (4 cols if wide, 2 cols otherwise)
        struct BentoInfo {
            int secIdx;
            int cat;
            const wchar_t* tag;
            const wchar_t* title;
            const wchar_t* desc;
        };
        static const BentoInfo kBentos[8] = {
            { 0, 1, L"01 • AI",       L"Predictive Ghost Text",  L"Inline completion with Tab accept" },
            { 1, 1, L"02 • LSP",      L"Smart IntelliSense",     L"Live JS/TS/PHP completions popup" },
            { 2, 3, L"03 • TASKS",    L"Task Checkboxes",        L"Clickable [ ] checkboxes & progress" },
            { 3, 2, L"04 • CSS",      L"Color Swatches & Picker",L"Inline hex/hsl/rgba swatches & picker" },
            { 4, 3, L"05 • SYNTAX",   L"Brackets & Comments",    L"Matched frames & Better Comments" },
            { 5, 1, L"06 • EDIT",     L"Multi-Cursor Editing",   L"Edit 4+ lines at once (Alt+Click)" },
            { 6, 4, L"07 • REST",     L"API Tester Simulator",   L"Test methods, endpoints & JSON body" },
            { 7, 4, L"08 • WORKBENCH",L"Tools & Shortcuts",      L"ERD, Task Runner, SQL & Keybindings" }
        };
        const int cols = (contentW >= config.ScaleF(780.0f)) ? 4 : 2;
        const float gap = config.ScaleF(10.0f);
        const float bw = (contentW - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        const float bh = config.ScaleF(74.0f);

        for (int bi = 0; bi < 8; ++bi) {
            int r = bi / cols;
            int c = bi % cols;
            float x = contentLeft + static_cast<float>(c) * (bw + gap);
            float y = curY + static_cast<float>(r) * (bh + gap);
            D2D1_RECT_F rb = D2D1::RectF(x, y, x + bw, y + bh);
            const bool hov = (m_hoveredBento == bi);
            const bool explored = (m_exploredSections.count(kBentos[bi].secIdx) > 0);
            if (hov ? brCardHover : brCardBg) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rb, 7.0f, 7.0f), (hov ? brCardHover : brCardBg).Get());
            }
            ID2D1Brush* bdr = hov ? brAccentBlue.Get() : (explored ? brGreen.Get() : brBorder.Get());
            if (bdr) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rb, 7.0f, 7.0f), bdr, hov ? 1.3f : 0.9f);

            if (fSmall) {
                pRT->DrawText(kBentos[bi].tag, static_cast<UINT32>(wcslen(kBentos[bi].tag)), fSmall,
                              D2D1::RectF(x + config.ScaleF(10.0f), y + config.ScaleF(8.0f), x + bw - config.ScaleF(10.0f), y + config.ScaleF(24.0f)),
                              explored ? brGreen.Get() : brAccentHov.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            if (fBody && textPrimary) {
                pRT->DrawText(kBentos[bi].title, static_cast<UINT32>(wcslen(kBentos[bi].title)), fBody,
                              D2D1::RectF(x + config.ScaleF(10.0f), y + config.ScaleF(25.0f), x + bw - config.ScaleF(10.0f), y + config.ScaleF(45.0f)),
                              textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            if (fSmall && textMuted) {
                pRT->DrawText(kBentos[bi].desc, static_cast<UINT32>(wcslen(kBentos[bi].desc)), fSmall,
                              D2D1::RectF(x + config.ScaleF(10.0f), y + config.ScaleF(46.0f), x + bw - config.ScaleF(10.0f), y + bh - config.ScaleF(6.0f)),
                              textMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            m_bentoHits.push_back({ kBentos[bi].secIdx, 0, 0.0f, rb });
        }
        const int rows = (8 + cols - 1) / cols;
        curY += static_cast<float>(rows) * bh + static_cast<float>(rows - 1) * gap + config.ScaleF(26.0f);
    }

    auto shouldShowCat = [&](int cat) -> bool {
        return m_activeCategory == 0 || m_activeCategory == cat;
    };

    // Helper: Render a Section Card containing a REAL embedded EditorCanvas
    auto renderEditorSection = [&](
        int secIdx,
        int edIdx,
        const wchar_t* badgeTxt,
        ID2D1Brush* badgeAccent,
        const std::wstring& title,
        const std::wstring& desc,
        const std::wstring& tip,
        const std::vector<std::pair<GuideButtonId, std::wstring>>& buttons,
        bool showTaskProgressBar) {

        m_sectionAnchorY[secIdx] = (curY + m_scrollY) - scrollTop;

        auto& ed = m_editors[static_cast<size_t>(edIdx)];
        const float cardPad = config.ScaleF(18.0f);
        const float innerW = contentW - cardPad * 2.0f;

        float descH = MeasureWrappedTextH(theme.dwriteFactory.Get(), fBody, desc, innerW, config.ScaleF(34.0f));
        float tipH  = tip.empty() ? 0.0f : MeasureWrappedTextH(theme.dwriteFactory.Get(), fSmall, tip, innerW - config.ScaleF(20.0f), config.ScaleF(22.0f));

        // Progress bar height if Task section
        float progH = showTaskProgressBar ? config.ScaleF(30.0f) : 0.0f;

        // Calculate exact real EditorCanvas block height from line count & config
        const float lineH = config.ScaleF(std::round(config.fontSize * config.lineHeightMultiplier));
        const size_t lineCnt = (std::max<size_t>)(4, ed.doc.buffer.GetLineCount());
        const float editorHdrH = config.ScaleF(30.0f);
        const float editorBodyH = static_cast<float>(lineCnt) * lineH + config.ScaleF(16.0f);
        const float tipBoxH = tip.empty() ? 0.0f : (tipH + config.ScaleF(14.0f));

        const float totalCardH = cardPad + config.ScaleF(28.0f) + config.ScaleF(6.0f) + descH +
                                 config.ScaleF(12.0f) + progH + editorHdrH + editorBodyH +
                                 (tipBoxH > 0.0f ? (config.ScaleF(10.0f) + tipBoxH) : 0.0f) + cardPad;

        D2D1_RECT_F rcSec = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + totalCardH);
        ed.cardBounds = rcSec;

        if (brCardBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSec, 10.0f, 10.0f), brCardBg.Get());
        ID2D1Brush* cardBorder = ed.isFocused ? brAccentBlue.Get() : brBorder.Get();
        if (cardBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcSec, 10.0f, 10.0f), cardBorder, ed.isFocused ? 1.4f : 1.0f);

        float yCursor = curY + cardPad;

        // Header Row: Badge + Title + Interactive Action Buttons
        {
            float hx = contentLeft + cardPad;
            float bW = MeasureSingleLineW(theme.dwriteFactory.Get(), fSmall, badgeTxt, config.ScaleF(40.0f)) + config.ScaleF(14.0f);
            D2D1_RECT_F rcBadge = D2D1::RectF(hx, yCursor + config.ScaleF(2.0f), hx + bW, yCursor + config.ScaleF(24.0f));
            if (badgeAccent) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcBadge, 4.0f, 4.0f), badgeAccent);
            DrawCenteredTxt(pRT, fSmall, badgeTxt, rcBadge, textWhite);
            hx = rcBadge.right + config.ScaleF(10.0f);

            float bx = contentLeft + contentW - cardPad;
            for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
                float btnW = MeasureSingleLineW(theme.dwriteFactory.Get(), fSmall, it->second, config.ScaleF(70.0f)) + config.ScaleF(20.0f);
                if (bx - btnW > hx + config.ScaleF(80.0f)) {
                    D2D1_RECT_F rBtn = D2D1::RectF(bx - btnW, yCursor + config.ScaleF(1.0f), bx, yCursor + config.ScaleF(26.0f));
                    drawPillBtn(it->first, rBtn, it->second, (it == buttons.rbegin() + (buttons.size() > 1 ? 1 : 0)) && buttons.size() > 1 ? false : (it->first != GuideButtonId::ResetGhost && it->first != GuideButtonId::ResetIntelliSense && it->first != GuideButtonId::ResetTasks && it->first != GuideButtonId::ResetColors && it->first != GuideButtonId::ResetBrackets && it->first != GuideButtonId::ResetMultiCursor));
                    bx = rBtn.left - config.ScaleF(8.0f);
                }
            }
            if (fHead && textPrimary && bx > hx) {
                pRT->DrawText(title.c_str(), static_cast<UINT32>(title.size()), fHead,
                              D2D1::RectF(hx, yCursor, bx - config.ScaleF(8.0f), yCursor + config.ScaleF(28.0f)),
                              textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
        yCursor += config.ScaleF(34.0f);

        // Wrapped Description
        if (fBody && textMuted) {
            pRT->DrawText(desc.c_str(), static_cast<UINT32>(desc.size()), fBody,
                          D2D1::RectF(contentLeft + cardPad, yCursor, contentLeft + contentW - cardPad, yCursor + descH + config.ScaleF(2.0f)),
                          textMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        yCursor += descH + config.ScaleF(12.0f);

        // Optional Live Task Progress Bar (Section 3)
        if (showTaskProgressBar) {
            int done = 0, total = 0;
            for (size_t l = 0; l < ed.doc.buffer.GetLineCount(); ++l) {
                const auto& ln = ed.doc.buffer.GetLine(l);
                if (ln.find(L"[x]") != std::wstring::npos || ln.find(L"[X]") != std::wstring::npos) {
                    done++; total++;
                } else if (ln.find(L"[ ]") != std::wstring::npos || ln.find(L"[]") != std::wstring::npos) {
                    total++;
                }
            }
            const float ratio = (total > 0) ? (static_cast<float>(done) / static_cast<float>(total)) : 0.0f;
            std::wstring pLbl = L"Sprint Progress: " + std::to_wstring(done) + L" / " + std::to_wstring(total) +
                                L" completed (" + std::to_wstring(static_cast<int>(std::round(ratio * 100.0f))) + L"%)";
            if (fSmall && textPrimary) {
                pRT->DrawText(pLbl.c_str(), static_cast<UINT32>(pLbl.size()), fSmall,
                              D2D1::RectF(contentLeft + cardPad, yCursor, contentLeft + cardPad + innerW, yCursor + config.ScaleF(16.0f)),
                              done == total && total > 0 ? brGreen.Get() : textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            D2D1_RECT_F rcTrack = D2D1::RectF(contentLeft + cardPad, yCursor + config.ScaleF(18.0f),
                                              contentLeft + contentW - cardPad, yCursor + config.ScaleF(24.0f));
            if (brPillBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcTrack, 3.0f, 3.0f), brPillBg.Get());
            if (ratio > 0.0f && brGreen) {
                D2D1_RECT_F rcFill = D2D1::RectF(rcTrack.left, rcTrack.top,
                                                 rcTrack.left + (rcTrack.right - rcTrack.left) * ratio, rcTrack.bottom);
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcFill, 3.0f, 3.0f), brGreen.Get());
            }
            yCursor += progH;
        }

        // Embedded REAL Editor Window Frame (Header tab bar + EditorCanvas::RenderEmbeddedCodeBlock)
        D2D1_RECT_F rcEdFrame = D2D1::RectF(contentLeft + cardPad, yCursor, contentLeft + contentW - cardPad, yCursor + editorHdrH + editorBodyH);
        D2D1_RECT_F rcEdHdr = D2D1::RectF(rcEdFrame.left, rcEdFrame.top, rcEdFrame.right, rcEdFrame.top + editorHdrH);
        ed.codeBoxBounds = D2D1::RectF(rcEdFrame.left + 1.0f, rcEdHdr.bottom, rcEdFrame.right - 1.0f, rcEdFrame.bottom - 1.0f);

        if (brEditorHdr) pRT->FillRectangle(rcEdHdr, brEditorHdr.Get());
        if (brBorder) pRT->DrawLine(D2D1::Point2F(rcEdHdr.left, rcEdHdr.bottom), D2D1::Point2F(rcEdHdr.right, rcEdHdr.bottom), brBorder.Get(), 1.0f);
        {
            std::wstring fileChip = ed.doc.fileName + L"  •  " + (ed.isFocused ? L"Focused (Real EditorCanvas)" : L"Click to Edit (Real EditorCanvas)");
            if (fSmall) {
                DWRITE_PARAGRAPH_ALIGNMENT oldP = fSmall->GetParagraphAlignment();
                fSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                pRT->DrawText(fileChip.c_str(), static_cast<UINT32>(fileChip.size()), fSmall,
                              D2D1::RectF(rcEdHdr.left + config.ScaleF(12.0f), rcEdHdr.top, rcEdHdr.right - config.ScaleF(12.0f), rcEdHdr.bottom),
                              ed.isFocused ? brAccentHov.Get() : textMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                fSmall->SetParagraphAlignment(oldP);
            }
        }

        if (m_pHostEditor) {
            m_pHostEditor->RenderEmbeddedCodeBlock(
                pRT, theme, config, ed.doc, ed.codeBoxBounds, ed.isFocused,
                ed.ghostText, ed.ghostLine, ed.ghostCol,
                &ed.lastCaretX, &ed.lastCaretY, &ed.lastCharW, &ed.lastLineH, &ed.lastGutterW);
        }
        if (brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcEdFrame, 6.0f, 6.0f), ed.isFocused ? brAccentBlue.Get() : brBorder.Get(), 1.0f);

        yCursor += editorHdrH + editorBodyH;

        // Tip Callout Strip
        if (tipBoxH > 0.0f) {
            yCursor += config.ScaleF(10.0f);
            D2D1_RECT_F rcTip = D2D1::RectF(contentLeft + cardPad, yCursor, contentLeft + contentW - cardPad, yCursor + tipBoxH);
            if (brBlueSoft) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcTip, 5.0f, 5.0f), brBlueSoft.Get());
            if (fSmall && textPrimary) {
                DWRITE_PARAGRAPH_ALIGNMENT oldP = fSmall->GetParagraphAlignment();
                fSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                pRT->DrawText(tip.c_str(), static_cast<UINT32>(tip.size()), fSmall,
                              D2D1::RectF(rcTip.left + config.ScaleF(10.0f), rcTip.top, rcTip.right - config.ScaleF(10.0f), rcTip.bottom),
                              textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                fSmall->SetParagraphAlignment(oldP);
            }
        }

        curY += totalCardH + config.ScaleF(20.0f);
    };

    // Sections 1..6 (using REAL EditorCanvas!)
    if (shouldShowCat(1)) {
        renderEditorSection(
            0, 0, L"01 • AI GHOST TEXT", brAccentBlue.Get(),
            L"Predictive Ghost Text & Adaptive Autocomplete",
            L"PhIDE renders context-aware inline ghost suggestions right ahead of your cursor inside the real editor. Press Tab to accept or click 'Accept Ghost Text'.",
            L"Tip: Click inside UserController.php and press Tab (or click Accept) to complete the PDO query statement.",
            { { GuideButtonId::AcceptGhost, L"Accept (Tab)" }, { GuideButtonId::ResetGhost, L"Reset" } },
            false);

        renderEditorSection(
            1, 1, L"02 • INTELLISENSE", brPurple.Get(),
            L"Smart IntelliSense & Live Completions",
            L"Real symbol & DOM/Web API proposals powered by IntelliSenseEngine. Use Up/Down arrows, Enter, or click any suggestion below—or press Ctrl+Space anytime.",
            L"Tip: Keep typing after `context.` (e.g. `fill`) to watch IntelliSense filter matching methods live.",
            { { GuideButtonId::TriggerIntelliSense, L"Ctrl+Space" }, { GuideButtonId::ResetIntelliSense, L"Reset" } },
            false);
    }

    if (shouldShowCat(3)) {
        renderEditorSection(
            2, 2, L"03 • TASKS", brGreen.Get(),
            L"Interactive Task Checkboxes & Sprint Progress",
            L"Comments containing `// [ ] TASK` or `// [x] TASK` render real clickable checkboxes inside the editor. Toggle any box directly in code to track progress.",
            L"Tip: Click the checkboxes in `sprint-tasks.ts` (or Complete All / Clear All) to watch the progress bar update.",
            { { GuideButtonId::CompleteAllTasks, L"Complete All" }, { GuideButtonId::ClearAllTasks, L"Clear All" }, { GuideButtonId::ResetTasks, L"Reset" } },
            true);
    }

    if (shouldShowCat(2)) {
        renderEditorSection(
            3, 3, L"04 • COLORS", brAmber.Get(),
            L"Inline CSS Color Swatches & Visual Picker",
            L"Hex, RGB/RGBA, HSL, and Tailwind utility tokens show interactive inline swatches directly inside the editor. Click any square swatch to launch the visual Color Picker.",
            L"Tip: Click any colored square in `theme-tokens.css` to open the HSV & Hex Color Picker and edit live.",
            { { GuideButtonId::AddAccentColor, L"+ Add Token" }, { GuideButtonId::ResetColors, L"Reset" } },
            false);
    }

    if (shouldShowCat(3)) {
        renderEditorSection(
            4, 4, L"05 • BRACKETS & COMMENTS", brPurple.Get(),
            L"Rainbow Brackets, Bracket Matching & Better Comments",
            L"PhIDE highlights active bracket pairs with crisp aligned DirectWrite frames (`{ }`, `[ ]`, `( )`) and classifies comments (`/*!` License as Note, `// TODO:`, `// ^` Warning).",
            L"Tip: Notice `/*! ... */` at line 1 is classified as a calm Note (never Critical!). Click any bracket to see its matching frame.",
            { { GuideButtonId::JumpMatchedBracket, L"Match Bracket" }, { GuideButtonId::ResetBrackets, L"Reset" } },
            false);
    }

    if (shouldShowCat(1)) {
        renderEditorSection(
            5, 5, L"06 • MULTI-CURSOR", brAccentBlue.Get(),
            L"Multi-Cursor Editing & Batch Occurrences",
            L"Hold Alt+Click on multiple lines or use Ctrl+D to spawn synchronized cursors across matches. Click 'Select All \"baseUrl\"' and type `apiRoot` to edit every endpoint simultaneously.",
            L"Tip: Press Esc to return to a single cursor after testing batch edits.",
            { { GuideButtonId::SelectAllOccurrences, L"Select \"baseUrl\"" }, { GuideButtonId::AddLineCursors, L"4-Line Cursors" }, { GuideButtonId::ResetMultiCursor, L"Reset" } },
            false);
    }

    // Section 7: Built-in API Tester Simulator (with REAL JSON EditorCanvas response!)
    if (shouldShowCat(4)) {
        m_sectionAnchorY[6] = (curY + m_scrollY) - scrollTop;
        const float cardPad = config.ScaleF(18.0f);
        const float innerW = contentW - cardPad * 2.0f;
        const std::wstring s7Desc =
            L"Test REST endpoints, inspect payloads, and stream SSE responses inside PhIDE without leaving your editor. Try switching HTTP methods, picking a preset endpoint, and clicking Send Request below.";
        float descH = MeasureWrappedTextH(theme.dwriteFactory.Get(), fBody, s7Desc, innerW, config.ScaleF(36.0f));
        float barCtrlH = config.ScaleF(34.0f);
        float presetRowH = config.ScaleF(30.0f);
        float respHdrH = config.ScaleF(30.0f);
        auto& edJson = m_editors[6];
        const float lineH = config.ScaleF(std::round(config.fontSize * config.lineHeightMultiplier));
        const float respBodyH = static_cast<float>((std::max<size_t>)(5, edJson.doc.buffer.GetLineCount())) * lineH + config.ScaleF(14.0f);
        const float totalS7H = cardPad + config.ScaleF(34.0f) + descH + config.ScaleF(12.0f) +
                               barCtrlH + config.ScaleF(8.0f) + presetRowH + config.ScaleF(12.0f) +
                               respHdrH + respBodyH + cardPad;

        D2D1_RECT_F rcS7 = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + totalS7H);
        edJson.cardBounds = rcS7;
        if (brCardBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcS7, 10.0f, 10.0f), brCardBg.Get());
        if (brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcS7, 10.0f, 10.0f), brBorder.Get(), 1.0f);

        float yc = curY + cardPad;
        {
            float hx = contentLeft + cardPad;
            D2D1_RECT_F rB = D2D1::RectF(hx, yc + config.ScaleF(2.0f), hx + config.ScaleF(94.0f), yc + config.ScaleF(24.0f));
            if (brGreen) pRT->FillRoundedRectangle(D2D1::RoundedRect(rB, 4.0f, 4.0f), brGreen.Get());
            DrawCenteredTxt(pRT, fSmall, L"07 • REST API", rB, textWhite);
            hx = rB.right + config.ScaleF(10.0f);

            const std::wstring openLbl = L"Open Full API Tester ->";
            float ow = MeasureSingleLineW(theme.dwriteFactory.Get(), fSmall, openLbl, config.ScaleF(140.0f)) + config.ScaleF(22.0f);
            D2D1_RECT_F rOpen = D2D1::RectF(contentLeft + contentW - cardPad - ow, yc + config.ScaleF(1.0f), contentLeft + contentW - cardPad, yc + config.ScaleF(26.0f));
            drawPillBtn(GuideButtonId::OpenFullApiTester, rOpen, openLbl, true);
            if (fHead && textPrimary && rOpen.left > hx) {
                pRT->DrawText(L"Interactive Built-in API Tester", 31, fHead,
                              D2D1::RectF(hx, yc, rOpen.left - config.ScaleF(8.0f), yc + config.ScaleF(28.0f)),
                              textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
        yc += config.ScaleF(34.0f);

        if (fBody && textMuted) {
            pRT->DrawText(s7Desc.c_str(), static_cast<UINT32>(s7Desc.size()), fBody,
                          D2D1::RectF(contentLeft + cardPad, yc, contentLeft + contentW - cardPad, yc + descH + config.ScaleF(2.0f)),
                          textMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        yc += descH + config.ScaleF(12.0f);

        // Request Control Bar (Method pills + URL box + Send Request button)
        {
            D2D1_RECT_F rcReq = D2D1::RectF(contentLeft + cardPad, yc, contentLeft + contentW - cardPad, yc + barCtrlH);
            if (brEditorHdr) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcReq, 6.0f, 6.0f), brEditorHdr.Get());
            if (brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcReq, 6.0f, 6.0f), brBorder.Get(), 0.9f);

            float mx = rcReq.left + config.ScaleF(6.0f);
            const GuideButtonId mIds[3] = { GuideButtonId::ApiMethodGet, GuideButtonId::ApiMethodPost, GuideButtonId::ApiMethodPut };
            const wchar_t* mNames[3] = { L"GET", L"POST", L"PUT" };
            for (int mi = 0; mi < 3; ++mi) {
                D2D1_RECT_F rm = D2D1::RectF(mx, rcReq.top + config.ScaleF(5.0f), mx + config.ScaleF(46.0f), rcReq.bottom - config.ScaleF(5.0f));
                drawPillBtn(mIds[mi], rm, mNames[mi], m_apiMethodIdx == mi);
                mx = rm.right + config.ScaleF(5.0f);
            }

            float sendW = config.ScaleF(108.0f);
            D2D1_RECT_F rSend = D2D1::RectF(rcReq.right - sendW - config.ScaleF(6.0f), rcReq.top + config.ScaleF(4.0f),
                                            rcReq.right - config.ScaleF(6.0f), rcReq.bottom - config.ScaleF(4.0f));
            drawPillBtn(GuideButtonId::ApiSendRequest, rSend, L"Send Request", true);

            const wchar_t* presetUrls[3] = {
                L"https://api.phide.local/v1/chat/completions",
                L"https://api.phide.local/v1/users/profile",
                L"https://api.phide.local/api/health"
            };
            D2D1_RECT_F rcUrl = D2D1::RectF(mx + config.ScaleF(8.0f), rcReq.top, rSend.left - config.ScaleF(8.0f), rcReq.bottom);
            if (fCode && textPrimary && rcUrl.right > rcUrl.left) {
                DWRITE_PARAGRAPH_ALIGNMENT oldP = fCode->GetParagraphAlignment();
                fCode->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                pRT->DrawText(presetUrls[m_apiPresetIdx], static_cast<UINT32>(wcslen(presetUrls[m_apiPresetIdx])), fCode,
                              rcUrl, textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                fCode->SetParagraphAlignment(oldP);
            }
        }
        yc += barCtrlH + config.ScaleF(8.0f);

        // Endpoint Preset Pills + Live Status Pill Row
        {
            float px = contentLeft + cardPad;
            const GuideButtonId pIds[3] = { GuideButtonId::ApiPresetChat, GuideButtonId::ApiPresetUsers, GuideButtonId::ApiPresetHealth };
            const wchar_t* pNames[3] = { L"/v1/chat/completions", L"/v1/users/profile", L"/api/health" };
            for (int pi = 0; pi < 3; ++pi) {
                float pw = MeasureSingleLineW(theme.dwriteFactory.Get(), fSmall, pNames[pi], config.ScaleF(100.0f)) + config.ScaleF(18.0f);
                D2D1_RECT_F rp = D2D1::RectF(px, yc + config.ScaleF(2.0f), px + pw, yc + config.ScaleF(26.0f));
                drawPillBtn(pIds[pi], rp, pNames[pi], m_apiPresetIdx == pi);
                px = rp.right + config.ScaleF(6.0f);
            }
            std::wstring statusTxt = std::to_wstring(m_apiStatusCode) + (m_apiStatusCode == 200 ? L" OK" : L" No Content") +
                                     L"  •  " + std::to_wstring(m_apiLatencyMs) + L" ms  •  " +
                                     std::to_wstring(m_apiPayloadBytes) + L" B";
            float sw = MeasureSingleLineW(theme.dwriteFactory.Get(), fSmall, statusTxt, config.ScaleF(150.0f)) + config.ScaleF(16.0f);
            if (contentLeft + contentW - cardPad - sw > px + config.ScaleF(8.0f)) {
                D2D1_RECT_F rs = D2D1::RectF(contentLeft + contentW - cardPad - sw, yc + config.ScaleF(2.0f),
                                             contentLeft + contentW - cardPad, yc + config.ScaleF(26.0f));
                if (brGreenSoft) pRT->FillRoundedRectangle(D2D1::RoundedRect(rs, 4.0f, 4.0f), brGreenSoft.Get());
                DrawCenteredTxt(pRT, fSmall, statusTxt, rs, brGreen.Get());
            }
        }
        yc += presetRowH + config.ScaleF(12.0f);

        // Live JSON Response rendered via REAL EditorCanvas!
        D2D1_RECT_F rcJsonFrame = D2D1::RectF(contentLeft + cardPad, yc, contentLeft + contentW - cardPad, yc + respHdrH + respBodyH);
        D2D1_RECT_F rcJsonHdr = D2D1::RectF(rcJsonFrame.left, rcJsonFrame.top, rcJsonFrame.right, rcJsonFrame.top + respHdrH);
        edJson.codeBoxBounds = D2D1::RectF(rcJsonFrame.left + 1.0f, rcJsonHdr.bottom, rcJsonFrame.right - 1.0f, rcJsonFrame.bottom - 1.0f);
        if (brEditorHdr) pRT->FillRectangle(rcJsonHdr, brEditorHdr.Get());
        if (brBorder) pRT->DrawLine(D2D1::Point2F(rcJsonHdr.left, rcJsonHdr.bottom), D2D1::Point2F(rcJsonHdr.right, rcJsonHdr.bottom), brBorder.Get(), 1.0f);
        if (fSmall) {
            DWRITE_PARAGRAPH_ALIGNMENT oldP = fSmall->GetParagraphAlignment();
            fSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(L"response.json  •  Live Response Preview (Real EditorCanvas)", 58, fSmall,
                          D2D1::RectF(rcJsonHdr.left + config.ScaleF(12.0f), rcJsonHdr.top, rcJsonHdr.right - config.ScaleF(12.0f), rcJsonHdr.bottom),
                          edJson.isFocused ? brAccentHov.Get() : textMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            fSmall->SetParagraphAlignment(oldP);
        }
        if (m_pHostEditor) {
            m_pHostEditor->RenderEmbeddedCodeBlock(pRT, theme, config, edJson.doc, edJson.codeBoxBounds, edJson.isFocused);
        }
        if (brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcJsonFrame, 6.0f, 6.0f), edJson.isFocused ? brAccentBlue.Get() : brBorder.Get(), 1.0f);

        curY += totalS7H + config.ScaleF(20.0f);

        // Section 8: Workspace Tools & Shortcuts Launchpad
        m_sectionAnchorY[7] = (curY + m_scrollY) - scrollTop;
        const float launchBtnsH = config.ScaleF(36.0f);
        const float kGridRows = 4.0f;
        const float kRowH = config.ScaleF(30.0f);
        const float totalS8H = cardPad + config.ScaleF(34.0f) + config.ScaleF(28.0f) + launchBtnsH +
                               config.ScaleF(16.0f) + config.ScaleF(24.0f) + kGridRows * kRowH + cardPad;
        D2D1_RECT_F rcS8 = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + totalS8H);
        if (brCardBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcS8, 10.0f, 10.0f), brCardBg.Get());
        if (brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcS8, 10.0f, 10.0f), brBorder.Get(), 1.0f);

        float y8 = curY + cardPad;
        {
            D2D1_RECT_F rB = D2D1::RectF(contentLeft + cardPad, y8 + config.ScaleF(2.0f), contentLeft + cardPad + config.ScaleF(118.0f), y8 + config.ScaleF(24.0f));
            if (brAccentBlue) pRT->FillRoundedRectangle(D2D1::RoundedRect(rB, 4.0f, 4.0f), brAccentBlue.Get());
            DrawCenteredTxt(pRT, fSmall, L"08 • WORKBENCH", rB, textWhite);
            if (fHead && textPrimary) {
                pRT->DrawText(L"Workspace Launchpad & Essential Shortcuts", 41, fHead,
                              D2D1::RectF(rB.right + config.ScaleF(10.0f), y8, contentLeft + contentW - cardPad, y8 + config.ScaleF(28.0f)),
                              textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
        y8 += config.ScaleF(34.0f);
        if (fBody && textMuted) {
            pRT->DrawText(L"Jump directly into PhIDE's integrated workbench views or use keyboard shortcuts anywhere in the IDE.", 97,
                          fBody, D2D1::RectF(contentLeft + cardPad, y8, contentLeft + contentW - cardPad, y8 + config.ScaleF(22.0f)),
                          textMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        y8 += config.ScaleF(28.0f);

        // Launcher buttons row
        {
            const std::pair<GuideButtonId, const wchar_t*> launchBtns[5] = {
                { GuideButtonId::OpenTaskRunner, L"Open Task Runner" },
                { GuideButtonId::OpenErd,        L"Open ER Diagram" },
                { GuideButtonId::OpenSqlConsole, L"Open SQL Console" },
                { GuideButtonId::OpenPackages,   L"Package Manager" },
                { GuideButtonId::ToggleZenMode,  L"Toggle Zen Mode (Ctrl+K Z)" }
            };
            float lx = contentLeft + cardPad;
            for (const auto& lb : launchBtns) {
                float bw = MeasureSingleLineW(theme.dwriteFactory.Get(), fSmall, lb.second, config.ScaleF(100.0f)) + config.ScaleF(22.0f);
                if (lx + bw > contentLeft + contentW - cardPad) break;
                D2D1_RECT_F rl = D2D1::RectF(lx, y8, lx + bw, y8 + config.ScaleF(30.0f));
                drawPillBtn(lb.first, rl, lb.second, lb.first == GuideButtonId::OpenTaskRunner || lb.first == GuideButtonId::OpenErd);
                lx = rl.right + config.ScaleF(8.0f);
            }
        }
        y8 += launchBtnsH + config.ScaleF(16.0f);

        if (fBody && textPrimary) {
            pRT->DrawText(L"Essential Keyboard Shortcuts", 28, fBody,
                          D2D1::RectF(contentLeft + cardPad, y8, contentLeft + contentW - cardPad, y8 + config.ScaleF(20.0f)),
                          textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        y8 += config.ScaleF(24.0f);

        struct ShortcutEntry { const wchar_t* keys; const wchar_t* action; };
        static const ShortcutEntry kShortcuts[8] = {
            { L"Ctrl+P",       L"Quick Open File" },
            { L"Ctrl+Shift+P", L"Command Palette" },
            { L"Ctrl+Space",   L"Trigger IntelliSense" },
            { L"Alt+Click",    L"Add Multi-Cursor" },
            { L"Ctrl+D",       L"Select Next Occurrence" },
            { L"Ctrl+/",       L"Toggle Line Comment" },
            { L"Ctrl+K Z",     L"Toggle Zen Mode" },
            { L"Tab",          L"Accept Ghost Text" }
        };
        const float skColW = (innerW - config.ScaleF(14.0f)) * 0.5f;
        for (int si = 0; si < 8; ++si) {
            int row = si / 2;
            int col = si % 2;
            float sx = contentLeft + cardPad + static_cast<float>(col) * (skColW + config.ScaleF(14.0f));
            float sy = y8 + static_cast<float>(row) * kRowH;
            float kw = config.ScaleF(108.0f);
            D2D1_RECT_F rk = D2D1::RectF(sx, sy + config.ScaleF(3.0f), sx + kw, sy + kRowH - config.ScaleF(4.0f));
            if (brPillBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(rk, 4.0f, 4.0f), brPillBg.Get());
            if (brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rk, 4.0f, 4.0f), brBorder.Get(), 0.8f);
            DrawCenteredTxt(pRT, fCode ? fCode : fSmall, kShortcuts[si].keys, rk, brAccentHov.Get());
            if (fSmall && textMuted) {
                DWRITE_PARAGRAPH_ALIGNMENT oldP = fSmall->GetParagraphAlignment();
                fSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                pRT->DrawText(kShortcuts[si].action, static_cast<UINT32>(wcslen(kShortcuts[si].action)), fSmall,
                              D2D1::RectF(rk.right + config.ScaleF(10.0f), sy, sx + skColW, sy + kRowH),
                              textMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                fSmall->SetParagraphAlignment(oldP);
            }
        }
        curY += totalS8H + config.ScaleF(20.0f);
    }

    // Populate Bento jump offsets using actual measured section anchors!
    for (auto& bj : m_bentoHits) {
        if (bj.sectionIdx >= 0 && bj.sectionIdx < 8) {
            bj.scrollOffsetY = m_sectionAnchorY[bj.sectionIdx];
        }
    }

    // 4. IntelliSense Floating Popup (rendered cleanly inside ScrollArea above other layers)
    if (m_editors.size() > 1 && m_editors[1].showIntelliSense && !m_editors[1].intelliSenseItems.empty() &&
        m_editors[1].codeBoxBounds.right > m_editors[1].codeBoxBounds.left) {
        auto& ed1 = m_editors[1];
        const float itemH = config.ScaleF(24.0f);
        const size_t maxVis = 6;
        const size_t visCount = (std::min)(maxVis, ed1.intelliSenseItems.size());
        const float popW = config.ScaleF(340.0f);
        const float popH = static_cast<float>(visCount) * itemH + config.ScaleF(8.0f);
        float px = (ed1.lastCaretX > ed1.codeBoxBounds.left)
            ? ed1.lastCaretX
            : (ed1.codeBoxBounds.left + ed1.lastGutterW + config.ScaleF(80.0f));
        if (px + popW > rcScrollArea.right - config.ScaleF(12.0f)) {
            px = (std::max)(rcScrollArea.left + config.ScaleF(12.0f), rcScrollArea.right - popW - config.ScaleF(12.0f));
        }
        float py = (ed1.lastCaretY > 0.0f)
            ? (ed1.lastCaretY + ed1.lastLineH + config.ScaleF(2.0f))
            : (ed1.codeBoxBounds.bottom - config.ScaleF(4.0f));
        if (py + popH > rcScrollArea.bottom - config.ScaleF(8.0f)) {
            py = (std::max)(rcScrollArea.top + config.ScaleF(8.0f), ed1.lastCaretY - popH - config.ScaleF(2.0f));
        }
        ed1.intelliSensePopupBounds = D2D1::RectF(px, py, px + popW, py + popH);
        if (brTopBarBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(ed1.intelliSensePopupBounds, 6.0f, 6.0f), brTopBarBg.Get());
        if (brAccentBlue) pRT->DrawRoundedRectangle(D2D1::RoundedRect(ed1.intelliSensePopupBounds, 6.0f, 6.0f), brAccentBlue.Get(), 1.1f);

        for (size_t i = 0; i < visCount; ++i) {
            size_t itemIdx = ed1.intelliSenseScrollOffset + i;
            if (itemIdx >= ed1.intelliSenseItems.size()) break;
            const auto& item = ed1.intelliSenseItems[itemIdx];
            float iy = py + config.ScaleF(4.0f) + static_cast<float>(i) * itemH;
            D2D1_RECT_F rItem = D2D1::RectF(px + config.ScaleF(4.0f), iy, px + popW - config.ScaleF(4.0f), iy + itemH);
            const bool sel = (itemIdx == ed1.intelliSenseSelectedIdx);
            if (sel && brBlueSoft) pRT->FillRoundedRectangle(D2D1::RoundedRect(rItem, 4.0f, 4.0f), brBlueSoft.Get());
            if (fCode && textPrimary) {
                DWRITE_PARAGRAPH_ALIGNMENT oldP = fCode->GetParagraphAlignment();
                fCode->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                pRT->DrawText(item.label.c_str(), static_cast<UINT32>(item.label.size()), fCode,
                              D2D1::RectF(rItem.left + config.ScaleF(8.0f), rItem.top, rItem.left + config.ScaleF(185.0f), rItem.bottom),
                              textPrimary, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                fCode->SetParagraphAlignment(oldP);
            }
            if (!item.detail.empty() && fSmall && textMuted) {
                DWRITE_PARAGRAPH_ALIGNMENT oldP = fSmall->GetParagraphAlignment();
                fSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                pRT->DrawText(item.detail.c_str(), static_cast<UINT32>(item.detail.size()), fSmall,
                              D2D1::RectF(rItem.left + config.ScaleF(190.0f), rItem.top, rItem.right - config.ScaleF(6.0f), rItem.bottom),
                              textMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                fSmall->SetParagraphAlignment(oldP);
            }
        }
    }

    m_contentTotalHeight = (curY + m_scrollY) - scrollTop;
    m_maxScrollY = (std::max)(0.0f, m_contentTotalHeight - scrollH + config.ScaleF(24.0f));
    m_scrollY = std::clamp(m_scrollY, 0.0f, m_maxScrollY);

    pRT->PopAxisAlignedClip(); // ScrollArea

    // 5. Crisp Scrollbar
    if (m_maxScrollY > 0.0f) {
        m_scrollTrackBounds = D2D1::RectF(left + width - sbW, scrollTop, left + width, top + height);
        float visRatio = (std::min)(1.0f, scrollH / (std::max)(1.0f, m_contentTotalHeight));
        float thumbH = (std::max)(config.ScaleF(32.0f), scrollH * visRatio);
        float sRatio = m_scrollY / m_maxScrollY;
        float thumbY = scrollTop + sRatio * (scrollH - thumbH);
        m_scrollThumbBounds = D2D1::RectF(left + width - sbW + 2.0f, thumbY, left + width - 2.0f, thumbY + thumbH);
        if (brPillHov) pRT->FillRoundedRectangle(D2D1::RoundedRect(m_scrollThumbBounds, 3.0f, 3.0f), brPillHov.Get());
    }

    pRT->PopAxisAlignedClip(); // m_viewBounds

    // 6. Active Color Picker Modal on top
    if (m_colorPicker.IsOpen()) {
        m_colorPicker.Render(pRT, theme, config, width, height);
    }
}

bool InteractiveGuideView::HandleMouseMove(POINT clientPt) {
    if (m_colorPicker.IsOpen()) {
        auto onColorChange = [this](const std::wstring& newColor, size_t line, size_t col, size_t len) {
            if (m_editors.size() > 3 && line < m_editors[3].doc.buffer.GetLineCount()) {
                std::wstring lineStr = m_editors[3].doc.buffer.GetLine(line);
                if (col + len <= lineStr.length()) {
                    lineStr.replace(col, len, newColor);
                    m_editors[3].doc.buffer.SetLine(line, lineStr);
                }
            }
        };
        if (m_colorPicker.HandleMouseMove(D2D1::Point2F(static_cast<float>(clientPt.x), static_cast<float>(clientPt.y)), onColorChange)) {
            return true;
        }
    }

    GuideButtonId oldBtn = m_hoveredAction;
    int oldFlt = m_hoveredFilter;
    int oldBen = m_hoveredBento;
    m_hoveredAction = GuideButtonId::None;
    m_hoveredFilter = -1;
    m_hoveredBento = -1;

    for (const auto& ah : m_actionHits) {
        if (PtInRectF(clientPt, ah.bounds)) { m_hoveredAction = ah.id; break; }
    }
    for (const auto& fh : m_filterHits) {
        if (PtInRectF(clientPt, fh.bounds)) { m_hoveredFilter = fh.category; break; }
    }
    for (size_t i = 0; i < m_bentoHits.size(); ++i) {
        if (PtInRectF(clientPt, m_bentoHits[i].bounds)) { m_hoveredBento = static_cast<int>(i); break; }
    }

    bool changed = (oldBtn != m_hoveredAction || oldFlt != m_hoveredFilter || oldBen != m_hoveredBento);

    // Drag selection inside focused embedded EditorCanvas
    if (m_focusedEditorIdx >= 0 && m_focusedEditorIdx < static_cast<int>(m_editors.size())) {
        auto& ed = m_editors[static_cast<size_t>(m_focusedEditorIdx)];
        if (ed.doc.isSelecting && m_pHostEditor) {
            size_t line = 0, col = 0;
            if (m_pHostEditor->HitTestEmbeddedCodeBlock(clientPt, m_lastConfig, ed.doc, ed.codeBoxBounds, line, col)) {
                ed.doc.buffer.SetCursor(line, col);
                ed.doc.selEnd = { line, col };
                ed.doc.hasSelection = true;
                changed = true;
            }
        }
    }
    return changed;
}

bool InteractiveGuideView::HandleLButtonDown(POINT clientPt, bool altPressed) {
    if (m_colorPicker.IsOpen()) {
        auto onColorChange = [this](const std::wstring& newColor, size_t line, size_t col, size_t len) {
            if (m_editors.size() > 3 && line < m_editors[3].doc.buffer.GetLineCount()) {
                std::wstring lineStr = m_editors[3].doc.buffer.GetLine(line);
                if (col + len <= lineStr.length()) {
                    lineStr.replace(col, len, newColor);
                    m_editors[3].doc.buffer.SetLine(line, lineStr);
                }
            }
        };
        if (m_colorPicker.HandleLButtonDown(D2D1::Point2F(static_cast<float>(clientPt.x), static_cast<float>(clientPt.y)), onColorChange)) {
            return true;
        }
        m_colorPicker.Close();
    }

    // 1. Check Category Filters
    for (const auto& fh : m_filterHits) {
        if (PtInRectF(clientPt, fh.bounds)) {
            m_activeCategory = fh.category;
            m_scrollY = 0.0f;
            return true;
        }
    }

    // 2. Check Action Buttons
    for (const auto& ah : m_actionHits) {
        if (PtInRectF(clientPt, ah.bounds)) {
            ExecuteButtonAction(ah.id);
            return true;
        }
    }

    // 3. Check Bento Overview Jump Cards
    for (const auto& bh : m_bentoHits) {
        if (PtInRectF(clientPt, bh.bounds)) {
            MarkSectionExplored(bh.sectionIdx);
            m_scrollY = std::clamp(bh.scrollOffsetY - 12.0f, 0.0f, m_maxScrollY);
            if (bh.sectionIdx >= 0 && bh.sectionIdx < static_cast<int>(m_editors.size())) {
                m_focusedEditorIdx = bh.sectionIdx;
                for (size_t i = 0; i < m_editors.size(); ++i) m_editors[i].isFocused = (static_cast<int>(i) == m_focusedEditorIdx);
            }
            return true;
        }
    }

    // 4. IntelliSense popup click in Editor 1
    if (m_editors.size() > 1 && m_editors[1].showIntelliSense && !m_editors[1].intelliSenseItems.empty() &&
        PtInRectF(clientPt, m_editors[1].intelliSensePopupBounds)) {
        const float itemH = m_lastConfig.ScaleF(24.0f);
        const float relY = static_cast<float>(clientPt.y) - (m_editors[1].intelliSensePopupBounds.top + m_lastConfig.ScaleF(4.0f));
        const size_t clickedIdx = (relY > 0.0f) ? (m_editors[1].intelliSenseScrollOffset + static_cast<size_t>(relY / itemH)) : 0;
        if (clickedIdx < m_editors[1].intelliSenseItems.size()) {
            ApplyCompletion(m_editors[1], clickedIdx);
            return true;
        }
    }

    // 5. Real EditorCanvas Code Blocks
    for (size_t i = 0; i < m_editors.size(); ++i) {
        auto& ed = m_editors[i];
        if (!PtInRectF(clientPt, ed.codeBoxBounds)) continue;

        m_focusedEditorIdx = static_cast<int>(i);
        for (size_t j = 0; j < m_editors.size(); ++j) m_editors[j].isFocused = (j == i);
        MarkSectionExplored(static_cast<int>(i));

        // Check real rendered color swatch in CSS editor (Editor 3)
        if (i == 3 && m_pHostEditor) {
            if (const auto* sw = m_pHostEditor->HitTestColorSwatch(clientPt)) {
                m_colorPicker.Open(sw->bounds.left, sw->bounds.bottom + 6.0f, sw->colorText, sw->line, sw->col, sw->length);
                return true;
            }
        }

        // Check real rendered todo checkbox in Tasks editor (Editor 2)
        if (i == 2 && m_pHostEditor) {
            EditorCanvas::TodoCheckboxHit cbHit;
            if (m_pHostEditor->HitTestTodoCheckbox(clientPt, cbHit)) {
                if (cbHit.line < ed.doc.buffer.GetLineCount()) {
                    std::wstring ln = ed.doc.buffer.GetLine(cbHit.line);
                    size_t pDone = ln.find(L"[x]");
                    if (pDone == std::wstring::npos) pDone = ln.find(L"[X]");
                    size_t pEmpty = ln.find(L"[ ]");
                    if (pDone != std::wstring::npos) {
                        ln.replace(pDone, 3, L"[ ]");
                        ed.doc.buffer.SetLine(cbHit.line, ln);
                        return true;
                    }
                    if (pEmpty != std::wstring::npos) {
                        ln.replace(pEmpty, 3, L"[x]");
                        ed.doc.buffer.SetLine(cbHit.line, ln);
                        return true;
                    }
                }
            }
        }

        size_t line = 0, col = 0;
        if (m_pHostEditor && m_pHostEditor->HitTestEmbeddedCodeBlock(clientPt, m_lastConfig, ed.doc, ed.codeBoxBounds, line, col)) {
            if (altPressed) {
                ed.doc.secondarySelections.push_back({ { line, col }, { line, col } });
            } else {
                ed.doc.secondarySelections.clear();
                ed.doc.buffer.SetCursor(line, col);
                ed.doc.selAnchor = { line, col };
                ed.doc.selEnd = { line, col };
                ed.doc.hasSelection = false;
                ed.doc.isSelecting = true;
            }
            if (i == 1) {
                UpdateIntelliSense(ed);
                ed.showIntelliSense = !ed.intelliSenseItems.empty();
            }
        }
        return true;
    }

    return false;
}

bool InteractiveGuideView::HandleLButtonUp(POINT clientPt) {
    if (m_colorPicker.IsOpen()) {
        if (m_colorPicker.HandleLButtonUp(D2D1::Point2F(static_cast<float>(clientPt.x), static_cast<float>(clientPt.y)))) {
            return true;
        }
    }
    if (m_focusedEditorIdx >= 0 && m_focusedEditorIdx < static_cast<int>(m_editors.size())) {
        m_editors[static_cast<size_t>(m_focusedEditorIdx)].doc.isSelecting = false;
        return true;
    }
    return false;
}

bool InteractiveGuideView::HandleChar(wchar_t ch) {
    if (m_focusedEditorIdx < 0 || m_focusedEditorIdx >= static_cast<int>(m_editors.size())) return false;
    auto& ed = m_editors[static_cast<size_t>(m_focusedEditorIdx)];
    if (ed.readOnly || ch < 32 || ch == 127) return false;

    MarkSectionExplored(ed.id);
    if (!ed.doc.secondarySelections.empty()) {
        ed.doc.buffer.InsertChar(ch);
        for (auto& sel : ed.doc.secondarySelections) {
            size_t l = sel.cursor.line;
            size_t c = sel.cursor.col;
            if (l < ed.doc.buffer.GetLineCount()) {
                std::wstring line = ed.doc.buffer.GetLine(l);
                if (c <= line.length()) {
                    line.insert(c, 1, ch);
                    ed.doc.buffer.SetLine(l, line);
                    sel.cursor.col++;
                    sel.anchor.col++;
                }
            }
        }
    } else {
        if (ed.doc.HasSelection()) {
            size_t sL, sC, eL, eC;
            ed.doc.GetSelectionRange(sL, sC, eL, eC);
            ed.doc.buffer.DeleteRange(sL, sC, eL, eC);
            ed.doc.ClearSelection();
        }
        ed.doc.buffer.InsertChar(ch);
    }

    if (ed.id == 1) {
        UpdateIntelliSense(ed);
        ed.showIntelliSense = !ed.intelliSenseItems.empty();
    }
    return true;
}

bool InteractiveGuideView::HandleKeyDown(WPARAM key) {
    if (m_colorPicker.IsOpen() && m_colorPicker.HandleKeyDown(key)) {
        return true;
    }
    if (m_focusedEditorIdx < 0 || m_focusedEditorIdx >= static_cast<int>(m_editors.size())) return false;
    auto& ed = m_editors[static_cast<size_t>(m_focusedEditorIdx)];
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (ctrl && key == VK_SPACE && ed.id == 1) {
        UpdateIntelliSense(ed);
        ed.showIntelliSense = !ed.intelliSenseItems.empty();
        MarkSectionExplored(1);
        return true;
    }

    if (key == VK_ESCAPE) {
        if (ed.showIntelliSense) { ed.showIntelliSense = false; return true; }
        if (!ed.doc.secondarySelections.empty()) { ed.doc.secondarySelections.clear(); return true; }
        if (ed.doc.HasSelection()) { ed.doc.ClearSelection(); return true; }
        return false;
    }

    if (key == VK_TAB) {
        if (ed.showIntelliSense && !ed.intelliSenseItems.empty()) {
            ApplyCompletion(ed, ed.intelliSenseSelectedIdx);
            return true;
        }
        if (!ed.ghostText.empty() && ed.doc.buffer.GetCursor().line == ed.ghostLine) {
            for (wchar_t c : ed.ghostText) ed.doc.buffer.InsertChar(c);
            ed.ghostText.clear();
            MarkSectionExplored(0);
            return true;
        }
        for (int i = 0; i < 4; ++i) ed.doc.buffer.InsertChar(L' ');
        return true;
    }

    if (ed.showIntelliSense && !ed.intelliSenseItems.empty()) {
        if (key == VK_UP) {
            ed.intelliSenseSelectedIdx = (ed.intelliSenseSelectedIdx > 0) ? (ed.intelliSenseSelectedIdx - 1) : (ed.intelliSenseItems.size() - 1);
            return true;
        }
        if (key == VK_DOWN) {
            ed.intelliSenseSelectedIdx = (ed.intelliSenseSelectedIdx + 1 < ed.intelliSenseItems.size()) ? (ed.intelliSenseSelectedIdx + 1) : 0;
            return true;
        }
        if (key == VK_RETURN) {
            ApplyCompletion(ed, ed.intelliSenseSelectedIdx);
            return true;
        }
    }

    if (ctrl && (key == 'D' || key == 'd')) {
        ExecuteButtonAction(GuideButtonId::SelectAllOccurrences);
        return true;
    }

    if (ctrl && (key == 'A' || key == 'a')) {
        size_t cnt = ed.doc.buffer.GetLineCount();
        if (cnt > 0) {
            ed.doc.selAnchor = { 0, 0 };
            ed.doc.selEnd = { cnt - 1, ed.doc.buffer.GetLine(cnt - 1).length() };
            ed.doc.hasSelection = true;
            ed.doc.buffer.SetCursor(cnt - 1, ed.doc.buffer.GetLine(cnt - 1).length());
        }
        return true;
    }

    if (key == VK_BACK) {
        if (ed.doc.HasSelection()) {
            size_t sL, sC, eL, eC;
            ed.doc.GetSelectionRange(sL, sC, eL, eC);
            ed.doc.buffer.DeleteRange(sL, sC, eL, eC);
            ed.doc.ClearSelection();
        } else if (!ed.doc.secondarySelections.empty()) {
            ed.doc.buffer.Backspace();
            for (auto& sel : ed.doc.secondarySelections) {
                size_t l = sel.cursor.line, c = sel.cursor.col;
                if (l < ed.doc.buffer.GetLineCount() && c > 0) {
                    std::wstring line = ed.doc.buffer.GetLine(l);
                    line.erase(c - 1, 1);
                    ed.doc.buffer.SetLine(l, line);
                    sel.cursor.col--;
                    sel.anchor.col--;
                }
            }
        } else {
            ed.doc.buffer.Backspace();
        }
        if (ed.id == 1) { UpdateIntelliSense(ed); ed.showIntelliSense = !ed.intelliSenseItems.empty(); }
        return true;
    }

    if (key == VK_DELETE) {
        if (ed.doc.HasSelection()) {
            size_t sL, sC, eL, eC;
            ed.doc.GetSelectionRange(sL, sC, eL, eC);
            ed.doc.buffer.DeleteRange(sL, sC, eL, eC);
            ed.doc.ClearSelection();
        } else {
            ed.doc.buffer.DeleteForward();
        }
        return true;
    }

    if (key == VK_RETURN) {
        ed.doc.buffer.InsertChar(L'\n');
        return true;
    }
    if (key == VK_LEFT)  { ed.doc.buffer.MoveCursorLeft();  ed.doc.ClearSelection(); return true; }
    if (key == VK_RIGHT) { ed.doc.buffer.MoveCursorRight(); ed.doc.ClearSelection(); return true; }
    if (key == VK_UP)    { ed.doc.buffer.MoveCursorUp();    ed.doc.ClearSelection(); return true; }
    if (key == VK_DOWN)  { ed.doc.buffer.MoveCursorDown();  ed.doc.ClearSelection(); return true; }
    return false;
}

bool InteractiveGuideView::HandleMouseWheel(short delta, float /*viewHeight*/) {
    if (m_maxScrollY <= 0.0f) return false;
    const float scrollAmount = (delta > 0) ? -68.0f : 68.0f;
    const float oldY = m_scrollY;
    m_scrollY = std::clamp(m_scrollY + scrollAmount, 0.0f, m_maxScrollY);
    return (m_scrollY != oldY);
}

LPCWSTR InteractiveGuideView::GetCursorForPoint(POINT pt) const {
    if (m_colorPicker.IsOpen() && PtInRectF(pt, m_colorPicker.GetBounds())) {
        return IDC_ARROW;
    }
    for (const auto& fh : m_filterHits) {
        if (PtInRectF(pt, fh.bounds)) return IDC_HAND;
    }
    for (const auto& ah : m_actionHits) {
        if (PtInRectF(pt, ah.bounds)) return IDC_HAND;
    }
    for (const auto& bh : m_bentoHits) {
        if (PtInRectF(pt, bh.bounds)) return IDC_HAND;
    }
    if (m_editors.size() > 1 && m_editors[1].showIntelliSense && !m_editors[1].intelliSenseItems.empty() &&
        PtInRectF(pt, m_editors[1].intelliSensePopupBounds)) {
        return IDC_HAND;
    }
    if (m_pHostEditor) {
        if (m_pHostEditor->HitTestColorSwatch(pt)) return IDC_HAND;
        EditorCanvas::TodoCheckboxHit cb;
        if (m_pHostEditor->HitTestTodoCheckbox(pt, cb)) return IDC_HAND;
    }
    for (const auto& ed : m_editors) {
        if (PtInRectF(pt, ed.codeBoxBounds)) {
            return IDC_IBEAM;
        }
    }
    return IDC_ARROW;
}

} // namespace phide::ui
