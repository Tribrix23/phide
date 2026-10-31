/**
 * @file GraphView.hpp
 * @brief Obsidian-styled interactive File Network Graph View with physics layout, node dragging, zoom & pan, and colored modes.
 */

#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <memory>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @struct GraphNode
 * @brief Represents a file or folder in the project network graph.
 */
struct GraphNode {
    std::wstring id;          ///< Unique relative file/folder path
    std::wstring label;       ///< Basename display label (e.g. "config.php")
    std::wstring ext;         ///< Extension in lowercase (e.g. "php", "js", "sql")
    bool isDirectory = false;

    float x = 0.0f;           ///< World X position
    float y = 0.0f;           ///< World Y position
    float vx = 0.0f;          ///< Velocity X
    float vy = 0.0f;          ///< Velocity Y
    float radius = 5.5f;      ///< Node radius in world units
    D2D1_COLOR_F color{};     ///< Accent color in colored mode
    bool isPinned = false;    ///< True if repositioned by user drag; physics retains exact coordinates

    std::vector<size_t> neighbors; ///< Indices of connected neighbor nodes
};

/**
 * @struct GraphEdge
 * @brief Represents an edge connection between two nodes.
 */
struct GraphEdge {
    size_t from = 0;
    size_t to = 0;
    float weight = 1.0f;
};

/**
 * @class GraphView
 * @brief High-performance Direct2D canvas rendering an interactive file network graph.
 */
class GraphView {
public:
    GraphView();
    ~GraphView() = default;

    /**
     * @brief Populates graph nodes and edges by scanning the workspace directory.
     * @param rootPath Absolute directory of the current open project.
     * @param preserveExistingPositions When true (e.g. file watcher/save updates), keeps existing node positions.
     */
    void RefreshFromWorkspace(const std::wstring& rootPath, bool preserveExistingPositions = false);

    /**
     * @brief Saves node coordinates and pan/zoom state to disk (.phide/graph_layout.json).
     * @return True if successfully saved.
     */
    bool SaveLayout();

    /**
     * @brief Loads saved node coordinates and pan/zoom state from disk (.phide/graph_layout.json).
     * @return True if saved positions were found and applied.
     */
    bool LoadLayout();

    /**
     * @brief Returns the layout file path for the current workspace.
     */
    [[nodiscard]] std::wstring GetLayoutFilePath() const;

    /**
     * @brief Returns the formatted status bar text (e.g. "Graph view • 116% • 9 files • Monochrome").
     */
    [[nodiscard]] std::wstring GetStatusString(const app::AppConfig& config) const;

    /**
     * @brief Renders the Obsidian-styled graph canvas with nodes, edges, labels and HUD.
     */
    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float left, float width, float top, float height);

    /**
     * @brief Handles mouse movement (hovering nodes, dragging nodes, panning canvas).
     * @return True if a repaint is required.
     */
    bool HandleMouseMove(POINT clientPt);

    /**
     * @brief Handles left mouse button down (select node to drag or begin panning).
     * @return True if a repaint is required.
     */
    bool HandleLButtonDown(POINT clientPt);

    /**
     * @brief Handles left mouse button release (finish dragging node or panning).
     * @return True if a repaint is required.
     */
    bool HandleLButtonUp(POINT clientPt);

    /**
     * @brief Handles mouse wheel scrolling for smooth cursor-centered zooming.
     * @return True if a repaint is required.
     */
    bool HandleMouseWheel(short delta, POINT clientPt);

    /**
     * @brief Resets pan and zoom back to default centered framing.
     */
    void ResetView();

    /**
     * @brief Starts the organic Obsidian bouncy intro physics animation.
     */
    void StartIntroAnimation();

    /**
     * @brief Starts a quick spring recoil bounce after node dragging.
     */
    void StartBounceAnimation();

    /**
     * @brief Steps animation simulation frame by frame.
     * @return True if another frame should be rendered.
     */
    bool UpdateAnimation();

    /**
     * @brief Checks if the graph is currently animating.
     */
    [[nodiscard]] bool IsAnimating() const noexcept { return m_isAnimating; }

    /**
     * @brief Advances force-directed physics layout with given energy alpha.
     */
    void StepPhysics(float alpha = 1.0f);

    [[nodiscard]] size_t GetNodeCount() const noexcept { return m_nodes.size(); }
    [[nodiscard]] size_t GetEdgeCount() const noexcept { return m_edges.size(); }
    [[nodiscard]] float GetZoom() const noexcept { return m_zoom; }
    void SetZoom(float zoom) { m_zoom = zoom; }

    [[nodiscard]] int GetHoveredNodeIndex() const noexcept { return m_hoveredNode; }
    [[nodiscard]] int GetDraggedNodeIndex() const noexcept { return m_draggedNode; }
    [[nodiscard]] const std::vector<GraphNode>& GetNodes() const noexcept { return m_nodes; }

    [[nodiscard]] const std::wstring& GetLastClickedFile() const noexcept { return m_lastClickedFile; }
    void ClearLastClickedFile() noexcept { m_lastClickedFile.clear(); }

private:
    std::wstring m_currentRootPath;
    std::vector<GraphNode> m_nodes;
    std::vector<GraphEdge> m_edges;

    // Viewport transform
    float m_panX = 0.0f;
    float m_panY = 0.0f;
    float m_zoom = 1.0f;

    // Canvas bounds cached during render
    D2D1_RECT_F m_canvasBounds{};

    // Interaction states
    int m_hoveredNode = -1;
    int m_draggedNode = -1;
    bool m_isPanning = false;
    bool m_hasDragged = false;
    POINT m_lastMousePos{};
    std::wstring m_lastClickedFile;

    // Obsidian bouncy physics animation state
    float m_animAlpha = 0.0f;
    int m_animFrames = 0;
    bool m_isAnimating = false;

    // Color palette resolution
    static D2D1_COLOR_F GetExtensionColor(const std::wstring& ext, bool isDir);

    // Dependency analysis
    void ParseFileDependencies();
    static std::vector<std::string> ExtractReferencedTargets(const std::string& content, const std::wstring& ext);
    static bool MatchesCandidate(const std::string& ref, const std::wstring& candLabel, const std::wstring& candRelPath);
};

} // namespace phide::ui
