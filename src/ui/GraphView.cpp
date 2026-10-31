/**
 * @file GraphView.cpp
 * @brief Obsidian-styled interactive File Network Graph View implementation.
 */

#include "phide/ui/GraphView.hpp"
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <sstream>

namespace phide::ui {

GraphView::GraphView() {
    RefreshFromWorkspace(L"");
}

D2D1_COLOR_F GraphView::GetExtensionColor(const std::wstring& ext, bool isDir) {
    if (isDir) {
        return D2D1::ColorF(0.98f, 0.42f, 0.42f, 1.0f); // Crimson / Coral
    }
    if (ext == L"php") {
        return D2D1::ColorF(0.68f, 0.58f, 0.98f, 1.0f); // Soft Purple / Violet
    } else if (ext == L"js" || ext == L"ts" || ext == L"jsx" || ext == L"tsx") {
        return D2D1::ColorF(0.98f, 0.82f, 0.16f, 1.0f); // Vibrant Yellow / Gold
    } else if (ext == L"css" || ext == L"scss" || ext == L"less") {
        return D2D1::ColorF(0.25f, 0.75f, 0.98f, 1.0f); // Sky Blue
    } else if (ext == L"html" || ext == L"htm") {
        return D2D1::ColorF(0.98f, 0.56f, 0.22f, 1.0f); // Coral / Orange
    } else if (ext == L"sql" || ext == L"db") {
        return D2D1::ColorF(0.20f, 0.84f, 0.62f, 1.0f); // Emerald / Mint
    } else if (ext == L"json" || ext == L"yaml" || ext == L"yml" || ext == L"xml") {
        return D2D1::ColorF(0.22f, 0.84f, 0.78f, 1.0f); // Cyan / Teal
    } else if (ext == L"md" || ext == L"txt" || ext == L"log") {
        return D2D1::ColorF(0.96f, 0.45f, 0.72f, 1.0f); // Pink / Rose
    } else if (ext == L"py") {
        return D2D1::ColorF(0.98f, 0.76f, 0.18f, 1.0f); // Warm Amber
    } else if (ext == L"cpp" || ext == L"hpp" || ext == L"c" || ext == L"h") {
        return D2D1::ColorF(0.38f, 0.65f, 0.98f, 1.0f); // Cobalt Blue
    }
    return D2D1::ColorF(0.70f, 0.75f, 0.82f, 1.0f); // Lavender Slate
}

void GraphView::RefreshFromWorkspace(const std::wstring& rootPath) {
    m_currentRootPath = rootPath;
    m_nodes.clear();
    m_edges.clear();
    m_hoveredNode = -1;
    m_draggedNode = -1;

    std::filesystem::path targetDir = rootPath;
    std::error_code ec;
    if (targetDir.empty() || !std::filesystem::exists(targetDir, ec)) {
        targetDir = std::filesystem::current_path();
    }

    // 1. Scan filesystem for project files (up to 75 files for a balanced, responsive graph)
    const size_t maxFiles = 75;
    std::vector<std::filesystem::path> collected;

    try {
        if (std::filesystem::exists(targetDir, ec) && std::filesystem::is_directory(targetDir, ec)) {
            for (auto const& entry : std::filesystem::recursive_directory_iterator(targetDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (collected.size() >= maxFiles) break;
                if (ec) break;

                const auto& path = entry.path();
                std::wstring uPath = path.wstring();
                // Filter out version control, binary build dirs and package caches
                if (uPath.find(L"\\.git") != std::wstring::npos ||
                    uPath.find(L"\\node_modules") != std::wstring::npos ||
                    uPath.find(L"\\vendor") != std::wstring::npos ||
                    uPath.find(L"\\.vs") != std::wstring::npos ||
                    uPath.find(L"\\bin") != std::wstring::npos ||
                    uPath.find(L"\\obj") != std::wstring::npos) {
                    continue;
                }

                if (entry.is_regular_file(ec)) {
                    collected.push_back(path);
                }
            }
        }
    } catch (...) {
        // Fallback gracefully
    }

    // If directory scan produced very few or no files, create standard representative project network
    if (collected.size() < 4) {
        struct SampleDef {
            const wchar_t* name;
            const wchar_t* ext;
            bool isDir;
            float rad;
        };
        const SampleDef samples[] = {
            { L"index.php",       L"php",  false, 7.5f },
            { L"config.php",      L"php",  false, 7.0f },
            { L"action.php",      L"php",  false, 6.0f },
            { L"database.sql",    L"sql",  false, 6.5f },
            { L"README.md",       L"md",   false, 5.5f },
            { L"styles.css",      L"css",  false, 5.5f },
            { L"app.js",          L"js",   false, 6.0f },
            { L"composer.json",   L"json", false, 5.5f },
            { L"assets",          L"",     true,  7.0f },
            { L"logo.png",        L"png",  false, 4.5f },
            { L"auth.php",        L"php",  false, 6.0f },
            { L"student.php",     L"php",  false, 6.0f },
            { L"api.js",          L"js",   false, 5.5f },
            { L"test.txt",        L"txt",  false, 4.5f },
            { L".gitignore",      L"git",  false, 4.5f },
            { L"Notebook.ipynb",  L"json", false, 5.5f },
            { L"routes.php",      L"php",  false, 6.0f },
            { L"helper.php",      L"php",  false, 5.0f },
            { L"main.css",        L"css",  false, 5.0f },
            { L"db_connect.php",  L"php",  false, 6.0f },
            { L"schema.sql",      L"sql",  false, 5.5f },
            { L"install.sh",      L"sh",   false, 4.5f },
            { L"vuln.txt",        L"txt",  false, 4.5f },
            { L"license.txt",     L"txt",  false, 4.5f }
        };

        for (const auto& s : samples) {
            GraphNode n;
            n.id = s.name;
            n.label = s.name;
            n.ext = s.ext;
            n.isDirectory = s.isDir;
            n.radius = s.rad;
            n.color = GetExtensionColor(s.ext, s.isDir);
            m_nodes.push_back(n);
        }
    } else {
        for (const auto& p : collected) {
            GraphNode n;
            n.id = p.wstring();
            n.label = p.filename().wstring();
            std::wstring ext = p.extension().wstring();
            if (!ext.empty() && ext.front() == L'.') ext.erase(0, 1);
            for (auto& c : ext) c = towlower(c);
            n.ext = ext;
            n.isDirectory = false;

            // Main files get larger radius
            std::wstring lowerLabel = n.label;
            for (auto& c : lowerLabel) c = towlower(c);
            if (lowerLabel.find(L"index") != std::wstring::npos ||
                lowerLabel.find(L"config") != std::wstring::npos ||
                lowerLabel.find(L"main") != std::wstring::npos ||
                lowerLabel.find(L"app") != std::wstring::npos) {
                n.radius = 7.5f;
            } else if (ext == L"php" || ext == L"js" || ext == L"sql") {
                n.radius = 6.0f;
            } else {
                n.radius = 5.0f;
            }
            n.color = GetExtensionColor(n.ext, false);
            m_nodes.push_back(n);
        }
    }

    if (m_nodes.empty()) return;

    // 2. Initial compact cluster layout with gentle outward burst (Obsidian intro)
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        float r = 12.0f + 25.0f * (static_cast<float>(i) / static_cast<float>((std::max)(size_t(1), m_nodes.size())));
        float theta = static_cast<float>(i) * 2.39996f; // Golden ratio spiral
        m_nodes[i].x = r * std::cos(theta);
        m_nodes[i].y = r * std::sin(theta);
        m_nodes[i].vx = std::cos(theta) * 2.5f;
        m_nodes[i].vy = std::sin(theta) * 2.5f;
    }

    // 3. Connect nodes organically based on file types, hubs, and proximity
    auto addEdge = [this](size_t a, size_t b) {
        if (a >= m_nodes.size() || b >= m_nodes.size() || a == b) return;
        for (const auto& e : m_edges) {
            if ((e.from == a && e.to == b) || (e.from == b && e.to == a)) return;
        }
        m_edges.push_back({ a, b, 1.0f });
        m_nodes[a].neighbors.push_back(b);
        m_nodes[b].neighbors.push_back(a);
    };

    // Central hubs (first 3 main nodes) connect to clusters
    for (size_t i = 1; i < m_nodes.size(); ++i) {
        size_t hub = (i % 3 == 0) ? 0 : ((i % 3 == 1) ? (m_nodes.size() > 1 ? 1 : 0) : (m_nodes.size() > 2 ? 2 : 0));
        addEdge(hub, i);
        // Connect to sequential neighbor to create organic graph webs
        if (i > 1 && (i % 2 == 0)) {
            addEdge(i - 1, i);
        }
        if (i + 2 < m_nodes.size() && (i % 4 == 0)) {
            addEdge(i, i + 2);
        }
    }

    // 4. Start Obsidian bouncy intro animation
    StartIntroAnimation();
}

void GraphView::StepPhysics(float alpha) {
    if (m_nodes.size() < 2) return;

    const float kRepulse   = 2200.0f * (std::max)(0.25f, alpha);
    const float kSpring    = 0.052f * (std::max)(0.25f, alpha);
    const float springLen  = 72.0f;
    const float centerPull = 0.006f * (std::max)(0.15f, alpha);
    const float damping    = 0.86f; // Elastic, springy bouncy motion like Obsidian!

    // A. Repulsion between all node pairs
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        for (size_t j = i + 1; j < m_nodes.size(); ++j) {
            float dx = m_nodes[j].x - m_nodes[i].x;
            float dy = m_nodes[j].y - m_nodes[i].y;
            float distSq = dx * dx + dy * dy + 80.0f;
            float dist = std::sqrt(distSq);
            float force = kRepulse / distSq;
            float fx = (dx / dist) * force;
            float fy = (dy / dist) * force;

            m_nodes[i].vx -= fx;
            m_nodes[i].vy -= fy;
            m_nodes[j].vx += fx;
            m_nodes[j].vy += fy;
        }
    }

