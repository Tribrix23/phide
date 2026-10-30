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
#include <string>

namespace phide::ui {

class Theme;

using Microsoft::WRL::ComPtr;

/**
 * @enum FileIconType
 * @brief Distinct semantic file and folder types for vector icon mapping.
 */
enum class FileIconType {
    Folder,
    FolderOpen,
    Php,
    Cpp,
    Header,
    Js,
    Ts,
    Json,
    Markdown,
    Html,
    Css,
    Terminal,
    Yaml,
    Image,
    Exe,
    Sql,
    DefaultFile
};

/**
 * @class LucideIcons
 * @brief GPU-accelerated vector drawing engine for Lucide icons and Seti file types.
 */
class LucideIcons {
public:
    LucideIcons();
    ~LucideIcons() = default;

    /**
     * @brief Maps a filename and directory state to its appropriate FileIconType.
     */
    static FileIconType GetIconTypeForPath(const std::wstring& filename, bool isFolder, bool isExpanded);

    /**
     * @brief High-level dispatcher to render the appropriate vector icon with proper theme colors.
     */
    void DrawFileIcon(ID2D1RenderTarget* pRT, Theme& theme, const D2D1_RECT_F& bounds, FileIconType type) const;

    /**
     * @brief Initializes stroke styles (round cap, round join) on the D2D factory.
     * @param pFactory Direct2D factory.
     * @return S_OK on success.
     */
    HRESULT Initialize(ID2D1Factory* pFactory);

    // --- Vector Tree File & Folder Icons ---
    void DrawFolder(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, bool isOpen, float strokeWidth = 1.3f) const;
    void DrawFileJson(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;
    void DrawFileJs(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.5f) const;
    void DrawFileTs(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.5f) const;
    void DrawFileCpp(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.5f) const;
    void DrawFileHeader(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.5f) const;
    void DrawFilePhp(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;
    void DrawFileMarkdown(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;
    void DrawFileImage(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.3f) const;
    void DrawFileYaml(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;
    void DrawFileHtml(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;
    void DrawFileCss(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;
    void DrawFileExe(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.3f) const;

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
     * @brief Draws the Lucide Package / Box icon (isometric 3D package, matching PyCharm/VS Code packages).
     */
    void DrawPackage(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.6f) const;

    /**
     * @brief Draws the Terminal + Notebook icon (Notebook outline with binding ticks and terminal prompt '>_').
     */
    void DrawTerminalNotebook(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.6f) const;

    /**
     * @brief Draws the Lucide Graduation Cap / Student Hat icon (mortarboard cap with skullcap and tassel 🎓).
     */
    void DrawGraduationCap(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.6f) const;

    /**
     * @brief Draws the Lucide Book icon (open book with two pages and center spine).
     */
    void DrawBook(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.3f) const;

    /**
     * @brief Draws the Lucide Test Flask icon (conical flask with rim, neck, conical body, and liquid line).
     */
    void DrawTestFlask(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.3f) const;

    /**
     * @brief Draws the Lucide Sidebar toggle icon (panel-left).
     */
    void DrawSidebar(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;

    /**
     * @brief Draws the Lucide Edit icon (pen / pencil).
     */
    void DrawEdit(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;

    /**
     * @brief Draws the Lucide View icon (layout / eye).
     */
    void DrawView(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;

    /**
     * @brief Draws the Lucide Code icon (< / >).
     */
    void DrawCode(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;

    /**
     * @brief Draws the Lucide Tools icon (Lucide 45-degree angled wrench).
     */
    void DrawTools(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;

    /**
     * @brief Draws the Lucide Run icon (vibrant play triangle ▶).
     */
    void DrawRun(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.5f) const;

    /**
     * @brief Draws the Lucide Restart / Reload circular arrow icon (↻ rotate-cw).
     */
    void DrawRestart(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;

    /**
     * @brief Draws the Lucide Bug / Debugger icon (beetle glyph).
     */
    void DrawBug(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.5f) const;

    /**
     * @brief Draws the Lucide Terminal icon (>_ prompt).
     */
    void DrawTerminal(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.5f) const;

    /**
     * @brief Draws the Lucide Window icon (app-window).
     */
    void DrawWindow(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;

    /**
     * @brief Draws the Lucide Help icon (circle-help).
     */
    void DrawHelp(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.4f) const;

    /**
     * @brief Draws the Split Editor icon (columns-2 split pane [ | ]).
     * @param pRT Active Direct2D render target.
     * @param bounds Screen rectangle where the icon is centered.
     * @param pBrush Direct2D brush (white in dark mode, black in light mode).
     * @param strokeWidth Line stroke thickness.
     */
    void DrawSplitEditor(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.3f) const;

    /**
     * @brief Draws the 2x2 grid icon for Extensions / Environment sidebar (Image 2).
     */
    void DrawExtensionsGrid(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.3f) const;

    /**
     * @brief Draws the curved return back arrow icon (Image 5).
     */
    void DrawReturnBack(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.8f) const;

    /**
     * @brief Draws the vector Minimize caption icon.
     */
    void DrawMinimize(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.2f) const;

    /**
     * @brief Draws the vector Maximize / Restore caption icon.
     */
    void DrawMaximize(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, bool isZoomed, float strokeWidth = 1.2f) const;

    /**
     * @brief Draws the vector Database cylinder icon.
     */
    void DrawDatabase(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.3f) const;

    /**
     * @brief Draws the vector Server rack chassis icon.
     */
    void DrawServer(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.3f) const;

    /**
     * @brief Draws the vector Log document icon with text lines.
     */
    void DrawLogIcon(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.3f) const;

    /**
     * @brief Draws the vector Close caption icon (✕).
     */
    void DrawClose(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.2f) const;

    /**
     * @brief Draws the vector Settings gear icon (cog).
     */
    void DrawSettings(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.2f) const;

    /**
     * @brief Draws the vector External Link / Open in Browser icon (↗).
     */
    void DrawExternalLink(ID2D1RenderTarget* pRT, const D2D1_RECT_F& bounds, ID2D1Brush* pBrush, float strokeWidth = 1.3f) const;

private:
    ComPtr<ID2D1StrokeStyle> m_pStrokeStyle;
    ComPtr<ID2D1PathGeometry> m_pWrenchGeometry;
    ComPtr<ID2D1PathGeometry> m_pRunGeometry;
    ComPtr<ID2D1PathGeometry> m_pRestartGeometry;
};

} // namespace phide::ui
