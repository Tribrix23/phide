/**
 * @file LucideIcons.cpp
 * @brief Implementation of vector-drawn Lucide icons and caption controls using Direct2D.
 */

#include "phide/ui/LucideIcons.hpp"
#include "phide/ui/Theme.hpp"
#include <algorithm>
#include <cmath>

namespace phide::ui {

LucideIcons::LucideIcons() = default;

HRESULT LucideIcons::Initialize(ID2D1Factory* pFactory) {
    if (!pFactory) return E_POINTER;

    if (!m_pStrokeStyle) {
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

        HRESULT hr = pFactory->CreateStrokeStyle(props, nullptr, 0, &m_pStrokeStyle);
        if (FAILED(hr)) return hr;
    }

    if (!m_pWrenchGeometry) {
        if (SUCCEEDED(pFactory->CreatePathGeometry(&m_pWrenchGeometry))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(m_pWrenchGeometry->Open(&sink))) {
                // Official Lucide Wrench path:
                // M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z
                sink->BeginFigure(D2D1::Point2F(14.7f, 6.3f), D2D1_FIGURE_BEGIN_HOLLOW);

                D2D1_ARC_SEGMENT a1{ D2D1::Point2F(14.7f, 7.7f), D2D1::SizeF(1.0f, 1.0f), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL };
                sink->AddArc(&a1);

                sink->AddLine(D2D1::Point2F(16.3f, 9.3f));

                D2D1_ARC_SEGMENT a2{ D2D1::Point2F(17.7f, 9.3f), D2D1::SizeF(1.0f, 1.0f), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL };
                sink->AddArc(&a2);

                sink->AddLine(D2D1::Point2F(21.47f, 5.53f));

                D2D1_ARC_SEGMENT a3{ D2D1::Point2F(13.53f, 13.47f), D2D1::SizeF(6.0f, 6.0f), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL };
                sink->AddArc(&a3);

                sink->AddLine(D2D1::Point2F(6.62f, 20.38f));

                D2D1_ARC_SEGMENT a4{ D2D1::Point2F(3.62f, 17.38f), D2D1::SizeF(2.12f, 2.12f), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL };
                sink->AddArc(&a4);

                sink->AddLine(D2D1::Point2F(10.53f, 10.47f));

                D2D1_ARC_SEGMENT a5{ D2D1::Point2F(18.47f, 2.53f), D2D1::SizeF(6.0f, 6.0f), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL };
                sink->AddArc(&a5);

                sink->AddLine(D2D1::Point2F(14.7f, 6.3f));

                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
            }
        }
    }

    if (!m_pRunGeometry) {
        if (SUCCEEDED(pFactory->CreatePathGeometry(&m_pRunGeometry))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(m_pRunGeometry->Open(&sink))) {
                sink->BeginFigure(D2D1::Point2F(8.5f, 5.5f), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(D2D1::Point2F(18.5f, 12.0f));
                sink->AddLine(D2D1::Point2F(8.5f, 18.5f));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
            }
        }
    }

    if (!m_pRestartGeometry) {
        if (SUCCEEDED(pFactory->CreatePathGeometry(&m_pRestartGeometry))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(m_pRestartGeometry->Open(&sink))) {
                // Official Lucide rotate-cw: 270 degree circular arc with arrowhead
                sink->BeginFigure(D2D1::Point2F(21.0f, 12.0f), D2D1_FIGURE_BEGIN_HOLLOW);
                D2D1_ARC_SEGMENT arc{ D2D1::Point2F(18.0f, 6.0f), D2D1::SizeF(8.5f, 8.5f), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE };
                sink->AddArc(&arc);
                sink->EndFigure(D2D1_FIGURE_END_OPEN);

                // Arrowhead at top-right
                sink->BeginFigure(D2D1::Point2F(21.0f, 2.5f), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(D2D1::Point2F(21.0f, 7.5f));
                sink->AddLine(D2D1::Point2F(16.0f, 7.5f));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);

                sink->Close();
            }
        }
    }

    return S_OK;
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

void LucideIcons::DrawPackage(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float minDim = (std::min)(bw, bh);
    // In small contexts like editor tabs (16x16), use a generous scale factor so the 3D cube facets are crisp and unmistakable
    const float scaleFactor = (minDim <= 24.0f) ? 0.88f : 0.60f;
    const float effStroke = (minDim <= 24.0f) ? 1.15f : strokeWidth;
    const float iconSize = minDim * scaleFactor;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Outer hexagon / 3D box profile
    D2D1_POINT_2F pTop      = MapPoint(12.0f, 2.5f, ox, oy, s);
    D2D1_POINT_2F pTopRight = MapPoint(21.0f, 7.5f, ox, oy, s);
    D2D1_POINT_2F pBotRight = MapPoint(21.0f, 16.5f, ox, oy, s);
    D2D1_POINT_2F pBot      = MapPoint(12.0f, 21.5f, ox, oy, s);
    D2D1_POINT_2F pBotLeft  = MapPoint(3.0f, 16.5f, ox, oy, s);
    D2D1_POINT_2F pTopLeft  = MapPoint(3.0f, 7.5f, ox, oy, s);
    D2D1_POINT_2F pCenter   = MapPoint(12.0f, 12.0f, ox, oy, s);

    // Draw outer boundary
    pRT->DrawLine(pTop, pTopRight, pBrush, effStroke, m_pStrokeStyle.Get());
    pRT->DrawLine(pTopRight, pBotRight, pBrush, effStroke, m_pStrokeStyle.Get());
    pRT->DrawLine(pBotRight, pBot, pBrush, effStroke, m_pStrokeStyle.Get());
    pRT->DrawLine(pBot, pBotLeft, pBrush, effStroke, m_pStrokeStyle.Get());
    pRT->DrawLine(pBotLeft, pTopLeft, pBrush, effStroke, m_pStrokeStyle.Get());
    pRT->DrawLine(pTopLeft, pTop, pBrush, effStroke, m_pStrokeStyle.Get());

    // Internal 3D edges meeting at center
    pRT->DrawLine(pCenter, pTopLeft, pBrush, effStroke, m_pStrokeStyle.Get());
    pRT->DrawLine(pCenter, pTopRight, pBrush, effStroke, m_pStrokeStyle.Get());
    pRT->DrawLine(pCenter, pBot, pBrush, effStroke, m_pStrokeStyle.Get());

    // Parcel tape / top detail line across top face (only drawn on larger icons to prevent cluttering 16px tabs)
    if (minDim > 24.0f) {
        pRT->DrawLine(MapPoint(16.5f, 5.0f, ox, oy, s), MapPoint(7.5f, 10.0f, ox, oy, s), pBrush, effStroke * 0.9f, m_pStrokeStyle.Get());
    }
}