    // B. Spring attraction along edges
    for (const auto& e : m_edges) {
        float dx = m_nodes[e.to].x - m_nodes[e.from].x;
        float dy = m_nodes[e.to].y - m_nodes[e.from].y;
        float dist = std::sqrt(dx * dx + dy * dy + 1.0f);
        float disp = dist - springLen;
        float force = kSpring * disp;
        float fx = (dx / dist) * force;
        float fy = (dy / dist) * force;

        m_nodes[e.from].vx += fx;
        m_nodes[e.from].vy += fy;
        m_nodes[e.to].vx   -= fx;
        m_nodes[e.to].vy   -= fy;
    }

    // C. Center gravity & update positions (skip currently dragged node)
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (static_cast<int>(i) == m_draggedNode) continue;

        m_nodes[i].vx -= m_nodes[i].x * centerPull;
        m_nodes[i].vy -= m_nodes[i].y * centerPull;

        m_nodes[i].vx *= damping;
        m_nodes[i].vy *= damping;

        m_nodes[i].x += m_nodes[i].vx;
        m_nodes[i].y += m_nodes[i].vy;
    }
}

void GraphView::StartIntroAnimation() {
    m_animAlpha = 1.0f;
    m_animFrames = 110;
    m_isAnimating = true;
}

void GraphView::StartBounceAnimation() {
    m_animAlpha = (std::max)(m_animAlpha, 0.45f);
    m_animFrames = (std::max)(m_animFrames, 40);
    m_isAnimating = true;
}

