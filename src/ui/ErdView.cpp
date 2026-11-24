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

enum class ErdIconKind {
    None = 0,
    Database,
    AutoLayout,
    ZoomFit,
    Refresh,
    SchemaDiff,
    Code,
    Layers,
    Info,
    ImportSql,
    ExportSvg,
    Search,
    ChevronDown,
    CloseSmall
};

/** @brief Renders crisp flat 16x16-grid Direct2D vector icons with zero bitmap blur or gloss. */
static void DrawErdVectorIcon(ID2D1RenderTarget* pRT, ErdIconKind kind, const D2D1_RECT_F& rc,
                              ID2D1Brush* br, ID2D1StrokeStyle* strokeStyle = nullptr, float strokeW = 1.35f) {
    if (!pRT || !br || kind == ErdIconKind::None) return;
    const float w = rc.right - rc.left;
    const float h = rc.bottom - rc.top;
    if (w <= 2.0f || h <= 2.0f) return;
    const float s = (std::min)(w, h) / 16.0f;
    const float ox = rc.left + (w - 16.0f * s) * 0.5f;
    const float oy = rc.top  + (h - 16.0f * s) * 0.5f;
    auto pt = [&](float gx, float gy) -> D2D1_POINT_2F {
        return D2D1::Point2F(ox + gx * s, oy + gy * s);
    };
    auto ln = [&](float x1, float y1, float x2, float y2, float swMul = 1.0f) {
        pRT->DrawLine(pt(x1, y1), pt(x2, y2), br, strokeW * swMul, strokeStyle);
    };

    switch (kind) {
    case ErdIconKind::Database: {
        D2D1_ELLIPSE topEl = D2D1::Ellipse(pt(8.0f, 4.0f), 5.2f * s, 1.9f * s);
        pRT->DrawEllipse(topEl, br, strokeW);
        ln(2.8f, 4.0f, 2.8f, 12.0f);
        ln(13.2f, 4.0f, 13.2f, 12.0f);
        ComPtr<ID2D1Factory> fac;
        pRT->GetFactory(&fac);
        auto drawArcBand = [&](float by) {
            if (!fac) { ln(2.8f, by, 13.2f, by); return; }
            ComPtr<ID2D1PathGeometry> geo;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(fac->CreatePathGeometry(&geo)) && SUCCEEDED(geo->Open(&sink))) {
                sink->BeginFigure(pt(2.8f, by), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddArc(D2D1::ArcSegment(pt(13.2f, by), D2D1::SizeF(5.2f * s, 1.9f * s),
                                              0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                pRT->DrawGeometry(geo.Get(), br, strokeW, strokeStyle);
            }
        };
        drawArcBand(8.0f);
        drawArcBand(12.0f);
        break;
    }
    case ErdIconKind::AutoLayout: {
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ox + 2.0f * s, oy + 2.5f * s, ox + 7.2f * s, oy + 6.5f * s), 1.2f * s, 1.2f * s), br, strokeW);
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ox + 9.2f * s, oy + 2.5f * s, ox + 14.0f * s, oy + 6.5f * s), 1.2f * s, 1.2f * s), br, strokeW);
        pRT->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(ox + 5.2f * s, oy + 9.5f * s, ox + 10.8f * s, oy + 13.5f * s), 1.2f * s, 1.2f * s), br, strokeW);
        ln(7.2f, 4.5f, 9.2f, 4.5f);
        ln(8.0f, 6.5f, 8.0f, 9.5f);
        break;
    }
    case ErdIconKind::ZoomFit: {
        ln(2.5f, 5.5f, 2.5f, 2.5f); ln(2.5f, 2.5f, 5.5f, 2.5f);
        ln(10.5f, 2.5f, 13.5f, 2.5f); ln(13.5f, 2.5f, 13.5f, 5.5f);
        ln(2.5f, 10.5f, 2.5f, 13.5f); ln(2.5f, 13.5f, 5.5f, 13.5f);
        ln(13.5f, 10.5f, 13.5f, 13.5f); ln(13.5f, 13.5f, 10.5f, 13.5f);
        break;
    }
    case ErdIconKind::Refresh: {
        ComPtr<ID2D1Factory> fac;
        pRT->GetFactory(&fac);
        if (fac) {
            ComPtr<ID2D1PathGeometry> geo;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(fac->CreatePathGeometry(&geo)) && SUCCEEDED(geo->Open(&sink))) {
                sink->BeginFigure(pt(12.8f, 5.4f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddArc(D2D1::ArcSegment(pt(13.1f, 9.8f), D2D1::SizeF(5.2f * s, 5.2f * s),
                                              0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                pRT->DrawGeometry(geo.Get(), br, strokeW, strokeStyle);
            }
        }
        ln(13.0f, 2.8f, 13.0f, 5.8f);
        ln(10.0f, 5.8f, 13.0f, 5.8f);
        break;
    }
    case ErdIconKind::SchemaDiff: {
        pRT->DrawEllipse(D2D1::Ellipse(pt(4.2f, 4.0f), 1.6f * s, 1.6f * s), br, strokeW);
        pRT->DrawEllipse(D2D1::Ellipse(pt(4.2f, 12.0f), 1.6f * s, 1.6f * s), br, strokeW);
        pRT->DrawEllipse(D2D1::Ellipse(pt(11.8f, 8.0f), 1.6f * s, 1.6f * s), br, strokeW);
        ln(4.2f, 5.6f, 4.2f, 10.4f);
        ln(5.8f, 4.5f, 10.4f, 7.2f);
        break;
    }
    case ErdIconKind::Code: {
        ln(5.2f, 4.5f, 2.2f, 8.0f); ln(2.2f, 8.0f, 5.2f, 11.5f);
        ln(10.8f, 4.5f, 13.8f, 8.0f); ln(13.8f, 8.0f, 10.8f, 11.5f);
        ln(9.2f, 3.5f, 6.8f, 12.5f);
        break;
    }
    case ErdIconKind::Layers: {
        ln(8.0f, 2.4f, 13.6f, 5.2f); ln(13.6f, 5.2f, 8.0f, 8.0f);
        ln(8.0f, 8.0f, 2.4f, 5.2f); ln(2.4f, 5.2f, 8.0f, 2.4f);
        ln(2.4f, 8.2f, 8.0f, 11.0f); ln(8.0f, 11.0f, 13.6f, 8.2f);
        ln(2.4f, 11.0f, 8.0f, 13.8f); ln(8.0f, 13.8f, 13.6f, 11.0f);
        break;
    }
    case ErdIconKind::Info: {
        pRT->DrawEllipse(D2D1::Ellipse(pt(8.0f, 8.0f), 5.6f * s, 5.6f * s), br, strokeW);
        ln(8.0f, 7.5f, 8.0f, 11.2f);
        pRT->FillEllipse(D2D1::Ellipse(pt(8.0f, 5.1f), 0.9f * s, 0.9f * s), br);
        break;
    }
    case ErdIconKind::ImportSql: {
        ln(8.0f, 2.4f, 8.0f, 9.8f);
        ln(5.3f, 7.3f, 8.0f, 10.1f); ln(10.7f, 7.3f, 8.0f, 10.1f);
        ln(2.8f, 10.5f, 2.8f, 13.2f); ln(2.8f, 13.2f, 13.2f, 13.2f); ln(13.2f, 13.2f, 13.2f, 10.5f);
        break;
    }
    case ErdIconKind::ExportSvg: {
        ln(8.0f, 9.8f, 8.0f, 2.4f);
        ln(5.3f, 5.1f, 8.0f, 2.4f); ln(10.7f, 5.1f, 8.0f, 2.4f);
        ln(2.8f, 10.5f, 2.8f, 13.2f); ln(2.8f, 13.2f, 13.2f, 13.2f); ln(13.2f, 13.2f, 13.2f, 10.5f);
        break;
    }
    case ErdIconKind::Search: {
        pRT->DrawEllipse(D2D1::Ellipse(pt(6.8f, 6.8f), 4.2f * s, 4.2f * s), br, strokeW);
        ln(10.0f, 10.0f, 13.5f, 13.5f);
        break;
    }
    case ErdIconKind::ChevronDown: {
        ln(4.2f, 6.0f, 8.0f, 10.0f);
        ln(8.0f, 10.0f, 11.8f, 6.0f);
        break;
    }
    case ErdIconKind::CloseSmall: {
        ln(4.5f, 4.5f, 11.5f, 11.5f);
        ln(11.5f, 4.5f, 4.5f, 11.5f);
        break;
    }
    default: break;
    }
}

static void DrawBtn(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                    const D2D1_RECT_F& rc, const wchar_t* lbl, bool active, bool hovered,
                    ID2D1Brush* brNormal, ID2D1Brush* brHover, ID2D1Brush* brBorder,
                    IDWriteFactory* dwf = nullptr, ErdIconKind icon = ErdIconKind::None,
                    ID2D1StrokeStyle* iconStroke = nullptr) {
    if (rc.right <= rc.left + 2.0f) return;
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rc, config.ScaleF(4.0f), config.ScaleF(4.0f));
    ID2D1Brush* fill = active ? theme.brushBrandAccent.Get() : (hovered ? brHover : brNormal);
    if (fill) pRT->FillRoundedRectangle(rrc, fill);
    if (brBorder) pRT->DrawRoundedRectangle(rrc, active ? theme.brushBrandAccent.Get() : brBorder, 1.0f);
    IDWriteTextFormat* pFmt = ResolveErdBtnFormat(theme);
    ID2D1Brush* txt = active
        ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
        : theme.brushTextPrimary.Get();
    if (icon != ErdIconKind::None && txt) {
        const float iconSz = config.ScaleF(13.0f);
        const float padL = config.ScaleF(8.0f);
        const float cy = (rc.top + rc.bottom) * 0.5f;
        D2D1_RECT_F rcIco = D2D1::RectF(rc.left + padL, cy - iconSz * 0.5f,
                                        rc.left + padL + iconSz, cy + iconSz * 0.5f);
        DrawErdVectorIcon(pRT, icon, rcIco, txt, iconStroke, 1.3f);
        if (pFmt && lbl) {
            D2D1_RECT_F rcTxt = D2D1::RectF(rcIco.right + config.ScaleF(4.0f), rc.top,
                                            rc.right - config.ScaleF(6.0f), rc.bottom);
            DrawSmallText(pRT, pFmt, lbl, rcTxt, txt, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
        }
    } else if (pFmt && txt && lbl) {
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
        m_needsInitialZoomFit = true;
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

void ErdView::EnsureCardTextFormats() {
    IDWriteFactory* dwf = EnsureErdDWriteFactory(m_dwFactory);
    if (!dwf) return;
    if (!m_cardTitleFmt) {
        dwf->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                              12.5f, L"en-us", &m_cardTitleFmt);
        if (m_cardTitleFmt) {
            m_cardTitleFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    if (!m_cardMonoFmt) {
        dwf->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                              11.5f, L"en-us", &m_cardMonoFmt);
        if (m_cardMonoFmt) {
            m_cardMonoFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    if (!m_cardSmallFmt) {
        dwf->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                              10.5f, L"en-us", &m_cardSmallFmt);
        if (m_cardSmallFmt) {
            m_cardSmallFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
}

void ErdView::RecalculateNodeHeights() {
    EnsureCardTextFormats();
    IDWriteFactory* dwf = m_dwFactory.Get();
    const auto interp = ErdAndMigrationEngine::InterpretSchema(m_tables, m_inferImplicitFks);
    for (size_t i = 0; i < m_nodes.size() && i < m_tables.size(); ++i) {
        const auto& tbl = m_tables[i];
        const size_t visCols = (std::min)(tbl.columns.size(), size_t(14));
        const float headerH = (m_notation == ErdNotation::Uml) ? 42.0f : 32.0f;

        float maxNameW = 86.0f;
        float maxTypeW = 56.0f;
        for (size_t ci = 0; ci < visCols; ++ci) {
            const auto& col = tbl.columns[ci];
            maxNameW = (std::max)(maxNameW, MeasureErdTextWidth(dwf, m_cardMonoFmt.Get(), col.name, 6.8f) + 6.0f);
            std::wstring compactT = FormatCompactSqlType(col.type);
            if (col.isUnique && !col.isPrimaryKey && IsFkCol(tbl, col.name)) compactT += L" UQ";
            maxTypeW = (std::max)(maxTypeW, MeasureErdTextWidth(dwf, m_cardMonoFmt.Get(), compactT, 6.8f) + 6.0f);
        }
        const float clampedNameW = std::clamp(maxNameW, 88.0f, 196.0f);
        const float clampedTypeW = std::clamp(maxTypeW, 56.0f, 128.0f);
        const bool showRoleBadge = (i < interp.entities.size() && !interp.entities[i].badgeLabel.empty() &&
                                    (m_showManyToManyBadges || interp.entities[i].role != phide::db::ErdEntityRole::Junction));
        const float badgeSpace = showRoleBadge ? 48.0f : 0.0f;
        const float measuredTitleW = MeasureErdTextWidth(dwf, m_cardTitleFmt.Get(), tbl.name, 7.0f) + 26.0f + badgeSpace;
        m_nodes[i].width = std::clamp((std::max)(36.0f + clampedNameW + 16.0f + clampedTypeW + 12.0f, measuredTitleW),
                                      252.0f, 386.0f);

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

void ErdView::ApplyZoomToFit(bool animate, float maxZoom) {
    const auto& fitNodes = (m_isAnimatingNodes && m_targetNodes.size() == m_nodes.size()) ? m_targetNodes : m_nodes;
    if (fitNodes.empty()) return;
    float minX = fitNodes[0].x, minY = fitNodes[0].y;
    float maxX = fitNodes[0].x + fitNodes[0].width, maxY = fitNodes[0].y + fitNodes[0].height;
    for (const auto& n : fitNodes) {
        minX = (std::min)(minX, n.x);
        minY = (std::min)(minY, n.y);
        maxX = (std::max)(maxX, n.x + n.width);
        maxY = (std::max)(maxY, n.y + n.height);
    }
    const float vw = (std::max)(100.0f, m_rcCanvasViewport.right - m_rcCanvasViewport.left - 56.0f);
    const float vh = (std::max)(100.0f, m_rcCanvasViewport.bottom - m_rcCanvasViewport.top - 52.0f);
    const float bw = (std::max)(1.0f, maxX - minX);
    const float bh = (std::max)(1.0f, maxY - minY);
    m_targetZoom = std::clamp((std::min)(vw / bw, vh / bh), 0.25f, maxZoom);
    m_targetPanX = 28.0f + (std::max)(0.0f, (vw - bw * m_targetZoom) * 0.5f) - minX * m_targetZoom;
    m_targetPanY = 24.0f + (std::max)(0.0f, (vh - bh * m_targetZoom) * 0.5f) - minY * m_targetZoom;
    if (animate) {
        m_isAnimatingZoomPan = true;
    } else {
        m_zoom = m_targetZoom;
        m_panX = m_targetPanX;
        m_panY = m_targetPanY;
        m_isAnimatingZoomPan = false;
    }
}

bool ErdView::IsAnimating() const noexcept {
    return m_isAnimatingZoomPan || m_isAnimatingNodes || m_isAnimatingDiffScroll || m_isConnectionBusy;
}

bool ErdView::UpdateAnimation() noexcept {
    bool active = false;
    constexpr float kPanZoomSpeed = 0.24f;
    constexpr float kNodeSpeed = 0.22f;

    if (m_isConnectionBusy) {
        m_loadingSpinnerDeg = std::fmod(m_loadingSpinnerDeg + 11.0f, 360.0f);
        active = true;
    }

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

    EnsureCardTextFormats();
    const bool dark = theme.IsDarkMode();
    ComPtr<ID2D1SolidColorBrush> brBg, brGrid, brToolbarBg, brRowSep, brSegBg, brCard, brCardHdr,
                                  brBorder, brAccent, brBtnBg, brBtnHover, brMenuBg,
                                  brPk, brFk, brRel, brAdd, brMod, brDel, brDim;
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x0F1218) : D2D1::ColorF(0xF3F5F9), &brBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x181D27) : D2D1::ColorF(0xE2E6EE), &brGrid);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x141821) : D2D1::ColorF(0xF4F6FA), &brToolbarBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x232A38) : D2D1::ColorF(0xDCE3EE), &brRowSep);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x1B212E) : D2D1::ColorF(0xE8EEF6), &brSegBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x181C24) : D2D1::ColorF(0xFFFFFF), &brCard);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x212836) : D2D1::ColorF(0xE6ECF5), &brCardHdr);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x2E3748) : D2D1::ColorF(0xBCC4D4), &brBorder);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x38BDF8) : D2D1::ColorF(0x0284C7), &brRel);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x1D2330) : D2D1::ColorF(0xFFFFFF), &brBtnBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x283042) : D2D1::ColorF(0xE2E8F0), &brBtnHover);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x1A202C) : D2D1::ColorF(0xFFFFFF), &brMenuBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0xFBBF24) : D2D1::ColorF(0xB45309), &brPk);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x38BDF8) : D2D1::ColorF(0x0369A1), &brFk);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x22C55E) : D2D1::ColorF(0x15803D), &brAdd);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0xF59E0B) : D2D1::ColorF(0xB45309), &brMod);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0xEF4444) : D2D1::ColorF(0xB91C1C), &brDel);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x1E293B, 0.9f) : D2D1::ColorF(0xEFF6FF, 0.95f), &brDim);
    brAccent = theme.brushBrandAccent;

    ComPtr<ID2D1Factory> pRtFactory;
    pRT->GetFactory(&pRtFactory);
    if (!m_erdStrokeStyle && pRtFactory) {
        D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
            D2D1_LINE_JOIN_ROUND);
        pRtFactory->CreateStrokeStyle(sp, nullptr, 0, &m_erdStrokeStyle);
    }

    IDWriteFactory* dwf = EnsureErdDWriteFactory(m_dwFactory);
    IDWriteTextFormat* pBtnFmt = ResolveErdBtnFormat(theme);
    IDWriteTextFormat* pUiFmt = ResolveErdUiFormat(theme);
    IDWriteTextFormat* pTitleFmt = m_cardTitleFmt ? m_cardTitleFmt.Get() : (theme.fontTitle ? theme.fontTitle.Get() : pUiFmt);
    IDWriteTextFormat* pCardMonoFmt = m_cardMonoFmt ? m_cardMonoFmt.Get() : (theme.fontCode ? theme.fontCode.Get() : pUiFmt);
    IDWriteTextFormat* pCardSmallFmt = m_cardSmallFmt ? m_cardSmallFmt.Get() : pUiFmt;
    IDWriteTextFormat* pCodeFmt = theme.fontCode ? theme.fontCode.Get() : pCardMonoFmt;
    ID2D1Brush* txtPri = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get();
    ID2D1Brush* txtSec = theme.brushTextSecondary ? theme.brushTextSecondary.Get() : txtPri;
    ID2D1Brush* txtMuted = theme.brushTextMuted ? theme.brushTextMuted.Get() : txtSec;
    ID2D1Brush* txtWhite = theme.brushTextWhite ? theme.brushTextWhite.Get() : txtPri;

    // 1. Flat 2-Row Toolbar Layout (Row 1: Title + Segmented Notations + Server/DB; Row 2: Flat Action Buttons + Search)
    const float pad = config.ScaleF(12.0f);
    const float btnH = config.ScaleF(28.0f);
    const float pillH = config.ScaleF(26.0f);
    const float rowPadY = config.ScaleF(7.0f);
    const float itemGap = config.ScaleF(5.0f);
    const float maxR = canvasLeft + canvasWidth - pad;

    m_profileHits.clear();
    m_dbHits.clear();
    m_diffDbHits.clear();
    m_rcBtnCrowsFoot = m_rcBtnUml = m_rcBtnChen = m_rcBtnBarker = m_rcBtnChenAttrs = {};
    m_rcBtnAutoLayout = m_rcBtnZoomFit = m_rcBtnRefresh = m_rcBtnQuickRefresh = {};
    m_rcBtnServerPicker = m_rcBtnDbPicker = {};
    m_rcServerMenuPopup = m_rcDbMenuPopup = m_rcServerDetectMenuItem = {};
    m_rcBtnDetect = m_rcBtnEmptyRefresh = {};
    m_rcBtnToggleDiff = m_rcBtnLoadSql = m_rcBtnExportSvg = m_rcBtnInferFks = m_rcBtnShowM2N = m_rcBtnLegend = {};
    m_rcSearchBox = m_rcSearchClear = {};

    auto measureIconBtnW = [&](const wchar_t* lbl, bool hasIcon = true) -> float {
        float textW = MeasureErdTextWidth(dwf, pBtnFmt, lbl ? lbl : L"");
        float extra = hasIcon ? config.ScaleF(34.0f) : config.ScaleF(20.0f);
        return (std::max)(config.ScaleF(56.0f), textW + extra);
    };
    auto measureSegW = [&](const wchar_t* lbl) -> float {
        return (std::max)(config.ScaleF(50.0f), MeasureErdTextWidth(dwf, pBtnFmt, lbl ? lbl : L"") + config.ScaleF(20.0f));
    };

    // --- Row 1 Geometry ---
    float row1Y = topY + rowPadY;
    float x1 = canvasLeft + pad;
    const float dbIconSz = config.ScaleF(16.0f);
    D2D1_RECT_F rcTitleIcon = D2D1::RectF(x1, row1Y + (btnH - dbIconSz) * 0.5f,
                                          x1 + dbIconSz, row1Y + (btnH + dbIconSz) * 0.5f);
    x1 += dbIconSz + config.ScaleF(7.0f);

    std::wstring hdr = L"ER Diagram (" + std::to_wstring(m_tables.size()) + L" tables)";
    const float hdrW = (std::min)(maxR - x1, MeasureErdTextWidth(dwf, pTitleFmt, hdr) + config.ScaleF(6.0f));
    D2D1_RECT_F rcHdrTitle = D2D1::RectF(x1, row1Y, x1 + hdrW, row1Y + btnH);
    x1 += hdrW + config.ScaleF(12.0f);

    struct SegItem {
        D2D1_RECT_F* outRc;
        const wchar_t* label;
        int id;
        bool active;
        float w;
        D2D1_RECT_F placed{};
    };
    std::vector<SegItem> segItems = {
        { &m_rcBtnCrowsFoot, L"Crow's Foot", 1, m_notation == ErdNotation::CrowsFoot, measureSegW(L"Crow's Foot") },
        { &m_rcBtnUml,       L"UML",         2, m_notation == ErdNotation::Uml,       measureSegW(L"UML") },
        { &m_rcBtnChen,      L"Chen",        3, m_notation == ErdNotation::Chen,      measureSegW(L"Chen") },
        { &m_rcBtnBarker,    L"Barker",      4, m_notation == ErdNotation::Barker,    measureSegW(L"Barker") },
    };
    float totalSegW = 0.0f;
    for (const auto& s : segItems) totalSegW += s.w;
    D2D1_RECT_F rcSegGroup{};
    if (x1 + totalSegW <= maxR) {
        rcSegGroup = D2D1::RectF(x1, row1Y, x1 + totalSegW, row1Y + btnH);
        float sx = x1;
        for (auto& s : segItems) {
            s.placed = D2D1::RectF(sx, row1Y, sx + s.w, row1Y + btnH);
            *s.outRc = s.placed;
            sx += s.w;
        }
        x1 += totalSegW + config.ScaleF(6.0f);
    }
    if (m_notation == ErdNotation::Chen) {
        float attrsW = measureIconBtnW(L"Attrs", false);
        if (x1 + attrsW <= maxR) {
            m_rcBtnChenAttrs = D2D1::RectF(x1, row1Y, x1 + attrsW, row1Y + btnH);
            x1 += attrsW + config.ScaleF(8.0f);
        }
    }

    // Right side of Row 1: Server Picker + Database Picker + Quick Refresh Icon Button
    std::wstring activeSrvLabel = L"No Server";
    bool activeSrvConnected = false;
    std::wstring activeDbLabel;
    if (m_selectedProfileIdx < m_profiles.size()) {
        activeSrvLabel = m_profiles[m_selectedProfileIdx].name;
        activeSrvConnected = m_profiles[m_selectedProfileIdx].isConnected;
        activeDbLabel = m_profiles[m_selectedProfileIdx].database;
    } else if (!m_profiles.empty()) {
        activeSrvLabel = m_profiles[0].name;
        activeSrvConnected = m_profiles[0].isConnected;
        activeDbLabel = m_profiles[0].database;
    }
    if (m_isConnectionBusy && m_profiles.empty()) activeSrvLabel = L"Detecting servers...";

    const float quickRefW = btnH;
    const float srvDropW = (std::max)(config.ScaleF(118.0f),
        MeasureErdTextWidth(dwf, pUiFmt, activeSrvLabel) + config.ScaleF(46.0f));
    const bool hasDbDropdown = (m_selectedProfileIdx < m_profiles.size() &&
                                (!m_profiles[m_selectedProfileIdx].availableDatabases.empty() || !activeDbLabel.empty()));
    const std::wstring dbDisp = activeDbLabel.empty() ? L"(default)" : activeDbLabel;
    const float dbDropW = hasDbDropdown
        ? (std::max)(config.ScaleF(74.0f), MeasureErdTextWidth(dwf, pUiFmt, dbDisp) + config.ScaleF(36.0f))
        : 0.0f;
    const float rightConnGap = config.ScaleF(6.0f);
    const float totalConnW = srvDropW + (hasDbDropdown ? (rightConnGap + dbDropW + config.ScaleF(6.0f)) : 0.0f) + rightConnGap + quickRefW;

    float connY = row1Y;
    float connRightX = maxR;
    if (x1 + config.ScaleF(10.0f) + totalConnW > maxR) {
        connY = row1Y + btnH + config.ScaleF(6.0f);
        connRightX = (std::min)(maxR, canvasLeft + pad + totalConnW);
    }
    m_rcBtnQuickRefresh = D2D1::RectF(connRightX - quickRefW, connY, connRightX, connY + btnH);
    float rx = m_rcBtnQuickRefresh.left - rightConnGap;
    if (hasDbDropdown) {
        m_rcBtnDbPicker = D2D1::RectF(rx - dbDropW, connY, rx, connY + btnH);
        rx = m_rcBtnDbPicker.left - config.ScaleF(10.0f);
    }
    m_rcBtnServerPicker = D2D1::RectF((std::max)(canvasLeft + pad, rx - srvDropW), connY, rx, connY + btnH);

    // --- Row 2 Geometry: Flat Action Buttons + Vertical Divider + Filter Search Box ---
    const float sepY = connY + btnH + rowPadY;
    float row2Y = sepY + rowPadY;
    float x2 = canvasLeft + pad;

    struct ActionBtnPlan {
        D2D1_RECT_F* outRc;
        const wchar_t* label;
        int id;
        bool active;
        ErdIconKind icon;
        float w;
        D2D1_RECT_F placed{};
    };
    std::vector<ActionBtnPlan> row2Btns = {
        { &m_rcBtnAutoLayout, L"Auto Layout", 6,  true,                    ErdIconKind::AutoLayout, measureIconBtnW(L"Auto Layout") },
        { &m_rcBtnZoomFit,    L"Zoom Fit",    7,  false,                   ErdIconKind::ZoomFit,    measureIconBtnW(L"Zoom Fit") },
        { &m_rcBtnRefresh,    L"Refresh",     8,  false,                   ErdIconKind::Refresh,    measureIconBtnW(L"Refresh") },
        { &m_rcBtnToggleDiff, L"Schema Diff", 9,  m_showDiffPanel,         ErdIconKind::SchemaDiff, measureIconBtnW(L"Schema Diff") },
        { &m_rcBtnInferFks,   L"Infer *_id",  16, m_inferImplicitFks,      ErdIconKind::Code,       measureIconBtnW(L"Infer *_id") },
        { &m_rcBtnShowM2N,    L"M:N Badges",  17, m_showManyToManyBadges,  ErdIconKind::Layers,     measureIconBtnW(L"M:N Badges") },
        { &m_rcBtnLegend,     L"Legend",      18, m_showLegend,            ErdIconKind::Info,       measureIconBtnW(L"Legend") },
        { &m_rcBtnLoadSql,    L"Load .sql",   10, false,                   ErdIconKind::ImportSql,  measureIconBtnW(L"Load .sql") },
        { &m_rcBtnExportSvg,  L"Export SVG",  11, false,                   ErdIconKind::ExportSvg,  measureIconBtnW(L"Export SVG") },
    };

    const float desiredSearchW = config.ScaleF(186.0f);
    const float minSearchReserve = config.ScaleF(164.0f);
    for (auto& b : row2Btns) {
        if (x2 + b.w > maxR && x2 > canvasLeft + pad) {
            x2 = canvasLeft + pad;
            row2Y += btnH + config.ScaleF(6.0f);
        }
        if (x2 + b.w <= maxR) {
            b.placed = D2D1::RectF(x2, row2Y, x2 + b.w, row2Y + btnH);
            *b.outRc = b.placed;
            x2 += b.w + itemGap;
        }
    }

    float searchDividerX = -1.0f;
    if (x2 + minSearchReserve <= maxR) {
        const float sw = (std::min)(desiredSearchW, maxR - x2 - config.ScaleF(14.0f));
        m_rcSearchBox = D2D1::RectF(maxR - sw, row2Y, maxR, row2Y + btnH);
        searchDividerX = m_rcSearchBox.left - config.ScaleF(8.0f);
    } else {
        row2Y += btnH + config.ScaleF(6.0f);
        m_rcSearchBox = D2D1::RectF(canvasLeft + pad, row2Y,
                                    (std::min)(maxR, canvasLeft + pad + config.ScaleF(240.0f)),
                                    row2Y + btnH);
    }

    const float barBottom = row2Y + btnH + rowPadY;

    // --- Paint Flat Toolbar Background & Dividers ---
    D2D1_RECT_F rcBar = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, barBottom);
    if (brToolbarBg) pRT->FillRectangle(rcBar, brToolbarBg.Get());
    pRT->DrawLine(D2D1::Point2F(canvasLeft, sepY), D2D1::Point2F(canvasLeft + canvasWidth, sepY),
                  brRowSep.Get(), 1.0f);
    pRT->DrawLine(D2D1::Point2F(canvasLeft, rcBar.bottom), D2D1::Point2F(canvasLeft + canvasWidth, rcBar.bottom),
                  brBorder.Get(), 1.0f);

    // Draw Row 1 Title + Segmented Notations
    DrawErdVectorIcon(pRT, ErdIconKind::Database, rcTitleIcon, txtSec, m_erdStrokeStyle.Get(), 1.4f);
    if (pTitleFmt && txtPri) {
        DrawSmallText(pRT, pTitleFmt, hdr, rcHdrTitle, txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
    }
    if (rcSegGroup.right > rcSegGroup.left) {
        D2D1_ROUNDED_RECT rrcSeg = D2D1::RoundedRect(rcSegGroup, config.ScaleF(5.0f), config.ScaleF(5.0f));
        if (brSegBg) pRT->FillRoundedRectangle(rrcSeg, brSegBg.Get());
        pRT->PushAxisAlignedClip(rcSegGroup, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (size_t si = 0; si < segItems.size(); ++si) {
            const auto& seg = segItems[si];
            if (seg.placed.right <= seg.placed.left) continue;
            const bool hov = (m_hoveredBtnId == seg.id);
            if (seg.active && brAccent) {
                pRT->FillRectangle(seg.placed, brAccent.Get());
            } else if (hov && brBtnHover) {
                pRT->FillRectangle(seg.placed, brBtnHover.Get());
            }
            if (si > 0 && brBorder) {
                pRT->DrawLine(D2D1::Point2F(seg.placed.left, seg.placed.top + 3.0f),
                              D2D1::Point2F(seg.placed.left, seg.placed.bottom - 3.0f), brBorder.Get(), 1.0f);
            }
            if (pBtnFmt) {
                DrawSmallText(pRT, pBtnFmt, seg.label, seg.placed, seg.active ? txtWhite : txtPri,
                              DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
            }
        }
        pRT->PopAxisAlignedClip();
        if (brBorder) pRT->DrawRoundedRectangle(rrcSeg, brBorder.Get(), 1.0f);
    }
    if (m_rcBtnChenAttrs.right > m_rcBtnChenAttrs.left) {
        DrawBtn(pRT, theme, config, m_rcBtnChenAttrs, L"Attrs", m_showChenAttributes, m_hoveredBtnId == 5,
                brBtnBg.Get(), brBtnHover.Get(), brBorder.Get(), dwf);
    }

    // Draw Row 1 Right Connection Controls (Server Picker + DB Picker + Quick Refresh)
    {
        const bool srvHov = (m_hoveredBtnId == 19);
        D2D1_ROUNDED_RECT rp = D2D1::RoundedRect(m_rcBtnServerPicker, config.ScaleF(4.0f), config.ScaleF(4.0f));
        pRT->FillRoundedRectangle(rp, (m_serverMenuOpen || srvHov) ? brBtnHover.Get() : brBtnBg.Get());
        pRT->DrawRoundedRectangle(rp, m_serverMenuOpen ? brAccent.Get() : brBorder.Get(), 1.0f);

        const float dotX = m_rcBtnServerPicker.left + config.ScaleF(10.0f);
        const float dotY = (m_rcBtnServerPicker.top + m_rcBtnServerPicker.bottom) * 0.5f;
        const float dotR = config.ScaleF(3.6f);
        ID2D1Brush* dotBr = m_isConnectionBusy ? brMod.Get()
                          : (m_profiles.empty() ? txtMuted : (activeSrvConnected ? brAdd.Get() : brDel.Get()));
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), dotR, dotR), dotBr);

        const float chevSz = config.ScaleF(10.0f);
        D2D1_RECT_F rcSrvChev = D2D1::RectF(m_rcBtnServerPicker.right - config.ScaleF(16.0f), dotY - chevSz * 0.5f,
                                            m_rcBtnServerPicker.right - config.ScaleF(6.0f), dotY + chevSz * 0.5f);
        DrawErdVectorIcon(pRT, ErdIconKind::ChevronDown, rcSrvChev, txtSec, m_erdStrokeStyle.Get(), 1.25f);
        DrawSmallText(pRT, pUiFmt, activeSrvLabel,
                      D2D1::RectF(m_rcBtnServerPicker.left + config.ScaleF(18.0f), m_rcBtnServerPicker.top,
                                  rcSrvChev.left - config.ScaleF(2.0f), m_rcBtnServerPicker.bottom),
                      txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
    }
    if (hasDbDropdown && m_rcBtnDbPicker.right > m_rcBtnDbPicker.left) {
        const float divX = m_rcBtnServerPicker.right + config.ScaleF(5.0f);
        pRT->DrawLine(D2D1::Point2F(divX, connY + config.ScaleF(5.0f)),
                      D2D1::Point2F(divX, connY + btnH - config.ScaleF(5.0f)), brBorder.Get(), 1.0f);

        const bool dbHov = (m_hoveredBtnId == 20);
        D2D1_ROUNDED_RECT rd = D2D1::RoundedRect(m_rcBtnDbPicker, config.ScaleF(4.0f), config.ScaleF(4.0f));
        pRT->FillRoundedRectangle(rd, (m_dbMenuOpen || dbHov) ? brBtnHover.Get() : brBtnBg.Get());
        pRT->DrawRoundedRectangle(rd, m_dbMenuOpen ? brAccent.Get() : brBorder.Get(), 1.0f);

        const float cy = (m_rcBtnDbPicker.top + m_rcBtnDbPicker.bottom) * 0.5f;
        const float chevSz = config.ScaleF(10.0f);
        D2D1_RECT_F rcDbChev = D2D1::RectF(m_rcBtnDbPicker.right - config.ScaleF(15.0f), cy - chevSz * 0.5f,
                                           m_rcBtnDbPicker.right - config.ScaleF(5.0f), cy + chevSz * 0.5f);
        DrawErdVectorIcon(pRT, ErdIconKind::ChevronDown, rcDbChev, txtSec, m_erdStrokeStyle.Get(), 1.25f);
        DrawSmallText(pRT, pUiFmt, dbDisp,
                      D2D1::RectF(m_rcBtnDbPicker.left + config.ScaleF(8.0f), m_rcBtnDbPicker.top,
                                  rcDbChev.left - config.ScaleF(2.0f), m_rcBtnDbPicker.bottom),
                      txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
    }
    {
        const bool qHov = (m_hoveredBtnId == 21);
        D2D1_ROUNDED_RECT rq = D2D1::RoundedRect(m_rcBtnQuickRefresh, config.ScaleF(4.0f), config.ScaleF(4.0f));
        pRT->FillRoundedRectangle(rq, qHov ? brBtnHover.Get() : brBtnBg.Get());
        pRT->DrawRoundedRectangle(rq, brBorder.Get(), 1.0f);
        const float icoSz = config.ScaleF(14.0f);
        const float cx = (m_rcBtnQuickRefresh.left + m_rcBtnQuickRefresh.right) * 0.5f;
        const float cy = (m_rcBtnQuickRefresh.top + m_rcBtnQuickRefresh.bottom) * 0.5f;
        DrawErdVectorIcon(pRT, ErdIconKind::Refresh,
                          D2D1::RectF(cx - icoSz * 0.5f, cy - icoSz * 0.5f, cx + icoSz * 0.5f, cy + icoSz * 0.5f),
                          txtPri, m_erdStrokeStyle.Get(), 1.35f);
    }

    // Draw Row 2 Action Buttons + Search Box
    for (const auto& b : row2Btns) {
        if (b.placed.right > b.placed.left) {
            DrawBtn(pRT, theme, config, b.placed, b.label, b.active, m_hoveredBtnId == b.id,
                    brBtnBg.Get(), brBtnHover.Get(), brBorder.Get(), dwf, b.icon, m_erdStrokeStyle.Get());
        }
    }
    if (searchDividerX > 0.0f && brBorder) {
        pRT->DrawLine(D2D1::Point2F(searchDividerX, row2Y + config.ScaleF(4.0f)),
                      D2D1::Point2F(searchDividerX, row2Y + btnH - config.ScaleF(4.0f)), brBorder.Get(), 1.0f);
    }
    if (m_rcSearchBox.right > m_rcSearchBox.left) {
        D2D1_ROUNDED_RECT rrcS = D2D1::RoundedRect(m_rcSearchBox, config.ScaleF(4.0f), config.ScaleF(4.0f));
        if (brBtnBg) pRT->FillRoundedRectangle(rrcS, brBtnBg.Get());
        pRT->DrawRoundedRectangle(rrcS, m_searchFocused ? brAccent.Get() : brBorder.Get(), m_searchFocused ? 1.3f : 1.0f);

        const float sIcoSz = config.ScaleF(13.0f);
        const float sCy = (m_rcSearchBox.top + m_rcSearchBox.bottom) * 0.5f;
        D2D1_RECT_F rcSIco = D2D1::RectF(m_rcSearchBox.left + config.ScaleF(8.0f), sCy - sIcoSz * 0.5f,
                                         m_rcSearchBox.left + config.ScaleF(8.0f) + sIcoSz, sCy + sIcoSz * 0.5f);
        DrawErdVectorIcon(pRT, ErdIconKind::Search, rcSIco, txtMuted, m_erdStrokeStyle.Get(), 1.3f);

        float textRightPad = config.ScaleF(8.0f);
        if (!m_searchFilter.empty()) {
            const float clrSz = config.ScaleF(14.0f);
            m_rcSearchClear = D2D1::RectF(m_rcSearchBox.right - clrSz - config.ScaleF(6.0f), sCy - clrSz * 0.5f,
                                          m_rcSearchBox.right - config.ScaleF(6.0f), sCy + clrSz * 0.5f);
            DrawErdVectorIcon(pRT, ErdIconKind::CloseSmall, m_rcSearchClear,
                              m_hoveredSearchClear ? txtPri : txtMuted, m_erdStrokeStyle.Get(), 1.3f);
            textRightPad = clrSz + config.ScaleF(10.0f);
        }

        std::wstring sDisp = m_searchFilter.empty() ? L"Filter tables..." : m_searchFilter;
        DrawSmallText(pRT, pUiFmt, sDisp,
                      D2D1::RectF(rcSIco.right + config.ScaleF(6.0f), m_rcSearchBox.top,
                                  m_rcSearchBox.right - textRightPad, m_rcSearchBox.bottom),
                      m_searchFilter.empty() ? txtMuted : txtPri,
                      DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
    }

    // 2. Main Split Area (ERD Canvas on left, optional Schema Diff & Migration drawer on right)
    const float statusH = config.ScaleF(28.0f);
    const float bodyBot = (std::max)(rcBar.bottom + 20.0f, clientHeight - statusH);
    const float diffW = m_showDiffPanel ? (std::min)(config.ScaleF(410.0f), canvasWidth * 0.42f) : 0.0f;
    m_rcCanvasViewport = D2D1::RectF(canvasLeft, rcBar.bottom, canvasLeft + canvasWidth - diffW, bodyBot);
    m_rcDiffViewport   = m_showDiffPanel
        ? D2D1::RectF(m_rcCanvasViewport.right, rcBar.bottom, canvasLeft + canvasWidth, bodyBot)
        : D2D1_RECT_F{};
    if (m_needsInitialZoomFit && !m_nodes.empty() && !m_userMovedNodes) {
        ApplyZoomToFit(false, 1.0f);
        m_needsInitialZoomFit = false;
    }

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
        if (m_isConnectionBusy) {
            // Skip static empty-state CTA while async schema loading card draws cleanly in pass 2d
        } else {
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

                const wchar_t* ctaLbl = L"Detect Servers & Refresh";
                const float ctaW = (std::max)(config.ScaleF(180.0f), MeasureErdTextWidth(dwf, pBtnFmt, ctaLbl) + config.ScaleF(30.0f));
                m_rcBtnEmptyRefresh = D2D1::RectF(cx - ctaW * 0.5f, cy + config.ScaleF(18.0f),
                                                  cx + ctaW * 0.5f, cy + config.ScaleF(18.0f) + config.ScaleF(32.0f));
                DrawBtn(pRT, theme, config, m_rcBtnEmptyRefresh, ctaLbl, true, m_hoveredBtnId == 15,
                        brBtnBg.Get(), brBtnHover.Get(), brBorder.Get(), dwf);
            }
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
            props.dashStyle = D2D1_DASH_STYLE_CUSTOM;
            float dashes[] = { 4.0f, 3.0f };
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
                    // Child end: always optional (0..1 O| for 1:1, 0..* O< for 1:N) since a parent may have 0 children.
                    DrawCrowsFootGlyph(pRT, x1, y1, dir1, false, edge.childOptional, lineBr, brBg.Get(), lineW);
                    DrawCrowsFootGlyph(pRT, x2, y2, dir2, !edge.isOneToOne, true, lineBr, brBg.Get(), lineW);
                } else if (m_notation == ErdNotation::Barker) {
                    if (!edge.isOneToOne) {
                        const float forkBaseX = x2 + dir2 * 10.0f;
                        pRT->DrawLine(D2D1::Point2F(forkBaseX, y2), D2D1::Point2F(x2, y2 - 6.0f), lineBr, lineW);
                        pRT->DrawLine(D2D1::Point2F(forkBaseX, y2), D2D1::Point2F(x2, y2 + 6.0f), lineBr, lineW);
                    }
                } else if (m_notation == ErdNotation::Uml) {
                    std::wstring pMult = edge.childOptional ? L"0..1" : L"1";
                    std::wstring cMult = edge.isOneToOne ? L"0..1" : L"0..*";
                    DrawSmallText(pRT, pCardSmallFmt, pMult,
                                  D2D1::RectF(x1 + (dir1 > 0 ? 12.0f : -42.0f), y1 - 21.0f, x1 + (dir1 > 0 ? 44.0f : -10.0f), y1 - 3.0f),
                                  txtPri, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                    DrawSmallText(pRT, pCardSmallFmt, cMult,
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

            // Compute non-overlapping name/type column split using crisp non-italic card monospace widths
            const size_t maxVis = (std::min)(tbl.columns.size(), size_t(14));
            float maxTypeW = 52.0f;
            for (size_t ci = 0; ci < maxVis; ++ci) {
                std::wstring compactT = FormatCompactSqlType(tbl.columns[ci].type);
                if (tbl.columns[ci].isUnique && !tbl.columns[ci].isPrimaryKey && IsFkCol(tbl, tbl.columns[ci].name)) {
                    compactT += L" UQ";
                }
                maxTypeW = (std::max)(maxTypeW, MeasureErdTextWidth(dwf, pCardMonoFmt, compactT, 6.8f) + 8.0f);
            }
            const float typeSlotW = std::clamp(maxTypeW, 56.0f, nd.width * 0.42f);
            const float badgeLeft = rcNode.left + 9.0f;
            const float badgeRight = rcNode.left + 30.0f;
            const float nameLeft = rcNode.left + 34.0f;
            const float typeRight = rcNode.right - 10.0f;
            const float typeLeft = (std::max)(nameLeft + 74.0f, typeRight - typeSlotW);
            const float nameRight = typeLeft - 12.0f;

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
                    DrawSmallText(pRT, pCardMonoFmt, badge,
                                  D2D1::RectF(badgeLeft, cy, badgeRight, cy + 19.0f),
                                  colBr, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
                }
                DrawSmallText(pRT, pCardMonoFmt, col.name,
                              D2D1::RectF(nameLeft, cy, nameRight, cy + 19.0f),
                              txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
                std::wstring dispType = FormatCompactSqlType(col.type);
                if (col.isUnique && !col.isPrimaryKey && isFk) dispType += L" UQ";
                DrawSmallText(pRT, pCardMonoFmt, dispType,
                              D2D1::RectF(typeLeft, cy, typeRight, cy + 19.0f),
                              txtSec, DWRITE_TEXT_ALIGNMENT_TRAILING, dwf, true);
                cy += 20.0f;
            }
            if (tbl.columns.size() > maxVis) {
                std::wstring more = L"... +" + std::to_wstring(tbl.columns.size() - maxVis) + L" more";
                DrawSmallText(pRT, pCardSmallFmt, more,
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
            const std::wstring cCard = edge.isOneToOne ? L"0..1" : L"0..*";
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

    // 2c. Optional Cardinality Notation Legend Overlay (bottom-right of canvas, vector glyphs)
    if (m_showLegend && (m_rcCanvasViewport.right - m_rcCanvasViewport.left) > config.ScaleF(300.0f) &&
        (m_rcCanvasViewport.bottom - m_rcCanvasViewport.top) > config.ScaleF(200.0f)) {
        ComPtr<ID2D1Factory> pLegFactory;
        pRT->GetFactory(&pLegFactory);
        ComPtr<ID2D1StrokeStyle> legDashStyle;
        if (pLegFactory) {
            D2D1_STROKE_STYLE_PROPERTIES dp = D2D1::StrokeStyleProperties();
            dp.dashStyle = D2D1_DASH_STYLE_CUSTOM;
            float dashes[] = { 4.0f, 3.0f };
            pLegFactory->CreateStrokeStyle(dp, dashes, 2, &legDashStyle);
        }

        const wchar_t* legTitle =
            (m_notation == ErdNotation::Uml) ? L"UML Multiplicity Guide"
          : (m_notation == ErdNotation::Chen) ? L"Peter Chen Notation Guide"
          : (m_notation == ErdNotation::Barker) ? L"Oracle Barker Notation Guide"
          : L"Crow's Foot Cardinality Guide";

        struct LegEntry {
            int kind; // notation-specific visual row id
            std::wstring desc;
        };
        std::vector<LegEntry> rows;
        if (m_notation == ErdNotation::Uml) {
            rows = {
                { 0, L"1 : 0..1 Composition (NOT NULL Unique FK)" },
                { 1, L"1 : 0..* Mandatory Foreign Key (NOT NULL)" },
                { 2, L"0..1 : 0..* Optional Foreign Key (Nullable)" },
                { 3, L"Public (+) / Foreign Key (#) / Attribute (-)" },
                { 4, L"Inferred *_id relationship (dashed)" }
            };
        } else if (m_notation == ErdNotation::Chen) {
            rows = {
                { 0, L"1 : 1 Relationship between entities" },
                { 1, L"1 : N One-to-Many relationship" },
                { 2, L"Weak Entity (FK inside Primary Key)" },
                { 3, L"Attribute Oval (Amber border = PK)" },
                { 4, L"Inferred *_id relationship (dashed)" }
            };
        } else if (m_notation == ErdNotation::Barker) {
            rows = {
                { 0, L"Solid = Mandatory half, Dashed = Optional" },
                { 1, L"Many end (<) on optional child side" },
                { 2, L"1 : 1 Single relationship (no fork)" },
                { 3, L"# Primary Key   * NOT NULL   o Nullable" },
                { 4, L"Inferred *_id relationship (dashed)" }
            };
        } else {
            rows = {
                { 0, L"1 : 1 (PK+FK or UNIQUE NOT NULL FK)" },
                { 1, L"1 : N Mandatory FK (NOT NULL)" },
                { 2, L"1 : N Optional FK (Nullable)" },
                { 3, L"Primary / Foreign / Unique Key badge" },
                { 4, L"Inferred *_id relationship (dashed)" },
                { 5, L"Bridge jump arc over crossing wire" }
            };
        }

        const float symColW = config.ScaleF(96.0f);
        const float rowH = config.ScaleF(23.0f);
        float maxDescW = MeasureErdTextWidth(dwf, pTitleFmt, legTitle) + config.ScaleF(24.0f);
        for (const auto& r : rows) {
            maxDescW = (std::max)(maxDescW, symColW + MeasureErdTextWidth(dwf, pUiFmt, r.desc) + config.ScaleF(26.0f));
        }
        const float lw = (std::min)(m_rcCanvasViewport.right - m_rcCanvasViewport.left - config.ScaleF(24.0f),
                                    (std::max)(config.ScaleF(330.0f), maxDescW));
        const float lh = config.ScaleF(40.0f) + static_cast<float>(rows.size()) * rowH + config.ScaleF(8.0f);
        const float lx = m_rcCanvasViewport.right - lw - config.ScaleF(14.0f);
        const float ly = m_rcCanvasViewport.bottom - lh - config.ScaleF(14.0f);

        D2D1_RECT_F rcLeg = D2D1::RectF(lx, ly, lx + lw, ly + lh);
        D2D1_ROUNDED_RECT rrcLeg = D2D1::RoundedRect(rcLeg, config.ScaleF(6.0f), config.ScaleF(6.0f));
        if (brCardHdr) pRT->FillRoundedRectangle(rrcLeg, brCardHdr.Get());
        if (brBorder)  pRT->DrawRoundedRectangle(rrcLeg, brBorder.Get(), 1.0f);

        DrawSmallText(pRT, pTitleFmt, legTitle,
                      D2D1::RectF(lx + config.ScaleF(12.0f), ly + config.ScaleF(6.0f), lx + lw - config.ScaleF(12.0f), ly + config.ScaleF(26.0f)),
                      txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
        pRT->DrawLine(D2D1::Point2F(lx + config.ScaleF(10.0f), ly + config.ScaleF(29.0f)),
                      D2D1::Point2F(lx + lw - config.ScaleF(10.0f), ly + config.ScaleF(29.0f)), brBorder.Get(), 1.0f);

        const float gLeft = lx + config.ScaleF(14.0f);
        const float gRight = lx + symColW - config.ScaleF(10.0f);
        const float gMid = (gLeft + gRight) * 0.5f;
        float rowY = ly + config.ScaleF(35.0f);

        for (const auto& entry : rows) {
            const float midY = rowY + rowH * 0.5f;
            if (m_notation == ErdNotation::CrowsFoot) {
                if (entry.kind == 0) {
                    pRT->DrawLine(D2D1::Point2F(gLeft, midY), D2D1::Point2F(gRight, midY), brRel.Get(), 1.5f);
                    DrawCrowsFootGlyph(pRT, gLeft, midY, +1.0f, false, false, brRel.Get(), brCardHdr.Get(), 1.4f);
                    DrawCrowsFootGlyph(pRT, gRight, midY, -1.0f, false, true, brRel.Get(), brCardHdr.Get(), 1.4f);
                } else if (entry.kind == 1) {
                    pRT->DrawLine(D2D1::Point2F(gLeft, midY), D2D1::Point2F(gRight, midY), brRel.Get(), 1.5f);
                    DrawCrowsFootGlyph(pRT, gLeft, midY, +1.0f, false, false, brRel.Get(), brCardHdr.Get(), 1.4f);
                    DrawCrowsFootGlyph(pRT, gRight, midY, -1.0f, true, true, brRel.Get(), brCardHdr.Get(), 1.4f);
                } else if (entry.kind == 2) {
                    pRT->DrawLine(D2D1::Point2F(gLeft, midY), D2D1::Point2F(gRight, midY), brRel.Get(), 1.5f);
                    DrawCrowsFootGlyph(pRT, gLeft, midY, +1.0f, false, true, brRel.Get(), brCardHdr.Get(), 1.4f);
                    DrawCrowsFootGlyph(pRT, gRight, midY, -1.0f, true, true, brRel.Get(), brCardHdr.Get(), 1.4f);
                } else if (entry.kind == 3) {
                    const float bw = config.ScaleF(20.0f);
                    const float bh = config.ScaleF(15.0f);
                    auto drawKeyTag = [&](float tx, const wchar_t* tag, ID2D1Brush* br) {
                        D2D1_RECT_F rcT = D2D1::RectF(tx, midY - bh * 0.5f, tx + bw, midY + bh * 0.5f);
                        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcT, 3.0f, 3.0f), brCard.Get());
                        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcT, 3.0f, 3.0f), brBorder.Get(), 0.9f);
                        DrawSmallText(pRT, pCardSmallFmt, tag, rcT, br, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                    };
                    drawKeyTag(gLeft, L"PK", brPk.Get());
                    drawKeyTag(gLeft + bw + config.ScaleF(4.0f), L"FK", brFk.Get());
                    drawKeyTag(gLeft + (bw + config.ScaleF(4.0f)) * 2.0f, L"UQ", brAdd.Get());
                } else if (entry.kind == 4) {
                    pRT->DrawLine(D2D1::Point2F(gLeft, midY), D2D1::Point2F(gRight, midY), txtSec, 1.4f, legDashStyle.Get());
                } else if (entry.kind == 5) {
                    const float jr = config.ScaleF(4.5f);
                    pRT->DrawLine(D2D1::Point2F(gMid, midY - config.ScaleF(8.0f)),
                                  D2D1::Point2F(gMid, midY + config.ScaleF(8.0f)), txtSec, 1.1f);
                    if (pLegFactory) {
                        ComPtr<ID2D1PathGeometry> pg;
                        if (SUCCEEDED(pLegFactory->CreatePathGeometry(&pg)) && pg) {
                            ComPtr<ID2D1GeometrySink> snk;
                            if (SUCCEEDED(pg->Open(&snk)) && snk) {
                                snk->BeginFigure(D2D1::Point2F(gLeft, midY), D2D1_FIGURE_BEGIN_HOLLOW);
                                snk->AddLine(D2D1::Point2F(gMid - jr, midY));
                                snk->AddArc(D2D1::ArcSegment(D2D1::Point2F(gMid + jr, midY), D2D1::SizeF(jr, jr),
                                                             0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                                snk->AddLine(D2D1::Point2F(gRight, midY));
                                snk->EndFigure(D2D1_FIGURE_END_OPEN);
                                snk->Close();
                                pRT->DrawGeometry(pg.Get(), brRel.Get(), 1.5f);
                            }
                        }
                    }
                }
            } else if (m_notation == ErdNotation::Uml) {
                auto drawUmlDiamond = [&](bool filled) {
                    if (!pLegFactory) return;
                    ComPtr<ID2D1PathGeometry> pg;
                    if (SUCCEEDED(pLegFactory->CreatePathGeometry(&pg)) && pg) {
                        ComPtr<ID2D1GeometrySink> snk;
                        if (SUCCEEDED(pg->Open(&snk)) && snk) {
                            snk->BeginFigure(D2D1::Point2F(gLeft, midY), D2D1_FIGURE_BEGIN_FILLED);
                            snk->AddLine(D2D1::Point2F(gLeft + 6.0f, midY - 4.0f));
                            snk->AddLine(D2D1::Point2F(gLeft + 12.0f, midY));
                            snk->AddLine(D2D1::Point2F(gLeft + 6.0f, midY + 4.0f));
                            snk->EndFigure(D2D1_FIGURE_END_CLOSED);
                            snk->Close();
                            if (filled) pRT->FillGeometry(pg.Get(), brRel.Get());
                            else        pRT->FillGeometry(pg.Get(), brCardHdr.Get());
                            pRT->DrawGeometry(pg.Get(), brRel.Get(), 1.3f);
                        }
                    }
                };
                if (entry.kind == 0 || entry.kind == 1 || entry.kind == 2) {
                    const bool optional = (entry.kind == 2);
                    pRT->DrawLine(D2D1::Point2F(gLeft + 12.0f, midY), D2D1::Point2F(gRight - 14.0f, midY), brRel.Get(), 1.4f);
                    drawUmlDiamond(!optional);
                    DrawSmallText(pRT, pCardSmallFmt, optional ? L"0..1" : L"1",
                                  D2D1::RectF(gLeft + 12.0f, midY - 12.0f, gLeft + 34.0f, midY),
                                  txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
                    DrawSmallText(pRT, pCardSmallFmt, entry.kind == 0 ? L"0..1" : L"0..*",
                                  D2D1::RectF(gRight - 28.0f, midY - 12.0f, gRight, midY),
                                  txtPri, DWRITE_TEXT_ALIGNMENT_TRAILING, dwf, true);
                } else if (entry.kind == 3) {
                    DrawSmallText(pRT, pCardMonoFmt, L"+", D2D1::RectF(gLeft, rowY, gLeft + 16.0f, rowY + rowH), brPk.Get(), DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                    DrawSmallText(pRT, pCardMonoFmt, L"#", D2D1::RectF(gLeft + 22.0f, rowY, gLeft + 38.0f, rowY + rowH), brFk.Get(), DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                    DrawSmallText(pRT, pCardMonoFmt, L"-", D2D1::RectF(gLeft + 44.0f, rowY, gLeft + 60.0f, rowY + rowH), txtSec, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                } else {
                    pRT->DrawLine(D2D1::Point2F(gLeft, midY), D2D1::Point2F(gRight, midY), txtSec, 1.4f, legDashStyle.Get());
                }
            } else if (m_notation == ErdNotation::Chen) {
                auto drawMiniDiamond = [&](ID2D1Brush* lineBr, ID2D1StrokeStyle* st) {
                    pRT->DrawLine(D2D1::Point2F(gLeft + 12.0f, midY), D2D1::Point2F(gMid - 10.0f, midY), lineBr, 1.2f, st);
                    pRT->DrawLine(D2D1::Point2F(gMid + 10.0f, midY), D2D1::Point2F(gRight - 12.0f, midY), lineBr, 1.2f, st);
                    if (!pLegFactory) return;
                    ComPtr<ID2D1PathGeometry> pg;
                    if (SUCCEEDED(pLegFactory->CreatePathGeometry(&pg)) && pg) {
                        ComPtr<ID2D1GeometrySink> snk;
                        if (SUCCEEDED(pg->Open(&snk)) && snk) {
                            snk->BeginFigure(D2D1::Point2F(gMid, midY - 6.0f), D2D1_FIGURE_BEGIN_FILLED);
                            snk->AddLine(D2D1::Point2F(gMid + 10.0f, midY));
                            snk->AddLine(D2D1::Point2F(gMid, midY + 6.0f));
                            snk->AddLine(D2D1::Point2F(gMid - 10.0f, midY));
                            snk->EndFigure(D2D1_FIGURE_END_CLOSED);
                            snk->Close();
                            if (brDim) pRT->FillGeometry(pg.Get(), brDim.Get());
                            pRT->DrawGeometry(pg.Get(), lineBr, 1.2f);
                        }
                    }
                };
                if (entry.kind == 0 || entry.kind == 1) {
                    pRT->DrawRectangle(D2D1::RectF(gLeft, midY - 6.0f, gLeft + 12.0f, midY + 6.0f), brBorder.Get(), 1.1f);
                    pRT->DrawRectangle(D2D1::RectF(gRight - 12.0f, midY - 6.0f, gRight, midY + 6.0f), brBorder.Get(), 1.1f);
                    drawMiniDiamond(brRel.Get(), nullptr);
                    DrawSmallText(pRT, pCardSmallFmt, L"1", D2D1::RectF(gLeft + 13.0f, midY - 12.0f, gLeft + 24.0f, midY), txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
                    DrawSmallText(pRT, pCardSmallFmt, entry.kind == 0 ? L"1" : L"N", D2D1::RectF(gRight - 25.0f, midY - 12.0f, gRight - 13.0f, midY), txtPri, DWRITE_TEXT_ALIGNMENT_TRAILING, dwf, true);
                } else if (entry.kind == 2) {
                    pRT->DrawRectangle(D2D1::RectF(gMid - 16.0f, midY - 8.0f, gMid + 16.0f, midY + 8.0f), brBorder.Get(), 1.1f);
                    pRT->DrawRectangle(D2D1::RectF(gMid - 13.0f, midY - 5.0f, gMid + 13.0f, midY + 5.0f), brBorder.Get(), 1.0f);
                } else if (entry.kind == 3) {
                    D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(gMid, midY), 18.0f, 8.0f);
                    if (brCard) pRT->FillEllipse(el, brCard.Get());
                    pRT->DrawEllipse(el, brPk.Get(), 1.2f);
                    DrawSmallText(pRT, pCardSmallFmt, L"PK", D2D1::RectF(gMid - 16.0f, midY - 8.0f, gMid + 16.0f, midY + 8.0f), brPk.Get(), DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                } else {
                    drawMiniDiamond(txtSec, legDashStyle.Get());
                }
            } else { // Barker
                if (entry.kind == 0) {
                    pRT->DrawLine(D2D1::Point2F(gLeft, midY), D2D1::Point2F(gMid, midY), brRel.Get(), 1.6f);
                    pRT->DrawLine(D2D1::Point2F(gMid, midY), D2D1::Point2F(gRight, midY), brRel.Get(), 1.5f, legDashStyle.Get());
                } else if (entry.kind == 1) {
                    pRT->DrawLine(D2D1::Point2F(gLeft, midY), D2D1::Point2F(gRight, midY), brRel.Get(), 1.5f);
                    pRT->DrawLine(D2D1::Point2F(gRight - 8.0f, midY), D2D1::Point2F(gRight, midY - 5.5f), brRel.Get(), 1.5f);
                    pRT->DrawLine(D2D1::Point2F(gRight - 8.0f, midY), D2D1::Point2F(gRight, midY + 5.5f), brRel.Get(), 1.5f);
                } else if (entry.kind == 2) {
                    pRT->DrawLine(D2D1::Point2F(gLeft, midY), D2D1::Point2F(gRight, midY), brRel.Get(), 1.5f);
                } else if (entry.kind == 3) {
                    DrawSmallText(pRT, pCardMonoFmt, L"#", D2D1::RectF(gLeft, rowY, gLeft + 16.0f, rowY + rowH), brPk.Get(), DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                    DrawSmallText(pRT, pCardMonoFmt, L"*", D2D1::RectF(gLeft + 22.0f, rowY, gLeft + 38.0f, rowY + rowH), txtPri, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                    DrawSmallText(pRT, pCardMonoFmt, L"o", D2D1::RectF(gLeft + 44.0f, rowY, gLeft + 60.0f, rowY + rowH), txtSec, DWRITE_TEXT_ALIGNMENT_CENTER, dwf, true);
                } else {
                    pRT->DrawLine(D2D1::Point2F(gLeft, midY), D2D1::Point2F(gRight, midY), txtSec, 1.4f, legDashStyle.Get());
                }
            }

            DrawSmallText(pRT, pUiFmt, entry.desc,
                          D2D1::RectF(lx + symColW, rowY, lx + lw - config.ScaleF(10.0f), rowY + rowH),
                          txtSec, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
            rowY += rowH;
        }
    }

    // 2d. Flat Animated Loading Overlay during Server / Database Switch or Refresh
    if (m_isConnectionBusy) {
        if (!m_tables.empty()) {
            ComPtr<ID2D1SolidColorBrush> brScrim;
            pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x0F1218, 0.58f) : D2D1::ColorF(0xF3F5F9, 0.62f), &brScrim);
            if (brScrim) pRT->FillRectangle(m_rcCanvasViewport, brScrim.Get());
        }
        std::wstring loadText = m_profiles.empty()
            ? L"Detecting database servers..."
            : (hasDbDropdown && !activeDbLabel.empty()
                ? (L"Loading schema for " + activeDbLabel + L"...")
                : (L"Loading schema from " + activeSrvLabel + L"..."));
        const float spinR = config.ScaleF(9.0f);
        const float boxW = (std::min)(m_rcCanvasViewport.right - m_rcCanvasViewport.left - config.ScaleF(32.0f),
                                      (std::max)(config.ScaleF(230.0f),
                                                 MeasureErdTextWidth(dwf, pUiFmt, loadText) + config.ScaleF(64.0f)));
        const float boxH = config.ScaleF(46.0f);
        const float cx = (m_rcCanvasViewport.left + m_rcCanvasViewport.right) * 0.5f;
        const float cy = (m_rcCanvasViewport.top + m_rcCanvasViewport.bottom) * 0.5f;
        D2D1_RECT_F rcLoad = D2D1::RectF(cx - boxW * 0.5f, cy - boxH * 0.5f, cx + boxW * 0.5f, cy + boxH * 0.5f);
        D2D1_ROUNDED_RECT rrcLoad = D2D1::RoundedRect(rcLoad, config.ScaleF(6.0f), config.ScaleF(6.0f));
        if (brCardHdr) pRT->FillRoundedRectangle(rrcLoad, brCardHdr.Get());
        if (brBorder)  pRT->DrawRoundedRectangle(rrcLoad, brBorder.Get(), 1.0f);

        const float sx = rcLoad.left + config.ScaleF(24.0f);
        const float sy = cy;
        pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(sx, sy), spinR, spinR), brBorder.Get(), 2.0f);
        ComPtr<ID2D1Factory> pSpinFactory;
        pRT->GetFactory(&pSpinFactory);
        if (pSpinFactory) {
            constexpr float kPi = 3.14159265f;
            const float a0 = m_loadingSpinnerDeg * kPi / 180.0f;
            const float a1 = a0 + 1.85f;
            ComPtr<ID2D1PathGeometry> pg;
            if (SUCCEEDED(pSpinFactory->CreatePathGeometry(&pg)) && pg) {
                ComPtr<ID2D1GeometrySink> snk;
                if (SUCCEEDED(pg->Open(&snk)) && snk) {
                    snk->BeginFigure(D2D1::Point2F(sx + std::cos(a0) * spinR, sy + std::sin(a0) * spinR), D2D1_FIGURE_BEGIN_HOLLOW);
                    snk->AddArc(D2D1::ArcSegment(
                        D2D1::Point2F(sx + std::cos(a1) * spinR, sy + std::sin(a1) * spinR),
                        D2D1::SizeF(spinR, spinR), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                    snk->EndFigure(D2D1_FIGURE_END_OPEN);
                    snk->Close();
                    pRT->DrawGeometry(pg.Get(), brAccent ? brAccent.Get() : brRel.Get(), 2.2f, m_erdStrokeStyle.Get());
                }
            }
        }
        DrawSmallText(pRT, pUiFmt, loadText,
                      D2D1::RectF(sx + spinR + config.ScaleF(12.0f), rcLoad.top, rcLoad.right - config.ScaleF(12.0f), rcLoad.bottom),
                      txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
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
    if (brToolbarBg) pRT->FillRectangle(rcStatus, brToolbarBg.Get());
    pRT->DrawLine(D2D1::Point2F(canvasLeft, bodyBot), D2D1::Point2F(canvasLeft + canvasWidth, bodyBot), brBorder.Get(), 1.0f);

    std::wstring status = m_schemaLabel + L"  |  Zoom: " + std::to_wstring(static_cast<int>(m_zoom * 100.0f)) +
                          L"%  |  Drag tables to arrange, drag canvas to pan, scroll to zoom";
    if (!m_toastMsg.empty() && GetTickCount64() < m_toastExpire) status = m_toastMsg;
    DrawSmallText(pRT, pUiFmt, status,
                  D2D1::RectF(canvasLeft + pad, bodyBot + config.ScaleF(2.0f), canvasLeft + canvasWidth - pad, clientHeight - config.ScaleF(2.0f)),
                  txtSec, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);

    // 5. Flat Dropdown Popups for Server & Database Selection (rendered above canvas)
    if (m_serverMenuOpen && m_rcBtnServerPicker.right > m_rcBtnServerPicker.left) {
        const float itemH = config.ScaleF(28.0f);
        const float menuPad = config.ScaleF(4.0f);
        float maxW = (std::max)(config.ScaleF(220.0f), m_rcBtnServerPicker.right - m_rcBtnServerPicker.left);
        for (const auto& prof : m_profiles) {
            maxW = (std::max)(maxW, MeasureErdTextWidth(dwf, pUiFmt, prof.name) + config.ScaleF(44.0f));
        }
        const wchar_t* detectMenuLbl = m_isConnectionBusy ? L"Detecting Servers..." : L"Detect Servers";
        maxW = (std::min)(canvasWidth - config.ScaleF(24.0f),
                          (std::max)(maxW, MeasureErdTextWidth(dwf, pUiFmt, detectMenuLbl) + config.ScaleF(44.0f)));
        const float sepH = config.ScaleF(7.0f);
        const size_t nProfs = m_profiles.size();
        const float menuH = menuPad * 2.0f + static_cast<float>(nProfs + 1) * itemH + (nProfs > 0 ? sepH : 0.0f);
        const float mxRight = (std::min)(canvasLeft + canvasWidth - config.ScaleF(10.0f), m_rcBtnServerPicker.right);
        const float mxLeft = (std::max)(canvasLeft + config.ScaleF(10.0f), mxRight - maxW);
        const float myTop = m_rcBtnServerPicker.bottom + config.ScaleF(4.0f);
        m_rcServerMenuPopup = D2D1::RectF(mxLeft, myTop, mxRight, myTop + menuH);

        D2D1_ROUNDED_RECT rrm = D2D1::RoundedRect(m_rcServerMenuPopup, config.ScaleF(4.0f), config.ScaleF(4.0f));
        if (brMenuBg) pRT->FillRoundedRectangle(rrm, brMenuBg.Get());
        if (brBorder) pRT->DrawRoundedRectangle(rrm, brBorder.Get(), 1.0f);

        float iy = myTop + menuPad;
        for (size_t i = 0; i < m_profiles.size(); ++i) {
            D2D1_RECT_F rcItem = D2D1::RectF(mxLeft + menuPad, iy, mxRight - menuPad, iy + itemH);
            m_profileHits.push_back({ rcItem, i, m_profiles[i].database });
            const bool isSel = (i == m_selectedProfileIdx);
            const bool isHov = (m_hoveredServerMenuIdx == static_cast<int>(i));
            if (isSel && brAccent) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcItem, config.ScaleF(3.0f), config.ScaleF(3.0f)), brAccent.Get());
            } else if (isHov && brBtnHover) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcItem, config.ScaleF(3.0f), config.ScaleF(3.0f)), brBtnHover.Get());
            }
            const float dotX = rcItem.left + config.ScaleF(11.0f);
            const float dotY = (rcItem.top + rcItem.bottom) * 0.5f;
            const float dotR = config.ScaleF(3.5f);
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), dotR, dotR),
                             m_profiles[i].isConnected ? brAdd.Get() : brDel.Get());
            DrawSmallText(pRT, pUiFmt, m_profiles[i].name,
                          D2D1::RectF(rcItem.left + config.ScaleF(20.0f), rcItem.top, rcItem.right - config.ScaleF(8.0f), rcItem.bottom),
                          isSel ? txtWhite : txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
            iy += itemH;
        }
        if (nProfs > 0) {
            pRT->DrawLine(D2D1::Point2F(mxLeft + config.ScaleF(6.0f), iy + sepH * 0.5f),
                          D2D1::Point2F(mxRight - config.ScaleF(6.0f), iy + sepH * 0.5f), brRowSep.Get(), 1.0f);
            iy += sepH;
        }
        m_rcServerDetectMenuItem = D2D1::RectF(mxLeft + menuPad, iy, mxRight - menuPad, iy + itemH);
        m_rcBtnDetect = m_rcServerDetectMenuItem;
        if (m_hoveredBtnId == 14 && brBtnHover) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_rcServerDetectMenuItem, config.ScaleF(3.0f), config.ScaleF(3.0f)), brBtnHover.Get());
        }
        const float icoSz = config.ScaleF(13.0f);
        const float dy = (m_rcServerDetectMenuItem.top + m_rcServerDetectMenuItem.bottom) * 0.5f;
        DrawErdVectorIcon(pRT, ErdIconKind::Refresh,
                          D2D1::RectF(m_rcServerDetectMenuItem.left + config.ScaleF(6.0f), dy - icoSz * 0.5f,
                                      m_rcServerDetectMenuItem.left + config.ScaleF(6.0f) + icoSz, dy + icoSz * 0.5f),
                          brRel.Get(), m_erdStrokeStyle.Get(), 1.3f);
        DrawSmallText(pRT, pUiFmt, detectMenuLbl,
                      D2D1::RectF(m_rcServerDetectMenuItem.left + config.ScaleF(24.0f), m_rcServerDetectMenuItem.top,
                                  m_rcServerDetectMenuItem.right - config.ScaleF(8.0f), m_rcServerDetectMenuItem.bottom),
                      txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
    }

    if (m_dbMenuOpen && hasDbDropdown && m_selectedProfileIdx < m_profiles.size() && m_rcBtnDbPicker.right > m_rcBtnDbPicker.left) {
        const auto& dbs = m_profiles[m_selectedProfileIdx].availableDatabases;
        const std::wstring& activeDb = m_profiles[m_selectedProfileIdx].database;
        const float itemH = config.ScaleF(27.0f);
        const float menuPad = config.ScaleF(4.0f);
        float maxW = (std::max)(config.ScaleF(150.0f), m_rcBtnDbPicker.right - m_rcBtnDbPicker.left);
        for (const auto& d : dbs) {
            maxW = (std::max)(maxW, MeasureErdTextWidth(dwf, pUiFmt, d) + config.ScaleF(34.0f));
        }
        maxW = (std::min)(canvasWidth - config.ScaleF(24.0f), maxW);
        const size_t visCount = (std::min<size_t>)(dbs.size(), 16);
        const float menuH = menuPad * 2.0f + static_cast<float>((std::max<size_t>)(1, visCount)) * itemH;
        const float mxRight = (std::min)(canvasLeft + canvasWidth - config.ScaleF(10.0f), m_rcBtnDbPicker.right);
        const float mxLeft = (std::max)(canvasLeft + config.ScaleF(10.0f), mxRight - maxW);
        const float myTop = m_rcBtnDbPicker.bottom + config.ScaleF(4.0f);
        m_rcDbMenuPopup = D2D1::RectF(mxLeft, myTop, mxRight, myTop + menuH);

        D2D1_ROUNDED_RECT rrm = D2D1::RoundedRect(m_rcDbMenuPopup, config.ScaleF(4.0f), config.ScaleF(4.0f));
        if (brMenuBg) pRT->FillRoundedRectangle(rrm, brMenuBg.Get());
        if (brBorder) pRT->DrawRoundedRectangle(rrm, brBorder.Get(), 1.0f);

        float iy = myTop + menuPad;
        for (size_t di = 0; di < visCount; ++di) {
            D2D1_RECT_F rcItem = D2D1::RectF(mxLeft + menuPad, iy, mxRight - menuPad, iy + itemH);
            m_dbHits.push_back({ rcItem, m_selectedProfileIdx, dbs[di] });
            const bool isSel = (dbs[di] == activeDb);
            const bool isHov = (m_hoveredDbMenuIdx == static_cast<int>(di));
            if (isSel && brAccent) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcItem, config.ScaleF(3.0f), config.ScaleF(3.0f)), brAccent.Get());
            } else if (isHov && brBtnHover) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcItem, config.ScaleF(3.0f), config.ScaleF(3.0f)), brBtnHover.Get());
            }
            DrawSmallText(pRT, pUiFmt, dbs[di],
                          D2D1::RectF(rcItem.left + config.ScaleF(10.0f), rcItem.top, rcItem.right - config.ScaleF(10.0f), rcItem.bottom),
                          isSel ? txtWhite : txtPri, DWRITE_TEXT_ALIGNMENT_LEADING, dwf, true);
            iy += itemH;
        }
    }
}

