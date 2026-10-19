/**
 * @file LucideIcons.cpp
 * @brief Implementation of vector-drawn Lucide icons and caption controls using Direct2D.
 */

#include "phide/ui/LucideIcons.hpp"
#include <algorithm>

namespace phide::ui {

LucideIcons::LucideIcons() = default;

HRESULT LucideIcons::Initialize(ID2D1Factory* pFactory) {
    if (!pFactory) return E_POINTER;

    // Create stroke style with rounded line caps and rounded joins matching Lucide icon specs
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND,      // startCap
        D2D1_CAP_STYLE_ROUND,      // endCap
        D2D1_CAP_STYLE_ROUND,      // dashCap
        D2D1_LINE_JOIN_ROUND,      // lineJoin
        10.0f,                     // miterLimit
        D2D1_DASH_STYLE_SOLID,     // dashStyle
        0.0f                       // dashOffset
    );

    return pFactory->CreateStrokeStyle(props, nullptr, 0, &m_pStrokeStyle);
}

// Coordinate mapping helper from standard Lucide 24x24 viewBox to arbitrary target bounds
static inline D2D1_POINT_2F MapPoint(float x, float y, float ox, float oy, float scale) {
    return D2D1::Point2F(ox + x * scale, oy + y * scale);
}

void LucideIcons::DrawHome(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f; // 52% of button box
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Outer roof & walls: (3, 9) -> (12, 2.5) -> (21, 9) -> (21, 20) -> (19, 22) -> (5, 22) -> (3, 20) -> closed
    // Roof lines:
    pRT->DrawLine(MapPoint(3.0f, 9.5f, ox, oy, s), MapPoint(12.0f, 2.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(12.0f, 2.5f, ox, oy, s), MapPoint(21.0f, 9.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Wall lines:
    pRT->DrawLine(MapPoint(4.0f, 9.0f, ox, oy, s), MapPoint(4.0f, 21.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(20.0f, 9.0f, ox, oy, s), MapPoint(20.0f, 21.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Floor lines:
    pRT->DrawLine(MapPoint(4.0f, 21.0f, ox, oy, s), MapPoint(9.0f, 21.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(15.0f, 21.0f, ox, oy, s), MapPoint(20.0f, 21.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Doorway: (9, 21) -> (9, 12) -> (15, 12) -> (15, 21)
    pRT->DrawLine(MapPoint(9.0f, 21.0f, ox, oy, s), MapPoint(9.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(9.0f, 12.0f, ox, oy, s), MapPoint(15.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(15.0f, 12.0f, ox, oy, s), MapPoint(15.0f, 21.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawSearch(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.50f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Lucide Search: Circle at (10.5, 10.5) with radius 7
    D2D1_ELLIPSE glass = D2D1::Ellipse(MapPoint(10.5f, 10.5f, ox, oy, s), 6.8f * s, 6.8f * s);
    pRT->DrawEllipse(glass, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Lucide Search Handle: from (15.5, 15.5) to (21, 21)
    pRT->DrawLine(MapPoint(15.5f, 15.5f, ox, oy, s), MapPoint(21.0f, 21.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawGitBranch(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Node 1: Top Branch Circle at (18, 6), radius 2.6
    D2D1_ELLIPSE nodeTop = D2D1::Ellipse(MapPoint(18.0f, 6.0f, ox, oy, s), 2.5f * s, 2.5f * s);
    pRT->DrawEllipse(nodeTop, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Node 2: Bottom Trunk Circle at (6, 18), radius 2.6
    D2D1_ELLIPSE nodeBottom = D2D1::Ellipse(MapPoint(6.0f, 18.0f, ox, oy, s), 2.5f * s, 2.5f * s);
    pRT->DrawEllipse(nodeBottom, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Node 3: Top Trunk Circle at (6, 6), radius 2.6
    D2D1_ELLIPSE nodeTrunkTop = D2D1::Ellipse(MapPoint(6.0f, 6.0f, ox, oy, s), 2.5f * s, 2.5f * s);
    pRT->DrawEllipse(nodeTrunkTop, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Straight Trunk Line connecting (6, 8.5) to (6, 15.5)
    pRT->DrawLine(MapPoint(6.0f, 8.5f, ox, oy, s), MapPoint(6.0f, 15.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Curved Branch: Smooth arc from (18, 8.5) curving into (6, 15.5)
    // Direct2D bezier approximation
    ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (pFactory) {
        ComPtr<ID2D1PathGeometry> path;
        if (SUCCEEDED(pFactory->CreatePathGeometry(&path))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(path->Open(&sink))) {
                sink->BeginFigure(MapPoint(18.0f, 8.5f, ox, oy, s), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(
                    MapPoint(18.0f, 14.5f, ox, oy, s),  // Control point
                    MapPoint(6.0f, 14.5f, ox, oy, s)    // End point connecting to trunk
                ));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                pRT->DrawGeometry(path.Get(), pBrush, strokeWidth, m_pStrokeStyle.Get());
            }
        }
    }
}

void LucideIcons::DrawFile(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Lucide File: Document outline with folded top-right corner
    pRT->DrawLine(MapPoint(14.0f, 2.0f, ox, oy, s), MapPoint(6.0f, 2.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(6.0f, 2.0f, ox, oy, s), MapPoint(4.0f, 4.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(4.0f, 4.0f, ox, oy, s), MapPoint(4.0f, 20.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(4.0f, 20.0f, ox, oy, s), MapPoint(6.0f, 22.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(6.0f, 22.0f, ox, oy, s), MapPoint(18.0f, 22.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(18.0f, 22.0f, ox, oy, s), MapPoint(20.0f, 20.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(20.0f, 20.0f, ox, oy, s), MapPoint(20.0f, 8.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(20.0f, 8.0f, ox, oy, s), MapPoint(14.0f, 2.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Corner fold: (14, 2) down to (14, 8) and right to (20, 8)
    pRT->DrawLine(MapPoint(14.0f, 2.0f, ox, oy, s), MapPoint(14.0f, 8.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(14.0f, 8.0f, ox, oy, s), MapPoint(20.0f, 8.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Subtle internal code line: (8, 14) to (16, 14) and (8, 17.5) to (13, 17.5)
    pRT->DrawLine(MapPoint(8.0f, 14.0f, ox, oy, s), MapPoint(16.0f, 14.0f, ox, oy, s), pBrush, strokeWidth * 0.9f, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(8.0f, 17.5f, ox, oy, s), MapPoint(13.0f, 17.5f, ox, oy, s), pBrush, strokeWidth * 0.9f, m_pStrokeStyle.Get());
}

void LucideIcons::DrawFiles(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.54f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // 1. Front sheet (upper-right foreground document with folded top-right corner, matching Image 2)
    // Top horizontal edge
    pRT->DrawLine(MapPoint(8.0f, 3.0f, ox, oy, s), MapPoint(15.0f, 3.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Slanted dog-ear fold edge
    pRT->DrawLine(MapPoint(15.0f, 3.0f, ox, oy, s), MapPoint(19.5f, 7.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Right vertical edge
    pRT->DrawLine(MapPoint(19.5f, 7.5f, ox, oy, s), MapPoint(19.5f, 18.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Bottom horizontal edge
    pRT->DrawLine(MapPoint(19.5f, 18.5f, ox, oy, s), MapPoint(8.0f, 18.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Left vertical edge
    pRT->DrawLine(MapPoint(8.0f, 18.5f, ox, oy, s), MapPoint(8.0f, 3.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Front dog-ear fold interior lines: (15, 3) -> (15, 7.5) -> (19.5, 7.5)
    pRT->DrawLine(MapPoint(15.0f, 3.0f, ox, oy, s), MapPoint(15.0f, 7.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(15.0f, 7.5f, ox, oy, s), MapPoint(19.5f, 7.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 2. Back sheet (background document offset down & left, cleanly visible without crossing inside front sheet)
    // Top horizontal segment (stops at front sheet's left edge at x=8.0)
    pRT->DrawLine(MapPoint(4.5f, 6.5f, ox, oy, s), MapPoint(8.0f, 6.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Left vertical segment
    pRT->DrawLine(MapPoint(4.5f, 6.5f, ox, oy, s), MapPoint(4.5f, 21.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Bottom horizontal segment
    pRT->DrawLine(MapPoint(4.5f, 21.0f, ox, oy, s), MapPoint(16.0f, 21.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Right vertical stub (stops at front sheet's bottom edge at y=18.5)
    pRT->DrawLine(MapPoint(16.0f, 21.0f, ox, oy, s), MapPoint(16.0f, 18.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawDebug(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // 1. Play Triangle (pointing right)
    pRT->DrawLine(MapPoint(9.0f, 4.5f, ox, oy, s), MapPoint(19.0f, 11.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(19.0f, 11.5f, ox, oy, s), MapPoint(9.0f, 18.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(9.0f, 18.5f, ox, oy, s), MapPoint(9.0f, 4.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 2. Bug Glyph on lower left:
    D2D1_ELLIPSE bugBody = D2D1::Ellipse(MapPoint(6.5f, 16.5f, ox, oy, s), 2.5f * s, 3.0f * s);
    pRT->DrawEllipse(bugBody, pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Bug Legs / Whiskers
    pRT->DrawLine(MapPoint(4.0f, 15.0f, ox, oy, s), MapPoint(2.5f, 14.0f, ox, oy, s), pBrush, strokeWidth * 0.9f, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(4.0f, 18.0f, ox, oy, s), MapPoint(2.5f, 19.0f, ox, oy, s), pBrush, strokeWidth * 0.9f, m_pStrokeStyle.Get());
}

void LucideIcons::DrawExtensions(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // 1. Top-Left Square (Block 1)
    D2D1_RECT_F b1 = D2D1::RectF(ox + 4.0f * s, oy + 4.0f * s, ox + 10.5f * s, oy + 10.5f * s);
    pRT->DrawRectangle(b1, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 2. Bottom-Left Square (Block 2)
    D2D1_RECT_F b2 = D2D1::RectF(ox + 4.0f * s, oy + 13.5f * s, ox + 10.5f * s, oy + 20.0f * s);
    pRT->DrawRectangle(b2, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 3. Bottom-Right Square (Block 3)
    D2D1_RECT_F b3 = D2D1::RectF(ox + 13.5f * s, oy + 13.5f * s, ox + 20.0f * s, oy + 20.0f * s);
    pRT->DrawRectangle(b3, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 4. Top-Right Rotated Diamond (Block 4 - detached plug-in module matching Image 2)
    pRT->DrawLine(MapPoint(16.5f, 3.5f, ox, oy, s), MapPoint(20.0f, 7.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(20.0f, 7.0f, ox, oy, s), MapPoint(16.5f, 10.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(16.5f, 10.5f, ox, oy, s), MapPoint(13.0f, 7.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(13.0f, 7.0f, ox, oy, s), MapPoint(16.5f, 3.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawMinimize(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float cx = bounds.left + (bounds.right - bounds.left) / 2.0f;
    const float cy = bounds.top + (bounds.bottom - bounds.top) / 2.0f;
    pRT->DrawLine(D2D1::Point2F(cx - 5.0f, cy), D2D1::Point2F(cx + 5.0f, cy), pBrush, strokeWidth);
}

void LucideIcons::DrawMaximize(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, bool isZoomed, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float cx = bounds.left + (bounds.right - bounds.left) / 2.0f;
    const float cy = bounds.top + (bounds.bottom - bounds.top) / 2.0f;

    if (!isZoomed) {
        // Single clean square when window is restored
        D2D1_RECT_F rc = D2D1::RectF(cx - 5.0f, cy - 5.0f, cx + 5.0f, cy + 5.0f);
        pRT->DrawRectangle(rc, pBrush, strokeWidth);
    } else {
        // Pixel-perfect restore icon matching Image 3:
        // Front rectangle (lower-left)
        D2D1_RECT_F front = D2D1::RectF(cx - 5.0f, cy - 3.0f, cx + 3.0f, cy + 5.0f);
        pRT->DrawRectangle(front, pBrush, strokeWidth);

        // Back square (upper-right) - drawn behind front rectangle without intersecting lines
        // 1. Top horizontal segment
        pRT->DrawLine(D2D1::Point2F(cx - 2.5f, cy - 5.5f), D2D1::Point2F(cx + 5.5f, cy - 5.5f), pBrush, strokeWidth);
        // 2. Right vertical segment
        pRT->DrawLine(D2D1::Point2F(cx + 5.5f, cy - 5.5f), D2D1::Point2F(cx + 5.5f, cy + 2.5f), pBrush, strokeWidth);
        // 3. Left vertical stub (stops precisely at top edge of the front rectangle)
        pRT->DrawLine(D2D1::Point2F(cx - 2.5f, cy - 5.5f), D2D1::Point2F(cx - 2.5f, cy - 3.0f), pBrush, strokeWidth);
        // 4. Bottom horizontal stub (stops precisely at right edge of the front rectangle)
        pRT->DrawLine(D2D1::Point2F(cx + 3.0f, cy + 2.5f), D2D1::Point2F(cx + 5.5f, cy + 2.5f), pBrush, strokeWidth);
    }
}

void LucideIcons::DrawClose(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float cx = bounds.left + (bounds.right - bounds.left) / 2.0f;
    const float cy = bounds.top + (bounds.bottom - bounds.top) / 2.0f;
    const float s = 4.8f;

    pRT->DrawLine(D2D1::Point2F(cx - s, cy - s), D2D1::Point2F(cx + s, cy + s), pBrush, strokeWidth);
    pRT->DrawLine(D2D1::Point2F(cx + s, cy - s), D2D1::Point2F(cx - s, cy + s), pBrush, strokeWidth);
}

} // namespace phide::ui
