/**
 * @file StructuredViewers.hpp
 * @brief Lightweight, bounded-memory structured viewers for Binary/Hex, CSV, JSON, and streaming Logs.
 */

#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "phide/app/AppConfig.hpp"
#include "phide/ui/Theme.hpp"

namespace phide::ui {

enum class StructuredViewMode {
    None = 0,
    HexBinary,
    CsvTable,
    JsonTree,
    LogTail
};

struct StructuredViewerState {
    static constexpr size_t kHexPageBytes = 32768u;      ///< Strictly 32 KB paged window in RAM
    static constexpr size_t kHexBytesPerRow = 16u;
    static constexpr size_t kCsvMaxChars = 8u * 1024u * 1024u;
    static constexpr size_t kCsvMaxRows = 50000u;
    static constexpr size_t kJsonMaxChars = 8u * 1024u * 1024u;
    static constexpr size_t kJsonMaxNodes = 50000u;
    static constexpr size_t kLogTailMaxBytes = 262144u;  ///< Strictly 256 KB tail window

    StructuredViewMode mode = StructuredViewMode::None;
    bool isActive = false;
    std::wstring filePath;

    // Toolbar & filter bar
    bool filterFocused = false;
    std::wstring filterText;
    std::wstring statusMessage;
    D2D1_RECT_F rcToolbar{};
    D2D1_RECT_F rcBtnRawText{};
    D2D1_RECT_F rcBtnAction1{};
    D2D1_RECT_F rcBtnAction2{};
    D2D1_RECT_F rcBtnAction3{};
    D2D1_RECT_F rcBtnAction4{};
    D2D1_RECT_F rcFilterBox{};
    D2D1_RECT_F rcViewport{};
    D2D1_RECT_F rcScrollbarThumb{};
    float scrollX = 0.0f;
    size_t scrollRow = 0;

    // 1. Hex/ASCII Binary Viewer (32 KB paged)
    uint64_t hexTotalSize = 0;
    uint64_t hexWindowOffset = 0;
    std::vector<uint8_t> hexWindowBytes;
    int64_t hexSelectedOffset = -1;
    struct HexRowHit {
        uint64_t rowIndex = 0;
        D2D1_RECT_F bounds{};
    };
    std::vector<HexRowHit> hexRowHits;

    // 2. CSV/TSV Table Viewer
    wchar_t csvDelimiter = L',';
    std::vector<std::wstring> csvHeaders;
    std::vector<std::vector<std::wstring>> csvRows;
    std::vector<uint32_t> csvViewIndices;
    int csvSortCol = -1;
    bool csvSortAsc = true;
    bool csvTooLarge = false;
    bool csvTruncated = false;
    std::vector<float> csvColWidths;
    struct CsvHeaderHit {
        int colIdx = 0;
        D2D1_RECT_F bounds{};
    };
    std::vector<CsvHeaderHit> csvHeaderHits;

    // 3. JSON Flat-Arena Tree Inspector
    struct JsonNode {
        int parentIdx = -1;
        uint16_t depth = 0;
        uint8_t kind = 0; ///< 0=Object, 1=Array, 2=String, 3=Number, 4=Bool, 5=Null
        bool expanded = false;
        bool hasChildren = false;
        uint32_t childCount = 0;
        std::wstring key;
        std::wstring valuePreview;
    };
    std::vector<JsonNode> jsonNodes;
    std::vector<uint32_t> jsonVisibleIndices;
    bool jsonParseError = false;
    bool jsonTooLarge = false;
    bool jsonTruncated = false;
    int jsonSelectedVisIdx = -1;
    struct JsonRowHit {
        uint32_t visIdx = 0;
        D2D1_RECT_F bounds{};
    };
    std::vector<JsonRowHit> jsonRowHits;

    // 4. Streaming Log Tail Viewer (256 KB ring/tail)
    uint64_t logFileSize = 0;
    uint64_t logTailStartOffset = 0;
    std::vector<std::wstring> logTailLines;
    std::vector<uint32_t> logFilteredIndices;
    bool logFollow = true;
    bool logRegex = false;
    int logLevelFilter = 0; ///< 0=All, 1=ERROR, 2=WARN, 3=INFO
    uint64_t logLastPollTick = 0;
    std::wstring logFilterError;

    // Operations
    bool LoadHexPage(const std::wstring& path, uint64_t startOffset);
    bool EnsureHexRowLoaded(uint64_t targetRow);
    bool SearchHexPattern(const std::wstring& queryText);
    bool JumpToHexOffsetInput(const std::wstring& input);

    bool ParseCsvText(const std::wstring& text, wchar_t forcedDelimiter = L'\0');
    void SortCsvColumn(int colIdx);
    void RebuildCsvFilter();

    bool ParseJsonText(const std::wstring& text);
    void ToggleJsonNode(uint32_t nodeIdx);
    void SetJsonExpandAll(bool expand);
    void RebuildJsonVisible();

    bool ReloadLogTailFromFile(const std::wstring& path, bool forceFullTail = false);
    bool PollLogTail(const std::wstring& path, uint64_t nowTick);
    void RebuildLogFilter();

    void ClearCachedBuffers();
};

class StructuredViewRenderer {
public:
    StructuredViewRenderer() = default;
    ~StructuredViewRenderer() = default;

    static StructuredViewMode DetectModeForExtension(const std::wstring& extLower) noexcept;

    void DiscardDeviceResources() noexcept;

    void Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                StructuredViewerState& state, float canvasLeft, float canvasWidth,
                float topY, float clientHeight);

    bool HandleLButtonDown(POINT clientPt, StructuredViewerState& state, const app::AppConfig& config,
                           bool& outSwitchToRawText);
    bool HandleMouseWheel(short delta, bool isShiftDown, StructuredViewerState& state,
                          const app::AppConfig& config);
    bool HandleChar(wchar_t ch, StructuredViewerState& state);
    bool HandleKeyDown(WPARAM vk, StructuredViewerState& state);

private:
    void EnsureBrushes(ID2D1RenderTarget* pRT, const Theme& theme);

    void RenderHexBinary(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                         StructuredViewerState& state, const D2D1_RECT_F& vp);
    void RenderCsvTable(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                        StructuredViewerState& state, const D2D1_RECT_F& vp);
    void RenderJsonTree(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                        StructuredViewerState& state, const D2D1_RECT_F& vp);
    void RenderLogTail(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                       StructuredViewerState& state, const D2D1_RECT_F& vp);

    ID2D1RenderTarget* m_pCachedRT = nullptr;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pHeaderBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pRowAltBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pGridLineBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pSelBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pPillBgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pErrBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pWarnBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_pInfoBrush;
};

} // namespace phide::ui
