/**
 * @file LucideIcons.hpp
 * @brief Vector-rendered Lucide icon suite for Direct2D (Home, Search, Git Branch, Caption controls).
 * 
 * Implements pixel-perfect vector geometry matching the official Lucide icon design specifications:
 * - Lucide Home (house with pitched roof and doorway)
 * - Lucide Search (precision circle with diagonal handle)
 * - Lucide Git Branch (multi-node version control tree)
 * - Vector window caption controls (Minimize, Maximize, Close)
 */

#pragma once

#include <d2d1.h>
#include <wrl/client.h>

namespace phide::ui {

using Microsoft::WRL::ComPtr;

/**
 * @class LucideIcons
 * @brief GPU-accelerated vector drawing engine for Lucide icons.
 */
class LucideIcons {
public:
    LucideIcons();
    ~LucideIcons() = default;

    /**
     * @brief Initializes stroke styles (round cap, round join) on the D2D factory.
     * @param pFactory Direct2D factory.
     * @return S_OK on success.
     */
    HRESULT Initialize(ID2D1Factory* pFactory);

    /**
     * @brief Draws the Lucide Home icon inside the specified bounding box.
     */
    void DrawHome(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.8f) const;

    /**
     * @brief Draws the Lucide File icon (single document) inside the specified bounding box.
     */
    void DrawFile(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.8f) const;

    /**
     * @brief Draws the Explorer Files icon (two overlapping sheets with dog-ear fold, matching Image 2).
     */
    void DrawFiles(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.6f) const;

    /**
     * @brief Draws the Lucide Search (magnifying glass) icon inside the specified bounding box.
     */
    void DrawSearch(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.8f) const;

    /**
     * @brief Draws the Lucide Git Branch icon inside the specified bounding box.
     */
    void DrawGitBranch(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.8f) const;

    /**
     * @brief Draws the Run & Debug icon (play triangle with overlapping bug glyph, matching Image 2).
     */
    void DrawDebug(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.6f) const;

    /**
     * @brief Draws the Extensions icon (three blocks + one detached rotated diamond, matching Image 2).
     */
    void DrawExtensions(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.6f) const;

    /**
     * @brief Draws the vector Minimize caption icon.
     */
    void DrawMinimize(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.2f) const;

    /**
     * @brief Draws the vector Maximize / Restore caption icon.
     */
    void DrawMaximize(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, bool isZoomed, float strokeWidth = 1.2f) const;

    /**
     * @brief Draws the vector Close caption icon (✕).
     */
    void DrawClose(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.2f) const;

private:
    ComPtr<ID2D1StrokeStyle> m_pStrokeStyle;
};

} // namespace phide::ui