bool ErdView::HandleLButtonDown(POINT clientPt, HWND hwnd, const app::AppConfig& /*config*/) {
    // 1. Handle open Server/Database dropdown popups first
    if (m_serverMenuOpen) {
        if (PtInRectF(clientPt, m_rcServerDetectMenuItem)) {
            m_serverMenuOpen = false;
            if (m_callbacks.onDetectServers) m_callbacks.onDetectServers();
            else if (m_callbacks.onRefreshLiveSchema) m_callbacks.onRefreshLiveSchema();
            SetToast(L"Detecting database servers and refreshing schema...");
            return true;
        }
        for (const auto& hit : m_profileHits) {
            if (PtInRectF(clientPt, hit.bounds)) {
                m_serverMenuOpen = false;
                m_selectedProfileIdx = hit.profileIdx;
                if (m_callbacks.onSelectProfile) m_callbacks.onSelectProfile(hit.profileIdx);
                SetToast(L"Switched active database server");
                return true;
            }
        }
        m_serverMenuOpen = false;
        if (PtInRectF(clientPt, m_rcBtnServerPicker) || PtInRectF(clientPt, m_rcServerMenuPopup)) {
            return true;
        }
    }
    if (m_dbMenuOpen) {
        for (const auto& hit : m_dbHits) {
            if (PtInRectF(clientPt, hit.bounds)) {
                m_dbMenuOpen = false;
                if (m_callbacks.onSelectDatabase) m_callbacks.onSelectDatabase(hit.dbName);
                SetToast(L"Loading schema for database: " + hit.dbName);
                return true;
            }
        }
        m_dbMenuOpen = false;
        if (PtInRectF(clientPt, m_rcBtnDbPicker) || PtInRectF(clientPt, m_rcDbMenuPopup)) {
            return true;
        }
    }

    if (!m_searchFilter.empty() && PtInRectF(clientPt, m_rcSearchClear)) {
        m_searchFilter.clear();
        m_searchFocused = true;
        return true;
    }

    m_searchFocused = PtInRectF(clientPt, m_rcSearchBox);
    if (m_searchFocused) return true;

    if (PtInRectF(clientPt, m_rcBtnServerPicker)) {
        m_serverMenuOpen = !m_serverMenuOpen;
        m_dbMenuOpen = false;
        return true;
    }
    if (PtInRectF(clientPt, m_rcBtnDbPicker)) {
        m_dbMenuOpen = !m_dbMenuOpen;
        m_serverMenuOpen = false;
        return true;
    }
    if (PtInRectF(clientPt, m_rcBtnQuickRefresh)) {
        if (m_profiles.empty() && m_callbacks.onDetectServers) {
            m_callbacks.onDetectServers();
            SetToast(L"Detecting database servers and refreshing schema...");
        } else if (m_callbacks.onRefreshLiveSchema) {
            m_callbacks.onRefreshLiveSchema();
            SetToast(L"Refreshing schema from active connection...");
        }
        return true;
    }

    if (PtInRectF(clientPt, m_rcBtnDetect) || PtInRectF(clientPt, m_rcBtnEmptyRefresh)) {
        if (m_callbacks.onDetectServers) m_callbacks.onDetectServers();
        else if (m_callbacks.onRefreshLiveSchema) m_callbacks.onRefreshLiveSchema();
        SetToast(L"Detecting database servers and refreshing schema...");
        return true;
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
        ApplyZoomToFit(true, 1.0f);
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
        ApplyZoomToFit(true, 1.25f);
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
    int hsMenu = -1;
    int hdbMenu = -1;
    if (m_serverMenuOpen) {
        if (PtInRectF(clientPt, m_rcServerDetectMenuItem)) h = 14;
        for (size_t i = 0; i < m_profileHits.size(); ++i) {
            if (PtInRectF(clientPt, m_profileHits[i].bounds)) { hsMenu = static_cast<int>(i); break; }
        }
    } else if (m_dbMenuOpen) {
        for (size_t i = 0; i < m_dbHits.size(); ++i) {
            if (PtInRectF(clientPt, m_dbHits[i].bounds)) { hdbMenu = static_cast<int>(i); break; }
        }
    }

    if (h < 0 && hsMenu < 0 && hdbMenu < 0) {
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
        else if (PtInRectF(clientPt, m_rcBtnServerPicker)) h = 19;
        else if (PtInRectF(clientPt, m_rcBtnDbPicker)) h = 20;
        else if (PtInRectF(clientPt, m_rcBtnQuickRefresh)) h = 21;
    }

    int hddb = -1;
    for (size_t i = 0; i < m_diffDbHits.size(); ++i) {
        if (PtInRectF(clientPt, m_diffDbHits[i].bounds)) { hddb = static_cast<int>(i); break; }
    }
    bool hSearchClear = (!m_searchFilter.empty() && PtInRectF(clientPt, m_rcSearchClear));
    bool hSearch = !hSearchClear && PtInRectF(clientPt, m_rcSearchBox);
    int hNode = -1;
    int hEdge = -1;
    const bool overOpenPopup = (m_serverMenuOpen && PtInRectF(clientPt, m_rcServerMenuPopup)) ||
                               (m_dbMenuOpen && PtInRectF(clientPt, m_rcDbMenuPopup));
    if (h < 0 && hsMenu < 0 && hdbMenu < 0 && hddb < 0 && !hSearch && !hSearchClear &&
        !overOpenPopup && PtInRectF(clientPt, m_rcCanvasViewport)) {
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

    const bool changed = (h != m_hoveredBtnId || hsMenu != m_hoveredServerMenuIdx ||
                          hdbMenu != m_hoveredDbMenuIdx || hddb != m_hoveredDiffDbIdx ||
                          hSearch != m_hoveredSearchBox || hSearchClear != m_hoveredSearchClear ||
                          hNode != m_hoveredNodeIdx || hEdge != m_hoveredEdgeIdx || hEdge >= 0);
    m_hoveredBtnId = h;
    m_hoveredServerMenuIdx = hsMenu;
    m_hoveredDbMenuIdx = hdbMenu;
    m_hoveredProfileIdx = hsMenu;
    m_hoveredDbIdx = hdbMenu;
    m_hoveredDiffDbIdx = hddb;
    m_hoveredSearchBox = hSearch;
    m_hoveredSearchClear = hSearchClear;
    m_hoveredNodeIdx = hNode;
    m_hoveredEdgeIdx = hEdge;
    return changed;
}

LPCWSTR ErdView::GetCursorForPoint(POINT clientPt) const noexcept {
    if (m_draggingNodeIdx >= 0 || m_isPanningCanvas) return IDC_SIZEALL;
    if (!m_searchFilter.empty() && PtInRectF(clientPt, m_rcSearchClear)) return IDC_HAND;
    if (PtInRectF(clientPt, m_rcSearchBox)) return IDC_IBEAM;
    if (m_serverMenuOpen && PtInRectF(clientPt, m_rcServerMenuPopup)) return IDC_HAND;
    if (m_dbMenuOpen && PtInRectF(clientPt, m_rcDbMenuPopup)) return IDC_HAND;
    if (PtInRectF(clientPt, m_rcBtnCrowsFoot) || PtInRectF(clientPt, m_rcBtnUml) ||
        PtInRectF(clientPt, m_rcBtnChen) || PtInRectF(clientPt, m_rcBtnBarker) ||
        PtInRectF(clientPt, m_rcBtnChenAttrs) || PtInRectF(clientPt, m_rcBtnAutoLayout) ||
        PtInRectF(clientPt, m_rcBtnZoomFit) || PtInRectF(clientPt, m_rcBtnRefresh) ||
        PtInRectF(clientPt, m_rcBtnQuickRefresh) || PtInRectF(clientPt, m_rcBtnServerPicker) ||
        PtInRectF(clientPt, m_rcBtnDbPicker) || PtInRectF(clientPt, m_rcBtnDetect) ||
        PtInRectF(clientPt, m_rcBtnEmptyRefresh) || PtInRectF(clientPt, m_rcBtnToggleDiff) ||
        PtInRectF(clientPt, m_rcBtnLoadSql) || PtInRectF(clientPt, m_rcBtnExportSvg) ||
        PtInRectF(clientPt, m_rcBtnInferFks) || PtInRectF(clientPt, m_rcBtnShowM2N) ||
        PtInRectF(clientPt, m_rcBtnLegend) || PtInRectF(clientPt, m_rcBtnOpenMigConsole) ||
        PtInRectF(clientPt, m_rcBtnCopyMigration)) {
        return IDC_HAND;
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
    if (vk == VK_ESCAPE && (m_serverMenuOpen || m_dbMenuOpen)) {
        m_serverMenuOpen = false;
        m_dbMenuOpen = false;
        return true;
    }
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