bool GraphView::UpdateAnimation() {
    if (!m_isAnimating) return false;

    StepPhysics(m_animAlpha);
    m_animAlpha *= 0.970f;
    m_animFrames--;

    if (m_animFrames <= 0 || m_animAlpha < 0.015f) {
        m_animAlpha = 0.0f;
        m_animFrames = 0;
        m_isAnimating = false;
        return false;
    }
    return true;
}

void GraphView::ResetView() {
    m_panX = 0.0f;
    m_panY = 0.0f;
    m_zoom = 1.0f;
    m_hoveredNode = -1;
    m_draggedNode = -1;
}

void GraphView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                       float left, float width, float top, float height) {
    if (!pRT || width < 10.0f || height < 10.0f) return;

    // Advance organic Obsidian bouncy intro animation if active
    if (m_isAnimating) {
        UpdateAnimation();
    }

    m_canvasBounds = D2D1::RectF(left, top, left + width, top + height);

    // 1. Canvas Deep Background (Obsidian dark background)
    D2D1_COLOR_F bgColor = theme.IsDarkMode() ? D2D1::ColorF(0.08f, 0.08f, 0.09f, 1.0f)
                                              : D2D1::ColorF(0.97f, 0.98f, 0.99f, 1.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    pRT->CreateSolidColorBrush(bgColor, &bgBrush);
    if (bgBrush) {
        pRT->FillRectangle(m_canvasBounds, bgBrush.Get());
    }

    pRT->PushAxisAlignedClip(m_canvasBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const float cx = left + width * 0.5f + m_panX;
    const float cy = top + height * 0.5f + m_panY;

    auto worldToScreen = [cx, cy, this](float wx, float wy) -> D2D1_POINT_2F {
        return D2D1::Point2F(cx + wx * m_zoom, cy + wy * m_zoom);
    };

    // 2. Draw Edges (Translucent connecting lines)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> edgeBrushNormal;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> edgeBrushHighlight;
    D2D1_COLOR_F normalEdgeColor = theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f)
                                                      : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.10f);
    D2D1_COLOR_F highlightEdgeColor = theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.80f)
                                                         : D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.75f);
    pRT->CreateSolidColorBrush(normalEdgeColor, &edgeBrushNormal);
    pRT->CreateSolidColorBrush(highlightEdgeColor, &edgeBrushHighlight);

    const float baseEdgeWidth = (std::max)(0.6f, 0.9f * m_zoom);

    for (const auto& e : m_edges) {
        if (e.from >= m_nodes.size() || e.to >= m_nodes.size()) continue;
        D2D1_POINT_2F p1 = worldToScreen(m_nodes[e.from].x, m_nodes[e.from].y);
        D2D1_POINT_2F p2 = worldToScreen(m_nodes[e.to].x, m_nodes[e.to].y);

        bool isConnectedToHover = (m_hoveredNode >= 0 && (static_cast<int>(e.from) == m_hoveredNode || static_cast<int>(e.to) == m_hoveredNode));

        if (isConnectedToHover) {
            pRT->DrawLine(p1, p2, edgeBrushHighlight ? edgeBrushHighlight.Get() : edgeBrushNormal.Get(), baseEdgeWidth * 1.6f);
        } else {
            pRT->DrawLine(p1, p2, edgeBrushNormal.Get(), baseEdgeWidth);
        }
    }

    // 3. Draw Nodes (Circular points with colored or monochrome theme)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> nodeBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &nodeBrush);

    const bool isColored = config.isGraphColored;
    const D2D1_COLOR_F monoColor = theme.IsDarkMode() ? D2D1::ColorF(0.94f, 0.95f, 0.98f, 1.0f)
                                                      : D2D1::ColorF(0.20f, 0.22f, 0.26f, 1.0f);

    IDWriteTextFormat* pFont = theme.fontHint ? theme.fontHint.Get() : theme.fontBody.Get();

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const auto& node = m_nodes[i];
        D2D1_POINT_2F sp = worldToScreen(node.x, node.y);

        // Viewport culling
        if (sp.x < left - 60.0f || sp.x > left + width + 60.0f ||
            sp.y < top - 60.0f || sp.y > top + height + 60.0f) {
            continue;
        }

        const float r = (std::clamp)(node.radius * m_zoom, 2.8f, 26.0f);
        const bool isHovered = (static_cast<int>(i) == m_hoveredNode);
        const bool isDragged = (static_cast<int>(i) == m_draggedNode);

        // Determine node color (Colored mode vs Monochrome mode)
        D2D1_COLOR_F c = isColored ? node.color : monoColor;

        // Dim unconnected nodes when another node is hovered
        if (m_hoveredNode >= 0 && !isHovered) {
            bool isNeighbor = false;
            for (size_t nIdx : m_nodes[m_hoveredNode].neighbors) {
                if (nIdx == i) { isNeighbor = true; break; }
            }
            if (!isNeighbor) {
                c.a = 0.28f;
            }
        }

        // Draw halo ring on hover or drag
        if ((isHovered || isDragged) && nodeBrush) {
            D2D1_COLOR_F haloColor = c;
            haloColor.a = 0.35f;
            nodeBrush->SetColor(haloColor);
            pRT->FillEllipse(D2D1::Ellipse(sp, r + 4.5f * m_zoom, r + 4.5f * m_zoom), nodeBrush.Get());
            haloColor.a = 0.90f;
            nodeBrush->SetColor(haloColor);
            pRT->DrawEllipse(D2D1::Ellipse(sp, r + 2.0f * m_zoom, r + 2.0f * m_zoom), nodeBrush.Get(), 1.2f);
        }

        if (nodeBrush) {
            nodeBrush->SetColor(c);
            pRT->FillEllipse(D2D1::Ellipse(sp, r, r), nodeBrush.Get());
        }

        // 4. Node Labels ("the namiong shouuldnt have a div color it transparent and it should be under the note itself")
        const bool showLabel = (m_zoom >= 0.85f) || isHovered || isDragged;
        if (showLabel && pFont && theme.dwriteFactory) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
            theme.dwriteFactory->CreateTextLayout(
                node.label.c_str(),
                static_cast<UINT32>(node.label.length()),
                pFont,
                240.0f,
                30.0f,
                &pLayout
            );

            if (pLayout) {
                pLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                pLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

                // Centered horizontally directly beneath the node (NO horizontal offset/gap)
                float labelX = sp.x - 120.0f;
                // Positioned right under the node circle
                float labelY = sp.y + r + 3.0f;

                ID2D1Brush* textBrush = isHovered ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
                                                  : (theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushTextMuted.Get());

                // Pure transparent text - NO div / pill background box
                pRT->DrawTextLayout(D2D1::Point2F(labelX, labelY), pLayout.Get(), textBrush);
            }
        }
    }

    // 5. Floating HUD Badge (Bottom-Left)
    {
        const float hudW = config.ScaleF(280.0f);
        const float hudH = config.ScaleF(26.0f);
        const float hudX = left + config.ScaleF(14.0f);
        const float hudY = top + height - hudH - config.ScaleF(14.0f);

        D2D1_ROUNDED_RECT rrcHud = D2D1::RoundedRect(D2D1::RectF(hudX, hudY, hudX + hudW, hudY + hudH), 5.0f, 5.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hudBg, hudBorder;
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.12f, 0.15f, 0.88f) : D2D1::ColorF(0.92f, 0.94f, 0.96f, 0.88f), &hudBg);
        pRT->CreateSolidColorBrush(theme.brushBorderSubtle ? theme.brushBorderSubtle->GetColor() : D2D1::ColorF(0.25f, 0.25f, 0.28f, 1.0f), &hudBorder);

        if (hudBg) pRT->FillRoundedRectangle(rrcHud, hudBg.Get());
        if (hudBorder) pRT->DrawRoundedRectangle(rrcHud, hudBorder.Get(), 0.8f);

        int zoomPct = static_cast<int>(m_zoom * 100.0f + 0.5f);
        std::wstring hudText = L"Graph view • " + std::to_wstring(zoomPct) + L"% • " +
                               std::to_wstring(m_nodes.size()) + L" files • " +
                               (isColored ? L"Colored" : L"Monochrome");

        ID2D1Brush* pTextBrush = theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushTextMuted.Get();
        if (pFont && pTextBrush) {
            D2D1_RECT_F rcText = D2D1::RectF(hudX + config.ScaleF(10.0f), hudY, hudX + hudW - config.ScaleF(10.0f), hudY + hudH);
            pRT->DrawText(hudText.c_str(), static_cast<UINT32>(hudText.length()), pFont, rcText, pTextBrush);
        }
    }

    pRT->PopAxisAlignedClip();
}

