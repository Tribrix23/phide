/**
 * @file ErdView.cpp
 * @brief Implementation of the multi-notation Database ER Diagram (Crow's Foot, UML, Chen, Barker)
 *        and Schema Diff / Migration SQL panel (#10).
 */

#include "phide/ui/ErdView.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace phide::ui {

using Microsoft::WRL::ComPtr;
using phide::db::ErdAndMigrationEngine;
using phide::db::ErdNodeLayout;
using phide::db::ErdNotation;
using phide::db::SchemaDiffKind;
using phide::db::SqlDialect;
using phide::db::SqlTableInfo;

namespace {

/** @brief Returns true if client point `pt` lies inside Direct2D rectangle `r`. */
static bool PtInRectF(POINT pt, const D2D1_RECT_F& r) noexcept {
    return static_cast<float>(pt.x) >= r.left && static_cast<float>(pt.x) <= r.right &&
           static_cast<float>(pt.y) >= r.top  && static_cast<float>(pt.y) <= r.bottom;
}

/** @brief Returns a lowercase copy of `s` for case-insensitive table/column matching. */
static std::wstring ToLowerW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

/** @brief Converts a UTF-8 std::string (e.g., loaded .sql file bytes) to UTF-16 std::wstring. */
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

/** @brief Copies `text` to the Win32 Unicode clipboard; returns false if empty or clipboard locked. */
static bool CopyWideClipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty() || !OpenClipboard(hwnd)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) { CloseClipboard(); return false; }
    void* p = GlobalLock(hMem);
    if (!p) { GlobalFree(hMem); CloseClipboard(); return false; }
    std::memcpy(p, text.c_str(), bytes);
    GlobalUnlock(hMem);
    if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

/** @brief Checks whether `colName` is an explicit FK column in `tbl`, optionally reporting nullability. */
static bool IsFkCol(const SqlTableInfo& tbl, const std::wstring& colName, bool* outNullable = nullptr) {
    std::wstring low = ToLowerW(colName);
    for (const auto& fk : tbl.foreignKeys) {
        if (ToLowerW(fk.column) == low) {
            if (outNullable) {
                *outNullable = true;
                for (const auto& c : tbl.columns) {
                    if (ToLowerW(c.name) == low) {
                        *outNullable = !c.notNull;
                        break;
                    }
                }
            }
            return true;
        }
    }
    return false;
}

/** @brief Returns true if `fkCol` enforces a 1:1 relationship (sole PK or single-column UNIQUE constraint). */
static bool IsFkUniqueOneToOne(const SqlTableInfo& childTbl, const std::wstring& fkCol) {
    std::wstring low = ToLowerW(fkCol);
    int pkCount = 0;
    bool fkIsPk = false;
    bool fkIsUnique = false;
    for (const auto& c : childTbl.columns) {
        if (c.isPrimaryKey) {
            ++pkCount;
            if (ToLowerW(c.name) == low) fkIsPk = true;
        }
        if (c.isUnique && ToLowerW(c.name) == low) {
            fkIsUnique = true;
        }
    }
    return (fkIsPk && pkCount == 1) || fkIsUnique;
}

/** @brief Squared Euclidean distance from point `(px, py)` to line segment `(ax, ay)-(bx, by)`. */
static float DistPointToSegmentSq(float px, float py, float ax, float ay, float bx, float by) noexcept {
    const float dx = bx - ax;
    const float dy = by - ay;
    const float lenSq = dx * dx + dy * dy;
    if (lenSq <= 0.0001f) {
        const float ex = px - ax;
        const float ey = py - ay;
        return ex * ex + ey * ey;
    }
    const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) / lenSq, 0.0f, 1.0f);
    const float projX = ax + t * dx;
    const float projY = ay + t * dy;
    const float ex = px - projX;
    const float ey = py - projY;
    return ex * ex + ey * ey;
}

/**
 * @brief Compacts verbose SQL engine type names (e.g. PostgreSQL "timestamp with time zone")
 *        so node cards stay readable without left-side truncation.
 */
static std::wstring FormatCompactSqlType(std::wstring t) {
    if (t.empty()) return t;
    std::wstring low = ToLowerW(t);
    auto replacePrefix = [&](const std::wstring& prefix, const std::wstring& shortPref) -> bool {
        if (low.rfind(prefix, 0) == 0) {
            t = shortPref + t.substr(prefix.size());
            return true;
        }
        return false;
    };
    if (replacePrefix(L"timestamp with time zone", L"timestamptz")) return t;
    if (replacePrefix(L"timestamp without time zone", L"timestamp")) return t;
    if (replacePrefix(L"time with time zone", L"timetz")) return t;
    if (replacePrefix(L"time without time zone", L"time")) return t;
    if (replacePrefix(L"character varying", L"varchar")) return t;
    if (replacePrefix(L"double precision", L"float8")) return t;
    return t;
}

static IDWriteFactory* EnsureErdDWriteFactory(ComPtr<IDWriteFactory>& cached) {
    if (!cached) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(cached.GetAddressOf()));
    }
    return cached.Get();
}

static float MeasureErdTextWidth(IDWriteFactory* dwf, IDWriteTextFormat* pFmt,
                                 const std::wstring& text, float fallbackCharPx = 7.4f) {
    if (text.empty()) return 0.0f;
    if (dwf && pFmt) {
        ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(dwf->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                            pFmt, 4096.0f, 64.0f, &pLayout)) && pLayout) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                return (std::max)(tm.widthIncludingTrailingWhitespace, tm.width);
            }
        }
    }
    return static_cast<float>(text.size()) * fallbackCharPx;
}

static IDWriteTextFormat* ResolveErdBtnFormat(Theme& theme) noexcept {
    if (theme.fontButtonCenter) return theme.fontButtonCenter.Get();
    if (theme.fontMenu) return theme.fontMenu.Get();
    return theme.fontBody.Get();
}

static IDWriteTextFormat* ResolveErdUiFormat(Theme& theme) noexcept {
    if (theme.fontMenu) return theme.fontMenu.Get();
    if (theme.fontBody) return theme.fontBody.Get();
    return theme.fontTitle.Get();
}

/**
 * @brief Draws single-line text strictly clipped inside `rc` with character ellipsis trimming
 *        so identifiers and SQL column types never overlap adjacent slots or clip on the left.
 */
static void DrawSmallText(ID2D1RenderTarget* pRT, IDWriteTextFormat* pFmt, const std::wstring& txt,
                          const D2D1_RECT_F& rc, ID2D1Brush* br,
                          DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                          IDWriteFactory* dwf = nullptr, bool verticalCenter = true) {
    if (!pRT || !pFmt || !br || txt.empty()) return;
    const float w = rc.right - rc.left;
    const float h = rc.bottom - rc.top;
    if (w <= 2.0f || h <= 2.0f) return;

    pRT->PushAxisAlignedClip(rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (dwf) {
        ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(dwf->CreateTextLayout(txt.c_str(), static_cast<UINT32>(txt.size()),
                                            pFmt, w, h, &pLayout)) && pLayout) {
            pLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            pLayout->SetParagraphAlignment(verticalCenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                                          : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

            // Switch overflowing right-aligned labels to leading alignment so prefix + ellipsis show.
            DWRITE_TEXT_METRICS tm{};
            DWRITE_TEXT_ALIGNMENT effAlign = align;
            if (align == DWRITE_TEXT_ALIGNMENT_TRAILING && SUCCEEDED(pLayout->GetMetrics(&tm))) {
                if (tm.width > w) effAlign = DWRITE_TEXT_ALIGNMENT_LEADING;
            }
            pLayout->SetTextAlignment(effAlign);

            ComPtr<IDWriteInlineObject> pEllipsis;
            if (SUCCEEDED(dwf->CreateEllipsisTrimmingSign(pFmt, &pEllipsis)) && pEllipsis) {
                DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                pLayout->SetTrimming(&trim, pEllipsis.Get());
            }
            pRT->DrawTextLayout(D2D1::Point2F(rc.left, rc.top), pLayout.Get(), br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            pRT->PopAxisAlignedClip();
            return;
        }
    }

    DWRITE_TEXT_ALIGNMENT prev = pFmt->GetTextAlignment();
    DWRITE_WORD_WRAPPING prevWrap = pFmt->GetWordWrapping();
    DWRITE_PARAGRAPH_ALIGNMENT prevPara = pFmt->GetParagraphAlignment();
    pFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    pFmt->SetTextAlignment(align);
    pFmt->SetParagraphAlignment(verticalCenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    pRT->DrawText(txt.c_str(), static_cast<UINT32>(txt.size()), pFmt, rc, br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    pFmt->SetTextAlignment(prev);
    pFmt->SetWordWrapping(prevWrap);
    pFmt->SetParagraphAlignment(prevPara);
    pRT->PopAxisAlignedClip();
}

static void DrawBtn(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                    const D2D1_RECT_F& rc, const wchar_t* lbl, bool active, bool hovered,
                    ID2D1Brush* brNormal, ID2D1Brush* brHover, ID2D1Brush* brBorder,
                    IDWriteFactory* dwf = nullptr) {
    if (rc.right <= rc.left + 2.0f) return;
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rc, config.ScaleF(5.0f), config.ScaleF(5.0f));
    ID2D1Brush* fill = active ? theme.brushBrandAccent.Get() : (hovered ? brHover : brNormal);
    if (fill) pRT->FillRoundedRectangle(rrc, fill);
    if (brBorder) pRT->DrawRoundedRectangle(rrc, active ? theme.brushBrandAccent.Get() : brBorder, hovered ? 1.35f : 1.0f);
    IDWriteTextFormat* pFmt = ResolveErdBtnFormat(theme);
    ID2D1Brush* txt = active
        ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
        : theme.brushTextPrimary.Get();
    if (pFmt && txt && lbl) {
        DrawSmallText(pRT, pFmt, lbl, rc, txt, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
    }
}

/**
 * @brief Draws a Crow's Foot or Barker cardinality endpoint at `(x, y)` pointing along `dir` (+1 right, -1 left).
 */
static void DrawCrowsFootGlyph(ID2D1RenderTarget* pRT, float x, float y, float dir,
                               bool isMany, bool isOptional, ID2D1Brush* br, ID2D1Brush* bgFill, float strokeW) {
    // 1. Optionality circle or mandatory bar placed slightly back from entity edge
    const float markerX = x + dir * 13.0f;
    if (isOptional) {
        D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(markerX, y), 4.0f, 4.0f);
        if (bgFill) pRT->FillEllipse(el, bgFill);
        pRT->DrawEllipse(el, br, strokeW);
    } else {
        pRT->DrawLine(D2D1::Point2F(markerX, y - 5.5f), D2D1::Point2F(markerX, y + 5.5f), br, strokeW);
    }

    // 2. Cardinality at entity edge: many-fork (<) or single-bar (|)
    if (isMany) {
        const float forkBaseX = x + dir * 8.5f;
        pRT->DrawLine(D2D1::Point2F(forkBaseX, y), D2D1::Point2F(x, y - 6.0f), br, strokeW);
        pRT->DrawLine(D2D1::Point2F(forkBaseX, y), D2D1::Point2F(x, y + 6.0f), br, strokeW);
    } else {
        const float barX = x + dir * 5.0f;
        pRT->DrawLine(D2D1::Point2F(barX, y - 5.5f), D2D1::Point2F(barX, y + 5.5f), br, strokeW);
    }
}

/**
 * @brief Draws one straight orthogonal segment `(a -> b)` broken cleanly by semicircular
 *        upward bridge arcs (`r = 6px`) at any horizontal crossing X coordinates in `jumpXs`.
 */
static void DrawOrthogonalSegmentWithJumps(ID2D1RenderTarget* pRT, ID2D1Factory* pFactory,
                                           const phide::db::ErdPointF& a, const phide::db::ErdPointF& b,
                                           std::vector<float> jumpXs, ID2D1Brush* br, float strokeW,
                                           ID2D1StrokeStyle* dashedStyle) {
    if (!pRT || !br) return;
    if (std::abs(a.x - b.x) < 0.25f && std::abs(a.y - b.y) < 0.25f) return;

    const bool isHoriz = (std::abs(a.y - b.y) < 0.5f);
    if (!isHoriz || jumpXs.empty() || !pFactory) {
        pRT->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), br, strokeW, dashedStyle);
        return;
    }

    constexpr float kJumpR = 5.0f;
    const float y = a.y;
    const float lo = (std::min)(a.x, b.x) + kJumpR + 0.25f;
    const float hi = (std::max)(a.x, b.x) - kJumpR - 0.25f;
    jumpXs.erase(std::remove_if(jumpXs.begin(), jumpXs.end(),
                                [lo, hi](float jx) { return jx <= lo || jx >= hi; }),
                 jumpXs.end());
    if (jumpXs.empty()) {
        pRT->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), br, strokeW, dashedStyle);
        return;
    }

    const bool leftToRight = (b.x >= a.x);
    if (leftToRight) std::sort(jumpXs.begin(), jumpXs.end());
    else std::sort(jumpXs.rbegin(), jumpXs.rend());

    ComPtr<ID2D1PathGeometry> pGeo;
    if (FAILED(pFactory->CreatePathGeometry(&pGeo)) || !pGeo) {
        pRT->DrawLine(D2D1::Point2F(a.x, a.y), D2D1::Point2F(b.x, b.y), br, strokeW, dashedStyle);
        return;
    }
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(pGeo->Open(&sink)) || !sink) return;
    sink->BeginFigure(D2D1::Point2F(a.x, y), D2D1_FIGURE_BEGIN_HOLLOW);
    for (float jx : jumpXs) {
        if (leftToRight) {
            sink->AddLine(D2D1::Point2F(jx - kJumpR, y));
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(jx + kJumpR, y), D2D1::SizeF(kJumpR, kJumpR),
                                          0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        } else {
            sink->AddLine(D2D1::Point2F(jx + kJumpR, y));
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(jx - kJumpR, y), D2D1::SizeF(kJumpR, kJumpR),
                                          0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        }
    }
    sink->AddLine(D2D1::Point2F(b.x, b.y));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    pRT->DrawGeometry(pGeo.Get(), br, strokeW, dashedStyle);
}

