/**
 * @file AuditView.hpp
 * @brief Interactive Workspace Audit view for Dead Code, Unused Exports, Unused CSS Selectors,
 *        Unused Dependencies (#7), and .env vs .env.example Drift (#8).
 */

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "phide/app/AppConfig.hpp"
#include "phide/editor/WorkspaceAuditEngine.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

/**
 * @struct AuditViewCallbacks
 * @brief Host callbacks invoked by AuditView on the UI thread when navigating or syncing .env.example.
 */
struct AuditViewCallbacks {
    std::function<void(const std::wstring& filePath, size_t line0, size_t col0)> onOpenLocation;
    std::function<void(int addedCount)> onEnvSynced;
    std::function<void()> onRequestRepaint;
};

/**
 * @class AuditView
 * @brief Thread-safe Direct2D special view rendering dead-code, CSS, dependency, and .env drift findings.
 */
class AuditView {
public:
    AuditView() = default;
    ~AuditView();

    AuditView(const AuditView&) = delete;
    AuditView& operator=(const AuditView&) = delete;

    void SetCallbacks(AuditViewCallbacks cbs) { m_callbacks = std::move(cbs); }

    /**
     * @brief Launches an asynchronous bounded workspace audit on `workspaceRoot`.
     *        Cancels any in-flight scan before starting.
     */
    void StartAuditAsync(const std::wstring& workspaceRoot);

    void DiscardDeviceResources() noexcept {}

    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                float canvasLeft, float canvasWidth, float topY, float clientHeight);

    bool HandleLButtonDown(POINT clientPt, HWND hwnd, const app::AppConfig& config);
    bool HandleMouseMove(POINT clientPt);
    bool HandleMouseWheel(short delta, const app::AppConfig& config);
    bool HandleChar(wchar_t ch);
    bool HandleKeyDown(WPARAM vk);
    [[nodiscard]] LPCWSTR GetCursorForPoint(POINT clientPt) const noexcept;

private:
    void StopWorker();

    AuditViewCallbacks m_callbacks;
    std::wstring m_workspaceRoot;

    mutable std::mutex m_reportMutex;
    phide::editor::WorkspaceAuditReport m_report;
    std::atomic<bool> m_isRunning{ false };
    std::atomic<bool> m_cancelFlag{ false };
    std::thread m_worker;

    int m_filterTab = 0; // 0=All, 1=Exports, 2=CSS, 3=Deps, 4=EnvDrift
    bool m_searchFocused = false;
    std::wstring m_searchText;
    float m_scrollY = 0.0f;
    float m_maxScrollY = 0.0f;
    int m_hoveredRowIdx = -1;
    int m_hoveredBtnId = -1;

    std::wstring m_statusNotice;
    ULONGLONG m_statusExpireTick = 0;

    struct RowHit {
        size_t findingIndex = 0;
        D2D1_RECT_F bounds{};
    };
    std::vector<RowHit> m_rowHits;

    D2D1_RECT_F m_rcViewport{};
    D2D1_RECT_F m_rcBtnRerun{};
    D2D1_RECT_F m_rcBtnSyncEnv{};
    D2D1_RECT_F m_rcTabAll{};
    D2D1_RECT_F m_rcTabExports{};
    D2D1_RECT_F m_rcTabCss{};
    D2D1_RECT_F m_rcTabDeps{};
    D2D1_RECT_F m_rcTabEnv{};
    D2D1_RECT_F m_rcSearchBox{};
};

} // namespace phide::ui