bool GraphView::HandleMouseMove(POINT clientPt) {
    if (m_canvasBounds.right - m_canvasBounds.left < 10.0f) return false;

    const float cx = (m_canvasBounds.left + m_canvasBounds.right) * 0.5f + m_panX;
    const float cy = (m_canvasBounds.top + m_canvasBounds.bottom) * 0.5f + m_panY;
    const float mouseX = static_cast<float>(clientPt.x);
    const float mouseY = static_cast<float>(clientPt.y);

    // Node Arranging / Dragging (User requirement: "and the user can drag the node to arrange it")
    if (m_draggedNode >= 0 && m_draggedNode < static_cast<int>(m_nodes.size())) {
        float wx = (mouseX - cx) / m_zoom;
        float wy = (mouseY - cy) / m_zoom;
        m_nodes[m_draggedNode].x = wx;
        m_nodes[m_draggedNode].y = wy;
        m_nodes[m_draggedNode].vx = 0.0f;
        m_nodes[m_draggedNode].vy = 0.0f;
        m_hasDragged = true;
        return true;
    }

    // Canvas Background Panning
    if (m_isPanning) {
        m_panX += (mouseX - static_cast<float>(m_lastMousePos.x));
        m_panY += (mouseY - static_cast<float>(m_lastMousePos.y));
        m_lastMousePos = clientPt;
        return true;
    }

    // Node Hover Testing
    int prevHover = m_hoveredNode;
    m_hoveredNode = -1;

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        float sx = cx + m_nodes[i].x * m_zoom;
        float sy = cy + m_nodes[i].y * m_zoom;
        float r = (std::clamp)(m_nodes[i].radius * m_zoom, 3.0f, 26.0f) + 6.0f;

        float dx = mouseX - sx;
        float dy = mouseY - sy;
        if (dx * dx + dy * dy <= r * r) {
            m_hoveredNode = static_cast<int>(i);
            break;
        }
    }

    return (m_hoveredNode != prevHover);
}