/**
 * @brief Renders an entire routed orthogonal polyline edge including Barker midpoint dashing,
 *        inferred-FK dashing, and bridge jump arcs at perpendicular crossings.
 */
static void DrawRoutedEdgePolyline(ID2D1RenderTarget* pRT, ID2D1Factory* pFactory,
                                   const phide::db::ErdRoutedEdge& edge,
                                   phide::db::ErdNotation notation,
                                   ID2D1Brush* br, float strokeW,
                                   ID2D1StrokeStyle* dashedStyle) {
    if (edge.points.size() < 2) return;

    float totalLen = 0.0f;
    for (size_t s = 0; s + 1 < edge.points.size(); ++s) {
        totalLen += std::abs(edge.points[s + 1].x - edge.points[s].x) +
                    std::abs(edge.points[s + 1].y - edge.points[s].y);
    }
    const float halfLen = totalLen * 0.5f;
    float accLen = 0.0f;

    for (size_t s = 0; s + 1 < edge.points.size(); ++s) {
        const auto& a = edge.points[s];
        const auto& b = edge.points[s + 1];
        const float segLen = std::abs(b.x - a.x) + std::abs(b.y - a.y);

        std::vector<float> jxs;
        for (const auto& j : edge.jumps) {
            if (j.segmentIndex == s) jxs.push_back(j.crossX);
        }

        if (edge.isInferred) {
            DrawOrthogonalSegmentWithJumps(pRT, pFactory, a, b, std::move(jxs), br, strokeW, dashedStyle);
        } else if (notation == phide::db::ErdNotation::Barker && edge.childOptional && segLen > 0.5f) {
            if (accLen + segLen <= halfLen) {
                DrawOrthogonalSegmentWithJumps(pRT, pFactory, a, b, std::move(jxs), br, strokeW, nullptr);
            } else if (accLen >= halfLen) {
                DrawOrthogonalSegmentWithJumps(pRT, pFactory, a, b, std::move(jxs), br, strokeW, dashedStyle);
            } else {
                const float t = (halfLen - accLen) / segLen;
                const phide::db::ErdPointF midPt{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
                DrawOrthogonalSegmentWithJumps(pRT, pFactory, a, midPt, jxs, br, strokeW, nullptr);
                DrawOrthogonalSegmentWithJumps(pRT, pFactory, midPt, b, std::move(jxs), br, strokeW, dashedStyle);
            }
        } else {
            DrawOrthogonalSegmentWithJumps(pRT, pFactory, a, b, std::move(jxs), br, strokeW, nullptr);
        }
        accLen += segLen;
    }
}

} // namespace

void ErdView::SetToast(const std::wstring& msg) {
    m_toastMsg = msg;
    m_toastExpire = GetTickCount64() + 2500ull;
}

D2D1_POINT_2F ErdView::ScreenToWorld(POINT pt) const noexcept {
    float wx = (static_cast<float>(pt.x) - m_rcCanvasViewport.left - m_panX) / m_zoom;
    float wy = (static_cast<float>(pt.y) - m_rcCanvasViewport.top  - m_panY) / m_zoom;
    return D2D1::Point2F(wx, wy);
}

void ErdView::SetSchemaTables(const std::vector<SqlTableInfo>& tables, SqlDialect dialect, const std::wstring& schemaLabel) {
    bool structureChanged = (tables.size() != m_tables.size());
    if (!structureChanged) {
        for (size_t i = 0; i < tables.size(); ++i) {
            if (tables[i].name != m_tables[i].name) { structureChanged = true; break; }
        }
    }
    m_tables = tables;
    m_dialect = dialect;
    if (!schemaLabel.empty()) m_schemaLabel = schemaLabel;
    if (structureChanged || m_nodes.size() != m_tables.size()) {
        AutoArrangeNodes();
    } else {
        RecalculateNodeHeights();
    }
    m_routesDirty = true;
    RecomputeDiff();
}

void ErdView::EnsureRoutesUpToDate() {
    if (!m_routesDirty) return;
    m_interp = ErdAndMigrationEngine::InterpretSchema(m_tables, m_inferImplicitFks);
    m_routedEdges = ErdAndMigrationEngine::RouteErdEdges(m_tables, m_nodes, m_interp, m_notation);
    m_routesDirty = false;
}

void ErdView::SetConnectionProfiles(const std::vector<phide::db::SqlConnectionProfile>& profiles,
                                     size_t selectedIdx,
                                     bool isBusy,
                                     const std::wstring& schemaError) {
    m_profiles = profiles;
    m_selectedProfileIdx = (selectedIdx < m_profiles.size()) ? selectedIdx : 0;
    m_isConnectionBusy = isBusy;
    m_connectionError = schemaError;
}

void ErdView::SetComparisonSchema(const std::vector<SqlTableInfo>& baselineTables, const std::wstring& baselineLabel) {
    m_baselineTables = baselineTables;
    m_baselineLabel = baselineLabel.empty() ? L"Snapshot" : baselineLabel;
    RecomputeDiff();
}

void ErdView::OpenSchemaDiffMode(bool open) {
    m_showDiffPanel = open;
    if (open) RecomputeDiff();
}

void ErdView::RecomputeDiff() {
    if (m_baselineTables.empty() && m_tables.empty()) {
        m_diffResult = {};
        return;
    }
    m_diffResult = ErdAndMigrationEngine::ComputeSchemaDiff(
        m_baselineTables, m_tables, m_dialect, m_baselineLabel, m_schemaLabel);
}

void ErdView::RecalculateNodeHeights() {
    const auto interp = ErdAndMigrationEngine::InterpretSchema(m_tables, m_inferImplicitFks);
    for (size_t i = 0; i < m_nodes.size() && i < m_tables.size(); ++i) {
        const auto& tbl = m_tables[i];
        const size_t visCols = (std::min)(tbl.columns.size(), size_t(14));
        const float headerH = (m_notation == ErdNotation::Uml) ? 42.0f : 32.0f;

        size_t maxNameLen = 8;
        size_t maxTypeLen = 6;
        for (size_t ci = 0; ci < visCols; ++ci) {
            maxNameLen = (std::max)(maxNameLen, tbl.columns[ci].name.size());
            maxTypeLen = (std::max)(maxTypeLen, FormatCompactSqlType(tbl.columns[ci].type).size());
        }
        const float estNameW = std::clamp(static_cast<float>(maxNameLen) * 7.4f, 92.0f, 164.0f);
        const float estTypeW = std::clamp(static_cast<float>(maxTypeLen) * 7.3f, 62.0f, 106.0f);
        const bool showRoleBadge = (i < interp.entities.size() && !interp.entities[i].badgeLabel.empty() &&
                                    (m_showManyToManyBadges || interp.entities[i].role != phide::db::ErdEntityRole::Junction));
        const float badgeSpace = showRoleBadge ? 44.0f : 0.0f;
        const float estTitleW = static_cast<float>(tbl.name.size()) * 6.5f + 22.0f + badgeSpace;
        m_nodes[i].width = std::clamp((std::max)(36.0f + estNameW + 12.0f + estTypeW + 10.0f, estTitleW),
                                      256.0f, 316.0f);

        // Ensure card body is tall enough to give every attached relationship a distinct >=19px port
        const size_t totalRels = (i < interp.entities.size())
            ? (interp.entities[i].inboundFkCount + interp.entities[i].outboundFkCount)
            : 0;
        const size_t minSlotRows = (totalRels > 0) ? ((totalRels + 1) / 2) : 0;
        const size_t effectiveRows = (std::max)(visCols, (std::min)(minSlotRows, size_t(18)));
        m_nodes[i].height = headerH + 10.0f + static_cast<float>((std::max)(size_t(1), effectiveRows)) * 20.0f + 8.0f;
        if (tbl.columns.size() > 14) m_nodes[i].height += 18.0f;
    }
    m_routesDirty = true;
}

void ErdView::AutoArrangeNodes(bool animate) {
    m_selectedNodeIdx = -1;
    m_hoveredEdgeIdx = -1;
    m_userMovedNodes = false;
    if (m_tables.empty()) {
        m_nodes.clear();
        m_targetNodes.clear();
        m_isAnimatingNodes = false;
        m_routesDirty = true;
        return;
    }

    const bool canAnimateFromExisting = animate && m_nodes.size() == m_tables.size();
    std::vector<ErdNodeLayout> prevNodes = m_nodes;

    m_nodes.resize(m_tables.size());
    for (size_t i = 0; i < m_tables.size(); ++i) {
        m_nodes[i].tableName = m_tables[i].name;
    }
    RecalculateNodeHeights();
    auto arranged = ErdAndMigrationEngine::ComputeSmartLayout(m_tables, m_nodes, m_notation, m_inferImplicitFks);

    if (canAnimateFromExisting) {
        // Preserve current positions and glide smoothly to `arranged` over animation ticks
        for (size_t i = 0; i < m_nodes.size(); ++i) {
            m_nodes[i].x = prevNodes[i].x;
            m_nodes[i].y = prevNodes[i].y;
            m_nodes[i].width = arranged[i].width;
            m_nodes[i].height = arranged[i].height;
        }
        m_targetNodes = std::move(arranged);
        m_isAnimatingNodes = true;
    } else {
        m_nodes = std::move(arranged);
        m_targetNodes = m_nodes;
        m_isAnimatingNodes = false;
    }
    m_routesDirty = true;
}

bool ErdView::IsAnimating() const noexcept {
    return m_isAnimatingZoomPan || m_isAnimatingNodes || m_isAnimatingDiffScroll;
}

bool ErdView::UpdateAnimation() noexcept {
    bool active = false;
    constexpr float kPanZoomSpeed = 0.24f;
    constexpr float kNodeSpeed = 0.22f;

    if (m_isAnimatingZoomPan) {
        const float dz = m_targetZoom - m_zoom;
        const float dx = m_targetPanX - m_panX;
        const float dy = m_targetPanY - m_panY;
        if (std::abs(dz) < 0.0015f && std::abs(dx) < 0.35f && std::abs(dy) < 0.35f) {
            m_zoom = m_targetZoom;
            m_panX = m_targetPanX;
            m_panY = m_targetPanY;
            m_isAnimatingZoomPan = false;
        } else {
            m_zoom += dz * kPanZoomSpeed;
            m_panX += dx * kPanZoomSpeed;
            m_panY += dy * kPanZoomSpeed;
            active = true;
        }
    }

    if (m_isAnimatingNodes && m_nodes.size() == m_targetNodes.size()) {
        bool anyMoving = false;
        for (size_t i = 0; i < m_nodes.size(); ++i) {
            const float dx = m_targetNodes[i].x - m_nodes[i].x;
            const float dy = m_targetNodes[i].y - m_nodes[i].y;
            if (std::abs(dx) < 0.4f && std::abs(dy) < 0.4f) {
                m_nodes[i].x = m_targetNodes[i].x;
                m_nodes[i].y = m_targetNodes[i].y;
            } else {
                m_nodes[i].x += dx * kNodeSpeed;
                m_nodes[i].y += dy * kNodeSpeed;
                anyMoving = true;
            }
        }
        m_routesDirty = true;
        if (!anyMoving) {
            m_isAnimatingNodes = false;
        } else {
            active = true;
        }
    } else if (m_isAnimatingNodes) {
        m_isAnimatingNodes = false;
    }

    if (m_isAnimatingDiffScroll) {
        const float ds = m_targetDiffScrollY - m_diffScrollY;
        if (std::abs(ds) < 0.4f) {
            m_diffScrollY = m_targetDiffScrollY;
            m_isAnimatingDiffScroll = false;
        } else {
            m_diffScrollY += ds * 0.28f;
            active = true;
        }
    }

    return active;
}