void LucideIcons::DrawSidebar(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
        D2D1::RectF(ox + 3.0f * s, oy + 3.5f * s, ox + 21.0f * s, oy + 20.5f * s),
        2.5f * s, 2.5f * s
    );
    pRT->DrawRoundedRectangle(rrc, pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(9.0f, 3.5f, ox, oy, s), MapPoint(9.0f, 20.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawEdit(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    pRT->DrawLine(MapPoint(12.0f, 3.5f, ox, oy, s), MapPoint(5.0f, 3.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(5.0f, 3.5f, ox, oy, s), MapPoint(3.5f, 5.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(3.5f, 5.0f, ox, oy, s), MapPoint(3.5f, 19.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(3.5f, 19.0f, ox, oy, s), MapPoint(5.0f, 20.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(5.0f, 20.5f, ox, oy, s), MapPoint(19.0f, 20.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(19.0f, 20.5f, ox, oy, s), MapPoint(20.5f, 19.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(20.5f, 19.0f, ox, oy, s), MapPoint(20.5f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    pRT->DrawLine(MapPoint(18.5f, 2.5f, ox, oy, s), MapPoint(8.5f, 12.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(8.5f, 12.5f, ox, oy, s), MapPoint(8.0f, 16.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(8.0f, 16.0f, ox, oy, s), MapPoint(11.5f, 15.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(11.5f, 15.5f, ox, oy, s), MapPoint(21.5f, 5.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawView(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
        D2D1::RectF(ox + 3.0f * s, oy + 3.5f * s, ox + 21.0f * s, oy + 20.5f * s),
        2.5f * s, 2.5f * s
    );
    pRT->DrawRoundedRectangle(rrc, pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(3.0f, 9.5f, ox, oy, s), MapPoint(21.0f, 9.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(12.0f, 9.5f, ox, oy, s), MapPoint(12.0f, 20.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawCode(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    pRT->DrawLine(MapPoint(16.0f, 18.0f, ox, oy, s), MapPoint(22.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(22.0f, 12.0f, ox, oy, s), MapPoint(16.0f, 6.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    pRT->DrawLine(MapPoint(8.0f, 6.0f, ox, oy, s), MapPoint(2.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(2.0f, 12.0f, ox, oy, s), MapPoint(8.0f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawTools(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    if (m_pWrenchGeometry) {
        D2D1_MATRIX_3X2_F oldTransform;
        pRT->GetTransform(&oldTransform);
        D2D1_MATRIX_3X2_F iconTransform = D2D1::Matrix3x2F::Scale(s, s) *
                                           D2D1::Matrix3x2F::Translation(ox, oy) *
                                           oldTransform;
        pRT->SetTransform(iconTransform);
        pRT->DrawGeometry(m_pWrenchGeometry.Get(), pBrush, strokeWidth / s, m_pStrokeStyle.Get());
        pRT->SetTransform(oldTransform);
    }
}

void LucideIcons::DrawRun(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    if (m_pRunGeometry) {
        D2D1_MATRIX_3X2_F oldTransform;
        pRT->GetTransform(&oldTransform);
        D2D1_MATRIX_3X2_F iconTransform = D2D1::Matrix3x2F::Scale(s, s) *
                                           D2D1::Matrix3x2F::Translation(ox, oy) *
                                           oldTransform;
        pRT->SetTransform(iconTransform);
        pRT->FillGeometry(m_pRunGeometry.Get(), pBrush);
        pRT->DrawGeometry(m_pRunGeometry.Get(), pBrush, strokeWidth / s, m_pStrokeStyle.Get());
        pRT->SetTransform(oldTransform);
    } else {
        D2D1_POINT_2F p1 = MapPoint(8.5f, 5.5f, ox, oy, s);
        D2D1_POINT_2F p2 = MapPoint(18.5f, 12.0f, ox, oy, s);
        D2D1_POINT_2F p3 = MapPoint(8.5f, 18.5f, ox, oy, s);
        pRT->DrawLine(p1, p2, pBrush, strokeWidth, m_pStrokeStyle.Get());
        pRT->DrawLine(p2, p3, pBrush, strokeWidth, m_pStrokeStyle.Get());
        pRT->DrawLine(p3, p1, pBrush, strokeWidth, m_pStrokeStyle.Get());
    }
}

void LucideIcons::DrawRestart(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    if (m_pRestartGeometry) {
        D2D1_MATRIX_3X2_F oldTransform;
        pRT->GetTransform(&oldTransform);
        D2D1_MATRIX_3X2_F iconTransform = D2D1::Matrix3x2F::Scale(s, s) *
                                           D2D1::Matrix3x2F::Translation(ox, oy) *
                                           oldTransform;
        pRT->SetTransform(iconTransform);
        pRT->DrawGeometry(m_pRestartGeometry.Get(), pBrush, strokeWidth / s, m_pStrokeStyle.Get());
        pRT->SetTransform(oldTransform);
    } else {
        // Fallback drawing if geometry not yet created
        D2D1_ELLIPSE circle = D2D1::Ellipse(MapPoint(12.0f, 12.0f, ox, oy, s), 8.5f * s, 8.5f * s);
        pRT->DrawEllipse(circle, pBrush, strokeWidth, m_pStrokeStyle.Get());
    }
}

void LucideIcons::DrawBug(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Beetle body capsule
    D2D1_ROUNDED_RECT body = D2D1::RoundedRect(
        D2D1::RectF(ox + 8.5f * s, oy + 7.5f * s, ox + 15.5f * s, oy + 18.5f * s),
        3.5f * s, 3.5f * s
    );
    pRT->DrawRoundedRectangle(body, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Head line
    pRT->DrawLine(MapPoint(8.5f, 11.0f, ox, oy, s), MapPoint(15.5f, 11.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Antennae
    pRT->DrawLine(MapPoint(10.0f, 7.5f, ox, oy, s), MapPoint(7.5f, 4.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(14.0f, 7.5f, ox, oy, s), MapPoint(16.5f, 4.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 3 Pairs of Legs:
    // Top pair
    pRT->DrawLine(MapPoint(8.5f, 11.0f, ox, oy, s), MapPoint(4.5f, 9.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(15.5f, 11.0f, ox, oy, s), MapPoint(19.5f, 9.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Middle pair
    pRT->DrawLine(MapPoint(8.5f, 14.5f, ox, oy, s), MapPoint(4.0f, 14.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(15.5f, 14.5f, ox, oy, s), MapPoint(20.0f, 14.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Bottom pair
    pRT->DrawLine(MapPoint(8.5f, 17.0f, ox, oy, s), MapPoint(4.5f, 19.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(15.5f, 17.0f, ox, oy, s), MapPoint(19.5f, 19.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawTerminal(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Prompt >
    pRT->DrawLine(MapPoint(5.0f, 7.5f, ox, oy, s), MapPoint(10.5f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(10.5f, 12.0f, ox, oy, s), MapPoint(5.0f, 16.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Cursor _
    pRT->DrawLine(MapPoint(12.5f, 16.5f, ox, oy, s), MapPoint(19.0f, 16.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawTerminalNotebook(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.54f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // 1. Notebook outer page outline
    D2D1_ROUNDED_RECT page = D2D1::RoundedRect(
        D2D1::RectF(ox + 4.5f * s, oy + 3.0f * s, ox + 20.5f * s, oy + 21.0f * s),
        2.5f * s, 2.5f * s
    );
    pRT->DrawRoundedRectangle(page, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 2. Vertical binding margin spine line
    pRT->DrawLine(MapPoint(7.5f, 3.0f, ox, oy, s), MapPoint(7.5f, 21.0f, ox, oy, s), pBrush, strokeWidth * 0.9f, m_pStrokeStyle.Get());

    // 3. Spiral binding loops on the left edge
    pRT->DrawLine(MapPoint(2.5f, 7.0f, ox, oy, s), MapPoint(5.0f, 7.0f, ox, oy, s), pBrush, strokeWidth * 0.9f, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(2.5f, 12.0f, ox, oy, s), MapPoint(5.0f, 12.0f, ox, oy, s), pBrush, strokeWidth * 0.9f, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(2.5f, 17.0f, ox, oy, s), MapPoint(5.0f, 17.0f, ox, oy, s), pBrush, strokeWidth * 0.9f, m_pStrokeStyle.Get());

    // 4. Terminal prompt inside the notebook: prompt chevron >
    pRT->DrawLine(MapPoint(10.0f, 9.5f, ox, oy, s), MapPoint(13.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(13.0f, 12.0f, ox, oy, s), MapPoint(10.0f, 14.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 5. Terminal cursor underline _
    pRT->DrawLine(MapPoint(14.5f, 14.5f, ox, oy, s), MapPoint(18.0f, 14.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawWindow(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(
        D2D1::RectF(ox + 2.5f * s, oy + 4.0f * s, ox + 21.5f * s, oy + 20.0f * s),
        2.5f * s, 2.5f * s
    );
    pRT->DrawRoundedRectangle(rrc, pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(2.5f, 9.0f, ox, oy, s), MapPoint(21.5f, 9.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(6.0f, 6.5f, ox, oy, s), MapPoint(6.5f, 6.5f, ox, oy, s), pBrush, strokeWidth * 1.3f, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(9.0f, 6.5f, ox, oy, s), MapPoint(9.5f, 6.5f, ox, oy, s), pBrush, strokeWidth * 1.3f, m_pStrokeStyle.Get());
}

void LucideIcons::DrawHelp(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.52f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    D2D1_ELLIPSE circle = D2D1::Ellipse(MapPoint(12.0f, 12.0f, ox, oy, s), 9.0f * s, 9.0f * s);
    pRT->DrawEllipse(circle, pBrush, strokeWidth, m_pStrokeStyle.Get());

    pRT->DrawLine(MapPoint(9.5f, 9.0f, ox, oy, s), MapPoint(12.0f, 6.8f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(12.0f, 6.8f, ox, oy, s), MapPoint(14.5f, 9.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(14.5f, 9.0f, ox, oy, s), MapPoint(12.0f, 11.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(12.0f, 11.5f, ox, oy, s), MapPoint(12.0f, 13.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    pRT->DrawLine(MapPoint(12.0f, 16.5f, ox, oy, s), MapPoint(12.0f, 17.0f, ox, oy, s), pBrush, strokeWidth * 1.3f, m_pStrokeStyle.Get());
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

static inline void EnsureStrokeStyle(ID2D1RenderTarget* pRT, ComPtr<ID2D1StrokeStyle>& pStyle) {
    if (!pStyle && pRT) {
        ComPtr<ID2D1Factory> pFactory;
        pRT->GetFactory(&pFactory);
        if (pFactory) {
            D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
                D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f
            );
            pFactory->CreateStrokeStyle(props, nullptr, 0, &pStyle);
        }
    }
}

FileIconType LucideIcons::GetIconTypeForPath(const std::wstring& filename, bool isFolder, bool isExpanded) {
    if (isFolder) {
        return isExpanded ? FileIconType::FolderOpen : FileIconType::Folder;
    }

    std::wstring lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    // Exact filename matches
    if (lower == L"package.json" || lower == L"package-lock.json" || lower == L"composer.json" || lower == L"tsconfig.json") {
        return FileIconType::Json;
    }
    if (lower == L"cmakelists.txt" || lower == L"makefile") {
        return FileIconType::Cpp;
    }
    if (lower == L"dockerfile") {
        return FileIconType::Terminal;
    }

    // Extension matches
    auto dot = lower.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        std::wstring ext = lower.substr(dot);
        if (ext == L".php" || ext == L".phtml") return FileIconType::Php;
        if (ext == L".cpp" || ext == L".cxx" || ext == L".cc" || ext == L".c") return FileIconType::Cpp;
        if (ext == L".hpp" || ext == L".hxx" || ext == L".hh" || ext == L".h") return FileIconType::Header;
        if (ext == L".js" || ext == L".mjs" || ext == L".cjs" || ext == L".jsx") return FileIconType::Js;
        if (ext == L".ts" || ext == L".tsx") return FileIconType::Ts;
        if (ext == L".json" || ext == L".jsonc") return FileIconType::Json;
        if (ext == L".md" || ext == L".markdown" || ext == L".mdown") return FileIconType::Markdown;
        if (ext == L".html" || ext == L".htm") return FileIconType::Html;
        if (ext == L".css" || ext == L".scss" || ext == L".sass" || ext == L".less") return FileIconType::Css;
        if (ext == L".bat" || ext == L".cmd" || ext == L".sh" || ext == L".ps1" || ext == L".bash") return FileIconType::Terminal;
        if (ext == L".yml" || ext == L".yaml") return FileIconType::Yaml;
        if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".svg" || ext == L".gif" || ext == L".ico" || ext == L".webp") return FileIconType::Image;
        if (ext == L".exe" || ext == L".dll" || ext == L".bin" || ext == L".so" || ext == L".obj") return FileIconType::Exe;
        if (ext == L".sql") return FileIconType::Sql;
    }

    return FileIconType::DefaultFile;
}

void LucideIcons::DrawFileIcon(ID2D1RenderTarget* pRT, Theme& theme, const D2D1_RECT_F& bounds, FileIconType type) const {
    if (!pRT) return;

    switch (type) {
        case FileIconType::Folder:
            DrawFolder(pRT, bounds, theme.brushIconFolder.Get(), false);
            break;
        case FileIconType::FolderOpen:
            DrawFolder(pRT, bounds, theme.brushIconFolderOpen.Get(), true);
            break;
        case FileIconType::Php:
            DrawFilePhp(pRT, bounds, theme.brushIconPurple.Get());
            break;
        case FileIconType::Cpp:
            DrawFileCpp(pRT, bounds, theme.brushIconBlue.Get());
            break;
        case FileIconType::Header:
            DrawFileHeader(pRT, bounds, theme.brushIconBlue.Get());
            break;
        case FileIconType::Js:
            DrawFileJs(pRT, bounds, theme.brushIconYellow.Get());
            break;
        case FileIconType::Ts:
            DrawFileTs(pRT, bounds, theme.brushIconBlue.Get());
            break;
        case FileIconType::Json:
            DrawFileJson(pRT, bounds, theme.brushIconYellow.Get());
            break;
        case FileIconType::Markdown:
            DrawFileMarkdown(pRT, bounds, theme.brushIconBlue.Get());
            break;
        case FileIconType::Html:
            DrawFileHtml(pRT, bounds, theme.brushIconOrange.Get());
            break;
        case FileIconType::Css:
            DrawFileCss(pRT, bounds, theme.brushIconBlue.Get());
            break;
        case FileIconType::Terminal:
            DrawTerminal(pRT, bounds, theme.brushIconGreen.Get());
            break;
        case FileIconType::Yaml:
            DrawFileYaml(pRT, bounds, theme.brushIconRed.Get());
            break;
        case FileIconType::Image:
            DrawFileImage(pRT, bounds, theme.brushIconPurple.Get());
            break;
        case FileIconType::Exe:
            DrawFileExe(pRT, bounds, theme.brushIconGreen.Get());
            break;
        case FileIconType::Sql:
            DrawDatabase(pRT, bounds, theme.brushIconOrange ? theme.brushIconOrange.Get() : theme.brushIconBlue.Get(), 1.25f);
            break;
        case FileIconType::DefaultFile:
        default:
            DrawFile(pRT, bounds, theme.brushIconGray.Get());
            break;
    }
}

void LucideIcons::DrawFolder(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, bool isOpen, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Folder tab & back: (3, 5) -> (9, 5) -> (11, 7.5) -> (21, 7.5) -> (21, 19) -> (3, 19) -> (3, 5)
    pRT->DrawLine(MapPoint(3.0f, 5.0f, ox, oy, s), MapPoint(9.0f, 5.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(9.0f, 5.0f, ox, oy, s), MapPoint(11.0f, 7.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(11.0f, 7.5f, ox, oy, s), MapPoint(21.0f, 7.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(21.0f, 7.5f, ox, oy, s), MapPoint(21.0f, 19.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(21.0f, 19.0f, ox, oy, s), MapPoint(3.0f, 19.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(3.0f, 19.0f, ox, oy, s), MapPoint(3.0f, 5.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    if (isOpen) {
        // Open front flap angled forward
        pRT->DrawLine(MapPoint(1.5f, 19.0f, ox, oy, s), MapPoint(4.5f, 11.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
        pRT->DrawLine(MapPoint(4.5f, 11.5f, ox, oy, s), MapPoint(21.5f, 11.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
        pRT->DrawLine(MapPoint(21.5f, 11.5f, ox, oy, s), MapPoint(18.5f, 19.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    } else {
        // Closed horizontal crease
        pRT->DrawLine(MapPoint(3.0f, 9.5f, ox, oy, s), MapPoint(21.0f, 9.5f, ox, oy, s), pBrush, strokeWidth * 0.85f, m_pStrokeStyle.Get());
    }
}

void LucideIcons::DrawFileJson(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Left curly brace '{'
    pRT->DrawLine(MapPoint(9.5f, 4.0f, ox, oy, s), MapPoint(7.0f, 4.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(7.0f, 4.0f, ox, oy, s), MapPoint(7.0f, 10.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(7.0f, 10.0f, ox, oy, s), MapPoint(4.5f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(4.5f, 12.0f, ox, oy, s), MapPoint(7.0f, 14.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(7.0f, 14.0f, ox, oy, s), MapPoint(7.0f, 20.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(7.0f, 20.0f, ox, oy, s), MapPoint(9.5f, 20.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Right curly brace '}'
    pRT->DrawLine(MapPoint(14.5f, 4.0f, ox, oy, s), MapPoint(17.0f, 4.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(17.0f, 4.0f, ox, oy, s), MapPoint(17.0f, 10.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(17.0f, 10.0f, ox, oy, s), MapPoint(19.5f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(19.5f, 12.0f, ox, oy, s), MapPoint(17.0f, 14.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(17.0f, 14.0f, ox, oy, s), MapPoint(17.0f, 20.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(17.0f, 20.0f, ox, oy, s), MapPoint(14.5f, 20.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawFileJs(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // 'J'
    pRT->DrawLine(MapPoint(6.5f, 6.0f, ox, oy, s), MapPoint(11.5f, 6.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(11.5f, 6.0f, ox, oy, s), MapPoint(11.5f, 15.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(11.5f, 15.5f, ox, oy, s), MapPoint(9.5f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(9.5f, 18.0f, ox, oy, s), MapPoint(6.5f, 16.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 'S'
    pRT->DrawLine(MapPoint(18.0f, 7.5f, ox, oy, s), MapPoint(14.5f, 6.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(14.5f, 6.0f, ox, oy, s), MapPoint(13.5f, 8.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(13.5f, 8.5f, ox, oy, s), MapPoint(18.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(18.0f, 12.0f, ox, oy, s), MapPoint(17.5f, 17.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(17.5f, 17.5f, ox, oy, s), MapPoint(13.5f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawFileTs(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // 'T'
    pRT->DrawLine(MapPoint(4.0f, 6.0f, ox, oy, s), MapPoint(13.0f, 6.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(8.5f, 6.0f, ox, oy, s), MapPoint(8.5f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 'S'
    pRT->DrawLine(MapPoint(19.0f, 7.5f, ox, oy, s), MapPoint(15.5f, 6.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(15.5f, 6.0f, ox, oy, s), MapPoint(14.5f, 8.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(14.5f, 8.5f, ox, oy, s), MapPoint(19.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(19.0f, 12.0f, ox, oy, s), MapPoint(18.5f, 17.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(18.5f, 17.5f, ox, oy, s), MapPoint(14.5f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawFileCpp(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // 'C'
    pRT->DrawLine(MapPoint(10.5f, 7.0f, ox, oy, s), MapPoint(6.5f, 7.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(6.5f, 7.0f, ox, oy, s), MapPoint(4.5f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(4.5f, 12.0f, ox, oy, s), MapPoint(6.5f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(6.5f, 17.0f, ox, oy, s), MapPoint(10.5f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // First '+'
    pRT->DrawLine(MapPoint(12.0f, 12.0f, ox, oy, s), MapPoint(16.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(14.0f, 10.0f, ox, oy, s), MapPoint(14.0f, 14.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Second '+'
    pRT->DrawLine(MapPoint(17.5f, 12.0f, ox, oy, s), MapPoint(21.5f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(19.5f, 10.0f, ox, oy, s), MapPoint(19.5f, 14.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawFileHeader(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // 'C'
    pRT->DrawLine(MapPoint(9.5f, 7.0f, ox, oy, s), MapPoint(5.5f, 7.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(5.5f, 7.0f, ox, oy, s), MapPoint(4.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(4.0f, 12.0f, ox, oy, s), MapPoint(5.5f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(5.5f, 17.0f, ox, oy, s), MapPoint(9.5f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 'H'
    pRT->DrawLine(MapPoint(13.5f, 7.0f, ox, oy, s), MapPoint(13.5f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(19.0f, 7.0f, ox, oy, s), MapPoint(19.0f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(13.5f, 12.0f, ox, oy, s), MapPoint(19.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawFilePhp(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // First 'p'
    pRT->DrawLine(MapPoint(4.0f, 8.0f, ox, oy, s), MapPoint(4.0f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(4.0f, 8.0f, ox, oy, s), MapPoint(8.0f, 8.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(8.0f, 8.0f, ox, oy, s), MapPoint(8.0f, 13.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(8.0f, 13.0f, ox, oy, s), MapPoint(4.0f, 13.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 'h'
    pRT->DrawLine(MapPoint(10.5f, 5.0f, ox, oy, s), MapPoint(10.5f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(10.5f, 10.5f, ox, oy, s), MapPoint(14.5f, 10.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(14.5f, 10.5f, ox, oy, s), MapPoint(14.5f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Second 'p'
    pRT->DrawLine(MapPoint(17.0f, 8.0f, ox, oy, s), MapPoint(17.0f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(17.0f, 8.0f, ox, oy, s), MapPoint(21.0f, 8.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(21.0f, 8.0f, ox, oy, s), MapPoint(21.0f, 13.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(21.0f, 13.0f, ox, oy, s), MapPoint(17.0f, 13.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawFileMarkdown(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // 'M'
    pRT->DrawLine(MapPoint(3.5f, 17.0f, ox, oy, s), MapPoint(3.5f, 7.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(3.5f, 7.0f, ox, oy, s), MapPoint(8.5f, 12.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(8.5f, 12.5f, ox, oy, s), MapPoint(13.5f, 7.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(13.5f, 7.0f, ox, oy, s), MapPoint(13.5f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // '↓'
    pRT->DrawLine(MapPoint(18.5f, 7.0f, ox, oy, s), MapPoint(18.5f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(16.0f, 14.5f, ox, oy, s), MapPoint(18.5f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(18.5f, 17.0f, ox, oy, s), MapPoint(21.0f, 14.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawFileImage(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Picture frame: (3, 4) to (21, 20) with 2px corner radius
    D2D1_RECT_F rcFrame = D2D1::RectF(ox + 3.0f * s, oy + 4.0f * s, ox + 21.0f * s, oy + 20.0f * s);
    pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcFrame, 2.0f * s, 2.0f * s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Circle Sun: Center at (8.5, 9.0) radius 2.0
    D2D1_ELLIPSE sun = D2D1::Ellipse(MapPoint(8.5f, 9.0f, ox, oy, s), 2.0f * s, 2.0f * s);
    pRT->DrawEllipse(sun, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Mountains: (21, 15) -> (16, 10.5) -> (12, 14.5) -> (9, 12) -> (3, 17.5)
    pRT->DrawLine(MapPoint(21.0f, 15.0f, ox, oy, s), MapPoint(16.0f, 10.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(16.0f, 10.5f, ox, oy, s), MapPoint(12.0f, 14.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(12.0f, 14.5f, ox, oy, s), MapPoint(9.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(9.0f, 12.0f, ox, oy, s), MapPoint(3.0f, 17.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawFileYaml(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // 'Y'
    pRT->DrawLine(MapPoint(5.0f, 6.0f, ox, oy, s), MapPoint(11.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(17.0f, 6.0f, ox, oy, s), MapPoint(11.0f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(11.0f, 12.0f, ox, oy, s), MapPoint(11.0f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // '!'
    pRT->DrawLine(MapPoint(20.0f, 6.0f, ox, oy, s), MapPoint(20.0f, 14.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    D2D1_ELLIPSE dot = D2D1::Ellipse(MapPoint(20.0f, 17.5f, ox, oy, s), 1.0f * s, 1.0f * s);
    pRT->FillEllipse(dot, pBrush);
}

void LucideIcons::DrawFileExe(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Lucide Cogwheel / Settings gear for Executables (.exe)
    D2D1_ELLIPSE hub = D2D1::Ellipse(MapPoint(12.0f, 12.0f, ox, oy, s), 4.5f * s, 4.5f * s);
    pRT->DrawEllipse(hub, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 8 gear teeth
    pRT->DrawLine(MapPoint(12.0f, 3.5f, ox, oy, s), MapPoint(12.0f, 6.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(12.0f, 17.5f, ox, oy, s), MapPoint(12.0f, 20.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(3.5f, 12.0f, ox, oy, s), MapPoint(6.5f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(17.5f, 12.0f, ox, oy, s), MapPoint(20.5f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    pRT->DrawLine(MapPoint(6.0f, 6.0f, ox, oy, s), MapPoint(8.1f, 8.1f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(15.9f, 15.9f, ox, oy, s), MapPoint(18.0f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(18.0f, 6.0f, ox, oy, s), MapPoint(15.9f, 8.1f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(8.1f, 15.9f, ox, oy, s), MapPoint(6.0f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawFileHtml(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // '<'
    pRT->DrawLine(MapPoint(7.5f, 7.5f, ox, oy, s), MapPoint(3.5f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(3.5f, 12.0f, ox, oy, s), MapPoint(7.5f, 16.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // '/'
    pRT->DrawLine(MapPoint(13.5f, 6.0f, ox, oy, s), MapPoint(10.5f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // '>'
    pRT->DrawLine(MapPoint(16.5f, 7.5f, ox, oy, s), MapPoint(20.5f, 12.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(20.5f, 12.0f, ox, oy, s), MapPoint(16.5f, 16.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawFileCss(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.90f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Lucide Hash '#'
    pRT->DrawLine(MapPoint(9.0f, 4.0f, ox, oy, s), MapPoint(7.0f, 20.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(17.0f, 4.0f, ox, oy, s), MapPoint(15.0f, 20.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(4.0f, 9.5f, ox, oy, s), MapPoint(20.0f, 9.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(4.0f, 15.0f, ox, oy, s), MapPoint(20.0f, 15.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawSplitEditor(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.70f;
    const float cx = bounds.left + bw * 0.5f;
    const float cy = bounds.top + bh * 0.5f;

    // Narrower width for compact, well-balanced split rectangle
    const float halfW = iconSize * 0.38f;
    const float halfH = iconSize * 0.45f;
    D2D1_RECT_F rc = D2D1::RectF(cx - halfW, cy - halfH, cx + halfW, cy + halfH);
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rc, 2.0f, 2.0f);

    pRT->DrawRoundedRectangle(&rrc, pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(
        D2D1::Point2F(cx, cy - halfH),
        D2D1::Point2F(cx, cy + halfH),
        pBrush, strokeWidth, m_pStrokeStyle.Get()
    );
}

void LucideIcons::DrawExtensionsGrid(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float cx = bounds.left + bw * 0.5f;
    const float cy = bounds.top + bh * 0.5f;

    // 2x2 grid of squares with rounded corners, sized proportionally to match beside buttons (~16-17px total)
    const float totalSize = (std::min)(bw, bh) * 0.56f;
    const float g = 2.6f;
    const float s = (totalSize - g) * 0.5f;
    const float off = g * 0.5f;
    const float r = 1.4f;

    // Top-left
    D2D1_ROUNDED_RECT rrcTL = D2D1::RoundedRect(D2D1::RectF(cx - s - off, cy - s - off, cx - off, cy - off), r, r);
    pRT->DrawRoundedRectangle(&rrcTL, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Top-right
    D2D1_ROUNDED_RECT rrcTR = D2D1::RoundedRect(D2D1::RectF(cx + off, cy - s - off, cx + s + off, cy - off), r, r);
    pRT->DrawRoundedRectangle(&rrcTR, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Bottom-left
    D2D1_ROUNDED_RECT rrcBL = D2D1::RoundedRect(D2D1::RectF(cx - s - off, cy + off, cx - off, cy + s + off), r, r);
    pRT->DrawRoundedRectangle(&rrcBL, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Bottom-right
    D2D1_ROUNDED_RECT rrcBR = D2D1::RoundedRect(D2D1::RectF(cx + off, cy + off, cx + s + off, cy + s + off), r, r);
    pRT->DrawRoundedRectangle(&rrcBR, pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawReturnBack(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float cx = bounds.left + bw * 0.5f;
    const float cy = bounds.top + bh * 0.5f;
    const float s = (std::min)(bw, bh) * 0.55f;

    // Dimensions matching return back icon (Image 5)
    const float arrowTipX = cx - s * 0.48f;
    const float topY = cy - s * 0.28f;
    const float botY = cy + s * 0.36f;
    const float r = (botY - topY) * 0.5f;
    const float rightX = cx + s * 0.48f;
    const float headSize = s * 0.28f;

    // Arrow Head pointing Left (<)
    pRT->DrawLine(
        D2D1::Point2F(arrowTipX + headSize, topY - headSize),
        D2D1::Point2F(arrowTipX, topY),
        pBrush, strokeWidth, m_pStrokeStyle.Get()
    );
    pRT->DrawLine(
        D2D1::Point2F(arrowTipX + headSize, topY + headSize),
        D2D1::Point2F(arrowTipX, topY),
        pBrush, strokeWidth, m_pStrokeStyle.Get()
    );

    // Top horizontal shaft
    pRT->DrawLine(
        D2D1::Point2F(arrowTipX, topY),
        D2D1::Point2F(rightX - r, topY),
        pBrush, strokeWidth, m_pStrokeStyle.Get()
    );

    // Bottom horizontal shaft
    const float botStartX = cx - s * 0.30f;
    pRT->DrawLine(
        D2D1::Point2F(botStartX, botY),
        D2D1::Point2F(rightX - r, botY),
        pBrush, strokeWidth, m_pStrokeStyle.Get()
    );

    // Right rounded curve connecting top and bottom shafts (Image 5)
    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (pFactory) {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> pPath;
        if (SUCCEEDED(pFactory->CreatePathGeometry(&pPath)) && pPath) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> pSink;
            if (SUCCEEDED(pPath->Open(&pSink)) && pSink) {
                pSink->BeginFigure(D2D1::Point2F(rightX - r, topY), D2D1_FIGURE_BEGIN_HOLLOW);
                pSink->AddArc(D2D1::ArcSegment(
                    D2D1::Point2F(rightX - r, botY),
                    D2D1::SizeF(r, r),
                    0.0f,
                    D2D1_SWEEP_DIRECTION_CLOCKWISE,
                    D2D1_ARC_SIZE_SMALL
                ));
                pSink->EndFigure(D2D1_FIGURE_END_OPEN);
                pSink->Close();
                pRT->DrawGeometry(pPath.Get(), pBrush, strokeWidth, m_pStrokeStyle.Get());
            }
        }
    }
}

void LucideIcons::DrawDatabase(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float cx = bounds.left + bw * 0.5f;
    const float cy = bounds.top + bh * 0.5f;
    const float s  = (std::min)(bw, bh) * 0.65f;
    const float rx = s * 0.48f;
    const float ry = s * 0.16f;
    const float topY = cy - s * 0.38f;
    const float midY = cy + s * 0.02f;
    const float botY = cy + s * 0.42f;

    // Top full ellipse
    pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, topY), rx, ry), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Left and right vertical spine lines
    pRT->DrawLine(D2D1::Point2F(cx - rx, topY), D2D1::Point2F(cx - rx, botY), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(D2D1::Point2F(cx + rx, topY), D2D1::Point2F(cx + rx, botY), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Middle curved arc and bottom curved arc
    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (pFactory) {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> pPath;
        if (SUCCEEDED(pFactory->CreatePathGeometry(&pPath)) && pPath) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> pSink;
            if (SUCCEEDED(pPath->Open(&pSink)) && pSink) {
                // Middle arc: from (cx - rx, midY) curving down to (cx + rx, midY)
                pSink->BeginFigure(D2D1::Point2F(cx - rx, midY), D2D1_FIGURE_BEGIN_HOLLOW);
                pSink->AddArc(D2D1::ArcSegment(
                    D2D1::Point2F(cx + rx, midY),
                    D2D1::SizeF(rx, ry),
                    0.0f,
                    D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                    D2D1_ARC_SIZE_SMALL
                ));
                pSink->EndFigure(D2D1_FIGURE_END_OPEN);

                // Bottom arc: from (cx - rx, botY) curving down to (cx + rx, botY)
                pSink->BeginFigure(D2D1::Point2F(cx - rx, botY), D2D1_FIGURE_BEGIN_HOLLOW);
                pSink->AddArc(D2D1::ArcSegment(
                    D2D1::Point2F(cx + rx, botY),
                    D2D1::SizeF(rx, ry),
                    0.0f,
                    D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                    D2D1_ARC_SIZE_SMALL
                ));
                pSink->EndFigure(D2D1_FIGURE_END_OPEN);

                pSink->Close();
                pRT->DrawGeometry(pPath.Get(), pBrush, strokeWidth, m_pStrokeStyle.Get());
            }
        }
    }
}

void LucideIcons::DrawServer(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float cx = bounds.left + bw * 0.5f;
    const float cy = bounds.top + bh * 0.5f;
    const float s  = (std::min)(bw, bh) * 0.65f;
    const float halfW = s * 0.50f;
    const float chassisH = s * 0.38f;
    const float gap = s * 0.16f;

    // Top server tray
    float topY1 = cy - gap * 0.5f - chassisH;
    float topY2 = cy - gap * 0.5f;
    D2D1_ROUNDED_RECT rrcTop = D2D1::RoundedRect(D2D1::RectF(cx - halfW, topY1, cx + halfW, topY2), 2.0f, 2.0f);
    pRT->DrawRoundedRectangle(&rrcTop, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Top tray status dot & drive line
    float midTopY = (topY1 + topY2) * 0.5f;
    pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - halfW + 4.0f, midTopY), 1.0f, 1.0f), pBrush, strokeWidth);
    pRT->DrawLine(D2D1::Point2F(cx - 1.0f, midTopY), D2D1::Point2F(cx + halfW - 4.0f, midTopY), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Bottom server tray
    float botY1 = cy + gap * 0.5f;
    float botY2 = cy + gap * 0.5f + chassisH;
    D2D1_ROUNDED_RECT rrcBot = D2D1::RoundedRect(D2D1::RectF(cx - halfW, botY1, cx + halfW, botY2), 2.0f, 2.0f);
    pRT->DrawRoundedRectangle(&rrcBot, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Bottom tray status dot & drive line
    float midBotY = (botY1 + botY2) * 0.5f;
    pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - halfW + 4.0f, midBotY), 1.0f, 1.0f), pBrush, strokeWidth);
    pRT->DrawLine(D2D1::Point2F(cx - 1.0f, midBotY), D2D1::Point2F(cx + halfW - 4.0f, midBotY), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawLogIcon(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float cx = bounds.left + bw * 0.5f;
    const float cy = bounds.top + bh * 0.5f;
    const float s  = (std::min)(bw, bh) * 0.65f;
    const float halfW = s * 0.42f;
    const float halfH = s * 0.50f;

    // Document outer frame with rounded corners
    D2D1_ROUNDED_RECT rrcDoc = D2D1::RoundedRect(D2D1::RectF(cx - halfW, cy - halfH, cx + halfW, cy + halfH), 2.0f, 2.0f);
    pRT->DrawRoundedRectangle(&rrcDoc, pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 3 horizontal log lines inside
    float lineLeft  = cx - halfW + 3.2f;
    float lineRight = cx + halfW - 3.2f;
    float lineY1    = cy - halfH * 0.35f;
    float lineY2    = cy;
    float lineY3    = cy + halfH * 0.35f;

    pRT->DrawLine(D2D1::Point2F(lineLeft, lineY1), D2D1::Point2F(lineRight, lineY1), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(D2D1::Point2F(lineLeft, lineY2), D2D1::Point2F(lineRight - 2.0f, lineY2), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(D2D1::Point2F(lineLeft, lineY3), D2D1::Point2F(lineRight - 4.5f, lineY3), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawSettings(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float cx = bounds.left + bw * 0.5f;
    const float cy = bounds.top + bh * 0.5f;
    const float s = (std::min)(bw, bh);
    const float rCenter = s * 0.18f;
    const float rOuter = s * 0.34f;
    const float toothLen = s * 0.12f;

    // Center circular hole
    pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rCenter, rCenter), pBrush, strokeWidth);

    // 6 cog teeth around outer perimeter
    for (int i = 0; i < 6; ++i) {
        float angle = i * (3.14159265f / 3.0f);
        float cosA = cosf(angle);
        float sinA = sinf(angle);
        D2D1_POINT_2F p1 = D2D1::Point2F(cx + (rOuter - toothLen * 0.2f) * cosA, cy + (rOuter - toothLen * 0.2f) * sinA);
        D2D1_POINT_2F p2 = D2D1::Point2F(cx + (rOuter + toothLen) * cosA, cy + (rOuter + toothLen) * sinA);
        pRT->DrawLine(p1, p2, pBrush, strokeWidth * 1.6f, m_pStrokeStyle.Get());
    }

    // Outer gear rim
    pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rOuter, rOuter), pBrush, strokeWidth);
}

void LucideIcons::DrawExternalLink(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.54f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Box with top-right opening
    pRT->DrawLine(MapPoint(11.0f, 6.0f, ox, oy, s), MapPoint(5.0f, 6.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(5.0f, 6.0f, ox, oy, s), MapPoint(5.0f, 19.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(5.0f, 19.0f, ox, oy, s), MapPoint(18.0f, 19.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(18.0f, 19.0f, ox, oy, s), MapPoint(18.0f, 13.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Diagonal arrow pointing out
    pRT->DrawLine(MapPoint(10.0f, 14.0f, ox, oy, s), MapPoint(20.0f, 4.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(15.0f, 4.0f, ox, oy, s), MapPoint(20.0f, 4.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(20.0f, 4.0f, ox, oy, s), MapPoint(20.0f, 9.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawGraduationCap(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.58f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Official Lucide graduation-cap (student hat):
    // 1. Mortarboard diamond cap top:
    pRT->DrawLine(MapPoint(2.5f, 10.0f, ox, oy, s), MapPoint(12.0f, 5.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(12.0f, 5.0f, ox, oy, s), MapPoint(21.5f, 10.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(21.5f, 10.0f, ox, oy, s), MapPoint(12.0f, 14.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(12.0f, 14.5f, ox, oy, s), MapPoint(2.5f, 10.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 2. Head / Cap underside curved band:
    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (pFactory) {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> pCapGeo;
        if (SUCCEEDED(pFactory->CreatePathGeometry(&pCapGeo))) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(pCapGeo->Open(&sink))) {
                sink->BeginFigure(MapPoint(6.0f, 12.5f, ox, oy, s), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(MapPoint(6.0f, 15.5f, ox, oy, s));
                D2D1_ARC_SEGMENT arc{};
                arc.point = MapPoint(18.0f, 15.5f, ox, oy, s);
                arc.size = D2D1::SizeF(6.0f * s, 3.2f * s);
                arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                sink->AddArc(arc);
                sink->AddLine(MapPoint(18.0f, 12.5f, ox, oy, s));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                pRT->DrawGeometry(pCapGeo.Get(), pBrush, strokeWidth, m_pStrokeStyle.Get());
            }
        }
    }

    // 3. Tassel hanging down from right corner
    pRT->DrawLine(MapPoint(22.0f, 10.2f, ox, oy, s), MapPoint(22.0f, 17.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Small tassel knot / bob at the tip
    pRT->DrawEllipse(D2D1::Ellipse(MapPoint(22.0f, 17.8f, ox, oy, s), 0.8f * s, 0.8f * s), pBrush, strokeWidth * 0.8f);
}

void LucideIcons::DrawBook(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.85f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Center spine
    pRT->DrawLine(MapPoint(12.0f, 6.5f, ox, oy, s), MapPoint(12.0f, 20.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Left page:
    // Top edge: (12, 6.5) -> (3.5, 4.5)
    pRT->DrawLine(MapPoint(12.0f, 6.5f, ox, oy, s), MapPoint(3.5f, 4.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Left outer edge: (3.5, 4.5) -> (3.5, 18.0)
    pRT->DrawLine(MapPoint(3.5f, 4.5f, ox, oy, s), MapPoint(3.5f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Bottom edge: (3.5, 18.0) -> (12.0, 20.0)
    pRT->DrawLine(MapPoint(3.5f, 18.0f, ox, oy, s), MapPoint(12.0f, 20.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // Right page:
    // Top edge: (12, 6.5) -> (20.5, 4.5)
    pRT->DrawLine(MapPoint(12.0f, 6.5f, ox, oy, s), MapPoint(20.5f, 4.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Right outer edge: (20.5, 4.5) -> (20.5, 18.0)
    pRT->DrawLine(MapPoint(20.5f, 4.5f, ox, oy, s), MapPoint(20.5f, 18.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Bottom edge: (20.5, 18.0) -> (12.0, 20.0)
    pRT->DrawLine(MapPoint(20.5f, 18.0f, ox, oy, s), MapPoint(12.0f, 20.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawTestFlask(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float iconSize = (std::min)(bw, bh) * 0.85f;
    const float s = iconSize / 24.0f;
    const float ox = bounds.left + (bw - 24.0f * s) / 2.0f;
    const float oy = bounds.top + (bh - 24.0f * s) / 2.0f;

    // Rim: (9.0, 3.0) -> (15.0, 3.0)
    pRT->DrawLine(MapPoint(9.0f, 3.0f, ox, oy, s), MapPoint(15.0f, 3.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Neck: (10.0, 3.0) -> (10.0, 8.5) and (14.0, 3.0) -> (14.0, 8.5)
    pRT->DrawLine(MapPoint(10.0f, 3.0f, ox, oy, s), MapPoint(10.0f, 8.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(14.0f, 3.0f, ox, oy, s), MapPoint(14.0f, 8.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Body: (10.0, 8.5) -> (5.0, 19.5) -> (19.0, 19.5) -> (14.0, 8.5)
    pRT->DrawLine(MapPoint(10.0f, 8.5f, ox, oy, s), MapPoint(5.0f, 19.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(5.0f, 19.5f, ox, oy, s), MapPoint(19.0f, 19.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(19.0f, 19.5f, ox, oy, s), MapPoint(14.0f, 8.5f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Liquid level line: (7.5, 15.0) -> (16.5, 15.0)
    pRT->DrawLine(MapPoint(7.5f, 15.0f, ox, oy, s), MapPoint(16.5f, 15.0f, ox, oy, s), pBrush, strokeWidth * 0.9f, m_pStrokeStyle.Get());
}

void LucideIcons::DrawMoreHorizontal(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush) const {
    if (!pRT || !pBrush) return;
    const float cx = (bounds.left + bounds.right) * 0.5f;
    const float cy = (bounds.top + bounds.bottom) * 0.5f;
    const float r = 1.9f;
    const float spacing = 5.2f;

    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - spacing, cy), r, r), pBrush);
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), pBrush);
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + spacing, cy), r, r), pBrush);
}

void LucideIcons::DrawFileNetwork(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float bw = bounds.right - bounds.left;
    const float bh = bounds.bottom - bounds.top;
    const float minDim = (std::min)(bw, bh);
    // Optical scale: Activity Rail items (~48px) scale by 0.46f (~22px); Tab icons (~16px) scale by 0.88f (~14px)
    const float iconSize = (minDim > 28.0f) ? (minDim * 0.46f) : (minDim * 0.88f);
    const float s = iconSize / 24.0f;
    const float cx = (bounds.left + bounds.right) * 0.5f;
    const float cy = (bounds.top + bounds.bottom) * 0.5f;

    const float sw = (minDim > 28.0f) ? (strokeWidth * s * 1.15f) : (strokeWidth * 0.95f);
    const float rCen = 3.0f * s;
    const float rSat = 2.0f * s;

    // 5 Satellite nodes matching user reference image
    const D2D1_POINT_2F satellites[5] = {
        D2D1::Point2F(cx - 6.2f * s, cy - 6.2f * s), // Top-Left
        D2D1::Point2F(cx + 3.2f * s, cy - 7.5f * s), // Top-Right
        D2D1::Point2F(cx + 7.8f * s, cy - 0.5f * s), // Right-Mid
        D2D1::Point2F(cx + 4.2f * s, cy + 7.5f * s), // Bottom-Right
        D2D1::Point2F(cx - 6.0f * s, cy + 6.0f * s)  // Bottom-Left
    };

    // Draw connecting spokes stopping cleanly at circle perimeters
    for (int i = 0; i < 5; ++i) {
        float dx = satellites[i].x - cx;
        float dy = satellites[i].y - cy;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 0.001f) {
            float ux = dx / len;
            float uy = dy / len;
            D2D1_POINT_2F pStart = D2D1::Point2F(cx + ux * rCen, cy + uy * rCen);
            D2D1_POINT_2F pEnd = D2D1::Point2F(satellites[i].x - ux * rSat, satellites[i].y - uy * rSat);
            pRT->DrawLine(pStart, pEnd, pBrush, sw, m_pStrokeStyle.Get());
        }
        // Draw satellite circle ring
        pRT->DrawEllipse(D2D1::Ellipse(satellites[i], rSat, rSat), pBrush, sw, m_pStrokeStyle.Get());
    }

    // Draw central hub circle ring
    pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rCen, rCen), pBrush, sw, m_pStrokeStyle.Get());
}

void LucideIcons::DrawBell(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float w = bounds.right - bounds.left;
    const float h = bounds.bottom - bounds.top;
    const float s = (std::min)(w, h) / 24.0f;
    const float ox = bounds.left + (w - 24.0f * s) * 0.5f;
    const float oy = bounds.top + (h - 24.0f * s) * 0.5f;

    // Top loop: (10.0, 4.0) -> (14.0, 4.0)
    pRT->DrawLine(MapPoint(10.0f, 4.0f, ox, oy, s), MapPoint(14.0f, 4.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Bell body sides
    pRT->DrawLine(MapPoint(10.0f, 4.0f, ox, oy, s), MapPoint(6.0f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(14.0f, 4.0f, ox, oy, s), MapPoint(18.0f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Rim
    pRT->DrawLine(MapPoint(4.0f, 17.0f, ox, oy, s), MapPoint(20.0f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Clapper at bottom (10.5, 20.0) -> (13.5, 20.0)
    pRT->DrawLine(MapPoint(10.5f, 20.0f, ox, oy, s), MapPoint(13.5f, 20.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawGitHub(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    const float w = bounds.right - bounds.left;
    const float h = bounds.bottom - bounds.top;
    const float s = (std::min)(w, h) / 24.0f;
    const float ox = bounds.left + (w - 24.0f * s) * 0.5f;
    const float oy = bounds.top + (h - 24.0f * s) * 0.5f;

    // GitHub Octocat silhouette mark
    // Circular body outline
    pRT->DrawEllipse(D2D1::Ellipse(MapPoint(12.0f, 12.0f, ox, oy, s), 8.5f * s, 8.5f * s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Pointed cat ears
    pRT->DrawLine(MapPoint(8.2f, 6.2f, ox, oy, s), MapPoint(9.4f, 9.2f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(15.8f, 6.2f, ox, oy, s), MapPoint(14.6f, 9.2f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    // Lower chest curve
    pRT->DrawLine(MapPoint(9.5f, 15.0f, ox, oy, s), MapPoint(12.0f, 17.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
    pRT->DrawLine(MapPoint(12.0f, 17.0f, ox, oy, s), MapPoint(14.5f, 15.0f, ox, oy, s), pBrush, strokeWidth, m_pStrokeStyle.Get());
}

void LucideIcons::DrawUser(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth) const {
    if (!pRT || !pBrush) return;
    EnsureStrokeStyle(pRT, const_cast<Microsoft::WRL::ComPtr<ID2D1StrokeStyle>&>(m_pStrokeStyle));

    const float w = bounds.right - bounds.left;
    const float h = bounds.bottom - bounds.top;
    const float s = (std::min)(w, h) / 24.0f;
    const float ox = bounds.left + (w - 24.0f * s) * 0.5f;
    const float oy = bounds.top + (h - 24.0f * s) * 0.5f;

    // 1. User Head (Circle at cx=12, cy=7, radius=4)
    pRT->DrawEllipse(D2D1::Ellipse(MapPoint(12.0f, 7.0f, ox, oy, s), 4.0f * s, 4.0f * s), pBrush, strokeWidth, m_pStrokeStyle.Get());

    // 2. User Shoulders / Body (Lucide path: M19 21v-2a4 4 0 0 0-4-4H9a4 4 0 0 0-4 4v2)
    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (pFactory) {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> pPath;
        if (SUCCEEDED(pFactory->CreatePathGeometry(&pPath)) && pPath) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> pSink;
            if (SUCCEEDED(pPath->Open(&pSink)) && pSink) {
                pSink->BeginFigure(MapPoint(5.0f, 21.0f, ox, oy, s), D2D1_FIGURE_BEGIN_HOLLOW);
                pSink->AddLine(MapPoint(5.0f, 19.0f, ox, oy, s));
                pSink->AddArc(D2D1::ArcSegment(
                    MapPoint(9.0f, 15.0f, ox, oy, s),
                    D2D1::SizeF(4.0f * s, 4.0f * s),
                    0.0f,
                    D2D1_SWEEP_DIRECTION_CLOCKWISE,
                    D2D1_ARC_SIZE_SMALL
                ));
                pSink->AddLine(MapPoint(15.0f, 15.0f, ox, oy, s));
                pSink->AddArc(D2D1::ArcSegment(
                    MapPoint(19.0f, 19.0f, ox, oy, s),
                    D2D1::SizeF(4.0f * s, 4.0f * s),
                    0.0f,
                    D2D1_SWEEP_DIRECTION_CLOCKWISE,
                    D2D1_ARC_SIZE_SMALL
                ));
                pSink->AddLine(MapPoint(19.0f, 21.0f, ox, oy, s));
                pSink->EndFigure(D2D1_FIGURE_END_OPEN);
                pSink->Close();
                pRT->DrawGeometry(pPath.Get(), pBrush, strokeWidth, m_pStrokeStyle.Get());
            }
        }
    }
}

} // namespace phide::ui