bool GraphView::HandleLButtonDown(POINT clientPt) {
    if (clientPt.x < m_canvasBounds.left || clientPt.x > m_canvasBounds.right ||
        clientPt.y < m_canvasBounds.top || clientPt.y > m_canvasBounds.bottom) {
        return false;
    }

    const float cx = (m_canvasBounds.left + m_canvasBounds.right) * 0.5f + m_panX;
    const float cy = (m_canvasBounds.top + m_canvasBounds.bottom) * 0.5f + m_panY;
    const float mouseX = static_cast<float>(clientPt.x);
    const float mouseY = static_cast<float>(clientPt.y);

    m_draggedNode = -1;
    m_hasDragged = false;
    m_lastMousePos = clientPt;

    // Check hit on nodes
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        float sx = cx + m_nodes[i].x * m_zoom;
        float sy = cy + m_nodes[i].y * m_zoom;
        float r = (std::clamp)(m_nodes[i].radius * m_zoom, 3.0f, 26.0f) + 6.0f;

        float dx = mouseX - sx;
        float dy = mouseY - sy;
        if (dx * dx + dy * dy <= r * r) {
            m_draggedNode = static_cast<int>(i);
            m_hoveredNode = static_cast<int>(i);
            return true;
        }
    }

    // Canvas background drag initiates pan
    m_isPanning = true;
    return true;
}