void ErdView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                     float canvasLeft, float canvasWidth, float topY, float clientHeight) {
    if (!pRT || canvasWidth <= 20.0f || clientHeight <= topY + 20.0f) return;

    const bool dark = theme.IsDarkMode();
    ComPtr<ID2D1SolidColorBrush> brBg, brGrid, brCard, brCardHdr, brBorder, brAccent, brBtnBg, brBtnHover,
                                  brPk, brFk, brRel, brAdd, brMod, brDel, brDim;
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x0F1218) : D2D1::ColorF(0xF3F5F9), &brBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x181D27) : D2D1::ColorF(0xE2E6EE), &brGrid);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x181C24) : D2D1::ColorF(0xFFFFFF), &brCard);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x212836) : D2D1::ColorF(0xE6ECF5), &brCardHdr);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x313A4B) : D2D1::ColorF(0xBCC4D4), &brBorder);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x38BDF8) : D2D1::ColorF(0x0284C7), &brRel);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x232936) : D2D1::ColorF(0xFFFFFF), &brBtnBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x2F3748) : D2D1::ColorF(0xDCE3EE), &brBtnHover);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0xFBBF24) : D2D1::ColorF(0xB45309), &brPk);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x38BDF8) : D2D1::ColorF(0x0369A1), &brFk);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x22C55E) : D2D1::ColorF(0x15803D), &brAdd);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0xF59E0B) : D2D1::ColorF(0xB45309), &brMod);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0xEF4444) : D2D1::ColorF(0xB91C1C), &brDel);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x1E293B, 0.9f) : D2D1::ColorF(0xEFF6FF, 0.95f), &brDim);
    brAccent = theme.brushBrandAccent;

    IDWriteFactory* dwf = EnsureErdDWriteFactory(m_dwFactory);
    IDWriteTextFormat* pBtnFmt = ResolveErdBtnFormat(theme);
    IDWriteTextFormat* pUiFmt = ResolveErdUiFormat(theme);
    IDWriteTextFormat* pTitleFmt = theme.fontTitle ? theme.fontTitle.Get() : pUiFmt;
    IDWriteTextFormat* pCodeFmt = theme.fontCode ? theme.fontCode.Get() : pUiFmt;
    ID2D1Brush* txtPri = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get();
    ID2D1Brush* txtSec = theme.brushTextSecondary ? theme.brushTextSecondary.Get() : txtPri;
    ID2D1Brush* txtWhite = theme.brushTextWhite ? theme.brushTextWhite.Get() : txtPri;

    // 1. Pre-compute responsive toolbar layout (zero fixed widths, zero clipped buttons)
    const float pad = config.ScaleF(12.0f);
    const float btnH = config.ScaleF(29.0f);
    const float pillH = config.ScaleF(26.0f);
    const float rowGap = config.ScaleF(7.0f);
    const float itemGap = config.ScaleF(6.0f);
    const float maxR = canvasLeft + canvasWidth - pad;

    m_profileHits.clear();
    m_dbHits.clear();
    m_diffDbHits.clear();
    m_rcBtnCrowsFoot = m_rcBtnUml = m_rcBtnChen = m_rcBtnBarker = m_rcBtnChenAttrs = {};
    m_rcBtnAutoLayout = m_rcBtnZoomFit = m_rcBtnRefresh = {};
    m_rcBtnDetect = m_rcBtnEmptyRefresh = {};
    m_rcBtnToggleDiff = m_rcBtnLoadSql = m_rcBtnExportSvg = m_rcBtnInferFks = m_rcBtnShowM2N = m_rcBtnLegend = {};
    m_rcSearchBox = {};

    auto measureBtnW = [&](const wchar_t* lbl) -> float {
        return (std::max)(config.ScaleF(58.0f), MeasureErdTextWidth(dwf, pBtnFmt, lbl ? lbl : L"") + config.ScaleF(24.0f));
    };

    float curY = topY + config.ScaleF(9.0f);
    float x1 = canvasLeft + pad;
    std::wstring hdr = L"ER Diagram (" + std::to_wstring(m_tables.size()) + L" tables)";
    const float hdrW = (std::min)(maxR - x1, MeasureErdTextWidth(dwf, pTitleFmt, hdr) + config.ScaleF(14.0f));
    D2D1_RECT_F rcHdrTitle = D2D1::RectF(x1, curY, x1 + hdrW, curY + btnH);
    x1 += hdrW + config.ScaleF(10.0f);

    struct PlannedBtn {
        D2D1_RECT_F* outRc;
        const wchar_t* label;
        int id;
        bool active;
        float w;
        D2D1_RECT_F placed{};
    };
    auto layoutRow = [&](float& startX, float& yPos, std::vector<PlannedBtn>& btns, float reserveRightW) {
        const float rowLimit = maxR - reserveRightW;
        for (auto& b : btns) {
            if (startX + b.w > rowLimit && startX > canvasLeft + pad) {
                startX = canvasLeft + pad;
                yPos += btnH + rowGap;
            }
            if (startX + b.w <= maxR) {
                b.placed = D2D1::RectF(startX, yPos, startX + b.w, yPos + btnH);
                *b.outRc = b.placed;
                startX += b.w + itemGap;
            }
        }
    };

    std::vector<PlannedBtn> row1Btns = {
        { &m_rcBtnCrowsFoot, L"Crow's Foot", 1, m_notation == ErdNotation::CrowsFoot, measureBtnW(L"Crow's Foot") },
        { &m_rcBtnUml,       L"UML",         2, m_notation == ErdNotation::Uml,       measureBtnW(L"UML") },
        { &m_rcBtnChen,      L"Chen",        3, m_notation == ErdNotation::Chen,      measureBtnW(L"Chen") },
        { &m_rcBtnBarker,    L"Barker",      4, m_notation == ErdNotation::Barker,    measureBtnW(L"Barker") },
    };
    if (m_notation == ErdNotation::Chen) {
        row1Btns.push_back({ &m_rcBtnChenAttrs, L"Attrs", 5, m_showChenAttributes, measureBtnW(L"Attrs") });
    }
    layoutRow(x1, curY, row1Btns, 0.0f);

    // Search box placed on right of Row 1 if plenty of space, else right of Row 2
    const float desiredSearchW = config.ScaleF(190.0f);
    bool searchOnRow1 = (x1 + config.ScaleF(130.0f) <= maxR);
    if (searchOnRow1) {
        const float sw = (std::min)(desiredSearchW, maxR - x1 - config.ScaleF(8.0f));
        m_rcSearchBox = D2D1::RectF(maxR - sw, curY, maxR, curY + btnH);
    }

    curY += btnH + rowGap;
    float x2 = canvasLeft + pad;
    std::vector<PlannedBtn> row2Btns = {
        { &m_rcBtnAutoLayout, L"Auto Layout", 6, false,                   measureBtnW(L"Auto Layout") },
        { &m_rcBtnZoomFit,    L"Zoom Fit",    7, false,                   measureBtnW(L"Zoom Fit") },
        { &m_rcBtnRefresh,    L"Refresh",     8, false,                   measureBtnW(L"Refresh") },
        { &m_rcBtnToggleDiff, L"Schema Diff", 9, m_showDiffPanel,         measureBtnW(L"Schema Diff") },
        { &m_rcBtnInferFks,   L"Infer *_id",  16, m_inferImplicitFks,     measureBtnW(L"Infer *_id") },
        { &m_rcBtnShowM2N,    L"M:N Badges",  17, m_showManyToManyBadges, measureBtnW(L"M:N Badges") },
        { &m_rcBtnLegend,     L"Legend",      18, m_showLegend,           measureBtnW(L"Legend") },
        { &m_rcBtnLoadSql,    L"Load .sql",   10, false,                  measureBtnW(L"Load .sql") },
        { &m_rcBtnExportSvg,  L"Export SVG",  11, false,                  measureBtnW(L"Export SVG") },
    };
    layoutRow(x2, curY, row2Btns, searchOnRow1 ? 0.0f : config.ScaleF(168.0f));
    if (!searchOnRow1) {
        if (x2 + config.ScaleF(100.0f) > maxR) {
            curY += btnH + rowGap;
            m_rcSearchBox = D2D1::RectF(canvasLeft + pad, curY, (std::min)(maxR, canvasLeft + pad + config.ScaleF(240.0f)), curY + btnH);
        } else {
            const float sw = (std::min)(desiredSearchW, maxR - x2 - config.ScaleF(8.0f));
            m_rcSearchBox = D2D1::RectF(maxR - sw, curY, maxR, curY + btnH);
        }
    }

    // Row 3: Live Server Connections & Database Switcher
    curY += btnH + rowGap;
    float sx = canvasLeft + pad;
    const float srvLblW = MeasureErdTextWidth(dwf, pUiFmt, L"Server:") + config.ScaleF(8.0f);
    D2D1_RECT_F rcSrvLbl = D2D1::RectF(sx, curY, sx + srvLblW, curY + pillH);
    sx += srvLblW + config.ScaleF(4.0f);

    for (size_t i = 0; i < m_profiles.size(); ++i) {
        const float pw = (std::max)(config.ScaleF(96.0f),
                                    MeasureErdTextWidth(dwf, pUiFmt, m_profiles[i].name) + config.ScaleF(30.0f));
        if (sx + pw > maxR && sx > canvasLeft + pad) {
            sx = canvasLeft + pad;
            curY += pillH + config.ScaleF(5.0f);
        }
        D2D1_RECT_F rcP = D2D1::RectF(sx, curY, (std::min)(maxR, sx + pw), curY + pillH);
        m_profileHits.push_back({ rcP, i, m_profiles[i].database });
        sx += pw + itemGap;
    }

    const wchar_t* detectLbl = m_isConnectionBusy ? L"Detecting..." : L"Detect Servers";
    const float detectW = (std::max)(config.ScaleF(110.0f), MeasureErdTextWidth(dwf, pBtnFmt, detectLbl) + config.ScaleF(24.0f));
    if (sx + detectW > maxR && sx > canvasLeft + pad) {
        sx = canvasLeft + pad;
        curY += pillH + config.ScaleF(5.0f);
    }
    if (sx + detectW <= maxR) {
        m_rcBtnDetect = D2D1::RectF(sx, curY, sx + detectW, curY + pillH);
    }

    // Row 4: Active Server's Databases Switcher (MySQL / PostgreSQL schemas/databases)
    D2D1_RECT_F rcDbLbl{};
    bool showDbRow = false;
    if (m_selectedProfileIdx < m_profiles.size() && !m_profiles[m_selectedProfileIdx].availableDatabases.empty()) {
        showDbRow = true;
        curY += pillH + config.ScaleF(6.0f);
        float dx = canvasLeft + pad;
        const float dbLblW = MeasureErdTextWidth(dwf, pUiFmt, L"Database:") + config.ScaleF(8.0f);
        rcDbLbl = D2D1::RectF(dx, curY, dx + dbLblW, curY + pillH);
        dx += dbLblW + config.ScaleF(4.0f);

        const auto& dbs = m_profiles[m_selectedProfileIdx].availableDatabases;
        for (size_t di = 0; di < dbs.size(); ++di) {
            const float dw = (std::max)(config.ScaleF(56.0f), MeasureErdTextWidth(dwf, pUiFmt, dbs[di]) + config.ScaleF(22.0f));
            if (dx + dw > maxR && dx > canvasLeft + pad) {
                dx = canvasLeft + pad;
                curY += pillH + config.ScaleF(5.0f);
            }
            D2D1_RECT_F rcD = D2D1::RectF(dx, curY, (std::min)(maxR, dx + dw), curY + pillH);
            m_dbHits.push_back({ rcD, m_selectedProfileIdx, dbs[di] });
            dx += dw + itemGap;
        }
        curY += pillH + config.ScaleF(9.0f);
    } else {
        curY += pillH + config.ScaleF(9.0f);
    }

    // Fill Toolbar Panel Background once now that exact height is known
    D2D1_RECT_F rcBar = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, curY);
    if (brCardHdr) pRT->FillRectangle(rcBar, brCardHdr.Get());
    pRT->DrawLine(D2D1::Point2F(canvasLeft, rcBar.bottom), D2D1::Point2F(canvasLeft + canvasWidth, rcBar.bottom),
                  brBorder.Get(), 1.0f);

    if (pTitleFmt && txtPri) {
        DrawSmallText(pRT, pTitleFmt, hdr, rcHdrTitle, txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
    }
    for (const auto& b : row1Btns) {
        if (b.placed.right > b.placed.left) {
            DrawBtn(pRT, theme, config, b.placed, b.label, b.active, m_hoveredBtnId == b.id,
                    brBtnBg.Get(), brBtnHover.Get(), brBorder.Get(), dwf);
        }
    }
    for (const auto& b : row2Btns) {
        if (b.placed.right > b.placed.left) {
            DrawBtn(pRT, theme, config, b.placed, b.label, b.active, m_hoveredBtnId == b.id,
                    brBtnBg.Get(), brBtnHover.Get(), brBorder.Get(), dwf);
        }
    }
    if (m_rcSearchBox.right > m_rcSearchBox.left) {
        D2D1_ROUNDED_RECT rrcS = D2D1::RoundedRect(m_rcSearchBox, config.ScaleF(5.0f), config.ScaleF(5.0f));
        if (brBtnBg) pRT->FillRoundedRectangle(rrcS, brBtnBg.Get());
        pRT->DrawRoundedRectangle(rrcS, m_searchFocused ? brAccent.Get() : brBorder.Get(), m_searchFocused ? 1.4f : 1.0f);
        std::wstring sDisp = m_searchFilter.empty() ? L"Filter tables..." : m_searchFilter;
        DrawSmallText(pRT, pUiFmt, sDisp,
                      D2D1::RectF(m_rcSearchBox.left + config.ScaleF(10.0f), m_rcSearchBox.top,
                                  m_rcSearchBox.right - config.ScaleF(8.0f), m_rcSearchBox.bottom),
                      m_searchFilter.empty() ? theme.brushTextMuted.Get() : txtPri,
                      DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
    }

    // Draw Server Profile Pills
    if (pUiFmt && txtSec) {
        DrawSmallText(pRT, pUiFmt, L"Server:", rcSrvLbl, txtSec, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
    }
    if (m_profiles.empty()) {
        DrawSmallText(pRT, pUiFmt, L"None detected",
                      D2D1::RectF(rcSrvLbl.right + config.ScaleF(6.0f), rcSrvLbl.top, maxR - detectW - config.ScaleF(10.0f), rcSrvLbl.bottom),
                      txtSec, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
    } else {
        for (size_t i = 0; i < m_profileHits.size() && i < m_profiles.size(); ++i) {
            const auto& hit = m_profileHits[i];
            const auto& prof = m_profiles[i];
            const bool sel = (i == m_selectedProfileIdx);
            const bool hov = (m_hoveredProfileIdx == static_cast<int>(i));
            D2D1_ROUNDED_RECT rp = D2D1::RoundedRect(hit.bounds, config.ScaleF(5.0f), config.ScaleF(5.0f));
            ID2D1Brush* fill = sel ? brAccent.Get() : (hov ? brBtnHover.Get() : brBtnBg.Get());
            if (fill) pRT->FillRoundedRectangle(rp, fill);
            pRT->DrawRoundedRectangle(rp, sel ? brAccent.Get() : brBorder.Get(), hov ? 1.35f : 1.0f);

            const float dotX = hit.bounds.left + config.ScaleF(11.0f);
            const float dotY = (hit.bounds.top + hit.bounds.bottom) * 0.5f;
            const float dotR = config.ScaleF(3.8f);
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), dotR, dotR),
                             prof.isConnected ? brAdd.Get() : brDel.Get());

            DrawSmallText(pRT, pUiFmt, prof.name,
                          D2D1::RectF(hit.bounds.left + config.ScaleF(19.0f), hit.bounds.top,
                                      hit.bounds.right - config.ScaleF(8.0f), hit.bounds.bottom),
                          sel ? txtWhite : txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
        }
    }
    if (m_rcBtnDetect.right > m_rcBtnDetect.left) {
        DrawBtn(pRT, theme, config, m_rcBtnDetect, detectLbl, false, m_hoveredBtnId == 14,
                brBtnBg.Get(), brBtnHover.Get(), brBorder.Get(), dwf);
    }

    // Draw Database Switcher Pills
    if (showDbRow && m_selectedProfileIdx < m_profiles.size()) {
        DrawSmallText(pRT, pUiFmt, L"Database:", rcDbLbl, txtSec, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
        const std::wstring& activeDb = m_profiles[m_selectedProfileIdx].database;
        for (size_t di = 0; di < m_dbHits.size(); ++di) {
            const auto& hit = m_dbHits[di];
            const bool sel = (hit.dbName == activeDb);
            const bool hov = (m_hoveredDbIdx == static_cast<int>(di));
            D2D1_ROUNDED_RECT rd = D2D1::RoundedRect(hit.bounds, config.ScaleF(4.5f), config.ScaleF(4.5f));
            ID2D1Brush* fill = sel ? brAccent.Get() : (hov ? brBtnHover.Get() : brBtnBg.Get());
            if (fill) pRT->FillRoundedRectangle(rd, fill);
            pRT->DrawRoundedRectangle(rd, sel ? brAccent.Get() : brBorder.Get(), hov ? 1.35f : 1.0f);
            DrawSmallText(pRT, pUiFmt, hit.dbName,
                          D2D1::RectF(hit.bounds.left + config.ScaleF(8.0f), hit.bounds.top,
                                      hit.bounds.right - config.ScaleF(8.0f), hit.bounds.bottom),
                          sel ? txtWhite : txtPri, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
        }
    }

    // 2. Main Split Area (ERD Canvas on left, optional Schema Diff & Migration drawer on right)
    const float statusH = config.ScaleF(28.0f);
    const float bodyBot = (std::max)(rcBar.bottom + 20.0f, clientHeight - statusH);
    const float diffW = m_showDiffPanel ? (std::min)(config.ScaleF(410.0f), canvasWidth * 0.42f) : 0.0f;
    m_rcCanvasViewport = D2D1::RectF(canvasLeft, rcBar.bottom, canvasLeft + canvasWidth - diffW, bodyBot);
    m_rcDiffViewport   = m_showDiffPanel
        ? D2D1::RectF(m_rcCanvasViewport.right, rcBar.bottom, canvasLeft + canvasWidth, bodyBot)
        : D2D1_RECT_F{};

    // Draw ERD Canvas
    if (brBg) pRT->FillRectangle(m_rcCanvasViewport, brBg.Get());
    pRT->PushAxisAlignedClip(m_rcCanvasViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Subtle dot/line grid in canvas coordinates
    if (brGrid) {
        const float step = 32.0f * m_zoom;
        if (step >= 10.0f) {
            float ox = std::fmod(m_panX, step);
            if (ox < 0) ox += step;
            float oy = std::fmod(m_panY, step);
            if (oy < 0) oy += step;
            for (float gx = m_rcCanvasViewport.left + ox; gx < m_rcCanvasViewport.right; gx += step) {
                pRT->DrawLine(D2D1::Point2F(gx, m_rcCanvasViewport.top), D2D1::Point2F(gx, m_rcCanvasViewport.bottom), brGrid.Get(), 0.6f);
            }
            for (float gy = m_rcCanvasViewport.top + oy; gy < m_rcCanvasViewport.bottom; gy += step) {
                pRT->DrawLine(D2D1::Point2F(m_rcCanvasViewport.left, gy), D2D1::Point2F(m_rcCanvasViewport.right, gy), brGrid.Get(), 0.6f);
            }
        }
    }

    if (m_tables.empty()) {
        std::wstring emptyMsg = !m_connectionError.empty()
            ? m_connectionError
            : L"No tables found in active schema.\nSelect a connected Server and Database above, or click 'Load .sql' to visualize a DDL file.";
        if (pUiFmt && txtSec) {
            const float cx = (m_rcCanvasViewport.left + m_rcCanvasViewport.right) * 0.5f;
            const float cy = (m_rcCanvasViewport.top + m_rcCanvasViewport.bottom) * 0.5f;
            D2D1_RECT_F rcEmptyTxt = D2D1::RectF(m_rcCanvasViewport.left + config.ScaleF(28.0f), cy - config.ScaleF(70.0f),
                                                 m_rcCanvasViewport.right - config.ScaleF(28.0f), cy + config.ScaleF(10.0f));
            DWRITE_TEXT_ALIGNMENT oldA = pUiFmt->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT oldP = pUiFmt->GetParagraphAlignment();
            pUiFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pUiFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(emptyMsg.c_str(), static_cast<UINT32>(emptyMsg.size()), pUiFmt, rcEmptyTxt, txtSec);
            pUiFmt->SetTextAlignment(oldA);
            pUiFmt->SetParagraphAlignment(oldP);

            const wchar_t* ctaLbl = m_isConnectionBusy ? L"Refreshing Schema..." : L"Detect Servers & Refresh";
            const float ctaW = (std::max)(config.ScaleF(180.0f), MeasureErdTextWidth(dwf, pBtnFmt, ctaLbl) + config.ScaleF(30.0f));
            m_rcBtnEmptyRefresh = D2D1::RectF(cx - ctaW * 0.5f, cy + config.ScaleF(18.0f),
                                              cx + ctaW * 0.5f, cy + config.ScaleF(18.0f) + config.ScaleF(32.0f));
            DrawBtn(pRT, theme, config, m_rcBtnEmptyRefresh, ctaLbl, true, m_hoveredBtnId == 15,
                    brBtnBg.Get(), brBtnHover.Get(), brBorder.Get(), dwf);
        }
    } else {
        // Apply pan + zoom transform strictly inside m_rcCanvasViewport
        D2D1_MATRIX_3X2_F prevTransform;
        pRT->GetTransform(&prevTransform);
        D2D1_MATRIX_3X2_F worldXform =
            D2D1::Matrix3x2F::Scale(m_zoom, m_zoom) *
            D2D1::Matrix3x2F::Translation(m_rcCanvasViewport.left + m_panX, m_rcCanvasViewport.top + m_panY) *
            prevTransform;
        pRT->SetTransform(worldXform);

        std::unordered_map<std::wstring, size_t> nameToIdx;
        for (size_t i = 0; i < m_tables.size(); ++i) nameToIdx[ToLowerW(m_tables[i].name)] = i;
        std::wstring filterLow = ToLowerW(m_searchFilter);

        auto matchesFilter = [&](const SqlTableInfo& t) -> bool {
            return filterLow.empty() || ToLowerW(t.name).find(filterLow) != std::wstring::npos;
        };

        // Pass 1: Draw Obstacle-Free Orthogonal Relationships with Line-Jump Bridge Arcs
        EnsureRoutesUpToDate();

        ComPtr<ID2D1StrokeStyle> dashedStyle;
        ComPtr<ID2D1Factory> pFactory;
        pRT->GetFactory(&pFactory);
        if (pFactory) {
            D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties();
            props.dashStyle = D2D1_DASH_STYLE_DASH;
            float dashes[] = { 5.0f, 3.5f };
            pFactory->CreateStrokeStyle(props, dashes, 2, &dashedStyle);
        }

        // Render normal edges first, then highlighted/hovered edges on top
        for (int hlPass = 0; hlPass < 2; ++hlPass) {
            for (size_t ei = 0; ei < m_routedEdges.size(); ++ei) {
                const auto& edge = m_routedEdges[ei];
                if (edge.parentTableIdx >= m_tables.size() || edge.childTableIdx >= m_tables.size()) continue;
                const auto& childTbl = m_tables[edge.childTableIdx];
                const auto& parTbl = m_tables[edge.parentTableIdx];
                if (!matchesFilter(childTbl) && !matchesFilter(parTbl)) continue;

                const bool isHoveredEdge = (m_hoveredEdgeIdx == static_cast<int>(ei));
                const bool highlighted = isHoveredEdge ||
                                         (m_selectedNodeIdx == static_cast<int>(edge.childTableIdx) ||
                                          m_selectedNodeIdx == static_cast<int>(edge.parentTableIdx));
                if ((hlPass == 0 && highlighted) || (hlPass == 1 && !highlighted)) continue;

                ID2D1Brush* lineBr = highlighted ? brAccent.Get() : (edge.isInferred ? txtSec : brRel.Get());
                const float lineW = isHoveredEdge ? 2.5f : (highlighted ? 2.2f : (edge.isInferred ? 1.25f : 1.55f));

                DrawRoutedEdgePolyline(pRT, pFactory.Get(), edge, m_notation, lineBr, lineW, dashedStyle.Get());

                const float x1 = edge.x1, y1 = edge.y1, dir1 = edge.dir1;
                const float x2 = edge.x2, y2 = edge.y2, dir2 = edge.dir2;
                const float mx = edge.midPoint.x;
                const float my = edge.midPoint.y;

                if (m_notation == ErdNotation::CrowsFoot) {
                    // Parent end: mandatory one (||) when FK is NOT NULL, optional zero-or-one (O|) when nullable.
                    // Child end: 0..1 (O|) for 1:1 relationships, 1..* (|<) for NOT NULL FK, 0..* (O<) for nullable FK.
                    DrawCrowsFootGlyph(pRT, x1, y1, dir1, false, edge.childOptional, lineBr, brBg.Get(), lineW);
                    DrawCrowsFootGlyph(pRT, x2, y2, dir2, !edge.isOneToOne,
                                       edge.isOneToOne ? true : edge.childOptional, lineBr, brBg.Get(), lineW);
                } else if (m_notation == ErdNotation::Barker) {
                    if (!edge.isOneToOne) {
                        const float forkBaseX = x2 + dir2 * 10.0f;
                        pRT->DrawLine(D2D1::Point2F(forkBaseX, y2), D2D1::Point2F(x2, y2 - 6.0f), lineBr, lineW);
                        pRT->DrawLine(D2D1::Point2F(forkBaseX, y2), D2D1::Point2F(x2, y2 + 6.0f), lineBr, lineW);
                    }
                } else if (m_notation == ErdNotation::Uml) {
                    std::wstring pMult = edge.childOptional ? L"0..1" : L"1";
                    std::wstring cMult = edge.isOneToOne ? L"0..1" : (edge.childOptional ? L"0..*" : L"1..*");
                    DrawSmallText(pRT, pUiFmt, pMult,
                                  D2D1::RectF(x1 + (dir1 > 0 ? 12.0f : -42.0f), y1 - 21.0f, x1 + (dir1 > 0 ? 44.0f : -10.0f), y1 - 3.0f),
                                  txtPri, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                    DrawSmallText(pRT, pUiFmt, cMult,
                                  D2D1::RectF(x2 + (dir2 > 0 ? 6.0f : -38.0f), y2 - 21.0f, x2 + (dir2 > 0 ? 38.0f : -6.0f), y2 - 3.0f),
                                  txtPri, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                    ComPtr<ID2D1PathGeometry> pDia;
                    if (pFactory && SUCCEEDED(pFactory->CreatePathGeometry(&pDia)) && pDia) {
                        ComPtr<ID2D1GeometrySink> s;
                        if (SUCCEEDED(pDia->Open(&s)) && s) {
                            s->BeginFigure(D2D1::Point2F(x1, y1), D2D1_FIGURE_BEGIN_FILLED);
                            s->AddLine(D2D1::Point2F(x1 + dir1 * 7.0f, y1 - 5.0f));
                            s->AddLine(D2D1::Point2F(x1 + dir1 * 14.0f, y1));
                            s->AddLine(D2D1::Point2F(x1 + dir1 * 7.0f, y1 + 5.0f));
                            s->EndFigure(D2D1_FIGURE_END_CLOSED);
                            s->Close();
                            if (!edge.childOptional) pRT->FillGeometry(pDia.Get(), lineBr);
                            else if (brBg)           pRT->FillGeometry(pDia.Get(), brBg.Get());
                            pRT->DrawGeometry(pDia.Get(), lineBr, 1.4f);
                        }
                    }
                } else if (m_notation == ErdNotation::Chen) {
                    ComPtr<ID2D1PathGeometry> pDia;
                    if (pFactory && SUCCEEDED(pFactory->CreatePathGeometry(&pDia)) && pDia) {
                        ComPtr<ID2D1GeometrySink> s;
                        if (SUCCEEDED(pDia->Open(&s)) && s) {
                            s->BeginFigure(D2D1::Point2F(mx, my - 18.0f), D2D1_FIGURE_BEGIN_FILLED);
                            s->AddLine(D2D1::Point2F(mx + 34.0f, my));
                            s->AddLine(D2D1::Point2F(mx, my + 18.0f));
                            s->AddLine(D2D1::Point2F(mx - 34.0f, my));
                            s->EndFigure(D2D1_FIGURE_END_CLOSED);
                            s->Close();
                            if (brDim) pRT->FillGeometry(pDia.Get(), brDim.Get());
                            pRT->DrawGeometry(pDia.Get(), lineBr, 1.6f);
                        }
                    }
                    DrawSmallText(pRT, pUiFmt, edge.relationLabel.empty() ? L"has" : edge.relationLabel,
                                  D2D1::RectF(mx - 30.0f, my - 10.0f, mx + 30.0f, my + 10.0f),
                                  txtPri, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                    DrawSmallText(pRT, pUiFmt, L"1",
                                  D2D1::RectF(x1 + (dir1 > 0 ? 6.0f : -22.0f), y1 - 21.0f, x1 + (dir1 > 0 ? 22.0f : -6.0f), y1 - 3.0f),
                                  txtPri, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                    DrawSmallText(pRT, pUiFmt, edge.isOneToOne ? L"1" : L"N",
                                  D2D1::RectF(x2 + (dir2 > 0 ? 6.0f : -22.0f), y2 - 21.0f, x2 + (dir2 > 0 ? 22.0f : -6.0f), y2 - 3.0f),
                                  txtPri, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                }
            }
        }

        // Pass 2: Draw Entity Nodes
        for (size_t i = 0; i < m_tables.size() && i < m_nodes.size(); ++i) {
            const auto& tbl = m_tables[i];
            if (!matchesFilter(tbl)) continue;
            const auto& nd = m_nodes[i];
            const bool isSel = (m_selectedNodeIdx == static_cast<int>(i));

            // Optional Peter Chen Attribute Ovals: 2 top + 2 bottom strictly within card X bounds
            if (m_notation == ErdNotation::Chen && m_showChenAttributes && !tbl.columns.empty()) {
                const size_t maxAttrs = (std::min)(tbl.columns.size(), size_t(4));
                for (size_t ai = 0; ai < maxAttrs; ++ai) {
                    const bool topRow = (ai % 2 == 0);
                    const bool leftSlot = (ai < 2);
                    const float aox = nd.x + nd.width * (leftSlot ? 0.28f : 0.72f);
                    const float ay = topRow ? (nd.y - 22.0f) : (nd.y + nd.height + 22.0f);
                    const float stemY = topRow ? nd.y : (nd.y + nd.height);
                    pRT->DrawLine(D2D1::Point2F(aox, stemY), D2D1::Point2F(aox, ay), brBorder.Get(), 1.0f);
                    D2D1_ELLIPSE oval = D2D1::Ellipse(D2D1::Point2F(aox, ay), 40.0f, 12.5f);
                    if (brCard) pRT->FillEllipse(oval, brCard.Get());
                    pRT->DrawEllipse(oval, tbl.columns[ai].isPrimaryKey ? brPk.Get() : brBorder.Get(), 1.1f);
                    DrawSmallText(pRT, pUiFmt, tbl.columns[ai].name,
                                  D2D1::RectF(aox - 36.0f, ay - 10.0f, aox + 36.0f, ay + 10.0f),
                                  tbl.columns[ai].isPrimaryKey ? brPk.Get() : txtSec,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                }
            }

            D2D1_RECT_F rcNode = D2D1::RectF(nd.x, nd.y, nd.x + nd.width, nd.y + nd.height);
            float radius = (m_notation == ErdNotation::Barker) ? 10.0f
                         : ((m_notation == ErdNotation::Chen || m_notation == ErdNotation::Uml) ? 1.5f : 6.0f);
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcNode, radius, radius);

            if (brCard) pRT->FillRoundedRectangle(rrc, brCard.Get());
            pRT->DrawRoundedRectangle(rrc, isSel ? brAccent.Get() : brBorder.Get(), isSel ? 2.2f : 1.2f);

            // Weak-entity double border in Chen notation when entity has FK inside PK
            if (m_notation == ErdNotation::Chen) {
                bool weakEntity = false;
                for (const auto& c : tbl.columns) {
                    if (c.isPrimaryKey && IsFkCol(tbl, c.name)) { weakEntity = true; break; }
                }
                if (weakEntity) {
                    D2D1_RECT_F rcInner = D2D1::RectF(rcNode.left + 3.5f, rcNode.top + 3.5f,
                                                      rcNode.right - 3.5f, rcNode.bottom - 3.5f);
                    pRT->DrawRectangle(rcInner, brBorder.Get(), 1.0f);
                }
            }

            const float hdrH = (m_notation == ErdNotation::Uml) ? 40.0f : 31.0f;
            D2D1_RECT_F rcHdr = D2D1::RectF(rcNode.left, rcNode.top, rcNode.right, rcNode.top + hdrH);
            if (brCardHdr) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcHdr, radius, radius), brCardHdr.Get());
            pRT->DrawLine(D2D1::Point2F(rcNode.left, rcHdr.bottom), D2D1::Point2F(rcNode.right, rcHdr.bottom), brBorder.Get(), 1.0f);

            const bool isJunction = (i < m_interp.entities.size() && m_interp.entities[i].role == phide::db::ErdEntityRole::Junction);
            const std::wstring roleBadge = (i < m_interp.entities.size() && (m_showManyToManyBadges || !isJunction))
                ? m_interp.entities[i].badgeLabel : L"";
            const float hdrRightPad = roleBadge.empty() ? 12.0f : 56.0f;

            if (m_notation == ErdNotation::Uml) {
                const std::wstring stereo = (isJunction && m_showManyToManyBadges)
                    ? L"<<junction>>" : L"<<entity>>";
                DrawSmallText(pRT, pUiFmt, stereo,
                              D2D1::RectF(rcHdr.left + 8.0f, rcHdr.top + 2.0f, rcHdr.right - 8.0f, rcHdr.top + 17.0f),
                              txtSec, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                DrawSmallText(pRT, pTitleFmt, tbl.name,
                              D2D1::RectF(rcHdr.left + 10.0f, rcHdr.top + 17.0f, rcHdr.right - 10.0f, rcHdr.bottom - 2.0f),
                              txtPri, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
            } else {
                DrawSmallText(pRT, pTitleFmt, tbl.name,
                              D2D1::RectF(rcHdr.left + 12.0f, rcHdr.top + 2.0f, rcHdr.right - hdrRightPad, rcHdr.bottom - 1.0f),
                              txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
                if (!roleBadge.empty()) {
                    D2D1_RECT_F rcRole = D2D1::RectF(rcHdr.right - 50.0f, rcHdr.top + 6.0f, rcHdr.right - 8.0f, rcHdr.bottom - 6.0f);
                    pRT->FillRoundedRectangle(D2D1::RoundedRect(rcRole, 4.0f, 4.0f), brDim.Get());
                    pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcRole, 4.0f, 4.0f), brRel.Get(), 0.9f);
                    DrawSmallText(pRT, pUiFmt, roleBadge, rcRole, brFk.Get(), DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                }
            }

            // Compute non-overlapping name/type column split using measured compact type widths
            const size_t maxVis = (std::min)(tbl.columns.size(), size_t(14));
            float maxTypeW = 54.0f;
            for (size_t ci = 0; ci < maxVis; ++ci) {
                const std::wstring compactT = FormatCompactSqlType(tbl.columns[ci].type);
                maxTypeW = (std::max)(maxTypeW, MeasureErdTextWidth(dwf, pCodeFmt, compactT, 7.6f) + 6.0f);
            }
            const float typeSlotW = std::clamp(maxTypeW, 58.0f, nd.width * 0.40f);
            const float badgeLeft = rcNode.left + 9.0f;
            const float badgeRight = rcNode.left + 31.0f;
            const float nameLeft = rcNode.left + 35.0f;
            const float typeRight = rcNode.right - 10.0f;
            const float typeLeft = (std::max)(nameLeft + 68.0f, typeRight - typeSlotW);
            const float nameRight = typeLeft - 10.0f;

            // Column rows inside entity box (strictly non-overlapping + ellipsis-trimmed)
            float cy = rcHdr.bottom + 6.0f;
            for (size_t ci = 0; ci < maxVis; ++ci) {
                const auto& col = tbl.columns[ci];
                bool isFk = IsFkCol(tbl, col.name);
                if (!isFk && m_inferImplicitFks) {
                    for (const auto& r : m_interp.relations) {
                        if (r.childTableIdx == i && ToLowerW(r.fkColumn) == ToLowerW(col.name)) {
                            isFk = true;
                            break;
                        }
                    }
                }
                std::wstring badge;
                if (m_notation == ErdNotation::Uml) {
                    badge = col.isPrimaryKey ? L"+" : (isFk ? L"#" : L"-");
                } else if (m_notation == ErdNotation::Barker) {
                    badge = col.isPrimaryKey ? L"#" : (col.notNull ? L"*" : L"o");
                } else {
                    badge = col.isPrimaryKey ? L"PK" : (isFk ? L"FK" : (col.isUnique ? L"UQ" : L""));
                }

                ID2D1Brush* colBr = col.isPrimaryKey ? brPk.Get() : (isFk ? brFk.Get() : (col.isUnique ? brAdd.Get() : txtPri));
                if (!badge.empty()) {
                    DrawSmallText(pRT, pCodeFmt, badge,
                                  D2D1::RectF(badgeLeft, cy, badgeRight, cy + 19.0f),
                                  colBr, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
                }
                DrawSmallText(pRT, pCodeFmt, col.name,
                              D2D1::RectF(nameLeft, cy, nameRight, cy + 19.0f),
                              txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
                std::wstring dispType = FormatCompactSqlType(col.type);
                if (col.isUnique && !col.isPrimaryKey && isFk) dispType += L" UQ";
                DrawSmallText(pRT, pCodeFmt, dispType,
                              D2D1::RectF(typeLeft, cy, typeRight, cy + 19.0f),
                              txtSec, DWRITE_TEXT_ALIGNMENT_TRAILING, dwf, true);
                cy += 20.0f;
            }
            if (tbl.columns.size() > maxVis) {
                std::wstring more = L"... +" + std::to_wstring(tbl.columns.size() - maxVis) + L" more";
                DrawSmallText(pRT, pUiFmt, more,
                              D2D1::RectF(rcNode.left + 12.0f, cy, rcNode.right - 10.0f, cy + 18.0f),
                              txtSec, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
            }
        }

        pRT->SetTransform(prevTransform);
    }

    // 2b. Floating Relationship Cardinality Tooltip (when hovering a routed edge)
    if (m_hoveredEdgeIdx >= 0 && static_cast<size_t>(m_hoveredEdgeIdx) < m_routedEdges.size() &&
        m_hoveredEdgeIdx < static_cast<int>(m_interp.relations.size())) {
        const auto& edge = m_routedEdges[static_cast<size_t>(m_hoveredEdgeIdx)];
        const auto& rel = m_interp.relations[static_cast<size_t>(m_hoveredEdgeIdx)];
        if (edge.parentTableIdx < m_tables.size() && edge.childTableIdx < m_tables.size()) {
            const std::wstring& pName = m_tables[edge.parentTableIdx].name;
            const std::wstring& cName = m_tables[edge.childTableIdx].name;
            const std::wstring pCard = edge.childOptional ? L"0..1" : L"1";
            const std::wstring cCard = edge.isOneToOne ? L"0..1" : (edge.childOptional ? L"0..*" : L"1..*");
            const std::wstring line1 = pName + L" (" + pCard + L")  \u2192  (" + cCard + L") " + cName;
            const std::wstring line2 = cName + L"." + rel.fkColumn + L" \u2192 " + pName + L"." + rel.refColumn +
                                       (edge.cardinalityReason.empty() ? L"" : (L"  \u2022  " + edge.cardinalityReason));

            const float tw = (std::min)(m_rcCanvasViewport.right - m_rcCanvasViewport.left - 24.0f,
                                        (std::max)(MeasureErdTextWidth(dwf, pUiFmt, line1), MeasureErdTextWidth(dwf, pUiFmt, line2)) + config.ScaleF(24.0f));
            const float th = config.ScaleF(46.0f);
            float tx = static_cast<float>(m_lastMousePt.x) + config.ScaleF(14.0f);
            float ty = static_cast<float>(m_lastMousePt.y) + config.ScaleF(16.0f);
            if (tx + tw > m_rcCanvasViewport.right - 10.0f) tx = m_rcCanvasViewport.right - tw - 10.0f;
            if (ty + th > m_rcCanvasViewport.bottom - 10.0f) ty = static_cast<float>(m_lastMousePt.y) - th - 10.0f;
            if (tx < m_rcCanvasViewport.left + 10.0f) tx = m_rcCanvasViewport.left + 10.0f;
            if (ty < m_rcCanvasViewport.top + 10.0f) ty = m_rcCanvasViewport.top + 10.0f;

            D2D1_RECT_F rcTip = D2D1::RectF(tx, ty, tx + tw, ty + th);
            D2D1_ROUNDED_RECT rrcTip = D2D1::RoundedRect(rcTip, config.ScaleF(6.0f), config.ScaleF(6.0f));
            if (brCardHdr) pRT->FillRoundedRectangle(rrcTip, brCardHdr.Get());
            if (brAccent)  pRT->DrawRoundedRectangle(rrcTip, brAccent.Get(), 1.2f);
            DrawSmallText(pRT, pUiFmt, line1, D2D1::RectF(tx + 10.0f, ty + 4.0f, tx + tw - 10.0f, ty + 22.0f),
                          txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
            DrawSmallText(pRT, pUiFmt, line2, D2D1::RectF(tx + 10.0f, ty + 22.0f, tx + tw - 10.0f, ty + 42.0f),
                          brFk.Get(), DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
        }
    }

    // 2c. Optional Compact Cardinality Notation Legend Overlay (bottom-right of canvas)
    if (m_showLegend && (m_rcCanvasViewport.right - m_rcCanvasViewport.left) > config.ScaleF(280.0f) &&
        (m_rcCanvasViewport.bottom - m_rcCanvasViewport.top) > config.ScaleF(190.0f)) {
        const float lw = config.ScaleF(292.0f);
        const float lh = config.ScaleF(138.0f);
        const float lx = m_rcCanvasViewport.right - lw - config.ScaleF(14.0f);
        const float ly = m_rcCanvasViewport.bottom - lh - config.ScaleF(14.0f);
        D2D1_RECT_F rcLeg = D2D1::RectF(lx, ly, lx + lw, ly + lh);
        D2D1_ROUNDED_RECT rrcLeg = D2D1::RoundedRect(rcLeg, config.ScaleF(7.0f), config.ScaleF(7.0f));
        if (brCardHdr) pRT->FillRoundedRectangle(rrcLeg, brCardHdr.Get());
        if (brBorder)  pRT->DrawRoundedRectangle(rrcLeg, brBorder.Get(), 1.1f);

        const wchar_t* legTitle =
            (m_notation == ErdNotation::Uml) ? L"UML Multiplicity Guide"
          : (m_notation == ErdNotation::Chen) ? L"Peter Chen Notation Guide"
          : (m_notation == ErdNotation::Barker) ? L"Oracle Barker Notation Guide"
          : L"Crow's Foot Cardinality Guide";
        DrawSmallText(pRT, pTitleFmt, legTitle,
                      D2D1::RectF(lx + 12.0f, ly + 6.0f, lx + lw - 12.0f, ly + 25.0f),
                      txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
        pRT->DrawLine(D2D1::Point2F(lx + 10.0f, ly + 28.0f), D2D1::Point2F(lx + lw - 10.0f, ly + 28.0f), brBorder.Get(), 0.9f);

        std::vector<std::pair<std::wstring, std::wstring>> rows;
        if (m_notation == ErdNotation::Uml) {
            rows = {
                { L"1 \u25c6\u2500 0..1", L"1:1 (PK+FK or UNIQUE FK, NOT NULL)" },
                { L"1 \u25c6\u2500 1..*", L"1:N Mandatory (NOT NULL FK)" },
                { L"0..1 \u25c7\u2500 0..*", L"1:N Optional (Nullable FK)" },
                { L"+ / # / -", L"PK / Foreign Key / Attribute" },
                { L"Dashed line", L"Inferred *_id relationship" }
            };
        } else if (m_notation == ErdNotation::Chen) {
            rows = {
                { L"1 \u2500\u25c7\u2500 1", L"1:1 (PK+FK or UNIQUE FK)" },
                { L"1 \u2500\u25c7\u2500 N", L"1:N Relationship" },
                { L"Double box", L"Weak entity (FK inside PK)" },
                { L"PK / FK / UQ", L"Primary / Foreign / Unique Key" },
                { L"Dashed line", L"Inferred *_id relationship" }
            };
        } else if (m_notation == ErdNotation::Barker) {
            rows = {
                { L"Solid half", L"Mandatory side (NOT NULL)" },
                { L"Dashed half", L"Optional side (Nullable FK)" },
                { L"Crow's foot <", L"Many end (omit for 1:1)" },
                { L"# / * / o", L"Primary Key / NOT NULL / Nullable" },
                { L"Dashed full", L"Inferred *_id relationship" }
            };
        } else {
            rows = {
                { L"|| \u2500\u2500 O|", L"1:1 (PK+FK or UNIQUE FK)" },
                { L"|| \u2500\u2500 |<", L"1:N Mandatory (NOT NULL FK)" },
                { L"O| \u2500\u2500 O<", L"1:N Optional (Nullable FK)" },
                { L"PK / FK / UQ", L"Primary / Foreign / Unique Key" },
                { L"Dashed line", L"Inferred *_id relationship" }
            };
        }
        float rowY = ly + 33.0f;
        for (const auto& [sym, desc] : rows) {
            DrawSmallText(pRT, pCodeFmt, sym, D2D1::RectF(lx + 12.0f, rowY, lx + 106.0f, rowY + 18.0f),
                          brFk.Get(), DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
            DrawSmallText(pRT, pUiFmt, desc, D2D1::RectF(lx + 110.0f, rowY, lx + lw - 10.0f, rowY + 18.0f),
                          txtSec, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
            rowY += 19.5f;
        }
    }
    pRT->PopAxisAlignedClip();

    // 3. Optional Right-Side Schema Diff & Migration SQL Panel
    m_rcBtnOpenMigConsole = {};
    m_rcBtnCopyMigration  = {};
    if (m_showDiffPanel && diffW > 40.0f) {
        if (brCard) pRT->FillRectangle(m_rcDiffViewport, brCard.Get());
        pRT->DrawLine(D2D1::Point2F(m_rcDiffViewport.left, m_rcDiffViewport.top),
                      D2D1::Point2F(m_rcDiffViewport.left, m_rcDiffViewport.bottom), brBorder.Get(), 1.0f);
        pRT->PushAxisAlignedClip(m_rcDiffViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        const float dp = config.ScaleF(12.0f);
        float dy = m_rcDiffViewport.top + dp;
        const float dMaxR = m_rcDiffViewport.right - dp;
        std::wstring diffTitle = L"Schema Diff (" + m_baselineLabel + L" \u2192 " + m_schemaLabel + L")";
        DrawSmallText(pRT, pTitleFmt, diffTitle,
                      D2D1::RectF(m_rcDiffViewport.left + dp, dy, dMaxR, dy + config.ScaleF(24.0f)),
                      txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
        dy += config.ScaleF(28.0f);

        // Action buttons (wrap cleanly onto next row if drawer is narrow)
        float dbx = m_rcDiffViewport.left + dp;
        const float wOpen = (std::max)(config.ScaleF(140.0f), MeasureErdTextWidth(dwf, pBtnFmt, L"Open in SQL Console") + config.ScaleF(24.0f));
        const float wCopy = (std::max)(config.ScaleF(112.0f), MeasureErdTextWidth(dwf, pBtnFmt, L"Copy Migration") + config.ScaleF(24.0f));
        if (dbx + wOpen <= dMaxR) {
            m_rcBtnOpenMigConsole = D2D1::RectF(dbx, dy, dbx + wOpen, dy + btnH);
            DrawBtn(pRT, theme, config, m_rcBtnOpenMigConsole, L"Open in SQL Console", false,
                    m_hoveredBtnId == 12, brBtnBg.Get(), brBtnHover.Get(), brBorder.Get(), dwf);
            dbx += wOpen + config.ScaleF(6.0f);
        }
        if (dbx + wCopy > dMaxR && dbx > m_rcDiffViewport.left + dp) {
            dbx = m_rcDiffViewport.left + dp;
            dy += btnH + config.ScaleF(6.0f);
        }
        if (dbx + wCopy <= dMaxR) {
            m_rcBtnCopyMigration = D2D1::RectF(dbx, dy, dbx + wCopy, dy + btnH);
            DrawBtn(pRT, theme, config, m_rcBtnCopyMigration, L"Copy Migration", false,
                    m_hoveredBtnId == 13, brBtnBg.Get(), brBtnHover.Get(), brBorder.Get(), dwf);
        }
        dy += btnH + config.ScaleF(8.0f);

        // Quick Baseline DB Switcher inside Schema Diff drawer
        if (!m_profiles.empty()) {
            struct DiffTarget { size_t pIdx; std::wstring db; std::wstring label; };
            std::vector<DiffTarget> targets;
            const std::wstring activeDb = (m_selectedProfileIdx < m_profiles.size())
                ? m_profiles[m_selectedProfileIdx].database : L"";
            if (m_selectedProfileIdx < m_profiles.size()) {
                for (const auto& db : m_profiles[m_selectedProfileIdx].availableDatabases) {
                    if (db != activeDb && targets.size() < 6) {
                        targets.push_back({ m_selectedProfileIdx, db, db });
                    }
                }
            }
            for (size_t pi = 0; pi < m_profiles.size() && targets.size() < 8; ++pi) {
                if (pi == m_selectedProfileIdx || !m_profiles[pi].isConnected) continue;
                std::wstring db = m_profiles[pi].database;
                std::wstring lbl = db.empty() ? m_profiles[pi].name : (m_profiles[pi].name + L"/" + db);
                targets.push_back({ pi, db, lbl });
            }
            if (!targets.empty()) {
                DrawSmallText(pRT, pUiFmt, L"Compare vs DB:",
                              D2D1::RectF(m_rcDiffViewport.left + dp, dy, dMaxR, dy + config.ScaleF(18.0f)),
                              txtSec, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
                dy += config.ScaleF(20.0f);
                float tx = m_rcDiffViewport.left + dp;
                for (size_t ti = 0; ti < targets.size(); ++ti) {
                    const float tw = (std::max)(config.ScaleF(58.0f),
                                                MeasureErdTextWidth(dwf, pUiFmt, targets[ti].label) + config.ScaleF(18.0f));
                    if (tx + tw > dMaxR && tx > m_rcDiffViewport.left + dp) {
                        tx = m_rcDiffViewport.left + dp;
                        dy += pillH + config.ScaleF(5.0f);
                    }
                    D2D1_RECT_F rcT = D2D1::RectF(tx, dy, (std::min)(dMaxR, tx + tw), dy + pillH);
                    const bool isSel = (m_baselineLabel.find(targets[ti].label) != std::wstring::npos);
                    const bool isHov = (m_hoveredDiffDbIdx == static_cast<int>(ti));
                    D2D1_ROUNDED_RECT rrt = D2D1::RoundedRect(rcT, config.ScaleF(4.0f), config.ScaleF(4.0f));
                    pRT->FillRoundedRectangle(rrt, isSel ? brAccent.Get() : (isHov ? brBtnHover.Get() : brBtnBg.Get()));
                    pRT->DrawRoundedRectangle(rrt, isSel ? brAccent.Get() : brBorder.Get(), 1.0f);
                    DrawSmallText(pRT, pUiFmt, targets[ti].label,
                                  D2D1::RectF(rcT.left + config.ScaleF(6.0f), rcT.top, rcT.right - config.ScaleF(6.0f), rcT.bottom),
                                  isSel ? txtWhite : txtPri, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                    m_diffDbHits.push_back({ rcT, targets[ti].pIdx, targets[ti].db });
                    tx += tw + config.ScaleF(5.0f);
                }
                dy += pillH + config.ScaleF(8.0f);
            }
        }

        std::wstring summary = L"+" + std::to_wstring(m_diffResult.addedCount) +
                               L" added   ~" + std::to_wstring(m_diffResult.modifiedCount) +
                               L" modified   -" + std::to_wstring(m_diffResult.removedCount) + L" removed";
        DrawSmallText(pRT, pUiFmt, summary,
                      D2D1::RectF(m_rcDiffViewport.left + dp, dy, dMaxR, dy + config.ScaleF(20.0f)),
                      txtSec, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
        dy += config.ScaleF(24.0f);

        D2D1_RECT_F rcList = D2D1::RectF(m_rcDiffViewport.left, dy, m_rcDiffViewport.right, m_rcDiffViewport.bottom);
        pRT->PushAxisAlignedClip(rcList, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float ly = dy - m_diffScrollY;
        float totalListH = 8.0f;
        const float rowH = config.ScaleF(23.0f);

        if (m_diffResult.items.empty()) {
            DrawSmallText(pRT, pUiFmt, L"Click 'Load .sql' or pick a baseline database above to diff.",
                          D2D1::RectF(m_rcDiffViewport.left + dp, dy + 6.0f, dMaxR, dy + 54.0f),
                          txtSec, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, false);
        } else {
            for (const auto& item : m_diffResult.items) {
                if (ly + rowH >= dy && ly <= m_rcDiffViewport.bottom) {
                    ID2D1Brush* cBr = (item.kind == SchemaDiffKind::TableAdded ||
                                       item.kind == SchemaDiffKind::ColumnAdded ||
                                       item.kind == SchemaDiffKind::ForeignKeyAdded)
                        ? brAdd.Get()
                        : ((item.kind == SchemaDiffKind::ColumnModified) ? brMod.Get() : brDel.Get());
                    DrawSmallText(pRT, pCodeFmt, item.summary,
                                  D2D1::RectF(m_rcDiffViewport.left + dp, ly, dMaxR, ly + rowH),
                                  cBr, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
                }
                ly += rowH;
                totalListH += rowH;
            }
        }
        m_diffMaxScrollY = (std::max)(0.0f, totalListH - (m_rcDiffViewport.bottom - dy));
        pRT->PopAxisAlignedClip();
        pRT->PopAxisAlignedClip();
    }

    // 4. Bottom Status Strip (12px crisp UI font, never fontBadge)
    D2D1_RECT_F rcStatus = D2D1::RectF(canvasLeft, bodyBot, canvasLeft + canvasWidth, clientHeight);
    if (brCardHdr) pRT->FillRectangle(rcStatus, brCardHdr.Get());
    pRT->DrawLine(D2D1::Point2F(canvasLeft, bodyBot), D2D1::Point2F(canvasLeft + canvasWidth, bodyBot), brBorder.Get(), 1.0f);

    std::wstring status = m_schemaLabel + L"  |  Zoom: " + std::to_wstring(static_cast<int>(m_zoom * 100.0f)) +
                          L"%  |  Drag tables to arrange, drag canvas to pan, scroll to zoom";
    if (!m_toastMsg.empty() && GetTickCount64() < m_toastExpire) status = m_toastMsg;
    DrawSmallText(pRT, pUiFmt, status,
                  D2D1::RectF(canvasLeft + pad, bodyBot + config.ScaleF(2.0f), canvasLeft + canvasWidth - pad, clientHeight - config.ScaleF(2.0f)),
                  txtSec, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
}

bool ErdView::HandleLButtonDown(POINT clientPt, HWND hwnd, const app::AppConfig& /*config*/) {
    m_searchFocused = PtInRectF(clientPt, m_rcSearchBox);
    if (m_searchFocused) return true;

    if (PtInRectF(clientPt, m_rcBtnDetect) || PtInRectF(clientPt, m_rcBtnEmptyRefresh)) {
        if (m_callbacks.onDetectServers) m_callbacks.onDetectServers();
        else if (m_callbacks.onRefreshLiveSchema) m_callbacks.onRefreshLiveSchema();
        SetToast(L"Detecting database servers and refreshing schema...");
        return true;
    }

    for (const auto& hit : m_profileHits) {
        if (PtInRectF(clientPt, hit.bounds)) {
            m_selectedProfileIdx = hit.profileIdx;
            if (m_callbacks.onSelectProfile) m_callbacks.onSelectProfile(hit.profileIdx);
            SetToast(L"Switched active database server");
            return true;
        }
    }

    for (const auto& hit : m_dbHits) {
        if (PtInRectF(clientPt, hit.bounds)) {
            if (m_callbacks.onSelectDatabase) m_callbacks.onSelectDatabase(hit.dbName);
            SetToast(L"Loading schema for database: " + hit.dbName);
            return true;
        }
    }

    for (const auto& hit : m_diffDbHits) {
        if (PtInRectF(clientPt, hit.bounds)) {
            if (m_callbacks.onSelectCompareDatabase) {
                m_callbacks.onSelectCompareDatabase(hit.profileIdx, hit.dbName);
            }
            SetToast(L"Inspecting baseline schema: " + hit.dbName);
            return true;
        }
    }

    auto switchNotation = [&](ErdNotation next) {
        if (m_notation == next) return;
        const bool needChenSpace = (next == ErdNotation::Chen || m_notation == ErdNotation::Chen);
        m_notation = next;
        if (!m_userMovedNodes && needChenSpace) {
            AutoArrangeNodes(true);
        } else {
            RecalculateNodeHeights();
        }
    };
    if (PtInRectF(clientPt, m_rcBtnCrowsFoot)) { switchNotation(ErdNotation::CrowsFoot); return true; }
    if (PtInRectF(clientPt, m_rcBtnUml))       { switchNotation(ErdNotation::Uml);       return true; }
    if (PtInRectF(clientPt, m_rcBtnChen))      { switchNotation(ErdNotation::Chen);      return true; }
    if (PtInRectF(clientPt, m_rcBtnBarker))    { switchNotation(ErdNotation::Barker);    return true; }
    if (PtInRectF(clientPt, m_rcBtnChenAttrs)) {
        m_showChenAttributes = !m_showChenAttributes;
        m_routesDirty = true;
        return true;
    }
    if (PtInRectF(clientPt, m_rcBtnAutoLayout)) {
        m_userMovedNodes = false;
        AutoArrangeNodes(true);
        m_targetPanX = 40.0f;
        m_targetPanY = 40.0f;
        m_targetZoom = 1.0f;
        m_isAnimatingZoomPan = true;
        return true;
    }
    if (PtInRectF(clientPt, m_rcBtnInferFks)) {
        m_inferImplicitFks = !m_inferImplicitFks;
        m_routesDirty = true;
        SetToast(m_inferImplicitFks ? L"Inferred *_id relationships enabled" : L"Showing explicit FK constraints only");
        return true;
    }
    if (PtInRectF(clientPt, m_rcBtnShowM2N)) {
        m_showManyToManyBadges = !m_showManyToManyBadges;
        RecalculateNodeHeights();
        SetToast(m_showManyToManyBadges ? L"M:N junction badges enabled" : L"M:N junction badges hidden");
        return true;
    }
    if (PtInRectF(clientPt, m_rcBtnLegend)) {
        m_showLegend = !m_showLegend;
        return true;
    }

    if (PtInRectF(clientPt, m_rcBtnZoomFit)) {
        const auto& fitNodes = (m_isAnimatingNodes && m_targetNodes.size() == m_nodes.size()) ? m_targetNodes : m_nodes;
        if (!fitNodes.empty()) {
            float minX = fitNodes[0].x, minY = fitNodes[0].y;
            float maxX = fitNodes[0].x + fitNodes[0].width, maxY = fitNodes[0].y + fitNodes[0].height;
            for (const auto& n : fitNodes) {
                minX = (std::min)(minX, n.x); minY = (std::min)(minY, n.y);
                maxX = (std::max)(maxX, n.x + n.width); maxY = (std::max)(maxY, n.y + n.height);
            }
            float vw = (std::max)(100.0f, m_rcCanvasViewport.right - m_rcCanvasViewport.left - 60.0f);
            float vh = (std::max)(100.0f, m_rcCanvasViewport.bottom - m_rcCanvasViewport.top - 60.0f);
            float bw = (std::max)(1.0f, maxX - minX);
            float bh = (std::max)(1.0f, maxY - minY);
            m_targetZoom = std::clamp((std::min)(vw / bw, vh / bh), 0.25f, 1.5f);
            m_targetPanX = 30.0f - minX * m_targetZoom;
            m_targetPanY = 30.0f - minY * m_targetZoom;
            m_isAnimatingZoomPan = true;
        }
        return true;
    }

    if (PtInRectF(clientPt, m_rcBtnRefresh)) {
        if (m_callbacks.onRefreshLiveSchema) m_callbacks.onRefreshLiveSchema();
        SetToast(L"Refreshing schema from active connection...");
        return true;
    }

    if (PtInRectF(clientPt, m_rcBtnToggleDiff)) {
        m_showDiffPanel = !m_showDiffPanel;
        if (m_showDiffPanel) RecomputeDiff();
        return true;
    }

    if (PtInRectF(clientPt, m_rcBtnLoadSql)) {
        if (m_callbacks.onPromptOpenSqlFile) {
            std::wstring path = m_callbacks.onPromptOpenSqlFile();
            if (!path.empty()) {
                std::ifstream ifs(path, std::ios::binary);
                if (ifs.is_open()) {
                    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                    auto parsed = ErdAndMigrationEngine::ParseSqlDdlSchema(Utf8ToWide(raw));
                    std::wstring fname = path;
                    auto sl = fname.find_last_of(L"\\/");
                    if (sl != std::wstring::npos) fname = fname.substr(sl + 1);
                    if (m_tables.empty()) {
                        SetSchemaTables(parsed, m_dialect, fname);
                    } else {
                        SetComparisonSchema(parsed, fname);
                        m_showDiffPanel = true;
                    }
                    SetToast(L"Loaded " + std::to_wstring(parsed.size()) + L" tables from " + fname);
                }
            }
        }
        return true;
    }

    if (PtInRectF(clientPt, m_rcBtnExportSvg)) {
        if (m_callbacks.onPromptSaveSvgFile) {
            std::wstring outPath = m_callbacks.onPromptSaveSvgFile(L"schema_erd.svg");
            if (!outPath.empty()) {
                std::string svg = ErdAndMigrationEngine::ExportErdToSvg(m_tables, m_nodes, m_notation, m_inferImplicitFks, m_showManyToManyBadges);
                std::ofstream ofs(outPath, std::ios::binary | std::ios::trunc);
                if (ofs.is_open()) {
                    ofs.write(svg.data(), static_cast<std::streamsize>(svg.size()));
                    SetToast(L"Exported ER Diagram SVG");
                }
            }
        }
        return true;
    }

    if (PtInRectF(clientPt, m_rcBtnOpenMigConsole)) {
        RecomputeDiff();
        if (m_callbacks.onOpenSqlInConsole) m_callbacks.onOpenSqlInConsole(m_diffResult.migrationSql);
        return true;
    }

    if (PtInRectF(clientPt, m_rcBtnCopyMigration)) {
        RecomputeDiff();
        if (CopyWideClipboard(hwnd, m_diffResult.migrationSql)) {
            SetToast(L"Copied migration SQL to clipboard");
        }
        return true;
    }

    if (PtInRectF(clientPt, m_rcCanvasViewport)) {
        D2D1_POINT_2F wp = ScreenToWorld(clientPt);
        for (int i = static_cast<int>(m_nodes.size()) - 1; i >= 0; --i) {
            const auto& n = m_nodes[static_cast<size_t>(i)];
            if (wp.x >= n.x && wp.x <= n.x + n.width && wp.y >= n.y && wp.y <= n.y + n.height) {
                m_selectedNodeIdx = i;
                m_draggingNodeIdx = i;
                m_nodeDragOffsetX = wp.x - n.x;
                m_nodeDragOffsetY = wp.y - n.y;
                return true;
            }
        }
        m_selectedNodeIdx = -1;
        m_isPanningCanvas = true;
        m_lastDragPt = clientPt;
        return true;
    }
    return false;
}

bool ErdView::HandleLButtonUp(POINT /*clientPt*/) {
    bool was = (m_draggingNodeIdx >= 0 || m_isPanningCanvas);
    m_draggingNodeIdx = -1;
    m_isPanningCanvas = false;
    return was;
}

bool ErdView::HandleMouseMove(POINT clientPt, const app::AppConfig& /*config*/) {
    m_lastMousePt = clientPt;
    if (m_draggingNodeIdx >= 0 && static_cast<size_t>(m_draggingNodeIdx) < m_nodes.size()) {
        D2D1_POINT_2F wp = ScreenToWorld(clientPt);
        m_nodes[static_cast<size_t>(m_draggingNodeIdx)].x = wp.x - m_nodeDragOffsetX;
        m_nodes[static_cast<size_t>(m_draggingNodeIdx)].y = wp.y - m_nodeDragOffsetY;
        if (static_cast<size_t>(m_draggingNodeIdx) < m_targetNodes.size()) {
            m_targetNodes[static_cast<size_t>(m_draggingNodeIdx)] = m_nodes[static_cast<size_t>(m_draggingNodeIdx)];
        }
        m_isAnimatingNodes = false;
        m_userMovedNodes = true;
        m_routesDirty = true;
        return true;
    }
    if (m_isPanningCanvas) {
        const float dx = static_cast<float>(clientPt.x - m_lastDragPt.x);
        const float dy = static_cast<float>(clientPt.y - m_lastDragPt.y);
        m_panX += dx;
        m_panY += dy;
        m_targetPanX = m_panX;
        m_targetPanY = m_panY;
        m_isAnimatingZoomPan = false;
        m_lastDragPt = clientPt;
        return true;
    }

    int h = -1;
    if (PtInRectF(clientPt, m_rcBtnCrowsFoot)) h = 1;
    else if (PtInRectF(clientPt, m_rcBtnUml)) h = 2;
    else if (PtInRectF(clientPt, m_rcBtnChen)) h = 3;
    else if (PtInRectF(clientPt, m_rcBtnBarker)) h = 4;
    else if (PtInRectF(clientPt, m_rcBtnChenAttrs)) h = 5;
    else if (PtInRectF(clientPt, m_rcBtnAutoLayout)) h = 6;
    else if (PtInRectF(clientPt, m_rcBtnZoomFit)) h = 7;
    else if (PtInRectF(clientPt, m_rcBtnRefresh)) h = 8;
    else if (PtInRectF(clientPt, m_rcBtnToggleDiff)) h = 9;
    else if (PtInRectF(clientPt, m_rcBtnLoadSql)) h = 10;
    else if (PtInRectF(clientPt, m_rcBtnExportSvg)) h = 11;
    else if (PtInRectF(clientPt, m_rcBtnOpenMigConsole)) h = 12;
    else if (PtInRectF(clientPt, m_rcBtnCopyMigration)) h = 13;
    else if (PtInRectF(clientPt, m_rcBtnDetect)) h = 14;
    else if (PtInRectF(clientPt, m_rcBtnEmptyRefresh)) h = 15;
    else if (PtInRectF(clientPt, m_rcBtnInferFks)) h = 16;
    else if (PtInRectF(clientPt, m_rcBtnShowM2N)) h = 17;
    else if (PtInRectF(clientPt, m_rcBtnLegend)) h = 18;

    int hp = -1;
    for (size_t i = 0; i < m_profileHits.size(); ++i) {
        if (PtInRectF(clientPt, m_profileHits[i].bounds)) { hp = static_cast<int>(i); break; }
    }
    int hdb = -1;
    for (size_t i = 0; i < m_dbHits.size(); ++i) {
        if (PtInRectF(clientPt, m_dbHits[i].bounds)) { hdb = static_cast<int>(i); break; }
    }
    int hddb = -1;
    for (size_t i = 0; i < m_diffDbHits.size(); ++i) {
        if (PtInRectF(clientPt, m_diffDbHits[i].bounds)) { hddb = static_cast<int>(i); break; }
    }
    bool hSearch = PtInRectF(clientPt, m_rcSearchBox);
    int hNode = -1;
    int hEdge = -1;
    if (h < 0 && hp < 0 && hdb < 0 && hddb < 0 && !hSearch && PtInRectF(clientPt, m_rcCanvasViewport)) {
        D2D1_POINT_2F wp = ScreenToWorld(clientPt);
        for (int i = static_cast<int>(m_nodes.size()) - 1; i >= 0; --i) {
            const auto& n = m_nodes[static_cast<size_t>(i)];
            if (wp.x >= n.x && wp.x <= n.x + n.width && wp.y >= n.y && wp.y <= n.y + n.height) {
                hNode = i;
                break;
            }
        }
        // Hit-test routed edges in world space when cursor is over open canvas corridor
        if (hNode < 0 && !m_routedEdges.empty()) {
            const float maxDist = 8.0f / (std::max)(0.25f, m_zoom);
            float bestDistSq = maxDist * maxDist;
            for (size_t ei = 0; ei < m_routedEdges.size(); ++ei) {
                const auto& edge = m_routedEdges[ei];
                for (size_t s = 0; s + 1 < edge.points.size(); ++s) {
                    const float d2 = DistPointToSegmentSq(wp.x, wp.y,
                                                          edge.points[s].x, edge.points[s].y,
                                                          edge.points[s + 1].x, edge.points[s + 1].y);
                    if (d2 < bestDistSq) {
                        bestDistSq = d2;
                        hEdge = static_cast<int>(ei);
                    }
                }
            }
        }
    }

    const bool changed = (h != m_hoveredBtnId || hp != m_hoveredProfileIdx ||
                          hdb != m_hoveredDbIdx || hddb != m_hoveredDiffDbIdx ||
                          hSearch != m_hoveredSearchBox || hNode != m_hoveredNodeIdx ||
                          hEdge != m_hoveredEdgeIdx || hEdge >= 0);
    m_hoveredBtnId = h;
    m_hoveredProfileIdx = hp;
    m_hoveredDbIdx = hdb;
    m_hoveredDiffDbIdx = hddb;
    m_hoveredSearchBox = hSearch;
    m_hoveredNodeIdx = hNode;
    m_hoveredEdgeIdx = hEdge;
    return changed;
}

LPCWSTR ErdView::GetCursorForPoint(POINT clientPt) const noexcept {
    if (m_draggingNodeIdx >= 0 || m_isPanningCanvas) return IDC_SIZEALL;
    if (PtInRectF(clientPt, m_rcSearchBox)) return IDC_IBEAM;
    if (PtInRectF(clientPt, m_rcBtnCrowsFoot) || PtInRectF(clientPt, m_rcBtnUml) ||
        PtInRectF(clientPt, m_rcBtnChen) || PtInRectF(clientPt, m_rcBtnBarker) ||
        PtInRectF(clientPt, m_rcBtnChenAttrs) || PtInRectF(clientPt, m_rcBtnAutoLayout) ||
        PtInRectF(clientPt, m_rcBtnZoomFit) || PtInRectF(clientPt, m_rcBtnRefresh) ||
        PtInRectF(clientPt, m_rcBtnDetect) || PtInRectF(clientPt, m_rcBtnEmptyRefresh) ||
        PtInRectF(clientPt, m_rcBtnToggleDiff) || PtInRectF(clientPt, m_rcBtnLoadSql) ||
        PtInRectF(clientPt, m_rcBtnExportSvg) || PtInRectF(clientPt, m_rcBtnInferFks) ||
        PtInRectF(clientPt, m_rcBtnShowM2N) || PtInRectF(clientPt, m_rcBtnLegend) ||
        PtInRectF(clientPt, m_rcBtnOpenMigConsole) || PtInRectF(clientPt, m_rcBtnCopyMigration)) {
        return IDC_HAND;
    }
    for (const auto& hit : m_profileHits) {
        if (PtInRectF(clientPt, hit.bounds)) return IDC_HAND;
    }
    for (const auto& hit : m_dbHits) {
        if (PtInRectF(clientPt, hit.bounds)) return IDC_HAND;
    }
    for (const auto& hit : m_diffDbHits) {
        if (PtInRectF(clientPt, hit.bounds)) return IDC_HAND;
    }
    if (PtInRectF(clientPt, m_rcCanvasViewport)) {
        D2D1_POINT_2F wp = ScreenToWorld(clientPt);
        for (int i = static_cast<int>(m_nodes.size()) - 1; i >= 0; --i) {
            const auto& n = m_nodes[static_cast<size_t>(i)];
            if (wp.x >= n.x && wp.x <= n.x + n.width && wp.y >= n.y && wp.y <= n.y + n.height) {
                return IDC_SIZEALL;
            }
        }
    }
    return IDC_ARROW;
}

bool ErdView::HandleMouseWheel(short delta, bool /*isCtrlDown*/, POINT clientPt, const app::AppConfig& config) {
    if (m_showDiffPanel && PtInRectF(clientPt, m_rcDiffViewport)) {
        float step = config.ScaleF(36.0f) * (delta > 0 ? -1.0f : 1.0f);
        float base = m_isAnimatingDiffScroll ? m_targetDiffScrollY : m_diffScrollY;
        m_targetDiffScrollY = std::clamp(base + step, 0.0f, m_diffMaxScrollY);
        m_isAnimatingDiffScroll = true;
        return true;
    }
    if (PtInRectF(clientPt, m_rcCanvasViewport)) {
        // Compute world anchor relative to current target pan/zoom so rapid wheel ticks stay centered under cursor
        const float curZ = m_isAnimatingZoomPan ? m_targetZoom : m_zoom;
        const float curPX = m_isAnimatingZoomPan ? m_targetPanX : m_panX;
        const float curPY = m_isAnimatingZoomPan ? m_targetPanY : m_panY;
        const float wx = (static_cast<float>(clientPt.x) - m_rcCanvasViewport.left - curPX) / curZ;
        const float wy = (static_cast<float>(clientPt.y) - m_rcCanvasViewport.top  - curPY) / curZ;
        float factor = (delta > 0) ? 1.12f : 0.89f;
        m_targetZoom = std::clamp(curZ * factor, 0.25f, 2.5f);
        m_targetPanX = static_cast<float>(clientPt.x) - m_rcCanvasViewport.left - wx * m_targetZoom;
        m_targetPanY = static_cast<float>(clientPt.y) - m_rcCanvasViewport.top  - wy * m_targetZoom;
        m_isAnimatingZoomPan = true;
        return true;
    }
    return false;
}

bool ErdView::HandleChar(wchar_t ch) {
    if (!m_searchFocused) return false;
    if (ch >= 32 && ch != 127 && m_searchFilter.size() < 64) {
        m_searchFilter.push_back(ch);
        return true;
    }
    return false;
}

bool ErdView::HandleKeyDown(WPARAM vk) {
    if (!m_searchFocused) return false;
    if (vk == VK_BACK && !m_searchFilter.empty()) {
        m_searchFilter.pop_back();
        return true;
    }
    if (vk == VK_ESCAPE) {
        m_searchFocused = false;
        return true;
    }
    return false;
}

} // namespace phide::ui