bool GraphView::HandleLButtonUp(POINT clientPt) {
    (void)clientPt;
    bool hadAction = (m_draggedNode >= 0 || m_isPanning);

    if (m_draggedNode >= 0) {
        if (!m_hasDragged) {
            // User clicked node without dragging -> store last clicked file
            if (m_draggedNode < static_cast<int>(m_nodes.size())) {
                m_lastClickedFile = m_nodes[m_draggedNode].id;
            }
        } else {
            // User finished dragging and arranging a node -> springy bounce settling
            StartBounceAnimation();
        }
    }

    m_draggedNode = -1;
    m_isPanning = false;
    m_hasDragged = false;
    return hadAction;
}

bool GraphView::HandleMouseWheel(short delta, POINT clientPt) {
    if (clientPt.x < m_canvasBounds.left || clientPt.x > m_canvasBounds.right ||
        clientPt.y < m_canvasBounds.top || clientPt.y > m_canvasBounds.bottom) {
        return false;
    }

    const float midX = (m_canvasBounds.left + m_canvasBounds.right) * 0.5f;
    const float midY = (m_canvasBounds.top + m_canvasBounds.bottom) * 0.5f;
    const float mouseX = static_cast<float>(clientPt.x);
    const float mouseY = static_cast<float>(clientPt.y);

    // World coordinate under cursor before zoom
    float wx = (mouseX - midX - m_panX) / m_zoom;
    float wy = (mouseY - midY - m_panY) / m_zoom;

    float zoomFactor = (delta > 0) ? 1.15f : 0.87f;
    float newZoom = (std::clamp)(m_zoom * zoomFactor, 0.25f, 3.5f);

    // Keep point under cursor stable after zoom
    m_panX = mouseX - midX - wx * newZoom;
    m_panY = mouseY - midY - wy * newZoom;
    m_zoom = newZoom;

    return true;
}

} // namespace phide::ui
