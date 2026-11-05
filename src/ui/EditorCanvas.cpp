/**
 * @file EditorCanvas.cpp
 * @brief Implementation of dark workspace surface with modern rounded container aesthetics.
 */

#include "phide/ui/EditorCanvas.hpp"
#include "phide/ui/WatermarkLogo.hpp"
#include "phide/ui/ColorPickerModal.hpp"
#include "phide/editor/TailwindHelper.hpp"
#include <algorithm>
#include <wincodec.h>
#include <filesystem>
#include <regex>

namespace phide::ui {

std::vector<LineColorSpan> EditorCanvas::ScanLineColors(const std::wstring& lineStr) {
    std::vector<LineColorSpan> lineColors;
    size_t scanCIdx = 0;
    while (scanCIdx < lineStr.length()) {
        if (lineStr[scanCIdx] == L'#') {
            size_t hStart = scanCIdx;
            size_t hEnd = hStart + 1;
            while (hEnd < lineStr.length() && iswxdigit(lineStr[hEnd])) hEnd++;
            size_t hLen = hEnd - hStart - 1;
            if (hLen == 3 || hLen == 4 || hLen == 6 || hLen == 8) {
                std::wstring colorStr = lineStr.substr(hStart, hEnd - hStart);
                D2D1_COLOR_F c{};
                ColorFormat fmt = ColorFormat::Hex;
                bool hasAlpha = false;
                if (ColorPickerModal::TryParseColor(colorStr, c, fmt, hasAlpha)) {
                    lineColors.push_back({ hStart, colorStr.length(), colorStr, c, fmt, hasAlpha });
                }
            }
            scanCIdx = hEnd;
            continue;
        }
        if (lineStr.compare(scanCIdx, 4, L"rgb(") == 0 || lineStr.compare(scanCIdx, 5, L"rgba(") == 0 ||
            lineStr.compare(scanCIdx, 4, L"hsl(") == 0 || lineStr.compare(scanCIdx, 5, L"hsla(") == 0) {
            size_t closeParen = lineStr.find(L')', scanCIdx);
            if (closeParen != std::wstring::npos) {
                std::wstring colorStr = lineStr.substr(scanCIdx, closeParen - scanCIdx + 1);
                D2D1_COLOR_F c{};
                ColorFormat fmt = ColorFormat::Hex;
                bool hasAlpha = false;
                if (ColorPickerModal::TryParseColor(colorStr, c, fmt, hasAlpha)) {
                    lineColors.push_back({ scanCIdx, colorStr.length(), colorStr, c, fmt, hasAlpha });
                }
                scanCIdx = closeParen + 1;
                continue;
            }
        }

        // Tailwind CSS Color Class Scanning (e.g. bg-black, bg-green-600, text-white, border-blue-400, dark:bg-black)
        if (scanCIdx == 0 || (!iswalnum(lineStr[scanCIdx - 1]) && lineStr[scanCIdx - 1] != L'-' && lineStr[scanCIdx - 1] != L':')) {
            size_t tokEnd = scanCIdx;
            while (tokEnd < lineStr.length() && (iswalnum(lineStr[tokEnd]) || lineStr[tokEnd] == L'-' || lineStr[tokEnd] == L':' || lineStr[tokEnd] == L'/')) {
                tokEnd++;
            }
            if (tokEnd > scanCIdx) {
                std::wstring_view word(lineStr.data() + scanCIdx, tokEnd - scanCIdx);
                size_t colPos = word.rfind(L':');
                std::wstring_view coreClass = (colPos != std::wstring_view::npos) ? word.substr(colPos + 1) : word;
                size_t swatchCol = (colPos != std::wstring_view::npos) ? (scanCIdx + colPos + 1) : scanCIdx;

                editor::TailwindColorInfo twInfo;
                if (editor::TailwindHelper::TryParseClass(coreClass, twInfo)) {
                    float r = ((twInfo.hex >> 16) & 0xFF) / 255.0f;
                    float g = ((twInfo.hex >> 8) & 0xFF) / 255.0f;
                    float b = (twInfo.hex & 0xFF) / 255.0f;
                    lineColors.push_back({ swatchCol, coreClass.length(), std::wstring(coreClass), D2D1::ColorF(r, g, b, 1.0f), ColorFormat::Hex, false });
                    scanCIdx = tokEnd;
                    continue;
                }
            }
        }

        scanCIdx++;
    }
    std::sort(lineColors.begin(), lineColors.end(), [](const LineColorSpan& a, const LineColorSpan& b) {
        return a.col < b.col;
    });
    return lineColors;
}

size_t EditorCanvas::CountSwatchesBefore(const std::vector<LineColorSpan>& spans, size_t col) {
    size_t count = 0;
    for (const auto& cs : spans) {
        if (col >= cs.col) count++;
    }
    return count;
}

float EditorCanvas::GetColumnVisualOffset(const std::wstring& lineText, const std::vector<LineColorSpan>& spans, size_t col, float charW, float swatchSpan, int tabSize) {
    float visualCols = 0.0f;
    for (size_t i = 0; i < col && i < lineText.length(); ++i) {
        if (lineText[i] == L'\t') {
            int curColInt = static_cast<int>(visualCols);
            int spaces = tabSize - (curColInt % (tabSize > 0 ? tabSize : 4));
            if (spaces <= 0) spaces = (tabSize > 0 ? tabSize : 4);
            visualCols += static_cast<float>(spaces);
        } else {
            visualCols += 1.0f;
        }
    }
    if (col > lineText.length()) {
        visualCols += static_cast<float>(col - lineText.length());
    }
    return visualCols * charW + static_cast<float>(CountSwatchesBefore(spans, col)) * swatchSpan;
}

float EditorCanvas::GetColumnVisualOffset(const std::vector<LineColorSpan>& spans, size_t col, float charW, float swatchSpan) {
    return static_cast<float>(col) * charW + static_cast<float>(CountSwatchesBefore(spans, col)) * swatchSpan;
}

static bool IsFunctionOrTestLine(const std::wstring& line) {
    if (line.empty()) return false;
    size_t start = 0;
    while (start < line.length() && (line[start] == L' ' || line[start] == L'\t')) {
        start++;
    }
    if (start >= line.length()) return false;
    std::wstring trimmed = line.substr(start);

    // Skip comment lines
    if (trimmed.starts_with(L"//") || trimmed.starts_with(L"/*") || trimmed.starts_with(L"*") || trimmed.starts_with(L"#")) {
        return false;
    }

    // 1. Test runner macros & functions (Image 1: test("renders learn react link", () => {)
    if (trimmed.starts_with(L"test(") || trimmed.starts_with(L"test.") ||
        trimmed.starts_with(L"it(") || trimmed.starts_with(L"it.") ||
        trimmed.starts_with(L"describe(") || trimmed.starts_with(L"describe.") ||
        trimmed.starts_with(L"TEST(") || trimmed.starts_with(L"TEST_F(") || trimmed.starts_with(L"TEST_P(")) {
        return true;
    }

    // 2. Python functions & tests
    if (trimmed.starts_with(L"def ") || trimmed.starts_with(L"async def ")) {
        return (start == 0 || trimmed.find(L"def test_") != std::wstring::npos);
    }

    // 3. Go / Rust (top-level)
    if (start == 0 && (trimmed.starts_with(L"func ") || trimmed.starts_with(L"fn ") || trimmed.starts_with(L"pub fn "))) {
        return true;
    }

    // 4. PHP / JS standard functions
    if (trimmed.starts_with(L"function ") || trimmed.starts_with(L"function* ") ||
        trimmed.starts_with(L"async function ") || trimmed.starts_with(L"async function* ")) {
        return (start <= 4);
    }
    if (trimmed.starts_with(L"public function ") || trimmed.starts_with(L"private function ") ||
        trimmed.starts_with(L"protected function ") || trimmed.starts_with(L"static function ") ||
        trimmed.starts_with(L"public static function ") || trimmed.starts_with(L"private static function ")) {
        return true;
    }

    // 5. JS/TS exports (top-level or exported)
    if (trimmed.starts_with(L"export default function") || trimmed.starts_with(L"export function") ||
        trimmed.starts_with(L"export async function")) {
        return true;
    }

    // 6. Arrow functions / React components (e.g. export const MyComp = () => ... or top-level const MyComp = () => ...)
    // Must NOT be a timer, interval, or callback variable like setInterval or setTimeout
    if ((start == 0 || trimmed.starts_with(L"export const ") || trimmed.starts_with(L"export let ")) &&
        (trimmed.starts_with(L"const ") || trimmed.starts_with(L"let ") || trimmed.starts_with(L"export const ") || trimmed.starts_with(L"export let ")) &&
        (trimmed.find(L"=>") != std::wstring::npos)) {
        if (trimmed.find(L"setInterval") == std::wstring::npos &&
            trimmed.find(L"setTimeout") == std::wstring::npos &&
            trimmed.find(L"addEventListener") == std::wstring::npos &&
            trimmed.find(L"subscribe") == std::wstring::npos) {
            size_t eqPos = trimmed.find(L'=');
            size_t arrowPos = trimmed.find(L"=>");
            if (eqPos != std::wstring::npos && arrowPos != std::wstring::npos && eqPos < arrowPos) {
                // Ensure there is no function call immediately before the arrow callback
                std::wstring between = trimmed.substr(eqPos + 1, arrowPos - (eqPos + 1));
                if (between.find(L'(') != std::wstring::npos) {
                    return true;
                }
            }
        }
    }

    // 7. C++ / Java / C# methods
    if (start <= 4 &&
        (trimmed.starts_with(L"void ") || trimmed.starts_with(L"bool ") || trimmed.starts_with(L"int ") ||
         trimmed.starts_with(L"float ") || trimmed.starts_with(L"double ") || trimmed.starts_with(L"auto ") ||
         trimmed.starts_with(L"std::") || trimmed.starts_with(L"virtual ")) &&
        (trimmed.find(L"(") != std::wstring::npos) && (trimmed.find(L")") != std::wstring::npos) &&
        !trimmed.ends_with(L";")) {
        return true;
    }

    return false;
}

static Microsoft::WRL::ComPtr<ID2D1Bitmap> CanvasLoadPngBitmap(ID2D1RenderTarget* pRT, const std::wstring& path) {
    if (!pRT || path.empty()) return nullptr;
    std::wstring tryPaths[] = { path, L"bin\\" + path, L"..\\" + path };
    for (const auto& p : tryPaths) {
        if (!std::filesystem::exists(p)) continue;
        Microsoft::WRL::ComPtr<IWICImagingFactory> pWicFactory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWicFactory)))) continue;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDecoder;
        if (FAILED(pWicFactory->CreateDecoderFromFilename(p.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder))) continue;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
        if (FAILED(pDecoder->GetFrame(0, &pFrame))) continue;
        Microsoft::WRL::ComPtr<IWICFormatConverter> pConverter;
        if (FAILED(pWicFactory->CreateFormatConverter(&pConverter))) continue;
        if (FAILED(pConverter->Initialize(pFrame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) continue;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> pBitmap;
        if (SUCCEEDED(pRT->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &pBitmap))) {
            return pBitmap;
        }
    }
    return nullptr;
}

static void FillUpperLeftRoundedRect(ID2D1RenderTarget* pRT, ID2D1Brush* pBrush,
                                     const D2D1_RECT_F& rc, float radius) {
    if (!pRT || !pBrush) return;
    if (radius <= 0.0f) {
        pRT->FillRectangle(rc, pBrush);
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (!pFactory) {
        pRT->FillRectangle(rc, pBrush);
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> pPath;
    if (FAILED(pFactory->CreatePathGeometry(&pPath))) {
        pRT->FillRectangle(rc, pBrush);
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(pPath->Open(&sink))) {
        pRT->FillRectangle(rc, pBrush);
        return;
    }

    const float maxR = (std::min)(rc.right - rc.left, rc.bottom - rc.top);
    const float r = (maxR > 0.0f) ? (std::min)(radius, maxR) : 0.0f;

    sink->BeginFigure(D2D1::Point2F(rc.left + r, rc.top), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(rc.right, rc.top));
    sink->AddLine(D2D1::Point2F(rc.right, rc.bottom));
    sink->AddLine(D2D1::Point2F(rc.left, rc.bottom));
    sink->AddLine(D2D1::Point2F(rc.left, rc.top + r));

    if (r > 0.0f) {
        D2D1_ARC_SEGMENT arc{};
        arc.point = D2D1::Point2F(rc.left + r, rc.top);
        arc.size = D2D1::SizeF(r, r);
        arc.rotationAngle = 0.0f;
        arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
        arc.arcSize = D2D1_ARC_SIZE_SMALL;
        sink->AddArc(arc);
    }

    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    pRT->FillGeometry(pPath.Get(), pBrush);
}

static void DrawKeycap(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                       const wchar_t* text, float& curRightX, float centerY, float capH) {
    const size_t len = wcslen(text);
    const float capW = config.ScaleF(static_cast<float>(len * 7.5f + 14.0f));
    const float x = curRightX - capW;
    const float y = centerY - capH / 2.0f;

    D2D1_RECT_F rc = D2D1::RectF(x, y, curRightX, y + capH);
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rc, 4.0f, 4.0f);

    pRT->FillRoundedRectangle(rrc, theme.brushCaptionHover.Get());
    pRT->DrawRoundedRectangle(rrc, theme.brushBorderSubtle.Get(), 1.0f);

    IDWriteTextFormat* pFmt = theme.fontKeycap ? theme.fontKeycap.Get() : theme.fontMenu.Get();
    pRT->DrawText(text, static_cast<UINT32>(len), pFmt, rc, theme.brushTextPrimary.Get());

    curRightX = x - config.ScaleF(4.0f);
}

static void DrawPlus(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                     float& curRightX, float centerY, float capH) {
    const float plusW = config.ScaleF(12.0f);
    const float x = curRightX - plusW;
    const float y = centerY - capH / 2.0f;

    D2D1_RECT_F rc = D2D1::RectF(x, y, curRightX, y + capH);
    IDWriteTextFormat* pFmt = theme.fontKeycap ? theme.fontKeycap.Get() : theme.fontMenu.Get();
    pRT->DrawText(L"+", 1, pFmt, rc, theme.brushTextMuted.Get());

    curRightX = x - config.ScaleF(4.0f);
}

EditorCanvas::EditorCanvas() = default;

void EditorCanvas::DiscardDeviceResources() {
    m_pWatermarkBitmap.Reset();
    m_pWatermarkBitmapLight.Reset();
    m_pCanvasGeometry.Reset();
    m_learningHubBitmaps.clear();
    m_charWidth = 0.0f;
    m_cachedLeft = -1.0f;
    m_cachedTop = -1.0f;
    m_cachedRight = -1.0f;
    m_cachedBottom = -1.0f;
}

void EditorCanvas::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          std::vector<editor::EditorGroup>& groups, int focusedGroupIdx,
                          float canvasLeft, float canvasWidth, float clientHeight,
                          bool hasLeftDrawer,
                          const std::wstring& highlightQuery,
                          bool highlightMatchCase,
                          bool highlightWholeWord,
                          bool highlightUseRegex,
                          bool isRenaming) {
    if (!pRT) return;
    if (groups.empty()) {
        groups.push_back(editor::EditorGroup{});
    }

    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float canvasRight = canvasLeft + canvasWidth;
    const float cornerRadius = hasLeftDrawer ? 0.0f : config.ScaleF(14.0f);

    // Check if cached geometry needs rebuilding (e.g. on resize, layout changes, or drawer open/close)
    if (!m_pCanvasGeometry ||
        m_cachedLeft != canvasLeft ||
        m_cachedTop != headerH ||
        m_cachedRight != canvasRight ||
        m_cachedBottom != clientHeight ||
        m_cachedHasLeftDrawer != hasLeftDrawer)
    {
        m_cachedLeft          = canvasLeft;
        m_cachedTop           = headerH;
        m_cachedRight         = canvasRight;
        m_cachedBottom        = clientHeight;
        m_cachedHasLeftDrawer = hasLeftDrawer;
        m_pCanvasGeometry.Reset();

        Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
        pRT->GetFactory(&pFactory);
        if (pFactory) {
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> pPath;
            if (SUCCEEDED(pFactory->CreatePathGeometry(&pPath))) {
                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(pPath->Open(&sink))) {
                    const float maxR = (std::min)(canvasWidth / 2.0f, (clientHeight - headerH) / 2.0f);
                    const float r = (maxR > 0.0f) ? (std::min)(cornerRadius, maxR) : 0.0f;

                    sink->BeginFigure(D2D1::Point2F(canvasLeft + r, headerH), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(canvasRight, headerH));
                    sink->AddLine(D2D1::Point2F(canvasRight, clientHeight));
                    sink->AddLine(D2D1::Point2F(canvasLeft, clientHeight));
                    sink->AddLine(D2D1::Point2F(canvasLeft, headerH + r));

                    if (r > 0.0f) {
                        D2D1_ARC_SEGMENT arc = {};
                        arc.point = D2D1::Point2F(canvasLeft + r, headerH);
                        arc.size = D2D1::SizeF(r, r);
                        arc.rotationAngle = 0.0f;
                        arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                        arc.arcSize = D2D1_ARC_SIZE_SMALL;
                        sink->AddArc(arc);
                    }

                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();

                    m_pCanvasGeometry = pPath;
                }
            }
        }
    }

    m_pCurrentGroups = &groups;

    // 1. Fill dark workspace surface (#1E1E1E) with geometry curved ONLY at upper-left
    if (m_pCanvasGeometry) {
        pRT->FillGeometry(m_pCanvasGeometry.Get(), theme.brushCanvasBg.Get());
    } else {
        D2D1_RECT_F rcFallback = D2D1::RectF(canvasLeft, headerH, canvasRight, clientHeight);
        pRT->FillRectangle(rcFallback, theme.brushCanvasBg.Get());
    }

    // 1b. Render ambient theme visual effects (3D neon perspective grid, stars, matrix rain) across canvas
    RenderCanvasThemeEffects(pRT, theme, config, canvasLeft, canvasWidth, headerH + config.ScaleF(34.0f), clientHeight);

    size_t numGroups = groups.size();
    const float paneGap = 1.0f;
    const float totalGaps = (numGroups > 1) ? static_cast<float>(numGroups - 1) * paneGap : 0.0f;
    const float paneWidth = (canvasWidth - totalGaps) / static_cast<float>(numGroups);

    for (size_t g = 0; g < numGroups; ++g) {
        float pLeft = canvasLeft + g * (paneWidth + paneGap);
        float pWidth = (g == numGroups - 1) ? (canvasRight - pLeft) : paneWidth;
        groups[g].bounds = D2D1::RectF(pLeft, headerH, pLeft + pWidth, clientHeight);

        bool isGroupFocused = (static_cast<int>(g) == focusedGroupIdx);
        bool isFirstPane = (g == 0);

        // Push clip rect for this editor group
        pRT->PushAxisAlignedClip(groups[g].bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        RenderSinglePane(pRT, theme, config, groups[g].docs, groups[g].activeDocIdx,
                         pLeft, pWidth, headerH, clientHeight,
                         (isFirstPane ? hasLeftDrawer : true), isGroupFocused,
                         highlightQuery, highlightMatchCase, highlightWholeWord, highlightUseRegex,
                         isRenaming, groups[g].tabScrollOffset, groups[g].totalTabsWidth, static_cast<int>(g),
                         &groups[g].tabScrollTrack, &groups[g].tabScrollThumb,
                         &groups[g].lastScrolledActiveDocIdx, groups[g].isDraggingTabScroll);

        // Visual drop zone feedback when dragging a tab across split groups (Image 4 / User request)
        if (m_isDraggingTab && m_tabDropTargetGroup == static_cast<int>(g) && m_dragTabGroup != static_cast<int>(g)) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dropZoneBrush;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.08f), &dropZoneBrush);
            if (dropZoneBrush) {
                pRT->FillRectangle(groups[g].bounds, dropZoneBrush.Get());
            }
            if (theme.brushBrandAccent) {
                pRT->DrawRectangle(groups[g].bounds, theme.brushBrandAccent.Get(), 1.5f);
            }
        }

        pRT->PopAxisAlignedClip();

        // 1px Vertical divider line between panes (Image 4)
        if (g < numGroups - 1) {
            float dividerX = pLeft + pWidth + 0.5f;
            pRT->DrawLine(
                D2D1::Point2F(dividerX, headerH),
                D2D1::Point2F(dividerX, clientHeight),
                theme.brushBorderSubtle.Get(),
                1.0f
            );
        }
    }

    // Render floating refactor balloon popover if visible (Image 5)
    if (m_showRefactorPopup) {
        RenderRefactorPopup(pRT, theme, config, clientHeight);
    }

    // Render floating autocomplete suggestions popup (Image 5)
    if (m_showAutocomplete && !m_autocompleteItems.empty()) {
        RenderAutocompletePopup(pRT, theme, config, canvasLeft + canvasWidth, clientHeight);
    }

    // Render floating dragged tab pill on TOP of EVERYTHING (above tabs, breadcrumbs, and editor canvas)
    if (m_isDraggingTab && !m_dragTabTitle.empty() && (m_dragTabPos.x > 0.0f || m_dragTabPos.y > 0.0f)) {
        float pillW = (std::min)(220.0f, (std::max)(130.0f, config.ScaleF(145.0f)));
        float pillH = config.ScaleF(28.0f);
        float pillX = m_dragTabPos.x - pillW / 2.0f;
        float pillY = m_dragTabPos.y - pillH / 2.0f;
        D2D1_RECT_F rcPill = D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH);
        D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(rcPill, 4.0f, 4.0f);

        // Elevated card drop shadow
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadowBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.40f), &shadowBrush);
        if (shadowBrush) {
            D2D1_ROUNDED_RECT rrcShadow = D2D1::RoundedRect(
                D2D1::RectF(pillX + 2.0f, pillY + 3.0f, pillX + pillW + 2.0f, pillY + pillH + 3.0f),
                4.0f, 4.0f
            );
            pRT->FillRoundedRectangle(rrcShadow, shadowBrush.Get());
        }

        // Pill background
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBg;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.18f, 0.18f, 0.20f, 0.98f) : D2D1::ColorF(0.92f, 0.92f, 0.94f, 0.98f),
            &pillBg
        );
        if (pillBg) pRT->FillRoundedRectangle(rrcPill, pillBg.Get());

        // Brand accent border
        if (theme.brushBrandAccent) {
            pRT->DrawRoundedRectangle(rrcPill, theme.brushBrandAccent.Get(), 1.5f);
        }

        // File Icon (Seti or Lucide)
        float iconX = pillX + config.ScaleF(8.0f);
        float iconY = pillY + (pillH - config.ScaleF(16.0f)) / 2.0f;
        D2D1_RECT_F iconBounds = D2D1::RectF(iconX, iconY, iconX + config.ScaleF(16.0f), iconY + config.ScaleF(16.0f));
        auto setiInfo = theme.GetSetiIcon(m_dragTabTitle, false, false);
        if (theme.fontSeti && setiInfo.pBrush) {
            pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), iconBounds, setiInfo.pBrush);
        } else {
            auto iconType = LucideIcons::GetIconTypeForPath(m_dragTabTitle, false, false);
            m_icons.DrawFileIcon(pRT, theme, iconBounds, iconType);
        }

        // File title text
        float textX = iconX + config.ScaleF(18.0f);
        float textW = pillW - config.ScaleF(44.0f);
        D2D1_RECT_F rcText = D2D1::RectF(textX, pillY, textX + textW, pillY + pillH);
        IDWriteTextFormat* pFmt = theme.fontTab ? theme.fontTab.Get() : theme.fontBody.Get();
        ID2D1Brush* pTxtBrush = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get();
        if (pFmt && pTxtBrush) {
            pRT->DrawText(m_dragTabTitle.c_str(), static_cast<UINT32>(m_dragTabTitle.size()),
                          pFmt, rcText, pTxtBrush);
        }

        // Close 'x' icon
        float closeX = pillX + pillW - config.ScaleF(18.0f);
        float closeY = pillY + (pillH - config.ScaleF(10.0f)) / 2.0f;
        float cx = closeX + config.ScaleF(5.0f);
        float cy = closeY + config.ScaleF(5.0f);
        float cr = config.ScaleF(3.5f);
        if (theme.brushTextSecondary) {
            pRT->DrawLine(D2D1::Point2F(cx - cr, cy - cr), D2D1::Point2F(cx + cr, cy + cr), theme.brushTextSecondary.Get(), 1.2f);
            pRT->DrawLine(D2D1::Point2F(cx + cr, cy - cr), D2D1::Point2F(cx - cr, cy + cr), theme.brushTextSecondary.Get(), 1.2f);
        }
    }
}

void EditorCanvas::RenderSinglePane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                    std::vector<editor::OpenDocument>& docs, int docIdx,
                                    float canvasLeft, float canvasWidth, float headerH, float clientHeight,
                                    bool hasLeftDrawer, bool isPaneFocused,
                                    const std::wstring& highlightQuery,
                                    bool highlightMatchCase,
                                    bool highlightWholeWord,
                                    bool highlightUseRegex,
                                    bool isRenaming,
                                    float& tabScrollOffset, float& totalTabsWidth, int groupIdx,
                                    D2D1_RECT_F* outTrack, D2D1_RECT_F* outThumb,
                                    int* pLastScrolledActiveDocIdx, bool isDraggingTabScroll) {
    if (docs.empty() || docIdx < 0 || docIdx >= static_cast<int>(docs.size())) {
        m_packageManagerView.HideWebView();
        RenderPlaceholder(pRT, theme, config, canvasLeft, canvasWidth, headerH, clientHeight);
        return;
    }

    const float tabH = config.ScaleF(34.0f);
    RenderTabs(pRT, theme, config, canvasLeft, canvasWidth, headerH, docs, docIdx, hasLeftDrawer, isPaneFocused,
               tabScrollOffset, totalTabsWidth, groupIdx, outTrack, outThumb, pLastScrolledActiveDocIdx, isDraggingTabScroll);

    auto& activeDoc = docs[docIdx];
    const float breadcrumbH = config.showBreadcrumbs ? config.ScaleF(24.0f) : 0.0f;
    if (config.showBreadcrumbs) {
        RenderBreadcrumbs(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, headerH + tabH, breadcrumbH);
    }

    float contentTopY = headerH + tabH + breadcrumbH;
    if (activeDoc.isSpecialView && activeDoc.viewType == L"keybindings") {
        m_packageManagerView.HideWebView();
        m_keybindingsView.Render(pRT, theme, config, canvasLeft, canvasWidth, contentTopY, clientHeight - contentTopY);
    } else if (activeDoc.isSpecialView && activeDoc.viewType == L"packages") {
        m_packageManagerView.Render(pRT, theme, config, canvasLeft, canvasWidth, contentTopY, clientHeight - contentTopY);
    } else if (activeDoc.isSpecialView && activeDoc.viewType == L"notebook") {
        m_packageManagerView.HideWebView();
        m_notebookView.Render(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight - contentTopY);
    } else if (activeDoc.isSpecialView && activeDoc.viewType == L"student") {
        m_packageManagerView.HideWebView();
        RenderStudentView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
    } else if (activeDoc.isSpecialView && activeDoc.viewType == L"graph") {
        m_packageManagerView.HideWebView();
        m_graphView.Render(pRT, theme, config, canvasLeft, canvasWidth, contentTopY, clientHeight - contentTopY);
    } else if (activeDoc.isSpecialView && activeDoc.viewType == L"settings") {
        m_packageManagerView.HideWebView();
        m_settingsView.Render(pRT, theme, config, canvasLeft, canvasWidth, contentTopY, clientHeight - contentTopY);
    } else if ((activeDoc.isSpecialView && activeDoc.viewType == L"course") || !activeDoc.courseId.empty()) {
        m_packageManagerView.HideWebView();
        RenderCourseView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
    } else if (activeDoc.isSpecialView && activeDoc.viewType == L"markdown_preview") {
        m_packageManagerView.HideWebView();
        RenderMarkdownPreview(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
    } else if (activeDoc.isSpecialView && activeDoc.viewType == L"extension_detail") {
        m_packageManagerView.HideWebView();
        RenderExtensionDetail(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
    } else {
        m_packageManagerView.HideWebView();
        if (activeDoc.isImageView) {
            RenderImageView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
        } else if (activeDoc.isUnsupportedBinary) {
            RenderUnsupportedBinaryView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
        } else if (activeDoc.isDiffView) {
            if (activeDoc.isMultiFileDiff) {
                RenderMultiFileDiffView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight, isPaneFocused);
            } else if (activeDoc.isDiffBinary) {
                RenderDiffBinaryView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight);
            } else {
                RenderDiffView(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight, isPaneFocused);
            }
        } else {
            RenderCode(pRT, theme, config, activeDoc, canvasLeft, canvasWidth, contentTopY, clientHeight, isPaneFocused,
                       highlightQuery, highlightMatchCase, highlightWholeWord, highlightUseRegex, isRenaming);
        }
    }
}

void EditorCanvas::RenderPlaceholder(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     float canvasLeft, float canvasWidth, float headerH, float clientHeight) {
    const float centerY = headerH + (clientHeight - headerH) / 2.0f;

    // Initialize metallic white watermark bitmap for dark mode if needed
    if (!m_pWatermarkBitmap) {
        D2D1_SIZE_U size = D2D1::SizeU(kWatermarkLogoWidth, kWatermarkLogoHeight);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        pRT->CreateBitmap(size, g_watermarkLogoBgra, kWatermarkLogoWidth * 4, props, &m_pWatermarkBitmap);
    }
    // Initialize inverted watermark bitmap for light mode if needed (black outer square, white inside)
    if (!m_pWatermarkBitmapLight) {
        D2D1_SIZE_U size = D2D1::SizeU(kWatermarkLogoWidth, kWatermarkLogoHeight);
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        std::vector<uint8_t> lightBgra(sizeof(g_watermarkLogoBgra));
        for (size_t i = 0; i < sizeof(g_watermarkLogoBgra); i += 4) {
            uint8_t a = g_watermarkLogoBgra[i + 3];
            if (a == 0) {
                lightBgra[i + 0] = 0;
                lightBgra[i + 1] = 0;
                lightBgra[i + 2] = 0;
                lightBgra[i + 3] = 0;
            } else {
                uint8_t invR = static_cast<uint8_t>((255 - (g_watermarkLogoBgra[i + 2] * 255 / a)) * a / 255);
                uint8_t invG = static_cast<uint8_t>((255 - (g_watermarkLogoBgra[i + 1] * 255 / a)) * a / 255);
                uint8_t invB = static_cast<uint8_t>((255 - (g_watermarkLogoBgra[i + 0] * 255 / a)) * a / 255);
                lightBgra[i + 0] = invB;
                lightBgra[i + 1] = invG;
                lightBgra[i + 2] = invR;
                lightBgra[i + 3] = a;
            }
        }
        pRT->CreateBitmap(size, lightBgra.data(), kWatermarkLogoWidth * 4, props, &m_pWatermarkBitmapLight);
    }

    // Render Logo Watermark in center of canvas (dark logo in light mode, metallic in dark mode)
    const float logoSize = config.ScaleF(84.0f);
    const float logoX = canvasLeft + (canvasWidth - logoSize) / 2.0f;
    const float logoY = centerY - logoSize / 2.0f - config.ScaleF(40.0f);
    D2D1_RECT_F rcLogo = D2D1::RectF(logoX, logoY, logoX + logoSize, logoY + logoSize);

    ID2D1Bitmap* pBmp = theme.IsDarkMode() ? m_pWatermarkBitmap.Get() : m_pWatermarkBitmapLight.Get();
    if (pBmp) {
        pRT->DrawBitmap(pBmp, rcLogo);
    }

    // Render Visually Appealing Keybinding Cheat-sheet Card (No emojis, sleek tactile keycaps)
    const float cardW = config.ScaleF(360.0f);
    const float cardH = config.ScaleF(118.0f);
    const float cardX = canvasLeft + (canvasWidth - cardW) / 2.0f;
    const float cardY = logoY + logoSize + config.ScaleF(24.0f);

    D2D1_RECT_F rcCard = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);
    D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, 8.0f, 8.0f);

    // Subtle dark glass card surface
    pRT->FillRoundedRectangle(rrcCard, theme.brushDrawerBg.Get());
    pRT->DrawRoundedRectangle(rrcCard, theme.brushBorderSubtle.Get(), 1.0f);

    // Card Row Metrics
    const float padX = config.ScaleF(16.0f);
    const float rowH = cardH / 3.0f;
    const float capH = config.ScaleF(20.0f);

    // Row Hover Highlight
    if (m_hoveredWatermarkRow >= 0 && m_hoveredWatermarkRow < 3) {
        float rTop = cardY + m_hoveredWatermarkRow * rowH + 2.0f;
        float rBot = cardY + (m_hoveredWatermarkRow + 1) * rowH - 2.0f;
        D2D1_RECT_F rcHover = D2D1::RectF(cardX + 2.0f, rTop, cardX + cardW - 2.0f, rBot);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcHover, 6.0f, 6.0f), theme.brushItemHover.Get());
    }

    // Row 1: Show Explorer  -->  Ctrl + B
    float row1CenterY = cardY + rowH * 0.5f;
    D2D1_RECT_F rcLabel1 = D2D1::RectF(cardX + padX, row1CenterY - config.ScaleF(10.0f), cardX + cardW / 2.0f, row1CenterY + config.ScaleF(10.0f));
    pRT->DrawText(L"Show Explorer", 13, theme.fontHint.Get(), rcLabel1,
                  (m_hoveredWatermarkRow == 0) ? (theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) : theme.brushTextSecondary.Get());
    float rx1 = cardX + cardW - padX;
    DrawKeycap(pRT, theme, config, L"B", rx1, row1CenterY, capH);
    DrawPlus(pRT, theme, config, rx1, row1CenterY, capH);
    DrawKeycap(pRT, theme, config, L"Ctrl", rx1, row1CenterY, capH);

    // Row 2: Open Workspace -->  Ctrl + O
    float row2CenterY = cardY + rowH * 1.5f;
    D2D1_RECT_F rcLabel2 = D2D1::RectF(cardX + padX, row2CenterY - config.ScaleF(10.0f), cardX + cardW / 2.0f, row2CenterY + config.ScaleF(10.0f));
    pRT->DrawText(L"Open Workspace", 14, theme.fontHint.Get(), rcLabel2,
                  (m_hoveredWatermarkRow == 1) ? (theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) : theme.brushTextSecondary.Get());
    float rx2 = cardX + cardW - padX;
    DrawKeycap(pRT, theme, config, L"O", rx2, row2CenterY, capH);
    DrawPlus(pRT, theme, config, rx2, row2CenterY, capH);
    DrawKeycap(pRT, theme, config, L"Ctrl", rx2, row2CenterY, capH);

    // Row 3: Command Palette --> Ctrl + Shift + P
    float row3CenterY = cardY + rowH * 2.5f;
    D2D1_RECT_F rcLabel3 = D2D1::RectF(cardX + padX, row3CenterY - config.ScaleF(10.0f), cardX + cardW / 2.0f, row3CenterY + config.ScaleF(10.0f));
    pRT->DrawText(L"Command Palette", 15, theme.fontHint.Get(), rcLabel3,
                  (m_hoveredWatermarkRow == 2) ? (theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) : theme.brushTextSecondary.Get());
    float rx3 = cardX + cardW - padX;
    DrawKeycap(pRT, theme, config, L"P", rx3, row3CenterY, capH);
    DrawPlus(pRT, theme, config, rx3, row3CenterY, capH);
    DrawKeycap(pRT, theme, config, L"Shift", rx3, row3CenterY, capH);
    DrawPlus(pRT, theme, config, rx3, row3CenterY, capH);
    DrawKeycap(pRT, theme, config, L"Ctrl", rx3, row3CenterY, capH);
}

int EditorCanvas::HitTestWatermarkCard(POINT pt, const app::AppConfig& config, float canvasLeft, float canvasWidth, float headerH, float clientHeight) const {
    const float centerY = headerH + (clientHeight - headerH) / 2.0f;
    const float logoSize = config.ScaleF(84.0f);
    const float logoY = centerY - logoSize / 2.0f - config.ScaleF(40.0f);
    const float cardW = config.ScaleF(360.0f);
    const float cardH = config.ScaleF(118.0f);
    const float cardX = canvasLeft + (canvasWidth - cardW) / 2.0f;
    const float cardY = logoY + logoSize + config.ScaleF(24.0f);

    if (pt.x < cardX || pt.x > cardX + cardW || pt.y < cardY || pt.y > cardY + cardH) {
        return -1;
    }

    const float rowH = cardH / 3.0f;
    float relY = static_cast<float>(pt.y) - cardY;
    if (relY < rowH) return 0;        // Show Explorer (Ctrl+B)
    if (relY < rowH * 2.0f) return 1;   // Open Workspace (Ctrl+O)
    return 2;                          // Command Palette (Ctrl+Shift+P)
}

void EditorCanvas::RenderTabs(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              float canvasLeft, float canvasWidth, float headerH,
                              std::vector<editor::OpenDocument>& docs, int activeDocIdx,
                              bool hasLeftDrawer, bool isFocused,
                              float& tabScrollOffset, float& totalTabsWidth, int groupIdx,
                              D2D1_RECT_F* outTrack, D2D1_RECT_F* outThumb,
                              int* pLastScrolledActiveDocIdx, bool isDraggingTabScroll) {
    const float tabH = config.ScaleF(34.0f);
    const float cornerRadius = hasLeftDrawer ? 0.0f : config.ScaleF(14.0f);

    // 1. Tab Bar Background (#181818 in dark, #ECECEC in light)
    D2D1_RECT_F rcTabBar = D2D1::RectF(canvasLeft, headerH, canvasLeft + canvasWidth, headerH + tabH);
    FillUpperLeftRoundedRect(pRT, theme.brushTabBarBg.Get(), rcTabBar, cornerRadius);

    // Bottom border of tab bar
    if (theme.brushTabBorder) {
        pRT->DrawLine(
            D2D1::Point2F(canvasLeft, headerH + tabH - 0.5f),
            D2D1::Point2F(canvasLeft + canvasWidth, headerH + tabH - 0.5f),
            theme.brushTabBorder.Get(),
            1.0f
        );
    }

    // First pass: Precalculate tab widths and total strip width for horizontal scrolling
    std::vector<float> tabWidths(docs.size());
    float sumW = 0.0f;
    for (size_t i = 0; i < docs.size(); ++i) {
        auto& doc = docs[i];
        bool isFirstTab = (i == 0);
        float tabCurve = (isFirstTab && !hasLeftDrawer) ? cornerRadius : 0.0f;

        std::wstring title = doc.fileName.empty() ? L"Untitled-1" : doc.fileName;
        float textW = static_cast<float>(title.length()) * config.ScaleF(7.5f);

        float tabW = config.ScaleF(16.0f + 16.0f + 42.0f) + textW;
        if (tabCurve > 0.0f) {
            tabW += config.ScaleF(6.0f);
        }
        tabW = (std::clamp)(tabW, config.ScaleF(100.0f), config.ScaleF(220.0f));
        tabWidths[i] = tabW;
        sumW += tabW;
    }
    totalTabsWidth = sumW;

    float maxScroll = (std::max)(0.0f, totalTabsWidth - canvasWidth);
    if (!isDraggingTabScroll && activeDocIdx >= 0 && activeDocIdx < static_cast<int>(docs.size())) {
        bool shouldAutoScroll = true;
        if (pLastScrolledActiveDocIdx) {
            if (*pLastScrolledActiveDocIdx == activeDocIdx) {
                shouldAutoScroll = false;
            } else {
                *pLastScrolledActiveDocIdx = activeDocIdx;
            }
        }
        if (shouldAutoScroll) {
            float activeStart = 0.0f;
            for (int i = 0; i < activeDocIdx; ++i) activeStart += tabWidths[i];
            float activeEnd = activeStart + tabWidths[activeDocIdx];

            if (activeStart < tabScrollOffset) {
                tabScrollOffset = activeStart;
            } else if (activeEnd > tabScrollOffset + canvasWidth) {
                tabScrollOffset = activeEnd - canvasWidth;
            }
        }
    }
    tabScrollOffset = (std::clamp)(tabScrollOffset, 0.0f, maxScroll);

    pRT->PushAxisAlignedClip(rcTabBar, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float curX = canvasLeft - tabScrollOffset;
    for (size_t i = 0; i < docs.size(); ++i) {
        auto& doc = docs[i];
        bool isActive = (static_cast<int>(i) == activeDocIdx);
        bool isFirstTab = (i == 0);
        float tabCurve = (isFirstTab && !hasLeftDrawer && tabScrollOffset <= 0.0f) ? cornerRadius : 0.0f;
        float tabW = tabWidths[i];
        std::wstring title = doc.fileName.empty() ? L"Untitled-1" : doc.fileName;

        D2D1_RECT_F rcTab = D2D1::RectF(curX, headerH, curX + tabW, headerH + tabH);
        doc.tabBounds = rcTab;

        // Draw tab surface
        if (isActive) {
            FillUpperLeftRoundedRect(pRT, theme.brushTabActiveBg.Get(), rcTab, tabCurve);
            // Top active tab highlight border (2px Accent Blue) - ONLY on focused editor group!
            if (isFocused) {
                if (tabCurve > 0.0f) {
                    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
                    pRT->GetFactory(&pFactory);
                    if (pFactory) {
                        Microsoft::WRL::ComPtr<ID2D1PathGeometry> pTopPath;
                        if (SUCCEEDED(pFactory->CreatePathGeometry(&pTopPath))) {
                            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sinkTop;
                            if (SUCCEEDED(pTopPath->Open(&sinkTop))) {
                                sinkTop->BeginFigure(D2D1::Point2F(rcTab.left + 0.5f, headerH + tabCurve), D2D1_FIGURE_BEGIN_HOLLOW);
                                D2D1_ARC_SEGMENT arcTop{};
                                arcTop.point = D2D1::Point2F(rcTab.left + tabCurve, headerH + 1.0f);
                                arcTop.size = D2D1::SizeF(tabCurve, tabCurve);
                                arcTop.rotationAngle = 0.0f;
                                arcTop.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                                arcTop.arcSize = D2D1_ARC_SIZE_SMALL;
                                sinkTop->AddArc(arcTop);
                                sinkTop->AddLine(D2D1::Point2F(rcTab.right, headerH + 1.0f));
                                sinkTop->EndFigure(D2D1_FIGURE_END_OPEN);
                                sinkTop->Close();
                                pRT->DrawGeometry(pTopPath.Get(), theme.brushBrandAccent.Get(), 2.0f);
                            }
                        }
                    }
                } else {
                    if (theme.brushBrandAccent) {
                        pRT->DrawLine(
                            D2D1::Point2F(rcTab.left, headerH + 1.0f),
                            D2D1::Point2F(rcTab.right, headerH + 1.0f),
                            theme.brushBrandAccent.Get(),
                            2.0f
                        );
                    }
                }
            }
        }

        // Right tab divider line
        if (theme.brushTabBorder) {
            pRT->DrawLine(
                D2D1::Point2F(rcTab.right - 0.5f, headerH),
                D2D1::Point2F(rcTab.right - 0.5f, headerH + tabH),
                theme.brushTabBorder.Get(),
                1.0f
            );
        }

        // Icon (18px)
        const float iconPadX = (tabCurve > 0.0f) ? config.ScaleF(14.0f) : config.ScaleF(10.0f);
        const float iconX = rcTab.left + iconPadX;
        const float iconCenterY = headerH + tabH / 2.0f;
        const float iconSz = config.ScaleF(18.0f);
        const float iconHalf = iconSz * 0.5f;
        D2D1_RECT_F iconBounds = D2D1::RectF(
            iconX,
            iconCenterY - iconHalf,
            iconX + iconSz,
            iconCenterY + iconHalf
        );

        if (doc.isSpecialView && doc.viewType == L"keybindings") {
            // Draw keyboard glyph / settings
            if (theme.fontSeti && theme.brushSetiJson) {
                wchar_t kbGlyph = 0xE055;
                pRT->DrawText(&kbGlyph, 1, theme.fontSeti.Get(), iconBounds, theme.brushSetiJson.Get());
            } else {
                m_icons.DrawFileIcon(pRT, theme, iconBounds, FileIconType::Json);
            }
        } else if (doc.isSpecialView && doc.viewType == L"graph") {
            m_icons.DrawFileNetwork(pRT, iconBounds, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(), 1.2f);
        } else if (doc.isSpecialView && doc.viewType == L"settings") {
            m_icons.DrawSettings(pRT, iconBounds, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(), 1.2f);
        } else if (doc.isSpecialView && doc.viewType == L"packages") {
            m_icons.DrawPackage(pRT, iconBounds, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(), 1.1f);
        } else if (doc.isSpecialView && doc.viewType == L"student") {
            m_icons.DrawGraduationCap(pRT, iconBounds, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(), 1.3f);
        } else if ((doc.isSpecialView && doc.viewType == L"course") || !doc.courseId.empty()) {
            ID2D1Bitmap* pPyIcon = GetLearningHubBitmap(pRT, L"python.png");
            if (pPyIcon && doc.fileName.find(L"Python") != std::wstring::npos) {
                pRT->DrawBitmap(pPyIcon, iconBounds, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                m_icons.DrawGraduationCap(pRT, iconBounds, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(), 1.3f);
            }
        } else if ((doc.isSpecialView && doc.viewType == L"notebook") || doc.filePath.ends_with(L".ipynb")) {
            m_icons.DrawTerminalNotebook(pRT, iconBounds, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(), 1.3f);
        } else if (doc.isSpecialView && doc.viewType == L"markdown_preview") {
            m_icons.DrawFileIcon(pRT, theme, iconBounds, FileIconType::Markdown);
        } else if (doc.isSpecialView && doc.viewType == L"extension_detail") {
            m_icons.DrawExtensionsGrid(pRT, iconBounds, theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextWhite.Get(), 1.3f);
        } else if (doc.filePath.ends_with(L".py") || doc.filePath.ends_with(L".pyw") || title.ends_with(L".py")) {
            ID2D1Bitmap* pPyIcon = GetLearningHubBitmap(pRT, L"python.png");
            if (pPyIcon) {
                pRT->DrawBitmap(pPyIcon, iconBounds, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                auto setiInfo = theme.GetSetiIcon(title, false, false);
                if (theme.fontSeti && setiInfo.pBrush) {
                    pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), iconBounds, setiInfo.pBrush);
                }
            }
        } else {
            auto setiInfo = theme.GetSetiIcon(title, false, false);
            if (theme.fontSeti && setiInfo.pBrush) {
                pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), iconBounds, setiInfo.pBrush);
            } else {
                auto iconType = LucideIcons::GetIconTypeForPath(title, false, false);
                m_icons.DrawFileIcon(pRT, theme, iconBounds, iconType);
            }
        }

        // Title: preview tabs use fontTabPreview (italic), pinned tabs use fontTab (regular)
        float textStartX = rcTab.left + ((tabCurve > 0.0f) ? config.ScaleF(36.0f) : config.ScaleF(32.0f));
        float textMaxW   = (std::max)(10.0f, rcTab.right - config.ScaleF(36.0f) - textStartX);
        D2D1_RECT_F rcTitle = D2D1::RectF(
            textStartX,
            headerH,
            textStartX + textMaxW,
            headerH + tabH
        );
        IDWriteTextFormat* pTextFmt = doc.isPreview ? 
            (theme.fontTabPreview ? theme.fontTabPreview.Get() : theme.fontMenuItem.Get()) :
            (theme.fontTab ? theme.fontTab.Get() : theme.fontMenuItem.Get());
        bool hasErrors = !doc.diagnostics.empty();
        ID2D1Brush* pTextBrush = hasErrors ?
            (theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushTextPrimary.Get()) :
            (isActive ? 
                (isFocused ? (theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) : theme.brushTextSecondary.Get()) :
                theme.brushTextSecondary.Get());

        if (pTextBrush && pTextFmt) {
            pRT->PushAxisAlignedClip(rcTitle, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            pRT->DrawText(title.c_str(), static_cast<UINT32>(title.length()), pTextFmt, rcTitle, pTextBrush);
            pRT->PopAxisAlignedClip();
        }

        // Close Tab Button (×)
        const float closeSize = config.ScaleF(16.0f);
        const float closeX = rcTab.right - config.ScaleF(20.0f);
        const float closeY = headerH + (tabH - closeSize) / 2.0f;
        D2D1_RECT_F rcClose = D2D1::RectF(closeX, closeY, closeX + closeSize, closeY + closeSize);
        doc.closeBounds = rcClose;

        // Red Error Indicator on Tab (Image 3)
        if (hasErrors) {
            float errDotX = closeX - config.ScaleF(doc.isDirty ? 18.0f : 9.0f);
            float errDotY = headerH + tabH / 2.0f;
            float errDotR = config.ScaleF(3.5f);
            D2D1_ELLIPSE elErr = D2D1::Ellipse(D2D1::Point2F(errDotX, errDotY), errDotR, errDotR);
            if (theme.brushIconRed) pRT->FillEllipse(elErr, theme.brushIconRed.Get());
        }

        // Dirty Circle Indicator: ONLY if doc.isDirty is true! (7px diameter)
        if (doc.isDirty) {
            float dotX = closeX - config.ScaleF(9.0f);
            float dotY = headerH + tabH / 2.0f;
            float dotRadius = config.ScaleF(3.5f);
            D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(dotX, dotY), dotRadius, dotRadius);
            pRT->FillEllipse(dot, theme.brushIconBlue ? theme.brushIconBlue.Get() : theme.brushBrandAccent.Get());
        }

        // Vector '×' mark
        const float cx = closeX + closeSize / 2.0f;
        const float cy = closeY + closeSize / 2.0f;
        const float r = config.ScaleF(3.5f);
        if (theme.brushTextSecondary) {
            pRT->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx + r, cy + r), theme.brushTextSecondary.Get(), 1.2f);
            pRT->DrawLine(D2D1::Point2F(cx + r, cy - r), D2D1::Point2F(cx - r, cy + r), theme.brushTextSecondary.Get(), 1.2f);
        }

        curX += tabW;
    }

    pRT->PopAxisAlignedClip();

    // 2. Horizontal Scrollbar for Tab Bar (User request: "since i do many tag it many now there should be a scrol bar so i could scroll throught them")
    if (totalTabsWidth > canvasWidth) {
        float trackH = config.ScaleF(5.0f);
        float hitH = config.ScaleF(14.0f);
        D2D1_RECT_F rcTrack = D2D1::RectF(canvasLeft, headerH + tabH - trackH, canvasLeft + canvasWidth, headerH + tabH);
        D2D1_RECT_F rcTrackHit = D2D1::RectF(canvasLeft, headerH + tabH - hitH, canvasLeft + canvasWidth, headerH + tabH + config.ScaleF(4.0f));
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTrack;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.6f) : D2D1::ColorF(0.85f, 0.85f, 0.85f, 0.6f),
            &brushTrack
        );
        if (brushTrack) pRT->FillRectangle(rcTrack, brushTrack.Get());

        float thumbW = (std::max)(config.ScaleF(36.0f), (canvasWidth / totalTabsWidth) * canvasWidth);
        float maxScroll = totalTabsWidth - canvasWidth;
        float ratio = (maxScroll > 0.0f) ? (tabScrollOffset / maxScroll) : 0.0f;
        float thumbX = canvasLeft + ratio * (canvasWidth - thumbW);
        D2D1_RECT_F rcThumb = D2D1::RectF(thumbX, headerH + tabH - trackH, thumbX + thumbW, headerH + tabH);
        D2D1_RECT_F rcThumbHit = D2D1::RectF(thumbX, headerH + tabH - hitH, thumbX + thumbW, headerH + tabH + config.ScaleF(4.0f));

        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(rcThumb, 2.0f, 2.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushThumb;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.48f, 0.48f, 0.52f, 0.85f) : D2D1::ColorF(0.52f, 0.52f, 0.56f, 0.85f),
            &brushThumb
        );
        if (brushThumb) pRT->FillRoundedRectangle(rrcThumb, brushThumb.Get());

        if (outTrack) *outTrack = rcTrackHit;
        if (outThumb) *outThumb = rcThumbHit;
    } else {
        if (outTrack) *outTrack = D2D1::RectF(0, 0, 0, 0);
        if (outThumb) *outThumb = D2D1::RectF(0, 0, 0, 0);
    }

    // 3. Tab Drag-and-Drop Insertion Line Indicator & Floating Tab Pill (Image 3: "Buffer.cpp")
    if (m_isDraggingTab && m_tabDropTargetGroup == groupIdx) {
        if (m_tabDropTargetIdx >= 0) {
            float dropX = canvasLeft - tabScrollOffset;
            for (int i = 0; i < m_tabDropTargetIdx && i < static_cast<int>(docs.size()); ++i) {
                dropX += tabWidths[i];
            }
            if (dropX >= canvasLeft && dropX <= canvasLeft + canvasWidth) {
                D2D1_RECT_F rcDropLine = D2D1::RectF(dropX - 1.5f, headerH + 1.0f, dropX + 1.5f, headerH + tabH - 1.0f);
                ID2D1Brush* pDropBrush = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTabBorder.Get();
                if (pDropBrush) {
                    pRT->FillRectangle(rcDropLine, pDropBrush);
                }
            }
        }
    }
}

void EditorCanvas::RenderBreadcrumbs(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     const editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                     float barY, float barHeight) {
    if (!pRT || barHeight <= 0.0f) return;
    const float canvasRight = canvasLeft + canvasWidth;
    D2D1_RECT_F rcBar = D2D1::RectF(canvasLeft, barY, canvasRight, barY + barHeight);

    // Subtle surface backdrop
    if (theme.IsDarkMode()) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.118f, 0.118f, 0.118f, 1.0f), &brushBg); // #1E1E1E
        if (brushBg) pRT->FillRectangle(rcBar, brushBg.Get());
    } else {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.97f, 0.97f, 0.97f, 1.0f), &brushBg); // #F7F7F7
        if (brushBg) pRT->FillRectangle(rcBar, brushBg.Get());
    }

    // Bottom subtle 1px divider
    pRT->DrawLine(
        D2D1::Point2F(canvasLeft, barY + barHeight - 0.5f),
        D2D1::Point2F(canvasRight, barY + barHeight - 0.5f),
        theme.brushBorderSubtle.Get(),
        0.8f
    );

    // Diagnostic error count badge on breadcrumb bar (Image 4) - centered text & vector icon
    size_t errorCount = 0;
    for (const auto& d : doc.diagnostics) {
        if (d.severity == 1) errorCount++;
    }
    if (errorCount > 0) {
        std::wstring errBadgeStr = std::to_wstring(errorCount) +
            (errorCount == 1 ? L" error" : L" errors");
        float badgeW = config.ScaleF(80.0f);
        float badgeH = config.ScaleF(18.0f);
        float badgeX = canvasRight - config.ScaleF(95.0f);
        float badgeY = barY + (barHeight - badgeH) / 2.0f;
        D2D1_RECT_F rcErrBadge = D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH);
        D2D1_ROUNDED_RECT rrcErrBadge = D2D1::RoundedRect(rcErrBadge, 4.0f, 4.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> errBadgeBg;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> errBadgeBorder;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.22f, 0.22f, 0.18f), &errBadgeBg);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.30f, 0.30f, 0.40f), &errBadgeBorder);
        if (errBadgeBg) pRT->FillRoundedRectangle(&rrcErrBadge, errBadgeBg.Get());
        if (errBadgeBorder) pRT->DrawRoundedRectangle(&rrcErrBadge, errBadgeBorder.Get(), 0.9f);

        ID2D1Brush* redBrush = theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushTextPrimary.Get();

        // Vector ⊗ symbol
        float iconR = config.ScaleF(4.5f);
        float iconCx = badgeX + config.ScaleF(9.5f);
        float iconCy = badgeY + badgeH / 2.0f;
        D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(iconCx, iconCy), iconR, iconR);
        pRT->DrawEllipse(el, redBrush, 1.2f);
        pRT->DrawLine(D2D1::Point2F(iconCx - 2.0f, iconCy - 2.0f), D2D1::Point2F(iconCx + 2.0f, iconCy + 2.0f), redBrush, 1.2f);
        pRT->DrawLine(D2D1::Point2F(iconCx + 2.0f, iconCy - 2.0f), D2D1::Point2F(iconCx - 2.0f, iconCy + 2.0f), redBrush, 1.2f);

        // Perfectly centered text
        D2D1_RECT_F rcErrText = D2D1::RectF(iconCx + config.ScaleF(6.0f), badgeY, badgeX + badgeW - config.ScaleF(2.0f), badgeY + badgeH);
        pRT->DrawText(errBadgeStr.c_str(), static_cast<UINT32>(errBadgeStr.length()),
                      theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(), rcErrText, redBrush);
    }

    // Parse segments from doc.filePath (Image 4) or course hierarchy
    std::vector<std::wstring> segments;
    if (doc.isSpecialView && doc.viewType == L"settings") {
        segments.push_back(L"Preferences");
        segments.push_back(L"Settings");
    } else if (doc.isSpecialView && doc.viewType == L"graph") {
        segments.push_back(L"File Network");
        segments.push_back(L"Graph view");
    } else if (!doc.courseId.empty() || (doc.isSpecialView && doc.viewType == L"course")) {
        segments.push_back(doc.fileName.empty() ? L"Python for Beginners" : doc.fileName);
        segments.push_back(L"Getting Started");
        segments.push_back(L"01. What is Python & Installation");
    } else if (doc.isSpecialView && doc.viewType == L"markdown_preview") {
        segments.push_back(L"Markdown");
        segments.push_back(doc.fileName.empty() ? L"Preview" : doc.fileName);
    } else if (doc.isSpecialView && doc.viewType == L"extension_detail") {
        segments.push_back(L"Extensions");
        segments.push_back(doc.fileName.empty() ? L"Extension" : doc.fileName);
    } else {
        std::wstring pathStr = doc.filePath.empty() ? doc.fileName : doc.filePath;
        for (auto& c : pathStr) {
            if (c == L'/') c = L'\\';
        }

        size_t start = 0;
        while (start < pathStr.length()) {
            size_t end = pathStr.find(L'\\', start);
            if (end == std::wstring::npos) end = pathStr.length();
            if (end > start) {
                std::wstring seg = pathStr.substr(start, end - start);
                // Skip Windows drive letter root if present (e.g. C:)
                if (!(seg.length() == 2 && seg[1] == L':')) {
                    segments.push_back(seg);
                }
            }
            start = end + 1;
        }

        // Keep up to 4 segments for a clean breadcrumb hierarchy
        if (segments.size() > 4) {
            segments.erase(segments.begin(), segments.end() - 4);
        }
        if (segments.empty()) {
            segments.push_back(doc.fileName.empty() ? L"Untitled" : doc.fileName);
        }
    }

    float curX = canvasLeft + config.ScaleF(14.0f);
    IDWriteTextFormat* pFmt = theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get();

    for (size_t s = 0; s < segments.size(); ++s) {
        const auto& seg = segments[s];
        bool isLast = (s == segments.size() - 1);

        float textW = static_cast<float>(seg.length()) * config.ScaleF(7.0f) + config.ScaleF(6.0f);
        D2D1_RECT_F rcSeg = D2D1::RectF(curX, barY + config.ScaleF(2.0f), curX + textW, barY + barHeight - config.ScaleF(2.0f));

        ID2D1Brush* segBrush = isLast
            ? theme.brushTextPrimary.Get()
            : theme.brushTextSecondary.Get();

        pRT->DrawText(seg.c_str(), static_cast<UINT32>(seg.length()), pFmt, rcSeg, segBrush);
        curX += textW;

        if (!isLast) {
            // Draw chevron separator › (Unicode \u203A)
            const wchar_t* sep = L"\u203A";
            float sepW = config.ScaleF(14.0f);
            D2D1_RECT_F rcSep = D2D1::RectF(curX, barY + config.ScaleF(2.0f), curX + sepW, barY + barHeight - config.ScaleF(2.0f));
            pRT->DrawText(sep, 1, pFmt, rcSep, theme.brushTextMuted.Get());
            curX += sepW;
        }
    }
}

void EditorCanvas::DrawVenvTag(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                               const D2D1_RECT_F& bounds, bool isHovered) {
    if (!pRT) return;

    if (isHovered) {
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(bounds, config.ScaleF(3.5f), config.ScaleF(3.5f));
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hoverBg;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.20f, 0.22f, 0.26f, 0.90f) : D2D1::ColorF(0.88f, 0.90f, 0.94f, 0.90f),
            &hoverBg
        );
        if (hoverBg) pRT->FillRoundedRectangle(rrc, hoverBg.Get());
    }

    const float bh = bounds.bottom - bounds.top;
    const float iconSize = config.ScaleF(13.0f);
    const float iconX = bounds.left + config.ScaleF(3.0f);
    const float iconY = bounds.top + (bh - iconSize) * 0.5f;

    // 1. Python Two-Tone vector logo
    float cx = iconX + iconSize * 0.5f;
    float cy = iconY + iconSize * 0.5f;
    float r = iconSize * 0.40f;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bBlue, bYellow, bWhite;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.23f, 0.49f, 0.72f), &bBlue);   // #3B7EB8
    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.84f, 0.25f), &bYellow); // #FFD740
    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), &bWhite);

    D2D1_ROUNDED_RECT rrcTop = D2D1::RoundedRect(D2D1::RectF(cx - r, cy - r, cx + r * 0.35f, cy + r * 0.35f), config.ScaleF(2.2f), config.ScaleF(2.2f));
    D2D1_ROUNDED_RECT rrcBot = D2D1::RoundedRect(D2D1::RectF(cx - r * 0.35f, cy - r * 0.35f, cx + r, cy + r), config.ScaleF(2.2f), config.ScaleF(2.2f));
    if (bBlue) pRT->FillRoundedRectangle(rrcTop, bBlue.Get());
    if (bYellow) pRT->FillRoundedRectangle(rrcBot, bYellow.Get());

    // Small eye dots
    if (bWhite) {
        float eyeR = config.ScaleF(0.9f);
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - r * 0.45f, cy - r * 0.45f), eyeR, eyeR), bWhite.Get());
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * 0.45f, cy + r * 0.45f), eyeR, eyeR), bWhite.Get());
    }

    // Subscript 'v' overlay at bottom right of icon (matching Image 2)
    float vx = iconX + iconSize - config.ScaleF(2.5f);
    float vy = iconY + iconSize - config.ScaleF(2.0f);
    float vr = config.ScaleF(2.2f);
    if (bWhite) {
        ID2D1StrokeStyle* pStroke = m_icons.GetStrokeStyle();
        pRT->DrawLine(D2D1::Point2F(vx - vr, vy - vr), D2D1::Point2F(vx, vy), bWhite.Get(), 1.2f, pStroke);
        pRT->DrawLine(D2D1::Point2F(vx, vy), D2D1::Point2F(vx + vr, vy - vr), bWhite.Get(), 1.2f, pStroke);
    }

    // 2. Text: .venv [3.14.7] (matching Image 2 exact colors & layout)
    float textX = iconX + iconSize + config.ScaleF(4.0f);
    IDWriteTextFormat* pFmt = theme.fontBadge ? theme.fontBadge.Get() : (theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get());

    if (pFmt && theme.dwriteFactory) {
        std::wstring fullStr = L".venv [3.14.7]";
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        theme.dwriteFactory->CreateTextLayout(
            fullStr.c_str(),
            static_cast<UINT32>(fullStr.length()),
            pFmt,
            bounds.right - textX,
            bh,
            &pLayout
        );
        if (pLayout) {
            pLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bVenv, bBracket, bVer;
            pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.61f, 0.86f, 0.99f) : D2D1::ColorF(0.06f, 0.39f, 0.61f), &bVenv); // #9CDCFE
            pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.34f, 0.61f, 0.84f) : D2D1::ColorF(0.20f, 0.45f, 0.70f), &bBracket); // #569CD6
            pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.81f, 0.57f, 0.47f) : D2D1::ColorF(0.70f, 0.42f, 0.30f), &bVer); // #CE9178

            DWRITE_TEXT_RANGE rVenv{ 0, 6 };     // ".venv "
            DWRITE_TEXT_RANGE rB1{ 6, 1 };       // "["
            DWRITE_TEXT_RANGE rVer{ 7, 6 };      // "3.14.7"
            DWRITE_TEXT_RANGE rB2{ 13, 1 };      // "]"

            if (bVenv) pLayout->SetDrawingEffect(bVenv.Get(), rVenv);
            if (bBracket) pLayout->SetDrawingEffect(bBracket.Get(), rB1);
            if (bVer) pLayout->SetDrawingEffect(bVer.Get(), rVer);
            if (bBracket) pLayout->SetDrawingEffect(bBracket.Get(), rB2);

            pRT->DrawTextLayout(D2D1::Point2F(textX, bounds.top + (bh - config.ScaleF(14.0f)) * 0.5f), pLayout.Get(), theme.brushTextPrimary.Get());
        }
    }
}

bool EditorCanvas::GetFoldRange(const editor::Buffer& buffer, size_t startLine, size_t& outEndLine) {
    if (startLine >= buffer.GetLineCount()) return false;
    const std::wstring& line = buffer.GetLine(startLine);

    // 1. Multiline comment /* or /** (Image 2 & 3)
    size_t commentStart = line.find(L"/*");
    if (commentStart != std::wstring::npos) {
        if (line.find(L"*/", commentStart + 2) == std::wstring::npos) {
            for (size_t l = startLine + 1; l < buffer.GetLineCount(); ++l) {
                const std::wstring& nextLine = buffer.GetLine(l);
                if (nextLine.find(L"*/") != std::wstring::npos) {
                    outEndLine = l;
                    return true;
                }
            }
        }
    }

    // 2. Open brace '{' matching block
    int braceBalance = 0;
    bool hasOpenBrace = false;
    for (wchar_t ch : line) {
        if (ch == L'{') { braceBalance++; hasOpenBrace = true; }
        else if (ch == L'}') { braceBalance--; }
    }

    if (hasOpenBrace && braceBalance > 0) {
        int curBalance = braceBalance;
        for (size_t l = startLine + 1; l < buffer.GetLineCount(); ++l) {
            const std::wstring& nextLine = buffer.GetLine(l);
            for (wchar_t ch : nextLine) {
                if (ch == L'{') curBalance++;
                else if (ch == L'}') {
                    curBalance--;
                    if (curBalance == 0) {
                        outEndLine = l;
                        return true;
                    }
                }
            }
        }
    }

    // 3. Indentation-based block folding (Python, YAML, and nested blocks)
    size_t startIndent = 0;
    while (startIndent < line.length() && (line[startIndent] == L' ' || line[startIndent] == L'\t')) {
        startIndent++;
    }
    if (startIndent < line.length()) {
        size_t lastIndented = startLine;
        for (size_t l = startLine + 1; l < buffer.GetLineCount(); ++l) {
            const std::wstring& nextLine = buffer.GetLine(l);
            size_t nextIndent = 0;
            while (nextIndent < nextLine.length() && (nextLine[nextIndent] == L' ' || nextLine[nextIndent] == L'\t')) {
                nextIndent++;
            }
            if (nextIndent == nextLine.length()) continue;
            if (nextIndent > startIndent) {
                lastIndented = l;
            } else {
                break;
            }
        }
        if (lastIndented > startLine) {
            outEndLine = lastIndented;
            return true;
        }
    }

    return false;
}

void EditorCanvas::RenderCode(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                              float topY, float clientHeight, bool isFocused,
                              const std::wstring& highlightQuery,
                              bool highlightMatchCase,
                              bool highlightWholeWord,
                              bool highlightUseRegex,
                              bool isRenaming) {
    auto& buffer = doc.buffer;
    const float gutterW = config.showLineNumbers ? config.ScaleF(50.0f) : config.ScaleF(12.0f);
    const float lineH   = config.ScaleF(std::round(config.fontSize * config.lineHeightMultiplier));

    // Auto-invalidate character width if font size or font family changed
    if (m_cachedRenderFontSize != config.fontSize || m_cachedRenderFontFamily != config.fontFamily) {
        m_charWidth = 0.0f;
        m_cachedRenderFontSize = config.fontSize;
        m_cachedRenderFontFamily = config.fontFamily;
    }

    if (m_charWidth <= 0.0f && theme.dwriteFactory && theme.fontCode) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                L"0123456789", 10, theme.fontCode.Get(), 1000.0f, 100.0f, &pLayout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                m_charWidth = tm.widthIncludingTrailingWhitespace / 10.0f;
            }
        }
    }
    const float charW   = (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.8f);
    const float scrollbarW = config.ScaleF(14.0f);
    const float minimapW = config.ScaleF(88.0f);
    const float canvasRight = canvasLeft + canvasWidth;
    const float codeAreaH = clientHeight - topY;
    const float codeRight = config.showMinimap ? (canvasRight - minimapW - config.ScaleF(6.0f)) : (canvasRight - scrollbarW);
    const float swatchSpan = config.ScaleF(14.0f);
    const float swatchSize = config.ScaleF(10.5f);

    std::wstring docExt;
    std::wstring pathToExtract = !doc.filePath.empty() ? doc.filePath : doc.fileName;
    size_t dotPos = pathToExtract.find_last_of(L".");
    if (dotPos != std::wstring::npos) {
        docExt = pathToExtract.substr(dotPos);
        for (auto& c : docExt) c = towlower(c);
    }

    m_foldChevrons.clear();
    m_visibleColorSwatches.clear();

    // 1. Gutter Background & Divider
    if (config.showLineNumbers) {
        D2D1_RECT_F rcGutter = D2D1::RectF(canvasLeft, topY, canvasLeft + gutterW, clientHeight);
        pRT->FillRectangle(rcGutter, theme.brushCanvasBg.Get());

        pRT->DrawLine(
            D2D1::Point2F(canvasLeft + gutterW - 0.5f, topY),
            D2D1::Point2F(canvasLeft + gutterW - 0.5f, clientHeight),
            theme.brushBorderSubtle.Get(),
            1.0f
        );
    }

    // Build visual lines layout (folding awareness & CodeLens virtual lines)
    auto cursor = buffer.GetCursor();

    struct VisualLine {
        size_t bufferLine;
        bool isFoldHeader;
        bool isFolded;
        size_t foldEndLine;
        bool isCodeLens = false;
        bool isPredictionVirtual = false;
        size_t predictionLineIndex = 0;
    };
    std::vector<VisualLine> visualLines;
    visualLines.reserve(buffer.GetLineCount() * 2);

    // Pre-calculate multi-line prediction ghost lines if active (Zed-style virtual space)
    std::vector<std::wstring> predGhostLines;
    if (isFocused && m_prediction.isValid && m_prediction.line == cursor.line && !IsAutocompleteVisible()) {
        const std::wstring& predGhost = m_prediction.ghostText;
        size_t pStart = 0;
        while (pStart < predGhost.length()) {
            size_t pEnd = predGhost.find(L'\n', pStart);
            if (pEnd == std::wstring::npos) {
                predGhostLines.push_back(predGhost.substr(pStart));
                break;
            }
            predGhostLines.push_back(predGhost.substr(pStart, pEnd - pStart));
            pStart = pEnd + 1;
        }
    }

    const size_t totalBufLines = buffer.GetLineCount();
    for (size_t b = 0; b < totalBufLines; ++b) {
        size_t foldEnd = 0;
        bool isFolded = (!doc.foldedLines.empty() && doc.foldedLines.count(b) > 0);
        if (isFolded) {
            GetFoldRange(buffer, b, foldEnd);
        }

        // Virtual CodeLens line directly above functions & tests (Image 1: Run | Debug)
        if (config.enableCodeLens && !doc.isDiffView && IsFunctionOrTestLine(buffer.GetLine(b))) {
            VisualLine clVl;
            clVl.bufferLine = b;
            clVl.isFoldHeader = false;
            clVl.isFolded = false;
            clVl.foldEndLine = 0;
            clVl.isCodeLens = true;
            clVl.isPredictionVirtual = false;
            clVl.predictionLineIndex = 0;
            visualLines.push_back(clVl);
        }

        VisualLine vl;
        vl.bufferLine = b;
        vl.isFoldHeader = isFolded;
        vl.isFolded = isFolded;
        vl.foldEndLine = foldEnd;
        vl.isCodeLens = false;
        vl.isPredictionVirtual = false;
        vl.predictionLineIndex = 0;
        visualLines.push_back(vl);

        // If this line has multi-line prediction continuation, insert virtual lines (Zed-style virtual space)
        if (b == cursor.line && predGhostLines.size() > 1) {
            for (size_t k = 1; k < predGhostLines.size(); ++k) {
                VisualLine pVl;
                pVl.bufferLine = b;
                pVl.isFoldHeader = false;
                pVl.isFolded = false;
                pVl.foldEndLine = 0;
                pVl.isCodeLens = false;
                pVl.isPredictionVirtual = true;
                pVl.predictionLineIndex = k;
                visualLines.push_back(pVl);
            }
        }

        if (isFolded && foldEnd > b) {
            b = foldEnd; // Skip folded child lines!
        }
    }

    const size_t visibleCount = (lineH > 0.0f) ? static_cast<size_t>(codeAreaH / lineH) : 20;
    // Scroll beyond last line (VS Code scrollBeyondLastLine feature): allows scrolling closing brackets comfortably up into view
    const size_t maxScroll = visualLines.empty() ? 0 : (visualLines.size() > 3 ? visualLines.size() - 3 : 0);
    if (doc.scrollLine > maxScroll) {
        doc.scrollLine = maxScroll;
    }

    // 2. Current Line Highlight - Rendered if focused or has search target line
    cursor = buffer.GetCursor();
    size_t visualCursorIdx = 0;
    bool foundCursorVisual = false;
    for (size_t v = 0; v < visualLines.size(); ++v) {
        if (!visualLines[v].isCodeLens && !visualLines[v].isPredictionVirtual && visualLines[v].bufferLine == cursor.line) {
            visualCursorIdx = v;
            foundCursorVisual = true;
            break;
        }
        if (!visualLines[v].isCodeLens && !visualLines[v].isPredictionVirtual && visualLines[v].isFolded && cursor.line > visualLines[v].bufferLine && cursor.line <= visualLines[v].foldEndLine) {
            visualCursorIdx = v;
            foundCursorVisual = true;
            break;
        }
    }

    if ((isFocused || doc.targetHighlightLine != static_cast<size_t>(-1)) && foundCursorVisual && visualCursorIdx >= doc.scrollLine) {
        float activeLineY = topY + config.ScaleF(6.0f) + (visualCursorIdx - doc.scrollLine) * lineH;
        if (activeLineY >= topY && activeLineY + lineH <= clientHeight) {
            D2D1_RECT_F rcActiveLine = D2D1::RectF(canvasLeft + gutterW, activeLineY, codeRight, activeLineY + lineH);
            pRT->FillRectangle(rcActiveLine, theme.brushCurrentLineBg.Get());
        }
    }

    // 3. Line Numbers, Fold Chevrons & Code Lines
    const float codeLeft = canvasLeft + gutterW + config.ScaleF(12.0f);
    const float scrollX = doc.scrollOffsetX;
    float maxLineWidth = 0.0f;

    D2D1_RECT_F clipCode = D2D1::RectF(canvasLeft + gutterW, topY, codeRight, clientHeight);
    pRT->PushAxisAlignedClip(clipCode, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Analyze rainbow bracket pairs, nesting depths, and active cursor pair with caching
    if (m_cachedBufferVersion != buffer.GetVersion() || m_cachedCursorLine != cursor.line || m_cachedCursorCol != cursor.col) {
        m_cachedBracketAnalysis = editor::BracketPairAnalyzer::Analyze(buffer, cursor.line, cursor.col);
        m_cachedBufferVersion = buffer.GetVersion();
        m_cachedCursorLine = cursor.line;
        m_cachedCursorCol = cursor.col;
    }
    const auto& bracketAnalysis = m_cachedBracketAnalysis;
    std::unordered_map<size_t, std::vector<const editor::BracketItem*>> bracketsByLine;
    for (const auto& b : bracketAnalysis.brackets) {
        if (b.line < totalBufLines) {
            bracketsByLine[b.line].push_back(&b);
        }
    }

    const editor::BracketPair* pActivePair = nullptr;
    float activeOpenLineY = 0.0f;
    float activeCloseLineY = 0.0f;
    bool hasActiveOpenY = false;
    bool hasActiveCloseY = false;

    if (isFocused && bracketAnalysis.activePairIdx >= 0 &&
        bracketAnalysis.activePairIdx < static_cast<int>(bracketAnalysis.pairs.size())) {
        pActivePair = &bracketAnalysis.pairs[bracketAnalysis.activePairIdx];
        for (size_t v = 0; v < visualLines.size(); ++v) {
            if (!visualLines[v].isCodeLens && !visualLines[v].isPredictionVirtual && visualLines[v].bufferLine == pActivePair->openLine) {
                activeOpenLineY = topY + config.ScaleF(6.0f) + (static_cast<float>(v) - static_cast<float>(doc.scrollLine)) * lineH;
                hasActiveOpenY = true;
            }
            if (!visualLines[v].isCodeLens && !visualLines[v].isPredictionVirtual && visualLines[v].bufferLine == pActivePair->closeLine) {
                activeCloseLineY = topY + config.ScaleF(6.0f) + (static_cast<float>(v) - static_cast<float>(doc.scrollLine)) * lineH;
                hasActiveCloseY = true;
            }
            if (hasActiveOpenY && hasActiveCloseY) break;
        }
    }

    // Render active bracket connector guide lines (Images 4 & 5)
    if (pActivePair && hasActiveOpenY && hasActiveCloseY && pActivePair->closeLine > pActivePair->openLine) {
        ID2D1Brush* guideBrush = theme.GetBracketBrush(pActivePair->depth);
        if (guideBrush) {
            float guideX = codeLeft - scrollX + static_cast<float>(pActivePair->guideCol) * charW + charW * 0.5f;
            float openCharX = codeLeft - scrollX + static_cast<float>(pActivePair->openCol) * charW + charW * 0.5f;
            float closeCharX = codeLeft - scrollX + static_cast<float>(pActivePair->closeCol) * charW + charW * 0.5f;

            // Start vertical line at bottom of open line if openCol > guideCol (K&R style),
            // or at center of open line if openCol == guideCol (Allman style)
            float startY = (pActivePair->openCol == pActivePair->guideCol)
                ? (activeOpenLineY + lineH * 0.5f)
                : (activeOpenLineY + lineH);
            float endY = activeCloseLineY + lineH * 0.5f;

            if (endY > startY) {
                // Vertical connector line from opening bracket line down to closing bracket line
                pRT->DrawLine(
                    D2D1::Point2F(guideX, startY),
                    D2D1::Point2F(guideX, endY),
                    guideBrush,
                    1.5f
                );
            }

            // Bottom horizontal hook to closing bracket (if indented)
            if (closeCharX > guideX) {
                pRT->DrawLine(
                    D2D1::Point2F(guideX, endY),
                    D2D1::Point2F(closeCharX, endY),
                    guideBrush,
                    1.5f
                );
            }
        }
    }

    // Render skeletal highlight frames around opening and closing brackets of the active pair (Images 4 & 5)
    if (pActivePair) {
        ID2D1Brush* frameBrush = theme.GetBracketBrush(pActivePair->depth);
        if (frameBrush) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush;
            auto* solid = static_cast<ID2D1SolidColorBrush*>(frameBrush);
            D2D1_COLOR_F c = solid->GetColor();
            pRT->CreateSolidColorBrush(D2D1::ColorF(c.r, c.g, c.b, 0.22f), &fillBrush);

            // Opening bracket skeletal frame
            if (hasActiveOpenY && (activeOpenLineY + lineH >= topY) && (activeOpenLineY <= clientHeight)) {
                float ox = codeLeft - scrollX + static_cast<float>(pActivePair->openCol) * charW;
                D2D1_RECT_F rcOpen = D2D1::RectF(
                    ox - config.ScaleF(1.0f),
                    activeOpenLineY + config.ScaleF(2.0f),
                    ox + charW + config.ScaleF(1.0f),
                    activeOpenLineY + lineH - config.ScaleF(2.0f)
                );
                D2D1_ROUNDED_RECT rrcOpen = D2D1::RoundedRect(rcOpen, config.ScaleF(2.5f), config.ScaleF(2.5f));
                if (fillBrush) pRT->FillRoundedRectangle(rrcOpen, fillBrush.Get());
                pRT->DrawRoundedRectangle(rrcOpen, frameBrush, 1.5f);
            }

            // Closing bracket skeletal frame
            if (hasActiveCloseY && (activeCloseLineY + lineH >= topY) && (activeCloseLineY <= clientHeight)) {
                float cx = codeLeft - scrollX + static_cast<float>(pActivePair->closeCol) * charW;
                D2D1_RECT_F rcClose = D2D1::RectF(
                    cx - config.ScaleF(1.0f),
                    activeCloseLineY + config.ScaleF(2.0f),
                    cx + charW + config.ScaleF(1.0f),
                    activeCloseLineY + lineH - config.ScaleF(2.0f)
                );
                D2D1_ROUNDED_RECT rrcClose = D2D1::RoundedRect(rcClose, config.ScaleF(2.5f), config.ScaleF(2.5f));
                if (fillBrush) pRT->FillRoundedRectangle(rrcClose, fillBrush.Get());
                pRT->DrawRoundedRectangle(rrcClose, frameBrush, 1.5f);
            }
        }
    }

    size_t startV = doc.scrollLine;
    size_t endV = (std::min)(visualLines.size(), startV + visibleCount + 2);
    m_activeCodeLensActions.clear();

    for (size_t v = startV; v < endV; ++v) {
        const auto& vl = visualLines[v];
        float lineY = topY + config.ScaleF(6.0f) + (v - doc.scrollLine) * lineH;
        if (lineY + lineH < topY || lineY > clientHeight) continue;

        // CodeLens Virtual Line (Image 1: Run | Debug above function)
        if (vl.isCodeLens) {
            const auto& fnLineStr = buffer.GetLine(vl.bufferLine);
            size_t leadingSpaces = 0;
            while (leadingSpaces < fnLineStr.length() && (fnLineStr[leadingSpaces] == L' ' || fnLineStr[leadingSpaces] == L'\t')) {
                leadingSpaces += (fnLineStr[leadingSpaces] == L'\t') ? config.tabSize : 1;
            }
            float clX = codeLeft - scrollX + static_cast<float>(leadingSpaces) * charW;
            if (clX < codeLeft) clX = codeLeft;

            IDWriteTextFormat* clFormat = theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontHint.Get();

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushClNormal;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushClHover;
            pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.58f, 0.60f, 0.64f, 1.0f) : D2D1::ColorF(0.42f, 0.44f, 0.48f, 1.0f), &brushClNormal);
            pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.62f, 0.96f, 1.0f) : D2D1::ColorF(0.06f, 0.45f, 0.85f, 1.0f), &brushClHover);

            // GitLens Authorship CodeLens (e.g. "John David L. Perez, 2 weeks ago • 1 author | " or "You, Uncommitted changes | ")
            if (config.enableGitLens && !doc.gitBlameMap.empty()) {
                auto itBlame = doc.gitBlameMap.find(vl.bufferLine);
                if (itBlame != doc.gitBlameMap.end()) {
                    const auto& bl = itBlame->second;
                    std::wstring authorStr = bl.isUncommitted ? L"You, Uncommitted changes" : (bl.author + L", " + bl.relativeTime + L" \x2022 1 author");
                    float authorW = static_cast<float>(authorStr.length()) * config.ScaleF(6.4f);
                    if (theme.dwriteFactory && clFormat) {
                        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLAuthor;
                        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(authorStr.c_str(), static_cast<UINT32>(authorStr.length()), clFormat, 1000.0f, lineH, &pLAuthor))) {
                            DWRITE_TEXT_METRICS tm{};
                            pLAuthor->GetMetrics(&tm);
                            authorW = tm.widthIncludingTrailingWhitespace;
                        }
                    }
                    D2D1_RECT_F rcAuthor = D2D1::RectF(clX, lineY, clX + authorW, lineY + lineH);
                    pRT->DrawText(authorStr.c_str(), static_cast<UINT32>(authorStr.length()), clFormat, rcAuthor, brushClNormal.Get());
                    clX += authorW;

                    // Separator " | "
                    const wchar_t* sepAuth = L" | ";
                    float sepAuthW = config.ScaleF(14.0f);
                    D2D1_RECT_F rcSepAuth = D2D1::RectF(clX, lineY, clX + sepAuthW, lineY + lineH);
                    pRT->DrawText(sepAuth, static_cast<UINT32>(wcslen(sepAuth)), clFormat, rcSepAuth, brushClNormal.Get());
                    clX += sepAuthW;
                }
            }

            // 1. "Run" button
            const wchar_t* runText = L"Run";
            float runW = config.ScaleF(26.0f);
            D2D1_RECT_F rcRun = D2D1::RectF(clX, lineY, clX + runW, lineY + lineH);
            int runIdx = static_cast<int>(m_activeCodeLensActions.size());
            bool isRunHovered = (runIdx == m_hoveredCodeLensIdx);

            CodeLensAction runAct;
            runAct.bufferLine = vl.bufferLine;
            runAct.isDebug = false;
            runAct.bounds = rcRun;
            m_activeCodeLensActions.push_back(runAct);

            pRT->DrawText(runText, static_cast<UINT32>(wcslen(runText)), clFormat, rcRun,
                          isRunHovered ? brushClHover.Get() : brushClNormal.Get());
            if (isRunHovered && brushClHover) {
                pRT->DrawLine(D2D1::Point2F(rcRun.left, lineY + lineH - config.ScaleF(3.0f)),
                              D2D1::Point2F(rcRun.right - config.ScaleF(4.0f), lineY + lineH - config.ScaleF(3.0f)),
                              brushClHover.Get(), 1.0f);
            }

            // Separator " | "
            clX += runW;
            const wchar_t* sepText = L" | ";
            float sepW = config.ScaleF(14.0f);
            D2D1_RECT_F rcSep = D2D1::RectF(clX, lineY, clX + sepW, lineY + lineH);
            pRT->DrawText(sepText, static_cast<UINT32>(wcslen(sepText)), clFormat, rcSep, brushClNormal.Get());
            clX += sepW;

            // 2. "Debug" button
            const wchar_t* debugText = L"Debug";
            float debugW = config.ScaleF(38.0f);
            D2D1_RECT_F rcDebug = D2D1::RectF(clX, lineY, clX + debugW, lineY + lineH);
            int debugIdx = static_cast<int>(m_activeCodeLensActions.size());
            bool isDebugHovered = (debugIdx == m_hoveredCodeLensIdx);

            CodeLensAction debugAct;
            debugAct.bufferLine = vl.bufferLine;
            debugAct.isDebug = true;
            debugAct.bounds = rcDebug;
            m_activeCodeLensActions.push_back(debugAct);

            pRT->DrawText(debugText, static_cast<UINT32>(wcslen(debugText)), clFormat, rcDebug,
                          isDebugHovered ? brushClHover.Get() : brushClNormal.Get());
            if (isDebugHovered && brushClHover) {
                pRT->DrawLine(D2D1::Point2F(rcDebug.left, lineY + lineH - config.ScaleF(3.0f)),
                              D2D1::Point2F(rcDebug.right - config.ScaleF(4.0f), lineY + lineH - config.ScaleF(3.0f)),
                              brushClHover.Get(), 1.0f);
            }

            continue; // Virtual CodeLens line has no code tokens
        }

        // Virtual prediction continuation line (Zed-style virtual space)
        if (vl.isPredictionVirtual) {
            // Subtle, sleek translucent background tint for prediction area
            D2D1_RECT_F rcPredLine = D2D1::RectF(canvasLeft + gutterW, lineY, codeRight, lineY + lineH);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPredBg;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.18f, 0.22f, 0.22f), &brushPredBg);
            if (brushPredBg) pRT->FillRectangle(rcPredLine, brushPredBg.Get());
            continue; // Virtual prediction line has no buffer tokens
        }

        // Git Diff Line Highlights (Image 5)
        if (doc.isDiffView || doc.isDeletedInGit) {
            if (doc.isDeletedInGit || doc.diffDeletedLines.count(vl.bufferLine)) {
                // Red background for deleted lines / deleted file
                D2D1_RECT_F rcDiffLine = D2D1::RectF(canvasLeft + gutterW, lineY, codeRight, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRed;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.18f, 0.20f, 0.25f), &brushDiffRed);
                pRT->FillRectangle(rcDiffLine, brushDiffRed.Get());

                D2D1_RECT_F rcGutterBar = D2D1::RectF(canvasLeft + gutterW - config.ScaleF(3.0f), lineY, canvasLeft + gutterW, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushRedBar;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.878f, 0.424f, 0.459f, 1.0f), &brushRedBar);
                pRT->FillRectangle(rcGutterBar, brushRedBar.Get());
            } else if (doc.diffAddedLines.count(vl.bufferLine)) {
                // Green background for added lines
                D2D1_RECT_F rcDiffLine = D2D1::RectF(canvasLeft + gutterW, lineY, codeRight, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreen;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.45f, 0.20f, 0.25f), &brushDiffGreen);
                pRT->FillRectangle(rcDiffLine, brushDiffGreen.Get());

                D2D1_RECT_F rcGutterBar = D2D1::RectF(canvasLeft + gutterW - config.ScaleF(3.0f), lineY, canvasLeft + gutterW, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushGreenBar;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.596f, 0.765f, 0.475f, 1.0f), &brushGreenBar);
                pRT->FillRectangle(rcGutterBar, brushGreenBar.Get());
            }
        }

        // Target Line / Search Jump Highlight Banner (Image 1)
        if (vl.bufferLine == doc.targetHighlightLine) {
            D2D1_RECT_F rcTargetLine = D2D1::RectF(canvasLeft + gutterW, lineY, codeRight, lineY + lineH);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTargetBg;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTargetBorder;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.28f), &brushTargetBg);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.85f), &brushTargetBorder);
            if (brushTargetBg) pRT->FillRectangle(rcTargetLine, brushTargetBg.Get());
            if (brushTargetBorder) {
                pRT->DrawLine(D2D1::Point2F(rcTargetLine.left, rcTargetLine.top), D2D1::Point2F(rcTargetLine.right, rcTargetLine.top), brushTargetBorder.Get(), 1.2f);
                pRT->DrawLine(D2D1::Point2F(rcTargetLine.left, rcTargetLine.bottom), D2D1::Point2F(rcTargetLine.right, rcTargetLine.bottom), brushTargetBorder.Get(), 1.2f);
            }

            // High-contrast keyword box on target line
            if (doc.targetHighlightLen > 0) {
                float hx = codeLeft + doc.targetHighlightCol * charW;
                float hw = doc.targetHighlightLen * charW;
                D2D1_RECT_F rcKeyword = D2D1::RectF(hx - 2.0f, lineY + 1.0f, hx + hw + 2.0f, lineY + lineH - 1.0f);
                D2D1_ROUNDED_RECT rrcKey = D2D1::RoundedRect(rcKeyword, 3.0f, 3.0f);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> keyFill;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> keyBorder;
                pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.82f, 0.0f, 0.50f), &keyFill);
                pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.82f, 0.0f, 0.95f), &keyBorder);
                if (keyFill) pRT->FillRoundedRectangle(&rrcKey, keyFill.Get());
                if (keyBorder) pRT->DrawRoundedRectangle(&rrcKey, keyBorder.Get(), 1.5f);
            }
        }

        // Code Content
        const auto& lineStr = buffer.GetLine(vl.bufferLine);
        float curCodeEndX = codeLeft;

        /**
         * @brief Highlight all occurrences of the active search query on this line in amber.
         * Rendered before syntax tokens so code text remains crisp and readable on top.
         */
        if (!highlightQuery.empty() && !lineStr.empty()) {
            std::vector<std::pair<size_t, size_t>> matches; // (col, length)
            if (highlightUseRegex) {
                try {
                    std::wregex::flag_type flags = std::regex_constants::ECMAScript;
                    if (!highlightMatchCase) flags |= std::regex_constants::icase;
                    std::wregex re(highlightQuery, flags);
                    auto words_begin = std::wsregex_iterator(lineStr.begin(), lineStr.end(), re);
                    auto words_end = std::wsregex_iterator();
                    for (std::wsregex_iterator it = words_begin; it != words_end; ++it) {
                        std::wsmatch sm = *it;
                        size_t matchPos = static_cast<size_t>(sm.position());
                        size_t matchLen = static_cast<size_t>(sm.length());
                        if (matchLen > 0) {
                            if (highlightWholeWord) {
                                bool leftOk = (matchPos == 0) || !iswalnum(lineStr[matchPos - 1]);
                                bool rightOk = (matchPos + matchLen >= lineStr.length()) || !iswalnum(lineStr[matchPos + matchLen]);
                                if (!leftOk || !rightOk) continue;
                            }
                            matches.push_back({matchPos, matchLen});
                        }
                    }
                } catch (...) {}
            } else {
                std::wstring qLower = highlightQuery;
                std::wstring lLower = lineStr;
                if (!highlightMatchCase) {
                    for (auto& ch : qLower) ch = towlower(ch);
                    for (auto& ch : lLower) ch = towlower(ch);
                }
                const std::wstring& target = highlightMatchCase ? lineStr : lLower;
                const std::wstring& pattern = highlightMatchCase ? highlightQuery : qLower;
                size_t pos = 0;
                while ((pos = target.find(pattern, pos)) != std::wstring::npos) {
                    size_t matchLen = pattern.length();
                    bool keep = true;
                    if (highlightWholeWord) {
                        bool leftOk = (pos == 0) || !iswalnum(lineStr[pos - 1]);
                        bool rightOk = (pos + matchLen >= lineStr.length()) || !iswalnum(lineStr[pos + matchLen]);
                        if (!leftOk || !rightOk) keep = false;
                    }
                    if (keep) {
                        matches.push_back({pos, matchLen});
                    }
                    pos += (std::max)(size_t(1), matchLen);
                }
            }

            if (!matches.empty()) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.58f, 0.12f, 0.38f), &fillBrush);
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.58f, 0.12f, 0.85f), &borderBrush);

                for (const auto& m : matches) {
                    float hx = codeLeft + m.first * charW;
                    float hw = m.second * charW;
                    D2D1_RECT_F rcMatch = D2D1::RectF(hx - 1.0f, lineY + 1.0f, hx + hw + 1.0f, lineY + lineH - 1.0f);
                    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcMatch, 2.0f, 2.0f);
                    if (fillBrush) pRT->FillRoundedRectangle(&rrc, fillBrush.Get());
                    if (borderBrush) pRT->DrawRoundedRectangle(&rrc, borderBrush.Get(), 1.0f);
                }
            }
        }

        // Discover in-context color tokens on line to compute visual column layout
        auto lineColors = ScanLineColors(lineStr);

        const float swatchSpan = config.ScaleF(14.0f);
        const float swatchSize = config.ScaleF(10.5f);

        auto getColVisualX = [&](size_t col) -> float {
            return codeLeft - scrollX + GetColumnVisualOffset(lineColors, col, charW, swatchSpan);
        };

        // Active Text Selection Background on Code Line (Images 3 & 4)
        if (doc.HasSelection()) {
            size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
            doc.GetSelectionRange(sLine, sCol, eLine, eCol);
            if (vl.bufferLine >= sLine && vl.bufferLine <= eLine) {
                float selX1 = codeLeft - scrollX;
                float selX2 = codeLeft - scrollX;
                if (vl.bufferLine == sLine && vl.bufferLine == eLine) {
                    selX1 = getColVisualX(sCol);
                    selX2 = getColVisualX(eCol);
                } else if (vl.bufferLine == sLine) {
                    selX1 = getColVisualX(sCol);
                    selX2 = getColVisualX(lineStr.length()) + config.ScaleF(8.0f);
                } else if (vl.bufferLine == eLine) {
                    selX1 = codeLeft - scrollX;
                    selX2 = getColVisualX(eCol);
                } else {
                    selX1 = codeLeft - scrollX;
                    selX2 = (std::max)(getColVisualX(lineStr.length()) + config.ScaleF(8.0f), codeLeft - scrollX + config.ScaleF(24.0f));
                }

                if (selX2 > selX1) {
                    D2D1_RECT_F rcSel = D2D1::RectF(selX1, lineY + 0.5f, selX2, lineY + lineH - 0.5f);
                    D2D1_ROUNDED_RECT rrcSel = D2D1::RoundedRect(rcSel, 4.0f, 4.0f);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.38f, 0.68f, 0.48f), &selBrush);
                    if (selBrush) pRT->FillRoundedRectangle(&rrcSel, selBrush.Get());
                }
            }
        }

        // Secondary Text Selections Background (Ctrl+D multi-cursor)
        for (const auto& sec : doc.secondarySelections) {
            size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
            sec.GetRange(sLine, sCol, eLine, eCol);
            if (vl.bufferLine >= sLine && vl.bufferLine <= eLine) {
                float selX1 = codeLeft - scrollX;
                float selX2 = codeLeft - scrollX;
                if (vl.bufferLine == sLine && vl.bufferLine == eLine) {
                    selX1 = getColVisualX(sCol);
                    selX2 = getColVisualX(eCol);
                } else if (vl.bufferLine == sLine) {
                    selX1 = getColVisualX(sCol);
                    selX2 = getColVisualX(lineStr.length()) + config.ScaleF(8.0f);
                } else if (vl.bufferLine == eLine) {
                    selX1 = codeLeft - scrollX;
                    selX2 = getColVisualX(eCol);
                } else {
                    selX1 = codeLeft - scrollX;
                    selX2 = (std::max)(getColVisualX(lineStr.length()) + config.ScaleF(8.0f), codeLeft - scrollX + config.ScaleF(24.0f));
                }

                if (selX2 > selX1) {
                    D2D1_RECT_F rcSel = D2D1::RectF(selX1, lineY + 0.5f, selX2, lineY + lineH - 0.5f);
                    D2D1_ROUNDED_RECT rrcSel = D2D1::RoundedRect(rcSel, 4.0f, 4.0f);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.38f, 0.68f, 0.48f), &selBrush);
                    if (selBrush) pRT->FillRoundedRectangle(&rrcSel, selBrush.Get());
                }
            }
        }

        // Matching Word Occurrences Highlight across lines (Image 4)
        if (!doc.selectedWord.empty() && doc.selectedWord.length() > 1 && !lineStr.empty()) {
            size_t wpos = 0;
            while ((wpos = lineStr.find(doc.selectedWord, wpos)) != std::wstring::npos) {
                bool isCurrentSelection = false;
                if (doc.HasSelection()) {
                    size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                    doc.GetSelectionRange(sLine, sCol, eLine, eCol);
                    if (vl.bufferLine == sLine && vl.bufferLine == eLine && wpos == sCol) {
                        isCurrentSelection = true;
                    }
                }
                if (!isCurrentSelection) {
                    float wx = getColVisualX(wpos);
                    float ww = getColVisualX(wpos + doc.selectedWord.length()) - wx;
                    D2D1_RECT_F rcWord = D2D1::RectF(wx - 1.0f, lineY + 1.0f, wx + ww + 1.0f, lineY + lineH - 1.0f);
                    D2D1_ROUNDED_RECT rrcWord = D2D1::RoundedRect(rcWord, 2.0f, 2.0f);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> wordFill;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> wordBorder;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.48f, 0.75f, 0.22f), &wordFill);
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.65f, 0.95f, 0.65f), &wordBorder);
                    if (wordFill) pRT->FillRoundedRectangle(&rrcWord, wordFill.Get());
                    if (wordBorder) pRT->DrawRoundedRectangle(&rrcWord, wordBorder.Get(), 1.0f);
                }
                wpos += doc.selectedWord.length();
            }
        }

        if (!lineStr.empty()) {
            // Render whitespace indicators based on configured renderWhitespaceMode (none, selection, boundary, trailing, all)
            std::wstring wsMode = config.renderWhitespaceMode;
            if (wsMode.empty()) wsMode = config.renderWhitespace ? L"all" : L"none";
            if (!config.renderWhitespace && wsMode == L"all") wsMode = L"none";

            if (wsMode != L"none") {
                size_t firstNonWs = lineStr.find_first_not_of(L" \t");
                size_t lastNonWs  = lineStr.find_last_not_of(L" \t");

                bool hasSel = doc.HasSelection();
                size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                if (hasSel) doc.GetSelectionRange(sLine, sCol, eLine, eCol);

                for (size_t col = 0; col < lineStr.length(); ++col) {
                    wchar_t ch = lineStr[col];
                    if (ch != L' ' && ch != L'\t') continue;

                    bool shouldDraw = false;
                    if (wsMode == L"all") {
                        shouldDraw = true;
                    } else if (wsMode == L"trailing") {
                        shouldDraw = (lastNonWs == std::wstring::npos || col > lastNonWs);
                    } else if (wsMode == L"boundary") {
                        if (firstNonWs == std::wstring::npos || col < firstNonWs) {
                            shouldDraw = true; // Leading indentation
                        } else if (col > lastNonWs) {
                            shouldDraw = true; // Trailing whitespace
                        } else if ((col > 0 && lineStr[col - 1] == L' ') ||
                                   (col + 1 < lineStr.length() && lineStr[col + 1] == L' ')) {
                            shouldDraw = true; // 2 or more consecutive spaces
                        }
                    } else if (wsMode == L"selection") {
                        if (hasSel && vl.bufferLine >= sLine && vl.bufferLine <= eLine) {
                            if (sLine == eLine) {
                                shouldDraw = (col >= sCol && col < eCol);
                            } else if (vl.bufferLine == sLine) {
                                shouldDraw = (col >= sCol);
                            } else if (vl.bufferLine == eLine) {
                                shouldDraw = (col < eCol);
                            } else {
                                shouldDraw = true;
                            }
                        }
                    }

                    if (shouldDraw) {
                        float dotX = getColVisualX(col) + charW * 0.5f;
                        float dotY = lineY + lineH * 0.5f;
                        if (ch == L' ') {
                            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), 1.2f, 1.2f), theme.brushTextMuted.Get());
                        } else if (ch == L'\t') {
                            pRT->DrawLine(D2D1::Point2F(dotX - 2.5f, dotY), D2D1::Point2F(dotX + 2.5f, dotY), theme.brushTextMuted.Get(), 1.0f);
                        }
                    }
                }
            }

            auto tokens = buffer.GetTokensForLine(vl.bufferLine, docExt);
            if (theme.dwriteFactory && theme.fontCode) {
                float availW = (std::max)(1200.0f, codeRight - (codeLeft - scrollX) + 1000.0f);
                if (lineColors.empty()) {
                    // Unified DirectWrite Text Layout for contiguous characters, flawless spacing, ligatures, and zero gaps
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> pLineLayout;
                    if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                            lineStr.c_str(),
                            static_cast<UINT32>(lineStr.length()),
                            theme.fontCode.Get(),
                            availW,
                            lineH,
                            &pLineLayout))) {
                        pLineLayout->SetIncrementalTabStop(charW * static_cast<float>(config.tabSize > 0 ? config.tabSize : 4));
                        for (const auto& tok : tokens) {
                            if (tok.startCol + tok.length > lineStr.length()) continue;

                            ID2D1Brush* brush = theme.brushTextPrimary.Get();
                            bool isBracketToken = false;
                            if (tok.type == editor::TokenType::Operator && tok.length == 1) {
                                auto it = bracketsByLine.find(vl.bufferLine);
                                if (it != bracketsByLine.end()) {
                                    for (const auto* b : it->second) {
                                        if (b->col == tok.startCol) {
                                            brush = theme.GetBracketBrush(b->depth);
                                            isBracketToken = true;
                                            break;
                                        }
                                    }
                                }
                            }

                            if (!isBracketToken) {
                                switch (tok.type) {
                                    case editor::TokenType::Keyword:   brush = theme.brushSyntaxKeyword.Get(); break;
                                    case editor::TokenType::Type:      brush = theme.brushSyntaxType.Get(); break;
                                    case editor::TokenType::String:    brush = theme.brushSyntaxString.Get(); break;
                                    case editor::TokenType::Comment:   brush = theme.brushSyntaxComment.Get(); break;
                                    case editor::TokenType::Number:    brush = theme.brushSyntaxNumber.Get(); break;
                                    case editor::TokenType::Operator:  brush = theme.brushSyntaxOperator.Get(); break;
                                    case editor::TokenType::Tag:       brush = theme.brushSyntaxTag.Get(); break;
                                    case editor::TokenType::Attribute: brush = theme.brushSyntaxAttribute.Get(); break;
                                    default: brush = theme.brushTextPrimary.Get(); break;
                                }
                            }

                            DWRITE_TEXT_RANGE range = { static_cast<UINT32>(tok.startCol), static_cast<UINT32>(tok.length) };
                            pLineLayout->SetDrawingEffect(brush, range);
                        }

                        pRT->DrawTextLayout(D2D1::Point2F(codeLeft - scrollX, lineY), pLineLayout.Get(), theme.brushTextPrimary.Get());
                        curCodeEndX = getColVisualX(lineStr.length());
                    }
                } else {
                    // Divide line into segments between swatches so characters are virtually shifted and never layered or overlapped
                    std::vector<std::pair<size_t, size_t>> segments;
                    size_t curSegStart = 0;
                    for (const auto& cs : lineColors) {
                        if (cs.col > curSegStart) {
                            segments.push_back({ curSegStart, cs.col });
                        }
                        curSegStart = cs.col;
                    }
                    if (curSegStart < lineStr.length()) {
                        segments.push_back({ curSegStart, lineStr.length() });
                    }

                    for (const auto& seg : segments) {
                        size_t segStart = seg.first;
                        size_t segEnd = seg.second;
                        if (segEnd <= segStart) continue;

                        std::wstring segText = lineStr.substr(segStart, segEnd - segStart);
                        float segVisualX = getColVisualX(segStart);

                        Microsoft::WRL::ComPtr<IDWriteTextLayout> pSegLayout;
                        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                                segText.c_str(),
                                static_cast<UINT32>(segText.length()),
                                theme.fontCode.Get(),
                                availW,
                                lineH,
                                &pSegLayout))) {
                            pSegLayout->SetIncrementalTabStop(charW * static_cast<float>(config.tabSize > 0 ? config.tabSize : 4));

                            for (const auto& tok : tokens) {
                                size_t tokEnd = tok.startCol + tok.length;
                                if (tok.startCol >= segEnd || tokEnd <= segStart) continue;
                                size_t oStart = (std::max)(tok.startCol, segStart);
                                size_t oEnd = (std::min)(tokEnd, segEnd);
                                if (oEnd <= oStart) continue;

                                ID2D1Brush* brush = theme.brushTextPrimary.Get();
                                bool isBracketToken = false;
                                if (tok.type == editor::TokenType::Operator && tok.length == 1) {
                                    auto it = bracketsByLine.find(vl.bufferLine);
                                    if (it != bracketsByLine.end()) {
                                        for (const auto* b : it->second) {
                                            if (b->col == tok.startCol) {
                                                brush = theme.GetBracketBrush(b->depth);
                                                isBracketToken = true;
                                                break;
                                            }
                                        }
                                    }
                                }

                                if (!isBracketToken) {
                                    switch (tok.type) {
                                        case editor::TokenType::Keyword:   brush = theme.brushSyntaxKeyword.Get(); break;
                                        case editor::TokenType::Type:      brush = theme.brushSyntaxType.Get(); break;
                                        case editor::TokenType::String:    brush = theme.brushSyntaxString.Get(); break;
                                        case editor::TokenType::Comment:   brush = theme.brushSyntaxComment.Get(); break;
                                        case editor::TokenType::Number:    brush = theme.brushSyntaxNumber.Get(); break;
                                        case editor::TokenType::Operator:  brush = theme.brushSyntaxOperator.Get(); break;
                                        case editor::TokenType::Tag:       brush = theme.brushSyntaxTag.Get(); break;
                                        case editor::TokenType::Attribute: brush = theme.brushSyntaxAttribute.Get(); break;
                                        default: brush = theme.brushTextPrimary.Get(); break;
                                    }
                                }

                                DWRITE_TEXT_RANGE range = { static_cast<UINT32>(oStart - segStart), static_cast<UINT32>(oEnd - oStart) };
                                pSegLayout->SetDrawingEffect(brush, range);
                            }

                            pRT->DrawTextLayout(D2D1::Point2F(segVisualX, lineY), pSegLayout.Get(), theme.brushTextPrimary.Get());
                        }
                    }
                    curCodeEndX = getColVisualX(lineStr.length());
                }
            } else if (tokens.empty()) {
                D2D1_RECT_F rcLine = D2D1::RectF(codeLeft - scrollX, lineY, codeRight, lineY + lineH);
                pRT->DrawText(
                    lineStr.c_str(),
                    static_cast<UINT32>(lineStr.length()),
                    theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                    rcLine,
                    theme.brushTextPrimary.Get()
                );
                curCodeEndX = getColVisualX(lineStr.length());
            } else {
                for (const auto& tok : tokens) {
                    if (tok.startCol + tok.length > lineStr.length()) continue;

                    float tokenX = getColVisualX(tok.startCol);
                    float tokenW = getColVisualX(tok.startCol + tok.length) - tokenX;
                    D2D1_RECT_F rcTok = D2D1::RectF(tokenX, lineY, tokenX + tokenW + config.ScaleF(4.0f), lineY + lineH);

                    ID2D1Brush* brush = theme.brushTextPrimary.Get();
                    bool isBracketToken = false;
                    if (tok.type == editor::TokenType::Operator && tok.length == 1) {
                        auto it = bracketsByLine.find(vl.bufferLine);
                        if (it != bracketsByLine.end()) {
                            for (const auto* b : it->second) {
                                if (b->col == tok.startCol) {
                                    brush = theme.GetBracketBrush(b->depth);
                                    isBracketToken = true;
                                    break;
                                }
                            }
                        }
                    }

                    if (!isBracketToken) {
                        switch (tok.type) {
                            case editor::TokenType::Keyword:   brush = theme.brushSyntaxKeyword.Get(); break;
                            case editor::TokenType::Type:      brush = theme.brushSyntaxType.Get(); break;
                            case editor::TokenType::String:    brush = theme.brushSyntaxString.Get(); break;
                            case editor::TokenType::Comment:   brush = theme.brushSyntaxComment.Get(); break;
                            case editor::TokenType::Number:    brush = theme.brushSyntaxNumber.Get(); break;
                            case editor::TokenType::Operator:  brush = theme.brushSyntaxOperator.Get(); break;
                            case editor::TokenType::Tag:       brush = theme.brushSyntaxTag.Get(); break;
                            case editor::TokenType::Attribute: brush = theme.brushSyntaxAttribute.Get(); break;
                            default: brush = theme.brushTextPrimary.Get(); break;
                        }
                    }

                    pRT->DrawText(
                        lineStr.c_str() + tok.startCol,
                        static_cast<UINT32>(tok.length),
                        theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                        rcTok,
                        brush
                    );
                    curCodeEndX = (std::max)(curCodeEndX, tokenX + tokenW);
                }
            }
        }

        // Render in-context Color Preview Swatches inside dedicated gap (before color code, never replacing characters)
        for (size_t sIdx = 0; sIdx < lineColors.size(); ++sIdx) {
            const auto& cs = lineColors[sIdx];
            float swatchX = codeLeft - scrollX + static_cast<float>(cs.col) * charW + static_cast<float>(sIdx) * swatchSpan + (swatchSpan - swatchSize) / 2.0f;
            float swatchY = lineY + (lineH - swatchSize) / 2.0f;
            if (swatchX >= codeLeft - swatchSize && swatchX + swatchSize < codeRight) {
                D2D1_RECT_F rcSwatch = D2D1::RectF(swatchX, swatchY, swatchX + swatchSize, swatchY + swatchSize);
                D2D1_ROUNDED_RECT rrcSwatch = D2D1::RoundedRect(rcSwatch, 1.5f, 1.5f);

                if (cs.hasAlpha && cs.color.a < 0.999f) {
                    float half = swatchSize / 2.0f;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bWhite;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bLightGray;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &bWhite);
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.82f, 0.82f, 0.82f, 1.0f), &bLightGray);
                    if (bWhite && bLightGray) {
                        pRT->FillRectangle(D2D1::RectF(swatchX, swatchY, swatchX + half, swatchY + half), bWhite.Get());
                        pRT->FillRectangle(D2D1::RectF(swatchX + half, swatchY + half, swatchX + swatchSize, swatchY + swatchSize), bWhite.Get());
                        pRT->FillRectangle(D2D1::RectF(swatchX + half, swatchY, swatchX + swatchSize, swatchY + half), bLightGray.Get());
                        pRT->FillRectangle(D2D1::RectF(swatchX, swatchY + half, swatchX + half, swatchY + swatchSize), bLightGray.Get());
                    }
                }

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bCol;
                pRT->CreateSolidColorBrush(cs.color, &bCol);
                if (bCol) pRT->FillRoundedRectangle(rrcSwatch, bCol.Get());
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bBorder;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.5f), &bBorder);
                if (bBorder) pRT->DrawRoundedRectangle(rrcSwatch, bBorder.Get(), 1.0f);

                m_visibleColorSwatches.push_back({ rcSwatch, vl.bufferLine, cs.col, cs.len, cs.colorStr });
            }
        }

        // Track max visual width for horizontal scrolling
        float lineVisualW = getColVisualX(lineStr.length()) - (codeLeft - scrollX);
        maxLineWidth = (std::max)(maxLineWidth, lineVisualW);

        // If line is folded, draw the "..." folded indicator pill (Image 3)
        if (vl.isFolded) {
            float pillX = curCodeEndX + config.ScaleF(8.0f);
            float pillW = config.ScaleF(24.0f);
            float pillH = config.ScaleF(16.0f);
            float pillY = lineY + (lineH - pillH) / 2.0f;
            D2D1_RECT_F rcPill = D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH);
            D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(rcPill, 3.0f, 3.0f);

            pRT->FillRoundedRectangle(rrcPill, theme.brushItemHover.Get());
            pRT->DrawRoundedRectangle(rrcPill, theme.brushBorderSubtle.Get(), 0.8f);

            pRT->DrawText(L"...", 3, theme.fontMenuShortcut.Get(), rcPill, theme.brushTextMuted.Get());
        }

        // =====================================================================
        // Inlay Hints & Virtual Type Badges (Images 1, 2, 3, 5)
        // Renders non-editable virtual type/parameter hints without modifying buffer text.
        // =====================================================================
        bool renderedHint = false;
        for (const auto& hint : doc.inlayHints) {
            if (hint.line == vl.bufferLine) {
                float hintX = (hint.col > 0) ? getColVisualX(hint.col) : (curCodeEndX + config.ScaleF(6.0f));
                if (hintX + config.ScaleF(20.0f) < codeRight) {
                    if (hint.isGhostText) {
                        D2D1_RECT_F rcGhost = D2D1::RectF(hintX, lineY, codeRight, lineY + lineH);
                        pRT->DrawText(hint.label.c_str(), static_cast<UINT32>(hint.label.length()),
                                      theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                                      rcGhost, theme.brushTextMuted.Get());
                    } else {
                        float badgeW = static_cast<float>(hint.label.length()) * charW * 0.88f + config.ScaleF(8.0f);
                        float badgeH = lineH - config.ScaleF(5.0f);
                        float badgeY = lineY + config.ScaleF(2.5f);
                        D2D1_RECT_F rcBadge = D2D1::RectF(hintX, badgeY, (std::min)(hintX + badgeW, codeRight), badgeY + badgeH);
                        D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(rcBadge, 3.0f, 3.0f);

                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBgBrush;
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBorderBrush;
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.26f, 0.32f, 0.45f) : D2D1::ColorF(0.88f, 0.90f, 0.94f, 0.65f), &badgeBgBrush);
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.40f, 0.44f, 0.52f, 0.55f) : D2D1::ColorF(0.70f, 0.74f, 0.80f, 0.75f), &badgeBorderBrush);
                        if (badgeBgBrush) pRT->FillRoundedRectangle(rrcBadge, badgeBgBrush.Get());
                        if (badgeBorderBrush) pRT->DrawRoundedRectangle(rrcBadge, badgeBorderBrush.Get(), 0.8f);

                        D2D1_RECT_F rcBadgeText = D2D1::RectF(hintX + config.ScaleF(4.0f), badgeY + config.ScaleF(1.0f), rcBadge.right - config.ScaleF(4.0f), badgeY + badgeH);
                        pRT->DrawText(hint.label.c_str(), static_cast<UINT32>(hint.label.length()),
                                      theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                                      rcBadgeText, theme.brushTextSecondary.Get());
                    }
                    renderedHint = true;
                }
            }
        }

        // Render Syntax Error Squiggly Underlines (Image 4)
        for (const auto& diag : doc.diagnostics) {
            if (diag.line == vl.bufferLine) {
                float errX1 = getColVisualX(diag.col);
                size_t errLen = (diag.length > 0) ? diag.length : (std::max)(1ULL, lineStr.length() > diag.col ? lineStr.length() - diag.col : 1);
                float errX2 = (std::max)(getColVisualX(diag.col + errLen), errX1 + config.ScaleF(14.0f));
                if (errX1 < codeRight) {
                    errX2 = (std::min)(errX2, codeRight);
                    float waveY = lineY + lineH - config.ScaleF(2.0f);
                    float waveH = config.ScaleF(2.0f);
                    float step = config.ScaleF(2.5f);
                    ID2D1Brush* pErrBrush = (diag.severity == 1) ?
                        (theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushCloseHoverBg.Get()) :
                        (theme.brushIconOrange ? theme.brushIconOrange.Get() : theme.brushSyntaxNumber.Get());
                    for (float wx = errX1; wx < errX2; wx += step * 2.0f) {
                        pRT->DrawLine(D2D1::Point2F(wx, waveY), D2D1::Point2F(wx + step, waveY - waveH), pErrBrush, 1.3f);
                        pRT->DrawLine(D2D1::Point2F(wx + step, waveY - waveH), D2D1::Point2F((std::min)(wx + step * 2.0f, errX2), waveY), pErrBrush, 1.3f);
                    }
                }
            }
        }
    }

    // 4. Blinking Caret (Image 5: High-contrast in both Dark and Light modes!) - Only in focused group
    if (foundCursorVisual && visualCursorIdx >= doc.scrollLine) {
        float caretX = codeLeft - scrollX + cursor.col * charW;
        if (cursor.line < buffer.GetLineCount()) {
            const auto& curLineStr = buffer.GetLine(cursor.line);
            auto curLineColors = ScanLineColors(curLineStr);
            caretX = codeLeft - scrollX + GetColumnVisualOffset(curLineStr, curLineColors, cursor.col, charW, swatchSpan, config.tabSize);
        }
        float caretY = topY + config.ScaleF(6.0f) + (visualCursorIdx - doc.scrollLine) * lineH;
        if (isFocused) {
            m_lastCaretX = caretX;
            m_lastCaretY = caretY;
        }
        if (isFocused && !isRenaming && m_caretVisible) {
            if (caretY >= topY && caretY + lineH <= clientHeight) {
                // In Light Mode: #1E1E1E Deep Black/Charcoal. In Dark Mode: #FFFFFF Pure White.
                ID2D1Brush* caretBrush = theme.IsDarkMode()
                    ? theme.brushTextWhite.Get()
                    : (theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushHeaderIcon.Get());

                const std::wstring& style = config.cursorStyle;
                if (style == L"block") {
                    // Full character block cursor with semi-transparent fill and crisp border
                    D2D1_RECT_F rcBlock = D2D1::RectF(caretX, caretY + config.ScaleF(1.0f), caretX + charW, caretY + lineH - config.ScaleF(1.0f));
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> blockFill;
                    pRT->CreateSolidColorBrush(
                        theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.65f) : D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.65f),
                        &blockFill
                    );
                    pRT->FillRectangle(rcBlock, blockFill ? blockFill.Get() : caretBrush);
                    pRT->DrawRectangle(rcBlock, caretBrush, 1.0f);
                } else if (style == L"underline") {
                    // Horizontal underline cursor at bottom of line
                    float underY = caretY + lineH - config.ScaleF(2.5f);
                    pRT->DrawLine(
                        D2D1::Point2F(caretX, underY),
                        D2D1::Point2F(caretX + charW, underY),
                        caretBrush,
                        2.5f
                    );
                } else {
                    // Default "line": crisp vertical line
                    pRT->DrawLine(
                        D2D1::Point2F(caretX, caretY + config.ScaleF(2.0f)),
                        D2D1::Point2F(caretX, caretY + lineH - config.ScaleF(2.0f)),
                        caretBrush,
                        2.0f // Crisp 2.0px stroke
                    );
                }
            }
        }

        // 4-sec. Secondary Carets (Multi-Cursor Ctrl+D)
        if (isFocused && !isRenaming && m_caretVisible && !doc.secondarySelections.empty()) {
            ID2D1Brush* secCaretBrush = theme.IsDarkMode()
                ? theme.brushTextWhite.Get()
                : (theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushHeaderIcon.Get());

            for (const auto& sec : doc.secondarySelections) {
                size_t secVisualIdx = 0;
                bool foundSec = false;
                for (size_t vi = 0; vi < visualLines.size(); ++vi) {
                    if (!visualLines[vi].isCodeLens && !visualLines[vi].isPredictionVirtual && visualLines[vi].bufferLine == sec.cursor.line) {
                        secVisualIdx = vi;
                        foundSec = true;
                        break;
                    }
                }
                if (foundSec && secVisualIdx >= doc.scrollLine) {
                    float secCaretX = codeLeft - scrollX + sec.cursor.col * charW;
                    if (sec.cursor.line < buffer.GetLineCount()) {
                        const auto& sLineStr = buffer.GetLine(sec.cursor.line);
                        auto sColors = ScanLineColors(sLineStr);
                        secCaretX = codeLeft - scrollX + GetColumnVisualOffset(sLineStr, sColors, sec.cursor.col, charW, swatchSpan, config.tabSize);
                    }
                    float secCaretY = topY + config.ScaleF(6.0f) + (secVisualIdx - doc.scrollLine) * lineH;
                    if (secCaretY >= topY && secCaretY + lineH <= clientHeight) {
                        pRT->DrawLine(
                            D2D1::Point2F(secCaretX, secCaretY + config.ScaleF(2.0f)),
                            D2D1::Point2F(secCaretX, secCaretY + lineH - config.ScaleF(2.0f)),
                            secCaretBrush,
                            2.0f
                        );
                    }
                }
            }
        }

        // 4b. Inline Ghost Text with "Tab to complete" (Image 5)
        std::wstring displayGhost = m_ghostText;

        // ONLY allow inline ghost text if cursor is at the end of the line (no non-whitespace chars follow).
        // This strictly prevents ghost text from double-rendering over existing code (e.g. width=ddevvicee--wwiiddtthh).
        const auto& lineText = buffer.GetLine(cursor.line);
        bool isAtEndOfLine = true;
        for (size_t col = cursor.col; col < lineText.length(); ++col) {
            if (lineText[col] != L' ' && lineText[col] != L'\t' && lineText[col] != L'\r' && lineText[col] != L'\n') {
                isAtEndOfLine = false;
                break;
            }
        }

        if (!isAtEndOfLine) {
            displayGhost.clear();
        }

        if (isFocused && !displayGhost.empty() && caretX + config.ScaleF(40.0f) < codeRight) {
            float ghostX = caretX + config.ScaleF(2.0f);
            float ghostW = static_cast<float>(displayGhost.length()) * charW;
            D2D1_RECT_F rcGhost = D2D1::RectF(ghostX, caretY, (std::min)(ghostX + ghostW, codeRight), caretY + lineH);

            pRT->DrawText(
                displayGhost.c_str(),
                static_cast<UINT32>(displayGhost.length()),
                theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                rcGhost,
                theme.brushTextMuted.Get()
            );
        }

        // NEXT: In-File & Cross-File Multi-Line Code Prediction (Images 3, 4, 5)
        if (isFocused && m_prediction.isValid && m_prediction.line == cursor.line && !IsAutocompleteVisible()) {
            std::wstring predGhost = m_prediction.ghostText;
            std::vector<std::wstring> ghostLines;
            size_t pStart = 0;
            while (pStart < predGhost.length()) {
                size_t pEnd = predGhost.find(L'\n', pStart);
                if (pEnd == std::wstring::npos) {
                    ghostLines.push_back(predGhost.substr(pStart));
                    break;
                }
                ghostLines.push_back(predGhost.substr(pStart, pEnd - pStart));
                pStart = pEnd + 1;
            }

            size_t badgeLineIdx = 0;
            for (size_t k = 0; k < ghostLines.size(); ++k) {
                if (!ghostLines[k].empty()) {
                    badgeLineIdx = k;
                    break;
                }
            }

            for (size_t k = 0; k < ghostLines.size(); ++k) {
                float curY = caretY + static_cast<float>(k) * lineH;
                if (curY + lineH > clientHeight) break;

                float gX = (k == 0) ? (caretX + config.ScaleF(2.0f)) : (codeLeft - scrollX);
                std::wstring dispGhostLine;
                for (wchar_t ch : ghostLines[k]) {
                    if (ch == L'\t') dispGhostLine.append(config.tabSize > 0 ? config.tabSize : 4, L' ');
                    else dispGhostLine.push_back(ch);
                }
                float gW = static_cast<float>(dispGhostLine.length()) * charW;
                D2D1_RECT_F rcG = D2D1::RectF(gX, curY, (std::min)(gX + gW, codeRight), curY + lineH);

                pRT->DrawText(
                    dispGhostLine.c_str(),
                    static_cast<UINT32>(dispGhostLine.length()),
                    theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                    rcG,
                    theme.brushTextMuted.Get()
                );

                // Zed-Style [tab Accept] Pill Badge next to the first non-empty predicted line (Screenshot 2)
                if (k == badgeLineIdx && !m_prediction.isCrossFile) {
                    float badgeX = gX + gW + config.ScaleF(10.0f);
                    float badgeH = config.ScaleF(20.0f);
                    float keyW   = config.ScaleF(28.0f);
                    float keyH   = config.ScaleF(15.0f);
                    float badgeW = keyW + config.ScaleF(56.0f);
                    float badgeY = curY + (lineH - badgeH) * 0.5f;

                    if (badgeX + badgeW < codeRight) {
                        D2D1_RECT_F rcBadge = D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH);
                        D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(rcBadge, config.ScaleF(4.0f), config.ScaleF(4.0f));

                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brBadgeBg;
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brBadgeBorder;
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brKeyBg;
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brKeyBorder;
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brKeyText;
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brAcceptText;

                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.14f, 0.18f, 0.95f) : D2D1::ColorF(0.92f, 0.94f, 0.97f, 0.98f), &brBadgeBg);
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.28f, 0.36f, 0.85f) : D2D1::ColorF(0.78f, 0.82f, 0.88f, 0.90f), &brBadgeBorder);
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.20f, 0.24f, 0.32f, 0.98f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brKeyBg);
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.34f, 0.40f, 0.52f, 0.90f) : D2D1::ColorF(0.75f, 0.79f, 0.85f, 0.95f), &brKeyBorder);
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.85f, 0.89f, 0.95f, 1.0f) : D2D1::ColorF(0.10f, 0.12f, 0.16f, 1.0f), &brKeyText);
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.95f, 0.97f, 1.0f, 1.0f) : D2D1::ColorF(0.15f, 0.18f, 0.25f, 1.0f), &brAcceptText);

                        if (brBadgeBg) pRT->FillRoundedRectangle(rrcBadge, brBadgeBg.Get());
                        if (brBadgeBorder) pRT->DrawRoundedRectangle(rrcBadge, brBadgeBorder.Get(), 1.0f);

                        // Physical keycap button for "tab"
                        float keyX = badgeX + config.ScaleF(3.0f);
                        float keyY = badgeY + (badgeH - keyH) * 0.5f;
                        D2D1_RECT_F rcKey = D2D1::RectF(keyX, keyY, keyX + keyW, keyY + keyH);
                        D2D1_ROUNDED_RECT rrcKey = D2D1::RoundedRect(rcKey, config.ScaleF(3.0f), config.ScaleF(3.0f));

                        if (brKeyBg) pRT->FillRoundedRectangle(rrcKey, brKeyBg.Get());
                        if (brKeyBorder) pRT->DrawRoundedRectangle(rrcKey, brKeyBorder.Get(), 1.0f);

                        // Centered, non-italic keycap text
                        IDWriteTextFormat* kFmt = theme.fontKeycap ? theme.fontKeycap.Get() :
                            (theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get());
                        if (kFmt && brKeyText) {
                            pRT->DrawText(L"tab", 3, kFmt, rcKey, brKeyText.Get());
                        }

                        // Clean non-italic "Accept" label
                        IDWriteTextFormat* aFmt = theme.fontMenu ? theme.fontMenu.Get() : theme.fontBody.Get();
                        float acceptX = keyX + keyW + config.ScaleF(6.0f);
                        D2D1_RECT_F rcAccept = D2D1::RectF(acceptX, badgeY + config.ScaleF(1.0f), badgeX + badgeW - config.ScaleF(4.0f), badgeY + badgeH);
                        if (aFmt && brAcceptText) {
                            pRT->DrawText(L"Accept", 6, aFmt, rcAccept, brAcceptText.Get());
                        }
                    }
                }
            }

            // Cross-File Floating Pill Button: [ ->| Take to route.ts ] (Image 4)
            if (m_prediction.isCrossFile && !m_prediction.targetFileName.empty()) {
                float pillW = config.ScaleF(142.0f);
                float pillH = config.ScaleF(24.0f);
                float pillX = caretX + config.ScaleF(8.0f);
                float pillY = caretY + static_cast<float>(ghostLines.size()) * lineH + config.ScaleF(4.0f);
                if (pillY + pillH < clientHeight) {
                    m_crossFilePillBounds = D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH);
                    D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(m_crossFilePillBounds, 4.0f, 4.0f);

                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBg;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillBorder;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillText;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pillGreen;

                    pRT->CreateSolidColorBrush(m_crossFilePillHovered ? D2D1::ColorF(0.18f, 0.22f, 0.26f, 0.98f) : D2D1::ColorF(0.12f, 0.14f, 0.17f, 0.95f), &pillBg);
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.32f, 0.38f, 0.9f), &pillBorder);
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.94f, 0.96f, 1.0f), &pillText);
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.85f, 0.45f, 1.0f), &pillGreen);

                    if (pillBg) pRT->FillRoundedRectangle(rrcPill, pillBg.Get());
                    if (pillBorder) pRT->DrawRoundedRectangle(rrcPill, pillBorder.Get(), 1.0f);

                    // Green ->| icon symbol inside pill
                    if (pillGreen) {
                        float iconX = pillX + config.ScaleF(8.0f);
                        float iconY = pillY + pillH * 0.5f;
                        float aLen = config.ScaleF(6.0f);
                        pRT->DrawLine(D2D1::Point2F(iconX, iconY), D2D1::Point2F(iconX + aLen, iconY), pillGreen.Get(), 1.5f);
                        pRT->DrawLine(D2D1::Point2F(iconX + aLen - config.ScaleF(2.5f), iconY - config.ScaleF(2.0f)),
                                      D2D1::Point2F(iconX + aLen, iconY), pillGreen.Get(), 1.5f);
                        pRT->DrawLine(D2D1::Point2F(iconX + aLen - config.ScaleF(2.5f), iconY + config.ScaleF(2.0f)),
                                      D2D1::Point2F(iconX + aLen, iconY), pillGreen.Get(), 1.5f);
                        pRT->DrawLine(D2D1::Point2F(iconX + aLen + config.ScaleF(2.0f), iconY - config.ScaleF(3.5f)),
                                      D2D1::Point2F(iconX + aLen + config.ScaleF(2.0f), iconY + config.ScaleF(3.5f)), pillGreen.Get(), 1.5f);
                    }

                    // Pill text
                    std::wstring label = L"Take to " + m_prediction.targetFileName;
                    D2D1_RECT_F rcText = D2D1::RectF(pillX + config.ScaleF(22.0f), pillY + config.ScaleF(2.0f), pillX + pillW - config.ScaleF(4.0f), pillY + pillH);
                    if (pillText && theme.fontCode) {
                        pRT->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
                                      theme.fontCode.Get(), rcText, pillText.Get());
                    }
                }
            } else {
                m_crossFilePillBounds = D2D1::RectF(0, 0, 0, 0);
            }
        }

        // 4c. GitLens Current Line Inline Blame Annotation (e.g. "   You, 2 days ago • Add student CRUD handlers")
        // Suppress inline blame when autocomplete popup is active or text is actively selected to prevent overlapping what the user types
        if (config.enableGitLens && !doc.gitBlameMap.empty() && !IsAutocompleteVisible() && !doc.HasSelection()) {
            auto itBlame = doc.gitBlameMap.find(cursor.line);
            if (itBlame != doc.gitBlameMap.end()) {
                const auto& blame = itBlame->second;
                std::wstring blameText = L"   " + (blame.isUncommitted ? (blame.author + L", Uncommitted changes") : (blame.author + L", " + blame.relativeTime + L" \x2022 " + blame.summary));

                // Accurately measure the visual width of the full line of text
                float textVisualW = 0.0f;
                if (cursor.line < buffer.GetLineCount()) {
                    const auto& clText = buffer.GetLine(cursor.line);
                    auto clColors = ScanLineColors(clText);
                    const float swatchSpan = config.ScaleF(14.0f);
                    textVisualW = GetColumnVisualOffset(clText, clColors, clText.length(), charW, swatchSpan, config.tabSize);
                    if (theme.dwriteFactory && theme.fontCode) {
                        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLineMeasure;
                        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                                clText.c_str(), static_cast<UINT32>(clText.length()),
                                theme.fontCode.Get(), 20000.0f, lineH, &pLineMeasure))) {
                            pLineMeasure->SetIncrementalTabStop(charW * static_cast<float>(config.tabSize > 0 ? config.tabSize : 4));
                            DWRITE_TEXT_METRICS tm{};
                            if (SUCCEEDED(pLineMeasure->GetMetrics(&tm))) {
                                float dwW = tm.widthIncludingTrailingWhitespace + static_cast<float>(clColors.size()) * swatchSpan;
                                textVisualW = (std::max)(textVisualW, dwW);
                            }
                        }
                    }
                }

                // Place blame strictly in empty space after both line text and active cursor, never overlapping code
                float lineEndX = (std::max)(codeLeft - scrollX + textVisualW, caretX) + config.ScaleF(40.0f);
                if (m_prediction.isValid && m_prediction.line == cursor.line && !m_prediction.ghostText.empty()) {
                    lineEndX += static_cast<float>(m_prediction.ghostText.length()) * charW + config.ScaleF(24.0f);
                }

                if (lineEndX < codeRight - config.ScaleF(20.0f)) {
                    IDWriteTextFormat* blameFmt = theme.fontHint ? theme.fontHint.Get() : (theme.fontBody ? theme.fontBody.Get() : theme.fontCode.Get());

                    // Muted, subtle italic gray annotation matching GitLens
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBlame;
                    pRT->CreateSolidColorBrush(
                        theme.IsDarkMode() ? D2D1::ColorF(0.55f, 0.58f, 0.62f, 0.75f) : D2D1::ColorF(0.40f, 0.42f, 0.46f, 0.80f),
                        &brushBlame
                    );

                    if (theme.dwriteFactory && blameFmt) {
                        Microsoft::WRL::ComPtr<IDWriteTextLayout> pBlameLayout;
                        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                                blameText.c_str(), static_cast<UINT32>(blameText.length()),
                                blameFmt, 10000.0f, lineH, &pBlameLayout))) {
                            pBlameLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                            pRT->DrawTextLayout(D2D1::Point2F(lineEndX, caretY), pBlameLayout.Get(), brushBlame ? brushBlame.Get() : theme.brushTextMuted.Get());
                        }
                    } else {
                        D2D1_RECT_F rcBlame = D2D1::RectF(lineEndX, caretY, codeRight, caretY + lineH);
                        pRT->DrawText(blameText.c_str(), static_cast<UINT32>(blameText.length()),
                                      blameFmt, rcBlame, brushBlame ? brushBlame.Get() : theme.brushTextMuted.Get());
                    }
                }
            }
        }
    }


    pRT->PopAxisAlignedClip();

    // 4b. Gutter Elements (Line Numbers, Glowing Active Line, Breakpoints, Error Dots, Chevrons)
    if (config.showLineNumbers) {
        D2D1_RECT_F clipGutter = D2D1::RectF(canvasLeft, topY, canvasLeft + gutterW, clientHeight);
        pRT->PushAxisAlignedClip(clipGutter, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        // Gutter Background & Subtle Divider Line
        pRT->FillRectangle(clipGutter, theme.brushCanvasBg.Get());
        pRT->DrawLine(
            D2D1::Point2F(canvasLeft + gutterW - 0.5f, topY),
            D2D1::Point2F(canvasLeft + gutterW - 0.5f, clientHeight),
            theme.brushBorderSubtle.Get(),
            1.0f
        );

        for (size_t v = startV; v < endV; ++v) {
            const auto& vl = visualLines[v];
            float lineY = topY + config.ScaleF(6.0f) + (v - doc.scrollLine) * lineH;
            if (lineY + lineH < topY || lineY > clientHeight) continue;

            if (vl.isCodeLens) {
                // CodeLens virtual line: gutter remains completely empty, matching VS Code Image 1!
                continue;
            }

            if (vl.isPredictionVirtual) {
                // Virtual prediction continuation line: keep gutter number blank (Zed-style virtual space)
                // Render green vertical accent bar along gutter right border
                float barW = config.ScaleF(2.5f);
                D2D1_RECT_F rcPredBar = D2D1::RectF(canvasLeft + gutterW - barW, lineY, canvasLeft + gutterW, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> predBrush;
                pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.18f, 0.85f, 0.45f, 0.95f) : D2D1::ColorF(0.08f, 0.60f, 0.25f, 0.95f), &predBrush);
                if (predBrush) {
                    pRT->FillRectangle(rcPredBar, predBrush.Get());
                }
                continue;
            }

            // Diagnostic error in gutter
            bool hasDiagError = false;
            for (const auto& d : doc.diagnostics) {
                if (d.line == vl.bufferLine && d.severity == 1) {
                    hasDiagError = true;
                    break;
                }
            }

            // Line Number in Gutter (Right-aligned, Consolas Monospace)
            std::wstring lineNumStr = std::to_wstring(vl.bufferLine + 1);
            D2D1_RECT_F rcNum = D2D1::RectF(
                canvasLeft + config.ScaleF(4.0f),
                lineY,
                canvasLeft + gutterW - config.ScaleF(16.0f),
                lineY + lineH
            );

            bool isActiveLine = (isFocused && vl.bufferLine == cursor.line);
            ID2D1Brush* numBrush = hasDiagError ? (theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushCloseHoverBg.Get()) :
                (isActiveLine ? (theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) : theme.brushLineNumber.Get());
            IDWriteTextFormat* numFormat = isActiveLine ?
                (theme.fontLineNumberActive ? theme.fontLineNumberActive.Get() : (theme.fontLineNumber ? theme.fontLineNumber.Get() : theme.fontMenuShortcut.Get())) :
                (theme.fontLineNumber ? theme.fontLineNumber.Get() : theme.fontMenuShortcut.Get());

            pRT->DrawText(
                lineNumStr.c_str(),
                static_cast<UINT32>(lineNumStr.length()),
                numFormat,
                rcNum,
                numBrush
            );

            // Breakpoint Marker in Gutter (F9 / Debug Menu / Click Gutter)
            bool hasBreakpoint = (std::find(doc.breakpoints.begin(), doc.breakpoints.end(), vl.bufferLine) != doc.breakpoints.end());
            if (hasBreakpoint) {
                float bpX = canvasLeft + config.ScaleF(12.0f);
                float bpY = lineY + lineH / 2.0f;
                float bpR = config.ScaleF(4.5f);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bpBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.25f, 0.25f), &bpBrush);
                if (bpBrush) {
                    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bpX, bpY), bpR, bpR), bpBrush.Get());
                }
            }

            // Gutter Diagnostic Error Indicator Dot (Image 4)
            if (hasDiagError && !hasBreakpoint) {
                float errDotX = canvasLeft + config.ScaleF(8.0f);
                float errDotY = lineY + lineH / 2.0f;
                float errDotR = config.ScaleF(2.5f);
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(errDotX, errDotY), errDotR, errDotR), theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushCloseHoverBg.Get());
            }

            // NEXT: In-File & Cross-File Prediction Gutter Indicator (Images 3, 4, 5)
            if (m_prediction.isValid && m_prediction.line == vl.bufferLine) {
                float barW = config.ScaleF(2.5f);
                D2D1_RECT_F rcPredBar = D2D1::RectF(canvasLeft + gutterW - barW, lineY, canvasLeft + gutterW, lineY + lineH);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> predBrush;
                pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.18f, 0.85f, 0.45f, 1.0f) : D2D1::ColorF(0.08f, 0.60f, 0.25f, 1.0f), &predBrush);
                if (predBrush) {
                    pRT->FillRectangle(rcPredBar, predBrush.Get());

                    // Green ->| icon symbol (arrow pointing right with stop bar)
                    float symX = canvasLeft + config.ScaleF(6.0f);
                    float symY = lineY + lineH * 0.5f;
                    float arrowLen = config.ScaleF(6.5f);
                    float barH = config.ScaleF(4.0f);
                    pRT->DrawLine(D2D1::Point2F(symX, symY), D2D1::Point2F(symX + arrowLen, symY), predBrush.Get(), 1.6f);
                    pRT->DrawLine(D2D1::Point2F(symX + arrowLen - config.ScaleF(3.0f), symY - config.ScaleF(2.5f)),
                                  D2D1::Point2F(symX + arrowLen, symY), predBrush.Get(), 1.6f);
                    pRT->DrawLine(D2D1::Point2F(symX + arrowLen - config.ScaleF(3.0f), symY + config.ScaleF(2.5f)),
                                  D2D1::Point2F(symX + arrowLen, symY), predBrush.Get(), 1.6f);
                    pRT->DrawLine(D2D1::Point2F(symX + arrowLen + config.ScaleF(2.5f), symY - barH),
                                  D2D1::Point2F(symX + arrowLen + config.ScaleF(2.5f), symY + barH), predBrush.Get(), 1.6f);
                }

                // Lightbulb icon if triggered from comment/annotation (Image 5)
                if (m_prediction.hasLightbulb) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bulbBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.78f, 0.15f, 1.0f), &bulbBrush);
                    if (bulbBrush) {
                        float bulbX = canvasLeft + config.ScaleF(18.0f);
                        float bulbY = lineY + lineH * 0.5f;
                        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bulbX, bulbY - config.ScaleF(1.0f)), config.ScaleF(3.0f), config.ScaleF(3.0f)), bulbBrush.Get());
                    }
                }
            }

            // Code Folding Chevron in Gutter (Images 2 & 3)
            size_t foldEnd = 0;
            bool canFold = vl.isFolded || GetFoldRange(buffer, vl.bufferLine, foldEnd);
            if (canFold) {
                const float chevronSize = config.ScaleF(14.0f);
                const float chevronX = canvasLeft + gutterW - config.ScaleF(15.0f);
                const float chevronY = lineY + (lineH - chevronSize) / 2.0f;
                D2D1_RECT_F rcChevron = D2D1::RectF(chevronX, chevronY, chevronX + chevronSize, chevronY + chevronSize);

                FoldChevronHit hit;
                hit.bounds = rcChevron;
                hit.bufferLine = vl.bufferLine;
                m_foldChevrons.push_back(hit);

                ID2D1Brush* chevronBrush = isActiveLine
                    ? (theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextMuted.Get())
                    : theme.brushTextMuted.Get();

                const float cx = rcChevron.left + chevronSize * 0.5f;
                const float cy = rcChevron.top + chevronSize * 0.5f;
                const float cr = config.ScaleF(3.8f);

                ID2D1StrokeStyle* pStroke = m_icons.GetStrokeStyle();

                if (vl.isFolded) {
                    // Collapsed: Right Chevron > (Image 3)
                    pRT->DrawLine(D2D1::Point2F(cx - cr * 0.55f, cy - cr), D2D1::Point2F(cx + cr * 0.55f, cy), chevronBrush, 1.4f, pStroke);
                    pRT->DrawLine(D2D1::Point2F(cx + cr * 0.55f, cy), D2D1::Point2F(cx - cr * 0.55f, cy + cr), chevronBrush, 1.4f, pStroke);
                } else {
                    // Expanded: Down Chevron V (User request: "it should be V")
                    pRT->DrawLine(D2D1::Point2F(cx - cr, cy - cr * 0.55f), D2D1::Point2F(cx, cy + cr * 0.55f), chevronBrush, 1.4f, pStroke);
                    pRT->DrawLine(D2D1::Point2F(cx, cy + cr * 0.55f), D2D1::Point2F(cx + cr, cy - cr * 0.55f), chevronBrush, 1.4f, pStroke);
                }
            }
            // Gutter Diff Change Indicators (Images 1, 2, 3)
            // Blue Bar = Modified line (Image 1)
            // Green Bar = Added line (Image 3)
            // Red Marker = Deleted line(s) marker (Image 2)
            if (!doc.isDiffView) {
                float barW = config.ScaleF(3.0f);
                float barRight = canvasLeft + gutterW;
                float barLeft = barRight - barW;

                if (doc.diffModifiedLines.count(vl.bufferLine)) {
                    // Blue Bar for Modified line (Solid clean VS Code accent blue bar)
                    D2D1_RECT_F rcBar = D2D1::RectF(barLeft, lineY, barRight, lineY + lineH);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> blueBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.48f, 1.0f, 1.0f), &blueBrush);
                    if (blueBrush) {
                        pRT->FillRectangle(rcBar, blueBrush.Get());
                    }
                } else if (doc.diffAddedLines.count(vl.bufferLine)) {
                    // Green Bar for Added line (Image 3)
                    D2D1_RECT_F rcBar = D2D1::RectF(barLeft, lineY, barRight, lineY + lineH);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> greenBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.75f, 0.38f, 1.0f), &greenBrush);
                    if (greenBrush) {
                        pRT->FillRectangle(rcBar, greenBrush.Get());
                    }
                }

                if (doc.diffDeletedGutterMarkers.count(vl.bufferLine)) {
                    // Red indicator marker for Deleted lines (Image 2)
                    float markerH = config.ScaleF(6.0f);
                    float markerW = config.ScaleF(4.0f);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDel;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.25f, 0.25f, 1.0f), &brushDel);
                    if (brushDel) {
                        Microsoft::WRL::ComPtr<ID2D1Factory> pD2DFactory;
                        pRT->GetFactory(&pD2DFactory);
                        Microsoft::WRL::ComPtr<ID2D1PathGeometry> pathGeom;
                        if (pD2DFactory && SUCCEEDED(pD2DFactory->CreatePathGeometry(&pathGeom))) {
                            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                            if (SUCCEEDED(pathGeom->Open(&sink))) {
                                sink->BeginFigure(D2D1::Point2F(barRight - markerW, lineY - markerH * 0.5f), D2D1_FIGURE_BEGIN_FILLED);
                                sink->AddLine(D2D1::Point2F(barRight, lineY));
                                sink->AddLine(D2D1::Point2F(barRight - markerW, lineY + markerH * 0.5f));
                                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                                sink->Close();
                                pRT->FillGeometry(pathGeom.Get(), brushDel.Get());
                            }
                        } else {
                            D2D1_RECT_F rcDel = D2D1::RectF(barRight - markerW, lineY - markerH * 0.5f, barRight, lineY + markerH * 0.5f);
                            pRT->FillRectangle(rcDel, brushDel.Get());
                        }
                    }
                }

                if (vl.bufferLine + 1 == buffer.GetLineCount() && doc.diffDeletedGutterMarkers.count(buffer.GetLineCount())) {
                    float markerH = config.ScaleF(6.0f);
                    float markerW = config.ScaleF(4.0f);
                    float endLineY = lineY + lineH;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDel;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.25f, 0.25f, 1.0f), &brushDel);
                    if (brushDel) {
                        D2D1_RECT_F rcDel = D2D1::RectF(barRight - markerW, endLineY - markerH * 0.5f, barRight, endLineY + markerH * 0.5f);
                        pRT->FillRectangle(rcDel, brushDel.Get());
                    }
                }
            }
        }

        pRT->PopAxisAlignedClip();
    }

    // 5. Code Minimap (Image 5: scaled miniature code preview with syntax highlighting, search ticks, and viewport slider)
    if (config.showMinimap && !doc.isSpecialView && !doc.isImageView && !doc.isUnsupportedBinary) {
        doc.minimapTrack = D2D1::RectF(canvasRight - minimapW, topY, canvasRight, clientHeight);

        // Minimap background with subtle left border
        D2D1_COLOR_F miniBg = theme.IsDarkMode() ? D2D1::ColorF(0.11f, 0.11f, 0.12f, 0.85f) : D2D1::ColorF(0.965f, 0.970f, 0.980f, 0.95f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> miniBgBrush;
        pRT->CreateSolidColorBrush(miniBg, &miniBgBrush);
        if (miniBgBrush) pRT->FillRectangle(doc.minimapTrack, miniBgBrush.Get());

        pRT->DrawLine(
            D2D1::Point2F(doc.minimapTrack.left, topY),
            D2D1::Point2F(doc.minimapTrack.left, clientHeight),
            theme.brushBorderSubtle.Get(),
            1.0f
        );

        pRT->PushAxisAlignedClip(doc.minimapTrack, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        size_t totalLines = buffer.GetLineCount();
        float miniH = (std::max)(10.0f, clientHeight - topY);
        float baseLineStep = (std::max)(3.2f, config.ScaleF(3.8f));
        float stepY = (totalLines * baseLineStep > miniH && totalLines > 0) ? (miniH / static_cast<float>(totalLines)) : baseLineStep;
        float microH = (std::max)(1.2f, (totalLines * baseLineStep > miniH) ? (stepY * 0.70f) : 2.2f);
        float microCharW = (std::max)(0.8f, config.ScaleF(1.3f));

        // Render micro-lines with syntax colors, selection reflection, and word occurrences
        for (size_t b = 0; b < totalLines; ++b) {
            float my = topY + static_cast<float>(b) * stepY;
            if (my + microH > clientHeight) break;

            const auto& lineStr = buffer.GetLine(b);

            // Active text selection reflection on minimap (Images 3 & 4)
            if (doc.HasSelection()) {
                size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                doc.GetSelectionRange(sLine, sCol, eLine, eCol);
                if (b >= sLine && b <= eLine) {
                    D2D1_RECT_F rcSelMini = D2D1::RectF(
                        doc.minimapTrack.left + 2.0f,
                        my,
                        doc.minimapTrack.right - 2.0f,
                        my + (std::max)(microH, stepY)
                    );
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selMiniBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.45f, 0.85f, 0.45f), &selMiniBrush);
                    if (selMiniBrush) pRT->FillRectangle(rcSelMini, selMiniBrush.Get());
                }
            }

            // Active Caret Line Indicator on minimap (Image 1)
            // Displays a bright blue horizontal indicator line spanning across the minimap track
            if (cursor.line == b) {
                D2D1_RECT_F rcCursorMini = D2D1::RectF(
                    doc.minimapTrack.left + 1.0f,
                    my,
                    doc.minimapTrack.right - 1.0f,
                    my + (std::max)(2.5f, microH + 1.0f)
                );
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cursorMiniBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.00f, 0.48f, 1.00f, 0.95f), &cursorMiniBrush);
                if (cursorMiniBrush) pRT->FillRectangle(rcCursorMini, cursorMiniBrush.Get());
            }

            // Target search jump highlight banner on minimap
            if (doc.targetHighlightLine == b) {
                D2D1_RECT_F rcTargetMini = D2D1::RectF(
                    doc.minimapTrack.left + 1.0f,
                    my - 1.0f,
                    doc.minimapTrack.right - 1.0f,
                    my + microH + 1.0f
                );
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> targetMiniBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.82f, 0.0f, 0.65f), &targetMiniBrush);
                if (targetMiniBrush) pRT->FillRectangle(rcTargetMini, targetMiniBrush.Get());
            }

            // Diagnostic syntax error markers on minimap (Image 4)
            for (const auto& diag : doc.diagnostics) {
                if (diag.line == b) {
                    D2D1_RECT_F rcErrMini = D2D1::RectF(
                        doc.minimapTrack.left + 1.0f,
                        my - 0.5f,
                        doc.minimapTrack.right - 1.0f,
                        my + (std::max)(microH + 1.5f, stepY)
                    );
                    ID2D1Brush* pErrBrush = (diag.severity == 1) ?
                        (theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushCloseHoverBg.Get()) :
                        (theme.brushIconOrange ? theme.brushIconOrange.Get() : theme.brushSyntaxNumber.Get());
                    pRT->FillRectangle(rcErrMini, pErrBrush);
                    break;
                }
            }

            // Minimap Diff Gutter Indicators
            if (!doc.isDiffView) {
                if (doc.diffModifiedLines.count(b)) {
                    D2D1_RECT_F rcDiffMini = D2D1::RectF(doc.minimapTrack.left, my, doc.minimapTrack.left + config.ScaleF(2.5f), my + (std::max)(microH, stepY));
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> blueMini;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.478f, 0.800f, 0.95f), &blueMini);
                    if (blueMini) pRT->FillRectangle(rcDiffMini, blueMini.Get());
                } else if (doc.diffAddedLines.count(b)) {
                    D2D1_RECT_F rcDiffMini = D2D1::RectF(doc.minimapTrack.left, my, doc.minimapTrack.left + config.ScaleF(2.5f), my + (std::max)(microH, stepY));
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> greenMini;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.75f, 0.38f, 0.95f), &greenMini);
                    if (greenMini) pRT->FillRectangle(rcDiffMini, greenMini.Get());
                } else if (doc.diffDeletedGutterMarkers.count(b)) {
                    D2D1_RECT_F rcDiffMini = D2D1::RectF(doc.minimapTrack.left, my - 1.0f, doc.minimapTrack.left + config.ScaleF(3.0f), my + 1.0f);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> redMini;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.25f, 0.25f, 0.95f), &redMini);
                    if (redMini) pRT->FillRectangle(rcDiffMini, redMini.Get());
                }
            }
            if (lineStr.empty()) continue;

            size_t indent = lineStr.find_first_not_of(L" \t");
            if (indent == std::wstring::npos) continue;

            auto tokens = buffer.GetTokensForLine(b, docExt);
            if (!tokens.empty()) {
                for (const auto& tok : tokens) {
                    if (tok.startCol + tok.length > lineStr.length()) continue;
                    float tx = doc.minimapTrack.left + config.ScaleF(5.0f) + static_cast<float>(tok.startCol) * microCharW;
                    float tw = (std::min)(doc.minimapTrack.right - tx - 2.0f, static_cast<float>(tok.length) * microCharW);
                    if (tw <= 0.5f) continue;

                    ID2D1Brush* brush = theme.brushTextMuted.Get();
                    switch (tok.type) {
                        case editor::TokenType::Keyword:   brush = theme.brushSyntaxKeyword.Get(); break;
                        case editor::TokenType::Type:      brush = theme.brushSyntaxType.Get(); break;
                        case editor::TokenType::String:    brush = theme.brushSyntaxString.Get(); break;
                        case editor::TokenType::Comment:   brush = theme.brushSyntaxComment.Get(); break;
                        case editor::TokenType::Number:    brush = theme.brushSyntaxNumber.Get(); break;
                        case editor::TokenType::Operator:  brush = theme.brushSyntaxOperator.Get(); break;
                        case editor::TokenType::Tag:       brush = theme.brushSyntaxTag.Get(); break;
                        case editor::TokenType::Attribute: brush = theme.brushSyntaxAttribute.Get(); break;
                        default: brush = theme.brushTextPrimary.Get(); break;
                    }
                    D2D1_ROUNDED_RECT rrcTok = D2D1::RoundedRect(D2D1::RectF(tx, my, tx + tw, my + microH), 0.8f, 0.8f);
                    pRT->FillRoundedRectangle(&rrcTok, brush);
                }
            } else {
                float mx = doc.minimapTrack.left + config.ScaleF(5.0f) + static_cast<float>(indent) * microCharW;
                float tw = (std::min)(doc.minimapTrack.right - mx - 2.0f, static_cast<float>(lineStr.length() - indent) * microCharW);
                if (tw > 0.5f) {
                    D2D1_ROUNDED_RECT rrcLine = D2D1::RoundedRect(D2D1::RectF(mx, my, mx + tw, my + microH), 0.8f, 0.8f);
                    pRT->FillRoundedRectangle(&rrcLine, theme.brushTextMuted.Get());
                }
            }

            // Highlight matching word occurrences on minimap (Image 4)
            if (!doc.selectedWord.empty() && doc.selectedWord.length() > 1) {
                if (lineStr.find(doc.selectedWord) != std::wstring::npos) {
                    D2D1_RECT_F rcWordTick = D2D1::RectF(
                        doc.minimapTrack.right - config.ScaleF(12.0f),
                        my,
                        doc.minimapTrack.right - config.ScaleF(2.0f),
                        my + (std::max)(2.0f, microH)
                    );
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> wordTickBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.70f, 1.0f, 0.90f), &wordTickBrush);
                    if (wordTickBrush) pRT->FillRectangle(rcWordTick, wordTickBrush.Get());
                }
            }

            // Search highlight tick on right side of minimap line
            if (!highlightQuery.empty()) {
                std::wstring qLower = highlightQuery;
                std::wstring lLower = lineStr;
                if (!highlightMatchCase) {
                    for (auto& ch : qLower) ch = towlower(ch);
                    for (auto& ch : lLower) ch = towlower(ch);
                }
                if (lLower.find(qLower) != std::wstring::npos) {
                    D2D1_RECT_F rcTick = D2D1::RectF(
                        doc.minimapTrack.right - config.ScaleF(8.0f),
                        my,
                        doc.minimapTrack.right - config.ScaleF(1.0f),
                        my + (std::max)(2.0f, microH)
                    );
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hlTickBrush;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.58f, 0.12f, 0.90f), &hlTickBrush);
                    if (hlTickBrush) pRT->FillRectangle(rcTick, hlTickBrush.Get());
                }
            }
        }

        // Viewport Slider Box with smooth modern translucency (Images 3 & 4)
        float sliderH = (maxScroll > 0)
            ? (std::max)(config.ScaleF(24.0f), (static_cast<float>(visibleCount) / static_cast<float>(visualLines.size() + visibleCount)) * miniH)
            : miniH;
        if (sliderH > miniH) sliderH = miniH;

        float sliderTop = topY;
        if (maxScroll > 0) {
            float scrollRatio = (std::max)(0.0f, (std::min)(1.0f, static_cast<float>(doc.scrollLine) / static_cast<float>(maxScroll)));
            sliderTop = topY + scrollRatio * (miniH - sliderH);
        }
        float sliderBottom = (std::min)(topY + miniH, sliderTop + sliderH);

        doc.minimapSlider = D2D1::RectF(
            doc.minimapTrack.left + 1.0f,
            sliderTop,
            doc.minimapTrack.right - 1.0f,
            sliderBottom
        );

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sliderBgBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sliderBorderBrush;
        float sliderAlpha = doc.isDraggingMinimap ? 0.32f : 0.18f;
        if (theme.IsDarkMode()) {
            pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, sliderAlpha), &sliderBgBrush);
            pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, sliderAlpha + 0.22f), &sliderBorderBrush);
        } else {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.16f, 0.24f, sliderAlpha * 0.75f), &sliderBgBrush);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.16f, 0.24f, sliderAlpha + 0.20f), &sliderBorderBrush);
        }

        D2D1_ROUNDED_RECT rrcSlider = D2D1::RoundedRect(doc.minimapSlider, 2.0f, 2.0f);
        if (sliderBgBrush) pRT->FillRoundedRectangle(&rrcSlider, sliderBgBrush.Get());
        if (sliderBorderBrush) pRT->DrawRoundedRectangle(&rrcSlider, sliderBorderBrush.Get(), 1.0f);

        pRT->PopAxisAlignedClip();
    } else {
        doc.minimapTrack = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        doc.minimapSlider = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);

        // 6. Vertical Scrollbar on Right Edge when Minimap is disabled
        doc.scrollbarTrack = D2D1::RectF(canvasRight - scrollbarW, topY, canvasRight, clientHeight);
        if (maxScroll > 0) {
            float thumbH = (std::max)(config.ScaleF(28.0f), codeAreaH * (static_cast<float>(visibleCount) / (visualLines.size() + visibleCount)));
            float scrollRange = codeAreaH - thumbH;
            float scrollFrac = (maxScroll > 0)
                ? (static_cast<float>(doc.scrollLine) / static_cast<float>(maxScroll))
                : 0.0f;
            float thumbY = topY + scrollRange * scrollFrac;

            doc.scrollbarThumb = D2D1::RectF(
                canvasRight - scrollbarW + config.ScaleF(3.0f),
                thumbY,
                canvasRight - config.ScaleF(3.0f),
                thumbY + thumbH
            );

            D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(doc.scrollbarThumb, 3.5f, 3.5f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushThumb;
            if (theme.IsDarkMode()) {
                float alpha = doc.isDraggingScrollbar ? 0.85f : 0.50f;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.40f, 0.44f, alpha), &brushThumb);
            } else {
                float alpha = doc.isDraggingScrollbar ? 0.85f : 0.50f;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.55f, 0.60f, alpha), &brushThumb);
            }
            if (brushThumb) {
                pRT->FillRoundedRectangle(rrcThumb, brushThumb.Get());
            }
        } else {
            doc.scrollbarThumb = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    // 6b. Horizontal Scrollbar when content overflows horizontally
    doc.maxContentWidth = maxLineWidth;
    float viewportCodeW = codeRight - (canvasLeft + gutterW);
    float maxScrollX = (doc.maxContentWidth > viewportCodeW) ? (doc.maxContentWidth - viewportCodeW + config.ScaleF(40.0f)) : 0.0f;
    if (maxScrollX > 0.0f) {
        float hTrackH = config.ScaleF(10.0f);
        float hTrackY1 = clientHeight - hTrackH;
        float hTrackY2 = clientHeight - config.ScaleF(2.0f);
        float hTrackX1 = canvasLeft + gutterW;
        float hTrackX2 = codeRight;

        doc.hScrollbarTrack = D2D1::RectF(hTrackX1, hTrackY1, hTrackX2, hTrackY2);
        float trackW = hTrackX2 - hTrackX1;
        float thumbW = (std::max)(config.ScaleF(32.0f), (viewportCodeW / (doc.maxContentWidth + config.ScaleF(40.0f))) * trackW);
        float thumbMaxTravel = trackW - thumbW;
        float thumbX = hTrackX1 + (maxScrollX > 0.0f ? ((std::clamp)(doc.scrollOffsetX, 0.0f, maxScrollX) / maxScrollX) * thumbMaxTravel : 0.0f);
        doc.hScrollbarThumb = D2D1::RectF(thumbX, hTrackY1, thumbX + thumbW, hTrackY2);

        D2D1_ROUNDED_RECT rrcHThumb = D2D1::RoundedRect(doc.hScrollbarThumb, 3.5f, 3.5f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHThumb;
        float alpha = doc.isDraggingHScrollbar ? 0.85f : 0.50f;
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.40f, 0.40f, 0.44f, alpha) : D2D1::ColorF(0.55f, 0.55f, 0.60f, alpha), &brushHThumb);
        if (brushHThumb) {
            pRT->FillRoundedRectangle(rrcHThumb, brushHThumb.Get());
        }
    } else {
        doc.hScrollbarTrack = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        doc.hScrollbarThumb = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // 7. Diagnostic Error Hover Popover (VS Code / Antigravity style)
    if (doc.hoveredDiagnosticLine != static_cast<size_t>(-1)) {
        const lsp::Diagnostic* pDiag = nullptr;
        for (const auto& d : doc.diagnostics) {
            if (d.line == doc.hoveredDiagnosticLine) {
                pDiag = &d;
                break;
            }
        }
        if (pDiag && pDiag->line >= doc.scrollLine) {
            std::wstring locStr = doc.fileName + L" (" + std::to_wstring(pDiag->line + 1) + L":" + std::to_wstring(pDiag->col + 1) + L")";

            float textPadLeft = config.ScaleF(32.0f);
            float textPadRight = config.ScaleF(16.0f);
            float topPad = config.ScaleF(8.0f);
            float bottomPad = config.ScaleF(8.0f);

            float msgW = config.ScaleF(380.0f);
            float msgH = config.ScaleF(18.0f);
            float locW = config.ScaleF(150.0f);
            float locH = config.ScaleF(14.0f);

            Microsoft::WRL::ComPtr<IDWriteTextLayout> pMsgLayout;
            if (theme.dwriteFactory && theme.fontBody) {
                if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                    pDiag->message.c_str(),
                    static_cast<UINT32>(pDiag->message.length()),
                    theme.fontBody.Get(),
                    10000.0f,
                    1000.0f,
                    &pMsgLayout))) {
                    pMsgLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                    DWRITE_TEXT_METRICS tm;
                    if (SUCCEEDED(pMsgLayout->GetMetrics(&tm))) {
                        msgW = tm.width;
                        msgH = tm.height;
                    }
                }
            }

            Microsoft::WRL::ComPtr<IDWriteTextLayout> pLocLayout;
            if (theme.dwriteFactory && theme.fontMenuShortcut) {
                if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                    locStr.c_str(),
                    static_cast<UINT32>(locStr.length()),
                    theme.fontMenuShortcut.Get(),
                    10000.0f,
                    1000.0f,
                    &pLocLayout))) {
                    pLocLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                    DWRITE_TEXT_METRICS tm;
                    if (SUCCEEDED(pLocLayout->GetMetrics(&tm))) {
                        locW = tm.width;
                        locH = tm.height;
                    }
                }
            }

            float neededContentW = (std::max)(msgW, locW);
            float popW = neededContentW + textPadLeft + textPadRight;
            popW = (std::max)(popW, config.ScaleF(280.0f));

            float maxAvailableW = canvasWidth - config.ScaleF(30.0f);
            if (popW > maxAvailableW && maxAvailableW > config.ScaleF(200.0f)) {
                popW = maxAvailableW;
                // If constrained, recreate message layout with word wrapping
                if (theme.dwriteFactory && theme.fontBody) {
                    pMsgLayout.Reset();
                    float wrapW = popW - textPadLeft - textPadRight;
                    if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                        pDiag->message.c_str(),
                        static_cast<UINT32>(pDiag->message.length()),
                        theme.fontBody.Get(),
                        wrapW,
                        2000.0f,
                        &pMsgLayout))) {
                        pMsgLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                        pMsgLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                        DWRITE_TEXT_METRICS tm;
                        if (SUCCEEDED(pMsgLayout->GetMetrics(&tm))) {
                            msgH = tm.height;
                        }
                    }
                }
            }

            float popH = topPad + msgH + config.ScaleF(4.0f) + locH + bottomPad;

            // Find target line visual Y using visualLines (takes CodeLens and folding into account)
            float targetLineY = -1.0f;
            for (size_t v = doc.scrollLine; v < visualLines.size(); ++v) {
                if (!visualLines[v].isCodeLens && !visualLines[v].isPredictionVirtual && visualLines[v].bufferLine == pDiag->line) {
                    targetLineY = topY + config.ScaleF(6.0f) + static_cast<float>(v - doc.scrollLine) * lineH;
                    break;
                }
            }

            if (targetLineY < 0.0f) {
                targetLineY = topY + config.ScaleF(6.0f);
            }

            float tokenX = codeLeft + config.ScaleF(12.0f) + static_cast<float>(pDiag->col) * charW - doc.scrollOffsetX;
            float popX = (std::max)(canvasLeft + config.ScaleF(12.0f), tokenX);
            if (popX + popW > canvasLeft + canvasWidth - config.ScaleF(12.0f)) {
                popX = (std::max)(canvasLeft + config.ScaleF(12.0f), canvasLeft + canvasWidth - popW - config.ScaleF(12.0f));
            }

            // Place popover BELOW the line by default so preceding code, opening tags, and matching brackets are NEVER obscured
            float popY = targetLineY + lineH + config.ScaleF(4.0f);
            // Only if placed below would overflow canvas bottom, place ABOVE the line
            if (popY + popH > clientHeight - config.ScaleF(12.0f)) {
                popY = targetLineY - popH - config.ScaleF(4.0f);
            }

            D2D1_RECT_F rcCard = D2D1::RectF(popX, popY, popX + popW, popY + popH);
            D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, 4.0f, 4.0f);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBg;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cardBorder;
            pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f) : D2D1::ColorF(1.0f, 0.96f, 0.96f, 0.98f), &cardBg);
            pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.92f, 0.28f, 0.28f, 1.0f) : D2D1::ColorF(0.90f, 0.40f, 0.40f, 1.0f), &cardBorder);
            if (cardBg) pRT->FillRoundedRectangle(&rrcCard, cardBg.Get());
            if (cardBorder) pRT->DrawRoundedRectangle(&rrcCard, cardBorder.Get(), 1.2f);

            // Red Error badge circle (x)
            float badgeX = popX + config.ScaleF(15.0f);
            float badgeY = popY + topPad + (std::min)(msgH, config.ScaleF(18.0f)) / 2.0f;
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(badgeX, badgeY), config.ScaleF(6.0f), config.ScaleF(6.0f)),
                             theme.brushIconRed ? theme.brushIconRed.Get() : theme.brushCloseHoverBg.Get());
            // Draw 'x' inside badge
            float xr = config.ScaleF(2.5f);
            pRT->DrawLine(D2D1::Point2F(badgeX - xr, badgeY - xr), D2D1::Point2F(badgeX + xr, badgeY + xr), theme.brushTextWhite.Get(), 1.2f);
            pRT->DrawLine(D2D1::Point2F(badgeX + xr, badgeY - xr), D2D1::Point2F(badgeX - xr, badgeY + xr), theme.brushTextWhite.Get(), 1.2f);

            // Message text - draw layout or text with axis-aligned clipping so text NEVER leaks outside card
            pRT->PushAxisAlignedClip(rcCard, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            D2D1_POINT_2F ptMsg = D2D1::Point2F(popX + textPadLeft, popY + topPad);
            ID2D1Brush* textBrush = theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
            if (pMsgLayout) {
                pRT->DrawTextLayout(ptMsg, pMsgLayout.Get(), textBrush);
            } else {
                D2D1_RECT_F rcMsg = D2D1::RectF(ptMsg.x, ptMsg.y, popX + popW - textPadRight, ptMsg.y + msgH);
                pRT->DrawText(pDiag->message.c_str(), static_cast<UINT32>(pDiag->message.length()), theme.fontBody.Get(), rcMsg, textBrush);
            }

            // Source line/col info
            D2D1_POINT_2F ptLoc = D2D1::Point2F(popX + textPadLeft, popY + topPad + msgH + config.ScaleF(4.0f));
            if (pLocLayout) {
                pRT->DrawTextLayout(ptLoc, pLocLayout.Get(), theme.brushTextMuted.Get());
            } else {
                D2D1_RECT_F rcLoc = D2D1::RectF(ptLoc.x, ptLoc.y, popX + popW - textPadRight, ptLoc.y + locH);
                pRT->DrawText(locStr.c_str(), static_cast<UINT32>(locStr.length()), theme.fontMenuShortcut.Get(), rcLoc, theme.brushTextMuted.Get());
            }
            pRT->PopAxisAlignedClip();
        }
    }
}

int EditorCanvas::HitTestTab(POINT pt, const std::vector<editor::OpenDocument>& docs) const {
    for (size_t i = 0; i < docs.size(); ++i) {
        const auto& b = docs[i].tabBounds;
        if (pt.x >= b.left && pt.x <= b.right && pt.y >= b.top && pt.y <= b.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int EditorCanvas::HitTestTabClose(POINT pt, const std::vector<editor::OpenDocument>& docs) const {
    for (size_t i = 0; i < docs.size(); ++i) {
        const auto& b = docs[i].closeBounds;
        if (pt.x >= b.left - 4 && pt.x <= b.right + 4 && pt.y >= b.top - 4 && pt.y <= b.bottom + 4) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool EditorCanvas::HitTestScrollbar(POINT pt, const editor::OpenDocument& doc, bool& outHitThumb) const {
    outHitThumb = false;
    if (doc.scrollbarTrack.right > doc.scrollbarTrack.left) {
        if (pt.x >= doc.scrollbarTrack.left && pt.x <= doc.scrollbarTrack.right &&
            pt.y >= doc.scrollbarTrack.top && pt.y <= doc.scrollbarTrack.bottom) {
            if (pt.x >= doc.scrollbarThumb.left && pt.x <= doc.scrollbarThumb.right &&
                pt.y >= doc.scrollbarThumb.top && pt.y <= doc.scrollbarThumb.bottom) {
                outHitThumb = true;
            }
            return true;
        }
    }
    return false;
}

bool EditorCanvas::HitTestHScrollbar(POINT pt, const editor::OpenDocument& doc, bool& outHitThumb) const {
    outHitThumb = false;
    if (doc.hScrollbarTrack.right > doc.hScrollbarTrack.left) {
        float px = static_cast<float>(pt.x);
        float py = static_cast<float>(pt.y);
        if (px >= doc.hScrollbarTrack.left && px <= doc.hScrollbarTrack.right &&
            py >= doc.hScrollbarTrack.top && py <= doc.hScrollbarTrack.bottom) {
            if (px >= doc.hScrollbarThumb.left && px <= doc.hScrollbarThumb.right) {
                outHitThumb = true;
            }
            return true;
        }
    }
    return false;
}

bool EditorCanvas::HitTestMinimap(POINT pt, const editor::OpenDocument& doc, bool& outHitSlider) const {
    outHitSlider = false;
    if (doc.minimapTrack.right <= doc.minimapTrack.left) return false;
    if (pt.x >= doc.minimapTrack.left && pt.x <= doc.minimapTrack.right &&
        pt.y >= doc.minimapTrack.top && pt.y <= doc.minimapTrack.bottom) {
        if (pt.x >= doc.minimapSlider.left && pt.x <= doc.minimapSlider.right &&
            pt.y >= doc.minimapSlider.top && pt.y <= doc.minimapSlider.bottom) {
            outHitSlider = true;
        }
        return true;
    }
    return false;
}

int EditorCanvas::HitTestFoldChevron(POINT pt) const {
    for (const auto& hit : m_foldChevrons) {
        if (pt.x >= hit.bounds.left - 4 && pt.x <= hit.bounds.right + 4 &&
            pt.y >= hit.bounds.top - 2 && pt.y <= hit.bounds.bottom + 2) {
            return static_cast<int>(hit.bufferLine);
        }
    }
    return -1;
}

bool EditorCanvas::HitTestGutter(POINT pt, const app::AppConfig& config, const editor::OpenDocument& doc,
                                 float canvasLeft, float clientHeight, size_t& outLine) const {
    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float tabH    = config.ScaleF(34.0f);
    const float breadcrumbH = config.ScaleF(24.0f);
    const float topY    = headerH + tabH + breadcrumbH;
    const float gutterW = config.ScaleF(50.0f);
    const float lineH   = config.ScaleF(std::round(config.fontSize * config.lineHeightMultiplier));

    if (pt.x < canvasLeft || pt.x >= canvasLeft + gutterW || pt.y < topY || pt.y > clientHeight) {
        return false;
    }

    float relY = pt.y - (topY + config.ScaleF(6.0f));
    if (relY < 0.0f || lineH <= 0.0f) {
        return false;
    }

    size_t visualSlot = doc.scrollLine + static_cast<size_t>(relY / lineH);
    const auto& buffer = doc.buffer;
    size_t totalBufLines = buffer.GetLineCount();
    size_t curVisual = 0;
    size_t mappedLine = 0;
    bool found = false;

    for (size_t b = 0; b < totalBufLines; ++b) {
        size_t foldEnd = 0;
        bool canFold = GetFoldRange(buffer, b, foldEnd);
        bool isFolded = canFold && (doc.foldedLines.count(b) > 0);

        // Virtual CodeLens line directly above functions & tests
        if (config.enableCodeLens && !doc.isDiffView && IsFunctionOrTestLine(buffer.GetLine(b))) {
            if (curVisual == visualSlot) {
                // Click was on the CodeLens header line, not the code line
                return false;
            }
            curVisual++;
        }

        if (curVisual == visualSlot) {
            mappedLine = b;
            found = true;
            break;
        }
        curVisual++;

        // Virtual prediction continuation lines (Zed-style virtual space)
        if (m_prediction.isValid && m_prediction.line == b && !m_prediction.ghostText.empty() && !IsAutocompleteVisible()) {
            size_t pLines = 0;
            for (wchar_t ch : m_prediction.ghostText) {
                if (ch == L'\n') pLines++;
            }
            if (pLines > 0) {
                if (visualSlot >= curVisual && visualSlot < curVisual + pLines) {
                    // Click was in gutter of a virtual prediction line
                    return false;
                }
                curVisual += pLines;
            }
        }

        if (isFolded) {
            b = foldEnd;
        }
        mappedLine = b;
    }

    if (found) {
        outLine = mappedLine;
        return true;
    }
    return false;
}

bool EditorCanvas::HitTestCodeArea(POINT pt, const app::AppConfig& config, const editor::OpenDocument& doc,
                                   float canvasLeft, float clientHeight, size_t& outLine, size_t& outCol) const {
    const float headerH = config.ScaleF(static_cast<float>(config.headerHeight));
    const float tabH    = config.ScaleF(34.0f);
    const float breadcrumbH = config.ScaleF(24.0f);
    const float topY    = headerH + tabH + breadcrumbH;
    const float gutterW = config.ScaleF(50.0f);
    const float lineH   = config.ScaleF(std::round(config.fontSize * config.lineHeightMultiplier));
    const float charW   = (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.8f);

    if (pt.x < canvasLeft + gutterW || pt.y < topY || pt.y > clientHeight) {
        return false;
    }

    float relY = pt.y - (topY + config.ScaleF(6.0f));
    if (relY < 0.0f) relY = 0.0f;
    size_t visualSlot = doc.scrollLine + static_cast<size_t>(relY / lineH);

    // Map visual slot back to buffer line taking fold state into account
    auto& buffer = doc.buffer;
    size_t totalBufLines = buffer.GetLineCount();
    size_t curVisual = 0;
    size_t mappedLine = 0;

    for (size_t b = 0; b < totalBufLines; ++b) {
        size_t foldEnd = 0;
        bool canFold = GetFoldRange(buffer, b, foldEnd);
        bool isFolded = canFold && (doc.foldedLines.count(b) > 0);

        if (config.enableCodeLens && !doc.isDiffView && IsFunctionOrTestLine(buffer.GetLine(b))) {
            if (curVisual == visualSlot) {
                mappedLine = b;
                break;
            }
            curVisual++;
        }

        if (curVisual == visualSlot) {
            mappedLine = b;
            break;
        }
        curVisual++;

        // Virtual prediction continuation lines (Zed-style virtual space)
        if (m_prediction.isValid && m_prediction.line == b && !m_prediction.ghostText.empty() && !IsAutocompleteVisible()) {
            size_t pLines = 0;
            for (wchar_t ch : m_prediction.ghostText) {
                if (ch == L'\n') pLines++;
            }
            if (pLines > 0) {
                if (visualSlot >= curVisual && visualSlot < curVisual + pLines) {
                    // Click was on one of the virtual prediction lines
                    mappedLine = b;
                    break;
                }
                curVisual += pLines;
            }
        }
        if (isFolded) {
            b = foldEnd;
        }
        mappedLine = b;
    }

    const auto& lineStr = buffer.GetLine(mappedLine);
    const float swatchSpan = config.ScaleF(14.0f);
    float relX = pt.x - (canvasLeft + gutterW + config.ScaleF(12.0f)) + doc.scrollOffsetX;
    if (relX < 0.0f) relX = 0.0f;

    auto lineColors = ScanLineColors(lineStr);
    size_t col = 0;
    float bestDiff = 999999.0f;
    for (size_t c = 0; c <= lineStr.length(); ++c) {
        float vx = GetColumnVisualOffset(lineColors, c, charW, swatchSpan);
        float diff = std::abs(relX - vx);
        if (diff < bestDiff) {
            bestDiff = diff;
            col = c;
        }
    }

    if (mappedLine >= buffer.GetLineCount()) {
        mappedLine = buffer.GetLineCount() > 0 ? buffer.GetLineCount() - 1 : 0;
    }
    if (col > lineStr.length()) {
        col = lineStr.length();
    }

    outLine = mappedLine;
    outCol  = col;
    return true;
}

int EditorCanvas::HitTestCodeLens(POINT pt) const noexcept {
    for (size_t i = 0; i < m_activeCodeLensActions.size(); ++i) {
        const auto& act = m_activeCodeLensActions[i];
        if (pt.x >= act.bounds.left && pt.x <= act.bounds.right &&
            pt.y >= act.bounds.top && pt.y <= act.bounds.bottom) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool EditorCanvas::UpdateCodeLensHover(POINT pt) {
    int oldHover = m_hoveredCodeLensIdx;
    m_hoveredCodeLensIdx = HitTestCodeLens(pt);
    return m_hoveredCodeLensIdx != oldHover;
}

void EditorCanvas::RenderImageView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                   editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                   float topY, float clientHeight) {
    if (!pRT) return;

    // Load bitmap using WIC if not yet loaded
    if (!doc.pImageBitmap && !doc.filePath.empty()) {
        Microsoft::WRL::ComPtr<IWICImagingFactory> pWicFactory;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&pWicFactory)
        );
        if (SUCCEEDED(hr) && pWicFactory) {
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDecoder;
            hr = pWicFactory->CreateDecoderFromFilename(
                doc.filePath.c_str(),
                nullptr,
                GENERIC_READ,
                WICDecodeMetadataCacheOnDemand,
                &pDecoder
            );
            if (SUCCEEDED(hr) && pDecoder) {
                Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
                hr = pDecoder->GetFrame(0, &pFrame);
                if (SUCCEEDED(hr) && pFrame) {
                    pFrame->GetSize(&doc.imageWidth, &doc.imageHeight);

                    Microsoft::WRL::ComPtr<IWICFormatConverter> pConverter;
                    hr = pWicFactory->CreateFormatConverter(&pConverter);
                    if (SUCCEEDED(hr) && pConverter) {
                        hr = pConverter->Initialize(
                            pFrame.Get(),
                            GUID_WICPixelFormat32bppPBGRA,
                            WICBitmapDitherTypeNone,
                            nullptr,
                            0.0,
                            WICBitmapPaletteTypeCustom
                        );
                        if (SUCCEEDED(hr)) {
                            pRT->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &doc.pImageBitmap);
                        }
                    }
                }
            }
        }
        if (doc.imageFileSize == 0 && !doc.filePath.empty()) {
            std::error_code ec;
            doc.imageFileSize = std::filesystem::file_size(doc.filePath, ec);
        }
    }

    const float availW = canvasWidth - config.ScaleF(80.0f);
    const float availH = (clientHeight - topY) - config.ScaleF(100.0f);

    if (doc.pImageBitmap && doc.imageWidth > 0 && doc.imageHeight > 0 && availW > 10.0f && availH > 10.0f) {
        float scale = 1.0f;
        if (static_cast<float>(doc.imageWidth) > availW || static_cast<float>(doc.imageHeight) > availH) {
            float scaleX = availW / static_cast<float>(doc.imageWidth);
            float scaleY = availH / static_cast<float>(doc.imageHeight);
            scale = (std::min)(scaleX, scaleY);
        }

        const float drawW = static_cast<float>(doc.imageWidth) * scale;
        const float drawH = static_cast<float>(doc.imageHeight) * scale;
        const float drawX = canvasLeft + (canvasWidth - drawW) / 2.0f;
        const float drawY = topY + ((clientHeight - topY) - drawH) / 2.0f - config.ScaleF(20.0f);

        D2D1_RECT_F imgRect = D2D1::RectF(drawX, drawY, drawX + drawW, drawY + drawH);

        // Subtle frame around image
        D2D1_RECT_F borderRect = D2D1::RectF(drawX - 1.0f, drawY - 1.0f, drawX + drawW + 1.0f, drawY + drawH + 1.0f);
        pRT->DrawRectangle(borderRect, theme.brushBorderSubtle.Get(), 1.0f);

        // Draw image bitmap
        pRT->DrawBitmap(doc.pImageBitmap.Get(), imgRect);

        // Format metadata info
        std::wstring ext;
        auto dotPos = doc.filePath.find_last_of(L".");
        if (dotPos != std::wstring::npos) {
            ext = doc.filePath.substr(dotPos + 1);
            for (auto& ch : ext) ch = towupper(ch);
        } else {
            ext = L"IMAGE";
        }

        std::wstring sizeStr;
        if (doc.imageFileSize < 1024) {
            sizeStr = std::to_wstring(doc.imageFileSize) + L" B";
        } else if (doc.imageFileSize < 1024 * 1024) {
            sizeStr = std::to_wstring(doc.imageFileSize / 1024) + L" KB";
        } else {
            sizeStr = std::to_wstring(doc.imageFileSize / (1024 * 1024)) + L" MB";
        }

        std::wstring infoText = std::to_wstring(doc.imageWidth) + L" \u00D7 " +
                                std::to_wstring(doc.imageHeight) + L" px   |   " +
                                ext + L"   |   " + sizeStr;

        const float badgeH = config.ScaleF(26.0f);
        const float badgeW = config.ScaleF(250.0f);
        const float badgeX = canvasLeft + (canvasWidth - badgeW) / 2.0f;
        const float badgeY = clientHeight - badgeH - config.ScaleF(16.0f);

        D2D1_ROUNDED_RECT rcBadge = D2D1::RoundedRect(
            D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH),
            config.ScaleF(13.0f), config.ScaleF(13.0f)
        );
        pRT->FillRoundedRectangle(rcBadge, theme.brushItemHover.Get());
        pRT->DrawRoundedRectangle(rcBadge, theme.brushBorderSubtle.Get(), 1.0f);

        D2D1_RECT_F rcText = rcBadge.rect;
        IDWriteTextFormat* pFmt = theme.fontMenu ? theme.fontMenu.Get() : theme.fontCode.Get();
        if (pFmt) {
            DWRITE_TEXT_ALIGNMENT oldAlign = pFmt->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT oldPara = pFmt->GetParagraphAlignment();
            pFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(infoText.c_str(), static_cast<UINT32>(infoText.length()),
                          pFmt, rcText, theme.brushTextSecondary.Get());
            pFmt->SetTextAlignment(oldAlign);
            pFmt->SetParagraphAlignment(oldPara);
        }
    } else {
        std::wstring msg = L"Unable to display image: " + doc.fileName;
        D2D1_RECT_F rcMsg = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, clientHeight);
        IDWriteTextFormat* pFmt = theme.fontMenu ? theme.fontMenu.Get() : theme.fontCode.Get();
        if (pFmt) {
            pRT->DrawText(msg.c_str(), static_cast<UINT32>(msg.length()), pFmt, rcMsg, theme.brushTextMuted.Get());
        }
    }
}

/**
 * @brief Renders the unsupported binary file view (red circled X, center text, subheader) matching Image 2.
 */
void EditorCanvas::RenderUnsupportedBinaryView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                              editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                              float topY, float clientHeight) {
    if (doc.binaryFileSize == 0 && !doc.filePath.empty()) {
        std::error_code ec;
        doc.binaryFileSize = std::filesystem::file_size(doc.filePath, ec);
    }

    const float availH = clientHeight - topY;
    const float centerY = topY + availH * 0.42f;
    const float centerX = canvasLeft + canvasWidth / 2.0f;

    // 1. Red circled X icon (as requested: "have X incircle red or anything")
    const float circleR = config.ScaleF(28.0f);
    D2D1_ELLIPSE redCircle = D2D1::Ellipse(D2D1::Point2F(centerX, centerY - config.ScaleF(50.0f)), circleR, circleR);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushRedBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushRedBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushRedCross;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.22f, 0.22f, 0.16f), &brushRedBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.22f, 0.22f, 0.90f), &brushRedBorder);
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f) : D2D1::ColorF(0.90f, 0.18f, 0.18f, 1.0f), &brushRedCross);

    if (brushRedBg) pRT->FillEllipse(redCircle, brushRedBg.Get());
    if (brushRedBorder) pRT->DrawEllipse(redCircle, brushRedBorder.Get(), 2.0f);

    if (brushRedCross) {
        float cr = config.ScaleF(10.0f);
        float ccx = redCircle.point.x;
        float ccy = redCircle.point.y;
        pRT->DrawLine(D2D1::Point2F(ccx - cr, ccy - cr), D2D1::Point2F(ccx + cr, ccy + cr), brushRedCross.Get(), 2.4f);
        pRT->DrawLine(D2D1::Point2F(ccx + cr, ccy - cr), D2D1::Point2F(ccx - cr, ccy + cr), brushRedCross.Get(), 2.4f);
    }

    // 2. Bold Headline: "The file cannot be opened in the editor"
    const wchar_t* headline = L"The file cannot be opened in the editor";
    const float headlineW = config.ScaleF(460.0f);
    const float headlineH = config.ScaleF(28.0f);
    D2D1_RECT_F rcHeadline = D2D1::RectF(
        centerX - headlineW / 2.0f,
        centerY - config.ScaleF(6.0f),
        centerX + headlineW / 2.0f,
        centerY + headlineH - config.ScaleF(6.0f)
    );
    IDWriteTextFormat* pHeadFmt = theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontBody.Get();
    if (pHeadFmt) {
        DWRITE_TEXT_ALIGNMENT oldAlign = pHeadFmt->GetTextAlignment();
        pHeadFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRT->DrawText(headline, static_cast<UINT32>(wcslen(headline)), pHeadFmt, rcHeadline, theme.brushTextPrimary.Get());
        pHeadFmt->SetTextAlignment(oldAlign);
    }

    // 3. Sub-header: "This file is unsupported because it is binary or an executable file."
    const wchar_t* subheader = L"This file is unsupported because it is binary or an executable file.";
    const float subH = config.ScaleF(24.0f);
    D2D1_RECT_F rcSub = D2D1::RectF(
        centerX - headlineW / 2.0f,
        rcHeadline.bottom + config.ScaleF(4.0f),
        centerX + headlineW / 2.0f,
        rcHeadline.bottom + config.ScaleF(4.0f) + subH
    );
    if (theme.fontBody) {
        DWRITE_TEXT_ALIGNMENT oldAlign = theme.fontBody->GetTextAlignment();
        theme.fontBody->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRT->DrawText(subheader, static_cast<UINT32>(wcslen(subheader)), theme.fontBody.Get(), rcSub, theme.brushTextSecondary.Get());
        theme.fontBody->SetTextAlignment(oldAlign);
    }

    // 4. File metadata pill (e.g. coordinator.exe | Binary File | 1.42 MB)
    std::wstring sizeStr;
    if (doc.binaryFileSize < 1024) {
        sizeStr = std::to_wstring(doc.binaryFileSize) + L" B";
    } else if (doc.binaryFileSize < 1024 * 1024) {
        sizeStr = std::to_wstring(doc.binaryFileSize / 1024) + L" KB";
    } else {
        sizeStr = std::to_wstring(doc.binaryFileSize / (1024 * 1024)) + L" MB";
    }

    std::wstring typeStr = doc.binaryFileType.empty() ? L"Binary File" : doc.binaryFileType;
    std::wstring pillText = doc.fileName + L"   |   " + typeStr + L"   |   " + sizeStr;

    const float pillH = config.ScaleF(24.0f);
    const float pillW = config.ScaleF(static_cast<float>(pillText.length() * 7.0f + 24.0f));
    const float pillX = centerX - pillW / 2.0f;
    const float pillY = rcSub.bottom + config.ScaleF(14.0f);

    D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(
        D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH),
        config.ScaleF(12.0f), config.ScaleF(12.0f)
    );
    pRT->FillRoundedRectangle(rrcPill, theme.brushItemHover.Get());
    pRT->DrawRoundedRectangle(rrcPill, theme.brushBorderSubtle.Get(), 0.8f);

    IDWriteTextFormat* pMenuFmt = theme.fontMenu ? theme.fontMenu.Get() : theme.fontCode.Get();
    if (pMenuFmt) {
        DWRITE_TEXT_ALIGNMENT oldAlign = pMenuFmt->GetTextAlignment();
        pMenuFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRT->DrawText(pillText.c_str(), static_cast<UINT32>(pillText.length()), pMenuFmt, rrcPill.rect, theme.brushTextSecondary.Get());
        pMenuFmt->SetTextAlignment(oldAlign);
    }

    // 5. Action Button: "Reveal in File Explorer"
    const float btnW = config.ScaleF(170.0f);
    const float btnH = config.ScaleF(32.0f);
    const float btnX = centerX - btnW / 2.0f;
    const float btnY = pillY + pillH + config.ScaleF(20.0f);

    doc.unsupportedActionBtn = D2D1::RectF(btnX, btnY, btnX + btnW, btnY + btnH);
    D2D1_ROUNDED_RECT rrcBtn = D2D1::RoundedRect(doc.unsupportedActionBtn, 4.0f, 4.0f);
    pRT->FillRoundedRectangle(rrcBtn, theme.brushBrandAccent.Get());

    const wchar_t* btnLabel = L"Reveal in File Explorer";
    pRT->DrawText(btnLabel, static_cast<UINT32>(wcslen(btnLabel)),
                  theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(),
                  doc.unsupportedActionBtn, theme.brushTextWhite.Get());
}

/**
 * @brief Renders the Student Tutorials & Learning Hub view (Student hat 🎓) with modern Academy layout.
 */
void EditorCanvas::InitializeLearningCourses() {
    if (!m_learningCourses.empty()) return;

    // Row 1: Python
    {
        LearningCourseDef c;
        c.id = L"python-1";
        c.category = L"Python";
        c.imageFile = L"course-python-1.png";
        c.creator = L"Koda";
        c.title = L"Python for Beginners";
        c.description = L"Learn Python from scratch with simple examples and hands-on exercises.";
        c.lessons = 24;
        c.hours = 6;
        c.difficulty = L"Beginner";
        c.badgeColor = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"python-2";
        c.category = L"Python";
        c.imageFile = L"course-python-2.png";
        c.creator = L"Koda";
        c.title = L"DSA with Python";
        c.description = L"Master data structures and algorithms using Python with step-by-step explanations.";
        c.lessons = 32;
        c.hours = 10;
        c.difficulty = L"Intermediate";
        c.badgeColor = D2D1::ColorF(0.231f, 0.600f, 0.980f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"python-3";
        c.category = L"Python";
        c.imageFile = L"course-python-3.png";
        c.creator = L"Koda";
        c.title = L"Machine Learning with Python";
        c.description = L"Learn machine learning concepts and build real projects using Python.";
        c.lessons = 28;
        c.hours = 12;
        c.difficulty = L"Intermediate";
        c.badgeColor = D2D1::ColorF(0.231f, 0.600f, 0.980f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"python-4";
        c.category = L"Python";
        c.imageFile = L"course-python-4.png";
        c.creator = L"Koda";
        c.title = L"Introduction to Python";
        c.description = L"Understand Python syntax, variables, functions, and more with practical examples.";
        c.lessons = 20;
        c.hours = 5;
        c.difficulty = L"Beginner";
        c.badgeColor = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        m_learningCourses.push_back(c);
    }

    // Row 2: JavaScript
    {
        LearningCourseDef c;
        c.id = L"javascript-1";
        c.category = L"JavaScript";
        c.imageFile = L"course-javascript-1.png";
        c.creator = L"Koda";
        c.title = L"JavaScript Beginner";
        c.description = L"Learn JavaScript fundamentals with interactive examples.";
        c.lessons = 26;
        c.hours = 6;
        c.difficulty = L"Beginner";
        c.badgeColor = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"javascript-2";
        c.category = L"JavaScript";
        c.imageFile = L"course-javascript-2.png";
        c.creator = L"Koda";
        c.title = L"Introduction to JavaScript";
        c.description = L"Understand the core concepts of JavaScript and how it works in web development.";
        c.lessons = 24;
        c.hours = 6;
        c.difficulty = L"Beginner";
        c.badgeColor = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"javascript-3";
        c.category = L"JavaScript";
        c.imageFile = L"course-javascript-3.png";
        c.creator = L"Koda";
        c.title = L"Graphics with JavaScript";
        c.description = L"Create interactive graphics and visualizations using JavaScript.";
        c.lessons = 18;
        c.hours = 5;
        c.difficulty = L"Intermediate";
        c.badgeColor = D2D1::ColorF(0.231f, 0.600f, 0.980f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"javascript-4";
        c.category = L"JavaScript";
        c.imageFile = L"course-javascript-4.png";
        c.creator = L"Koda";
        c.title = L"Advanced JavaScript";
        c.description = L"Deep dive into advanced JavaScript concepts, patterns, and real-world applications.";
        c.lessons = 30;
        c.hours = 10;
        c.difficulty = L"Advanced";
        c.badgeColor = D2D1::ColorF(0.957f, 0.247f, 0.369f, 1.0f);
        m_learningCourses.push_back(c);
    }

    // Row 3: PHP
    {
        LearningCourseDef c;
        c.id = L"php-1";
        c.category = L"PHP";
        c.imageFile = L"course-php-1.png";
        c.creator = L"Koda";
        c.title = L"Beginner PHP";
        c.description = L"Learn PHP basics and build simple dynamic web applications.";
        c.lessons = 24;
        c.hours = 6;
        c.difficulty = L"Beginner";
        c.badgeColor = D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"php-2";
        c.category = L"PHP";
        c.imageFile = L"course-php-2.png";
        c.creator = L"Koda";
        c.title = L"Intermediate PHP";
        c.description = L"Work with databases, sessions, and more advanced PHP features.";
        c.lessons = 28;
        c.hours = 8;
        c.difficulty = L"Intermediate";
        c.badgeColor = D2D1::ColorF(0.231f, 0.600f, 0.980f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"php-3";
        c.category = L"PHP";
        c.imageFile = L"course-php-3.png";
        c.creator = L"Koda";
        c.title = L"Advanced PHP";
        c.description = L"Master advanced PHP concepts, security, and performance optimization.";
        c.lessons = 32;
        c.hours = 12;
        c.difficulty = L"Advanced";
        c.badgeColor = D2D1::ColorF(0.957f, 0.247f, 0.369f, 1.0f);
        m_learningCourses.push_back(c);
    }
    {
        LearningCourseDef c;
        c.id = L"php-4";
        c.category = L"PHP";
        c.imageFile = L"course-php-4.png";
        c.creator = L"Koda";
        c.title = L"PHP REST APIs & Microservices";
        c.description = L"Build robust, production-ready RESTful APIs, JWT auth, and microservices architecture.";
        c.lessons = 26;
        c.hours = 8;
        c.difficulty = L"Intermediate";
        c.badgeColor = D2D1::ColorF(0.231f, 0.600f, 0.980f, 1.0f);
        m_learningCourses.push_back(c);
    }
}

ID2D1Bitmap* EditorCanvas::GetLearningHubBitmap(ID2D1RenderTarget* pRT, const std::wstring& filename) {
    if (!pRT || filename.empty()) return nullptr;
    auto it = m_learningHubBitmaps.find(filename);
    if (it != m_learningHubBitmaps.end() && it->second) {
        return it->second.Get();
    }

    std::vector<std::wstring> candidates = {
        L"assets/learning-hub-assets/" + filename,
        L"bin/assets/learning-hub-assets/" + filename,
        L"../assets/learning-hub-assets/" + filename,
        L"assets/" + filename,
        L"bin/assets/" + filename,
        L"../assets/" + filename,
        filename
    };

    wchar_t modPath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, modPath, MAX_PATH)) {
        std::filesystem::path exeDir = std::filesystem::path(modPath).parent_path();
        candidates.push_back((exeDir / L"assets" / L"learning-hub-assets" / filename).wstring());
        candidates.push_back((exeDir / L".." / L"assets" / L"learning-hub-assets" / filename).wstring());
        candidates.push_back((exeDir / L"assets" / filename).wstring());
        candidates.push_back((exeDir / L".." / L"assets" / filename).wstring());
    }

    std::wstring foundPath = L"";
    for (const auto& c : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec)) {
            foundPath = c;
            break;
        }
    }
    if (foundPath.empty()) return nullptr;

    Microsoft::WRL::ComPtr<IWICImagingFactory> pWicFactory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pWicFactory)
    );
    if (FAILED(hr) || !pWicFactory) return nullptr;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDecoder;
    hr = pWicFactory->CreateDecoderFromFilename(
        foundPath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &pDecoder
    );
    if (FAILED(hr) || !pDecoder) return nullptr;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr) || !pFrame) return nullptr;

    Microsoft::WRL::ComPtr<IWICFormatConverter> pConverter;
    hr = pWicFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr) || !pConverter) return nullptr;

    hr = pConverter->Initialize(
        pFrame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr)) return nullptr;

    Microsoft::WRL::ComPtr<ID2D1Bitmap> pBmp;
    hr = pRT->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &pBmp);
    if (SUCCEEDED(hr) && pBmp) {
        m_learningHubBitmaps[filename] = pBmp;
        return pBmp.Get();
    }
    return nullptr;
}

/**
 * @brief Renders the Student Tutorials & Learning Hub view (Student hat 🎓) matching the modern Hub design.
 */
void EditorCanvas::RenderStudentView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                     float topY, float clientHeight) {
    if (!pRT) return;

    InitializeLearningCourses();
    m_renderedCourseCards.clear();

    const float viewW = canvasWidth;
    const float viewH = clientHeight - topY;
    if (viewW <= 10.0f || viewH <= 10.0f) return;

    D2D1_RECT_F rcClip = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, clientHeight);
    pRT->PushAxisAlignedClip(rcClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // 1. Deep charcoal-black background matching design (#0A0D12)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.040f, 0.052f, 0.070f, 1.0f), &bgBrush);
    if (bgBrush) {
        pRT->FillRectangle(rcClip, bgBrush.Get());
    }

    // DirectWrite Factory for typography
    Microsoft::WRL::ComPtr<IDWriteFactory> pDWrite;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &pDWrite);

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtTitle;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtSub;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtSearch;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtPill;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCardKoda;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCardTitle;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCardDesc;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCardMeta;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtCardBadge;

    if (pDWrite) {
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(24.0f), L"en-us", &fmtTitle);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(11.5f), L"en-us", &fmtSub);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(11.0f), L"en-us", &fmtSearch);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(10.5f), L"en-us", &fmtPill);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(9.5f), L"en-us", &fmtCardKoda);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(11.5f), L"en-us", &fmtCardTitle);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(9.2f), L"en-us", &fmtCardDesc);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(9.0f), L"en-us", &fmtCardMeta);
        pDWrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(8.5f), L"en-us", &fmtCardBadge);

        if (fmtCardBadge) {
            fmtCardBadge->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmtCardBadge->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // Color Brushes
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushWhite;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSub;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushMeta;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBorderHover;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSearchBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSearchBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSearchBorderActive;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushGreenAccent;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillActiveBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillActiveBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillActiveText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillInactiveBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillInactiveBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillInactiveText;

    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brushWhite);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.62f, 0.70f, 1.0f), &brushSub);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.42f, 0.49f, 0.58f, 1.0f), &brushMeta);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.067f, 0.086f, 0.122f, 1.0f), &brushCardBg); // #11161F
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.110f, 0.145f, 0.190f, 1.0f), &brushCardBorder);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.180f, 0.240f, 0.320f, 1.0f), &brushCardBorderHover);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.070f, 0.090f, 0.120f, 1.0f), &brushSearchBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.130f, 0.170f, 0.220f, 1.0f), &brushSearchBorder);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f), &brushSearchBorderActive);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f), &brushGreenAccent);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.043f, 0.188f, 0.137f, 1.0f), &brushPillActiveBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.063f, 0.725f, 0.506f, 1.0f), &brushPillActiveBorder);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.204f, 0.827f, 0.600f, 1.0f), &brushPillActiveText);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.067f, 0.086f, 0.122f, 1.0f), &brushPillInactiveBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.120f, 0.150f, 0.200f, 1.0f), &brushPillInactiveBorder);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.62f, 0.70f, 1.0f), &brushPillInactiveText);

    const float padX = config.ScaleF(10.0f);
    const float bannerTop = topY - m_learningScrollY + config.ScaleF(10.0f);
    const float bannerH = config.ScaleF(138.0f);
    const float bannerLeft = canvasLeft + padX;
    const float bannerRight = canvasLeft + canvasWidth - padX;
    D2D1_RECT_F rcHeaderBanner = D2D1::RectF(bannerLeft, bannerTop, bannerRight, bannerTop + bannerH);

    // 2. Header Banner: target wide banner with isometric green cubes on right and striped gradient on left
    ID2D1Bitmap* pBmpBanner = GetLearningHubBitmap(pRT, L"header-banner.png");
    if (!pBmpBanner) {
        pBmpBanner = GetLearningHubBitmap(pRT, L"banner-target.png");
    }
    if (pBmpBanner) {
        D2D1_SIZE_F bSize = pBmpBanner->GetSize();
        // Right cubes have source x: 500..783 (width: 283)
        float cubesSrcW = 283.0f;
        float cubesDestW = bannerH * (cubesSrcW / bSize.height);
        float bannerW = bannerRight - bannerLeft;
        if (bannerW > cubesDestW + config.ScaleF(80.0f)) {
            // Draw left background part (0..500)
            D2D1_RECT_F srcLeft = D2D1::RectF(0.0f, 0.0f, 500.0f, bSize.height);
            D2D1_RECT_F destLeft = D2D1::RectF(bannerLeft, bannerTop, bannerRight - cubesDestW, bannerTop + bannerH);
            pRT->DrawBitmap(pBmpBanner, destLeft, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcLeft);

            // Draw right cubes part (500..bSize.width)
            D2D1_RECT_F srcRight = D2D1::RectF(500.0f, 0.0f, bSize.width, bSize.height);
            D2D1_RECT_F destRight = D2D1::RectF(bannerRight - cubesDestW, bannerTop, bannerRight, bannerTop + bannerH);
            pRT->DrawBitmap(pBmpBanner, destRight, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcRight);
        } else {
            pRT->DrawBitmap(pBmpBanner, rcHeaderBanner, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    } else {
        // Fallback right image if header-banner is not present
        ID2D1Bitmap* pBmpRight = GetLearningHubBitmap(pRT, L"header-right.png");
        if (pBmpRight) {
            float bmpW = bannerH * (518.0f / 205.0f);
            D2D1_RECT_F rcRightImg = D2D1::RectF(bannerRight - bmpW, bannerTop, bannerRight, bannerTop + bannerH);
            pRT->DrawBitmap(pBmpRight, rcRightImg, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    }

    // 3. Header Title: "Learning Hub"
    D2D1_RECT_F rcTitle = D2D1::RectF(bannerLeft + config.ScaleF(18.0f), bannerTop + config.ScaleF(16.0f),
                                     bannerRight - config.ScaleF(260.0f), bannerTop + config.ScaleF(46.0f));
    const wchar_t* titleStr = L"Learning Hub";
    if (fmtTitle && brushWhite) {
        pRT->DrawText(titleStr, static_cast<UINT32>(wcslen(titleStr)), fmtTitle.Get(), rcTitle, brushWhite.Get());
    }

    // 4. Header Subtitle
    D2D1_RECT_F rcSub = D2D1::RectF(bannerLeft + config.ScaleF(18.0f), rcTitle.bottom + config.ScaleF(2.0f),
                                   bannerRight - config.ScaleF(260.0f), rcTitle.bottom + config.ScaleF(28.0f));
    const wchar_t* subStr = L"Step-by-step programming courses with hands-on examples, projects, and real-world skills.";
    if (fmtSub && brushSub) {
        pRT->DrawText(subStr, static_cast<UINT32>(wcslen(subStr)), fmtSub.Get(), rcSub, brushSub.Get());
    }

    // 5. Header Search Input Box
    const float searchW = (std::min)(config.ScaleF(326.0f), bannerRight - bannerLeft - config.ScaleF(36.0f));
    const float searchH = config.ScaleF(28.0f);
    D2D1_RECT_F rcSearch = D2D1::RectF(bannerLeft + config.ScaleF(18.0f), bannerTop + config.ScaleF(86.0f),
                                       bannerLeft + config.ScaleF(18.0f) + searchW, bannerTop + config.ScaleF(86.0f) + searchH);
    m_learningSearchBounds = rcSearch;
    D2D1_ROUNDED_RECT rrcSearch = D2D1::RoundedRect(rcSearch, config.ScaleF(5.0f), config.ScaleF(5.0f));

    if (brushSearchBg) pRT->FillRoundedRectangle(rrcSearch, brushSearchBg.Get());
    ID2D1Brush* pSearchBrd = m_isLearningSearchFocused ? brushSearchBorderActive.Get() :
                             (m_hoveredLearningSearch ? brushCardBorderHover.Get() : brushSearchBorder.Get());
    if (pSearchBrd) pRT->DrawRoundedRectangle(rrcSearch, pSearchBrd, 1.0f);

    // Magnifier Search Icon inside input
    const float searchIconSize = config.ScaleF(12.0f);
    D2D1_RECT_F rcSearchIcon = D2D1::RectF(rcSearch.left + config.ScaleF(8.0f),
                                          rcSearch.top + (searchH - searchIconSize) / 2.0f,
                                          rcSearch.left + config.ScaleF(8.0f) + searchIconSize,
                                          rcSearch.top + (searchH + searchIconSize) / 2.0f);
    m_icons.DrawSearch(pRT, rcSearchIcon, brushMeta ? brushMeta.Get() : brushSub.Get(), 1.4f);

    // Search query or placeholder
    D2D1_RECT_F rcSearchText = D2D1::RectF(rcSearchIcon.right + config.ScaleF(6.0f), rcSearch.top + config.ScaleF(5.0f),
                                          rcSearch.right - config.ScaleF(8.0f), rcSearch.bottom - config.ScaleF(5.0f));
    float textWidth = 0.0f;
    if (fmtSearch) {
        if (!m_learningSearchQuery.empty()) {
            pRT->DrawText(m_learningSearchQuery.c_str(), static_cast<UINT32>(m_learningSearchQuery.length()),
                          fmtSearch.Get(), rcSearchText, brushWhite.Get());

            if (pDWrite) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
                if (SUCCEEDED(pDWrite->CreateTextLayout(
                    m_learningSearchQuery.c_str(),
                    static_cast<UINT32>(m_learningSearchQuery.length()),
                    fmtSearch.Get(),
                    rcSearchText.right - rcSearchText.left,
                    rcSearchText.bottom - rcSearchText.top,
                    &pLayout))) {
                    DWRITE_TEXT_METRICS metrics;
                    if (SUCCEEDED(pLayout->GetMetrics(&metrics))) {
                        textWidth = metrics.widthIncludingTrailingWhitespace;
                    }
                }
            }
        } else {
            const wchar_t* placeHolder = L"Search courses, e.g. Python, PHP, JavaScript...";
            pRT->DrawText(placeHolder, static_cast<UINT32>(wcslen(placeHolder)), fmtSearch.Get(), rcSearchText, brushMeta.Get());
        }
    }

    // Natural Blinking Caret when focused
    if (m_isLearningSearchFocused && m_caretVisible && brushWhite) {
        float caretX = rcSearchText.left + textWidth + config.ScaleF(1.0f);
        if (caretX <= rcSearch.right - config.ScaleF(4.0f)) {
            pRT->DrawLine(D2D1::Point2F(caretX, rcSearch.top + config.ScaleF(5.0f)),
                          D2D1::Point2F(caretX, rcSearch.bottom - config.ScaleF(5.0f)),
                          brushWhite.Get(), config.ScaleF(1.2f));
        }
    }

    // 6. Filter & Sort Bar
    const float filterY = bannerTop + bannerH + config.ScaleF(14.0f);
    const float filterH = config.ScaleF(24.0f);

    struct FilterBtn {
        const wchar_t* label;
        float width;
    };
    FilterBtn filters[5] = {
        { L"All", config.ScaleF(46.0f) },
        { L"Python", config.ScaleF(72.0f) },
        { L"JavaScript", config.ScaleF(90.0f) },
        { L"PHP", config.ScaleF(62.0f) },
        { L"Others", config.ScaleF(64.0f) }
    };

    float curFilterX = canvasLeft + padX;
    const float pillGap = config.ScaleF(8.0f);

    for (int i = 0; i < 5; ++i) {
        D2D1_RECT_F rcPill = D2D1::RectF(curFilterX, filterY, curFilterX + filters[i].width, filterY + filterH);
        m_learningFilterBounds[i] = rcPill;
        D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(rcPill, config.ScaleF(6.0f), config.ScaleF(6.0f));

        bool isActive = (m_learningActiveFilter == i);
        bool isHover = (m_hoveredLearningFilter == i);

        ID2D1Brush* pPillBg = isActive ? brushPillActiveBg.Get() : (isHover ? brushCardBorderHover.Get() : brushPillInactiveBg.Get());
        ID2D1Brush* pPillBorder = isActive ? brushPillActiveBorder.Get() : (isHover ? brushCardBorderHover.Get() : brushPillInactiveBorder.Get());
        ID2D1Brush* pPillText = isActive ? brushPillActiveText.Get() : (isHover ? brushWhite.Get() : brushPillInactiveText.Get());

        if (pPillBg) pRT->FillRoundedRectangle(rrcPill, pPillBg);
        if (pPillBorder) pRT->DrawRoundedRectangle(rrcPill, pPillBorder, isActive ? 1.2f : 1.0f);

        // Icon for category
        float iconBoxW = config.ScaleF(14.0f);
        D2D1_RECT_F rcPillIcon = D2D1::RectF(rcPill.left + config.ScaleF(6.0f), rcPill.top + (filterH - iconBoxW) / 2.0f,
                                            rcPill.left + config.ScaleF(6.0f) + iconBoxW, rcPill.top + (filterH + iconBoxW) / 2.0f);

        if (i == 0) { // All: 2x2 grid
            float sq = config.ScaleF(4.0f);
            float g = config.ScaleF(1.5f);
            float ox = rcPillIcon.left + config.ScaleF(2.0f);
            float oy = rcPillIcon.top + config.ScaleF(2.0f);
            if (pPillText) {
                pRT->FillRectangle(D2D1::RectF(ox, oy, ox + sq, oy + sq), pPillText);
                pRT->FillRectangle(D2D1::RectF(ox + sq + g, oy, ox + sq * 2.0f + g, oy + sq), pPillText);
                pRT->FillRectangle(D2D1::RectF(ox, oy + sq + g, ox + sq, oy + sq * 2.0f + g), pPillText);
                pRT->FillRectangle(D2D1::RectF(ox + sq + g, oy + sq + g, ox + sq * 2.0f + g, oy + sq * 2.0f + g), pPillText);
            }
        } else if (i == 1) { // Python: official icon from assets/python.png
            ID2D1Bitmap* pPyIcon = GetLearningHubBitmap(pRT, L"python.png");
            if (pPyIcon) {
                pRT->DrawBitmap(pPyIcon, rcPillIcon, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        } else if (i == 2) { // JavaScript: official icon from assets/javascript.png
            ID2D1Bitmap* pJsIcon = GetLearningHubBitmap(pRT, L"javascript.png");
            if (pJsIcon) {
                pRT->DrawBitmap(pJsIcon, rcPillIcon, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        } else if (i == 3) { // PHP: official icon from assets/php.png
            ID2D1Bitmap* pPhpIcon = GetLearningHubBitmap(pRT, L"php.png");
            if (pPhpIcon) {
                pRT->DrawBitmap(pPhpIcon, rcPillIcon, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
        } else if (i == 4) { // Others: 3 dots
            if (pPillText) {
                float dotY = (rcPillIcon.top + rcPillIcon.bottom) / 2.0f;
                float dotR = config.ScaleF(1.2f);
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rcPillIcon.left + config.ScaleF(3.0f), dotY), dotR, dotR), pPillText);
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rcPillIcon.left + config.ScaleF(7.0f), dotY), dotR, dotR), pPillText);
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rcPillIcon.left + config.ScaleF(11.0f), dotY), dotR, dotR), pPillText);
            }
        }

        // Label
        D2D1_RECT_F rcPillLabel = D2D1::RectF(rcPillIcon.right + config.ScaleF(4.0f), rcPill.top + config.ScaleF(4.0f),
                                             rcPill.right - config.ScaleF(6.0f), rcPill.bottom - config.ScaleF(4.0f));
        if (fmtPill && pPillText) {
            pRT->DrawText(filters[i].label, static_cast<UINT32>(wcslen(filters[i].label)), fmtPill.Get(), rcPillLabel, pPillText);
        }

        curFilterX += filters[i].width + pillGap;
    }

    // Sort Dropdown Pill on Right
    const float sortW = config.ScaleF(112.0f);
    D2D1_RECT_F rcSort = D2D1::RectF(canvasLeft + canvasWidth - padX - sortW, filterY,
                                    canvasLeft + canvasWidth - padX, filterY + filterH);
    m_learningSortBounds = rcSort;
    D2D1_ROUNDED_RECT rrcSort = D2D1::RoundedRect(rcSort, config.ScaleF(6.0f), config.ScaleF(6.0f));

    ID2D1Brush* pSortBg = m_hoveredLearningSort ? brushCardBorderHover.Get() : brushPillInactiveBg.Get();
    ID2D1Brush* pSortBorder = m_hoveredLearningSort ? brushCardBorderHover.Get() : brushPillInactiveBorder.Get();
    if (pSortBg) pRT->FillRoundedRectangle(rrcSort, pSortBg);
    if (pSortBorder) pRT->DrawRoundedRectangle(rrcSort, pSortBorder, 1.0f);

    const wchar_t* sortLabel = (m_learningSortOrder == 1) ? L"⇅ Most Lessons ˅" :
                               (m_learningSortOrder == 2) ? L"⇅ Duration ˅" : L"⇅ Most Popular ˅";
    D2D1_RECT_F rcSortLabel = D2D1::RectF(rcSort.left + config.ScaleF(6.0f), rcSort.top + config.ScaleF(4.0f),
                                         rcSort.right - config.ScaleF(6.0f), rcSort.bottom - config.ScaleF(4.0f));
    if (fmtPill && brushWhite) {
        DWRITE_TEXT_ALIGNMENT oldAlign = fmtPill->GetTextAlignment();
        fmtPill->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRT->DrawText(sortLabel, static_cast<UINT32>(wcslen(sortLabel)), fmtPill.Get(), rcSortLabel, brushWhite.Get());
        fmtPill->SetTextAlignment(oldAlign);
    }

    // 7. Course Cards Grid (4 Columns)
    std::vector<size_t> visibleCourses;
    for (size_t i = 0; i < m_learningCourses.size(); ++i) {
        const auto& c = m_learningCourses[i];
        if (m_learningActiveFilter == 1 && c.category != L"Python") continue;
        if (m_learningActiveFilter == 2 && c.category != L"JavaScript") continue;
        if (m_learningActiveFilter == 3 && c.category != L"PHP") continue;
        if (m_learningActiveFilter == 4 && c.category != L"Others") continue;

        if (!m_learningSearchQuery.empty()) {
            auto ToLower = [](const std::wstring& s) {
                std::wstring r = s;
                for (auto& ch : r) ch = towlower(ch);
                return r;
            };
            std::wstring q = ToLower(m_learningSearchQuery);
            if (ToLower(c.title).find(q) == std::wstring::npos &&
                ToLower(c.description).find(q) == std::wstring::npos &&
                ToLower(c.category).find(q) == std::wstring::npos) {
                continue;
            }
        }
        visibleCourses.push_back(i);
    }

    if (m_learningSortOrder == 1) {
        std::stable_sort(visibleCourses.begin(), visibleCourses.end(), [&](size_t a, size_t b) {
            return m_learningCourses[a].lessons > m_learningCourses[b].lessons;
        });
    } else if (m_learningSortOrder == 2) {
        std::stable_sort(visibleCourses.begin(), visibleCourses.end(), [&](size_t a, size_t b) {
            return m_learningCourses[a].hours > m_learningCourses[b].hours;
        });
    }

    const float gapX = config.ScaleF(8.0f);
    const float gapY = config.ScaleF(10.0f);
    const float totalGridW = canvasWidth - padX * 2.0f;

    // Proportional, non-stretching responsive grid (195px base width, 142px base height)
    int numCols = std::clamp(static_cast<int>((totalGridW + gapX) / (config.ScaleF(195.0f) + gapX)), 2, 8);
    const float cardW = (totalGridW - gapX * static_cast<float>(numCols - 1)) / static_cast<float>(numCols);
    const float cardH = cardW * (142.0f / 195.0f);
    const float cardBannerH = cardH * (62.0f / 142.0f);
    const float filterCardGap = config.ScaleF(18.0f);
    const float gridStartY = filterY + filterH + filterCardGap;

    auto DrawBookIcon = [&](const D2D1_RECT_F& r, ID2D1Brush* pBr) {
        if (!pBr) return;
        float midX = (r.left + r.right) / 2.0f;
        float botY = r.bottom - 1.0f;
        float tY = r.top + 1.0f;
        pRT->DrawLine(D2D1::Point2F(midX, botY), D2D1::Point2F(r.left, botY + 0.5f), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(r.left, botY + 0.5f), D2D1::Point2F(r.left, tY + 0.5f), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(r.left, tY + 0.5f), D2D1::Point2F(midX, tY), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(midX, tY), D2D1::Point2F(r.right, tY + 0.5f), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(r.right, tY + 0.5f), D2D1::Point2F(r.right, botY + 0.5f), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(r.right, botY + 0.5f), D2D1::Point2F(midX, botY), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(midX, tY), D2D1::Point2F(midX, botY), pBr, 1.1f);
    };

    auto DrawClockIcon = [&](const D2D1_RECT_F& r, ID2D1Brush* pBr) {
        if (!pBr) return;
        float cx = (r.left + r.right) / 2.0f;
        float cy = (r.top + r.bottom) / 2.0f;
        float rad = (std::min)(r.right - r.left, r.bottom - r.top) / 2.0f - 0.5f;
        pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(cx, cy), D2D1::Point2F(cx, cy - rad * 0.55f), pBr, 1.1f);
        pRT->DrawLine(D2D1::Point2F(cx, cy), D2D1::Point2F(cx + rad * 0.50f, cy), pBr, 1.1f);
    };

    ID2D1Bitmap* pBmpKodaLogo = GetLearningHubBitmap(pRT, L"header-logo.png");

    for (size_t vIdx = 0; vIdx < visibleCourses.size(); ++vIdx) {
        size_t origIdx = visibleCourses[vIdx];
        const auto& course = m_learningCourses[origIdx];

        int col = static_cast<int>(vIdx % numCols);
        int row = static_cast<int>(vIdx / numCols);
        float cx = canvasLeft + padX + static_cast<float>(col) * (cardW + gapX);
        float cy = gridStartY + static_cast<float>(row) * (cardH + gapY);

        D2D1_RECT_F rcCard = D2D1::RectF(cx, cy, cx + cardW, cy + cardH);
        m_renderedCourseCards.push_back({ origIdx, rcCard });

        // Backward compatibility bounds for the first 4 cards
        if (origIdx < 4) {
            m_studentCardBounds[origIdx] = rcCard;
            m_studentCardBtnBounds[origIdx] = rcCard;
        }

        bool isHovered = (m_hoveredLearningCard == static_cast<int>(origIdx));
        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, config.ScaleF(8.0f), config.ScaleF(8.0f));

        if (brushCardBg) pRT->FillRoundedRectangle(rrcCard, brushCardBg.Get());
        ID2D1Brush* pCBorder = isHovered ? brushCardBorderHover.Get() : brushCardBorder.Get();
        if (pCBorder) pRT->DrawRoundedRectangle(rrcCard, pCBorder, isHovered ? 1.2f : 1.0f);

        // Top Banner Image
        D2D1_RECT_F rcBanner = D2D1::RectF(cx + 0.5f, cy + 0.5f, cx + cardW - 0.5f, cy + cardBannerH);
        ID2D1Bitmap* pBannerBmp = GetLearningHubBitmap(pRT, course.imageFile);
        if (pBannerBmp) {
            pRT->DrawBitmap(pBannerBmp, rcBanner, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }

        // Koda Row
        const float kodaY = cy + cardBannerH + config.ScaleF(5.0f);
        const float kodaIconSize = config.ScaleF(10.0f);
        D2D1_RECT_F rcKodaIcon = D2D1::RectF(cx + config.ScaleF(8.0f), kodaY,
                                            cx + config.ScaleF(8.0f) + kodaIconSize, kodaY + kodaIconSize);
        if (pBmpKodaLogo) {
            pRT->DrawBitmap(pBmpKodaLogo, rcKodaIcon);
        } else if (brushGreenAccent) {
            pRT->FillRectangle(rcKodaIcon, brushGreenAccent.Get());
        }

        D2D1_RECT_F rcKodaText = D2D1::RectF(rcKodaIcon.right + config.ScaleF(3.0f), kodaY - config.ScaleF(1.0f),
                                            cx + cardW - config.ScaleF(8.0f), kodaY + config.ScaleF(11.0f));
        if (fmtCardKoda && brushMeta) {
            pRT->DrawText(course.creator.c_str(), static_cast<UINT32>(course.creator.length()),
                          fmtCardKoda.Get(), rcKodaText, brushMeta.Get());
        }

        // Course Title
        const float titleTop = kodaY + config.ScaleF(12.0f);
        D2D1_RECT_F rcCourseTitle = D2D1::RectF(cx + config.ScaleF(8.0f), titleTop,
                                               cx + cardW - config.ScaleF(8.0f), titleTop + config.ScaleF(16.0f));
        if (fmtCardTitle && brushWhite) {
            pRT->DrawText(course.title.c_str(), static_cast<UINT32>(course.title.length()),
                          fmtCardTitle.Get(), rcCourseTitle, brushWhite.Get());
        }

        // Course Description (2 lines wrapped)
        const float descTop = titleTop + config.ScaleF(15.0f);
        D2D1_RECT_F rcCourseDesc = D2D1::RectF(cx + config.ScaleF(8.0f), descTop,
                                              cx + cardW - config.ScaleF(8.0f), descTop + config.ScaleF(28.0f));
        if (fmtCardDesc && brushSub) {
            DWRITE_WORD_WRAPPING oldWrap = fmtCardDesc->GetWordWrapping();
            fmtCardDesc->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            pRT->DrawText(course.description.c_str(), static_cast<UINT32>(course.description.length()),
                          fmtCardDesc.Get(), rcCourseDesc, brushSub.Get());
            fmtCardDesc->SetWordWrapping(oldWrap);
        }

        // Footer Row
        const float footerTop = cy + cardH - config.ScaleF(18.0f);

        // Book icon & lessons
        D2D1_RECT_F rcBook = D2D1::RectF(cx + config.ScaleF(8.0f), footerTop + config.ScaleF(2.0f),
                                         cx + config.ScaleF(17.0f), footerTop + config.ScaleF(11.0f));
        DrawBookIcon(rcBook, brushMeta.Get());

        std::wstring lessonStr = std::to_wstring(course.lessons) + L" lessons";
        D2D1_RECT_F rcLessons = D2D1::RectF(rcBook.right + config.ScaleF(3.0f), footerTop,
                                           rcBook.right + config.ScaleF(56.0f), footerTop + config.ScaleF(14.0f));
        if (fmtCardMeta && brushMeta) {
            pRT->DrawText(lessonStr.c_str(), static_cast<UINT32>(lessonStr.length()), fmtCardMeta.Get(), rcLessons, brushMeta.Get());
        }

        // Clock icon & hours
        D2D1_RECT_F rcClock = D2D1::RectF(rcLessons.right + config.ScaleF(4.0f), footerTop + config.ScaleF(2.0f),
                                          rcLessons.right + config.ScaleF(13.0f), footerTop + config.ScaleF(11.0f));
        DrawClockIcon(rcClock, brushMeta.Get());

        std::wstring hourStr = std::to_wstring(course.hours) + L" hours";
        D2D1_RECT_F rcHours = D2D1::RectF(rcClock.right + config.ScaleF(3.0f), footerTop,
                                         rcClock.right + config.ScaleF(44.0f), footerTop + config.ScaleF(14.0f));
        if (fmtCardMeta && brushMeta) {
            pRT->DrawText(hourStr.c_str(), static_cast<UINT32>(hourStr.length()), fmtCardMeta.Get(), rcHours, brushMeta.Get());
        }

        // Difficulty Badge (Right-aligned)
        const float badgeW = config.ScaleF(56.0f);
        const float badgeH = config.ScaleF(16.0f);
        D2D1_RECT_F rcDiffBadge = D2D1::RectF(cx + cardW - config.ScaleF(8.0f) - badgeW, footerTop - config.ScaleF(1.0f),
                                             cx + cardW - config.ScaleF(8.0f), footerTop - config.ScaleF(1.0f) + badgeH);
        D2D1_ROUNDED_RECT rrcDiffBadge = D2D1::RoundedRect(rcDiffBadge, badgeH / 2.0f, badgeH / 2.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bBg, bBrd, bTxt;
        if (course.difficulty == L"Beginner") {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.040f, 0.160f, 0.120f, 1.0f), &bBg);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.080f, 0.320f, 0.240f, 1.0f), &bBrd);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.125f, 0.796f, 0.482f, 1.0f), &bTxt);
        } else if (course.difficulty == L"Intermediate") {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.050f, 0.140f, 0.250f, 1.0f), &bBg);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.080f, 0.250f, 0.450f, 1.0f), &bBrd);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.231f, 0.600f, 0.980f, 1.0f), &bTxt);
        } else { // Advanced
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.200f, 0.080f, 0.090f, 1.0f), &bBg);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.380f, 0.120f, 0.140f, 1.0f), &bBrd);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.957f, 0.247f, 0.369f, 1.0f), &bTxt);
        }

        if (bBg) pRT->FillRoundedRectangle(rrcDiffBadge, bBg.Get());
        if (bBrd) pRT->DrawRoundedRectangle(rrcDiffBadge, bBrd.Get(), 1.0f);
        if (fmtCardBadge && bTxt) {
            pRT->DrawText(course.difficulty.c_str(), static_cast<UINT32>(course.difficulty.length()),
                          fmtCardBadge.Get(), rcDiffBadge, bTxt.Get());
        }
    }

    // Scroll metrics & Scrollbar
    int numRows = (static_cast<int>(visibleCourses.size()) + numCols - 1) / numCols;
    float contentBottom = gridStartY + static_cast<float>(numRows) * (cardH + gapY) + config.ScaleF(24.0f) + m_learningScrollY;
    float visibleBottom = topY + viewH;
    m_learningMaxScrollY = (std::max)(0.0f, contentBottom - visibleBottom);
    if (m_learningScrollY > m_learningMaxScrollY) m_learningScrollY = m_learningMaxScrollY;

    if (m_learningMaxScrollY > 0.0f && brushCardBorderHover) {
        float trackH = viewH - config.ScaleF(16.0f);
        float thumbH = (std::max)(config.ScaleF(32.0f), (viewH / contentBottom) * trackH);
        float thumbY = topY + config.ScaleF(8.0f) + (m_learningScrollY / m_learningMaxScrollY) * (trackH - thumbH);
        D2D1_RECT_F rcThumb = D2D1::RectF(canvasLeft + canvasWidth - config.ScaleF(6.0f), thumbY,
                                         canvasLeft + canvasWidth - config.ScaleF(2.0f), thumbY + thumbH);
        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcThumb, 2.0f, 2.0f), brushCardBorderHover.Get());
    }

    pRT->PopAxisAlignedClip();
}

void EditorCanvas::RenderCourseView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                    editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                    float topY, float clientHeight) {
    if (!pRT) return;

    D2D1_RECT_F rcView = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, topY + clientHeight);
    pRT->PushAxisAlignedClip(rcView, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (theme.brushCanvasBg) {
        pRT->FillRectangle(rcView, theme.brushCanvasBg.Get());
    }

    InitializeLearningCourses();

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushWhite;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushProse;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushMeta;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBadgeBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBadgeBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushScrollbar;

    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brushWhite);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.88f, 0.92f, 1.0f), &brushProse);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.60f, 0.65f, 0.72f, 1.0f), &brushMeta);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.15f, 0.20f, 1.0f), &brushBadgeBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.28f, 0.36f, 1.0f), &brushBadgeBorder);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.24f, 0.30f, 1.0f), &brushScrollbar);

    // Responsive, centered reading layout for wide screens
    const float minPadX = config.ScaleF(48.0f);
    float maxContentW = config.ScaleF(960.0f);
    float availW = canvasWidth - minPadX * 2.0f;
    float contentW = (availW > maxContentW) ? maxContentW : (std::max)(config.ScaleF(400.0f), availW);
    float contentLeft = canvasLeft + minPadX;
    if (availW > maxContentW) {
        contentLeft = canvasLeft + (canvasWidth - contentW) * 0.5f;
    }
    float startY = topY + config.ScaleF(28.0f) - m_courseScrollY;
    float curY = startY;

    // Helper: Draw Paragraph with dynamic DirectWrite text layout (zero collision, exact height wrap)
    auto DrawParagraph = [&](const std::wstring& text, IDWriteTextFormat* pFmt, ID2D1Brush* pBrush, float gapAfter = 18.0f) {
        if (!pFmt || text.empty() || !theme.dwriteFactory) return;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        HRESULT hr = theme.dwriteFactory->CreateTextLayout(
            text.c_str(), static_cast<UINT32>(text.length()),
            pFmt, contentW, 10000.0f, &pLayout
        );
        if (SUCCEEDED(hr) && pLayout) {
            DWRITE_TEXT_METRICS tm{};
            pLayout->GetMetrics(&tm);
            D2D1_POINT_2F origin = D2D1::Point2F(contentLeft, curY);
            pRT->DrawTextLayout(origin, pLayout.Get(), pBrush);
            curY += tm.height + config.ScaleF(gapAfter);
        }
    };

    IDWriteTextFormat* pBodyFmt = theme.fontCourseBody ? theme.fontCourseBody.Get() : theme.fontBody.Get();
    IDWriteTextFormat* pHdFmt = theme.fontCourseHeading ? theme.fontCourseHeading.Get() : theme.fontBody.Get();

    // --- 1. Header: Python Icon + Big Bold Title "What is Python & Installation" ---
    const float iconSize = config.ScaleF(40.0f);
    D2D1_RECT_F rcIcon = D2D1::RectF(contentLeft, curY + config.ScaleF(2.0f), contentLeft + iconSize, curY + config.ScaleF(2.0f) + iconSize);
    ID2D1Bitmap* pPyIcon = GetLearningHubBitmap(pRT, L"python.png");
    if (pPyIcon) {
        pRT->DrawBitmap(pPyIcon, rcIcon, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        auto seti = theme.GetSetiIcon(L"main.py", false, false);
        if (theme.fontSeti && seti.pBrush) {
            pRT->DrawText(&seti.glyph, 1, theme.fontSeti.Get(), rcIcon, seti.pBrush);
        } else {
            m_icons.DrawFileIcon(pRT, theme, rcIcon, FileIconType::DefaultFile);
        }
    }

    float titleX = contentLeft + iconSize + config.ScaleF(14.0f);
    const wchar_t* mainTitle = L"What is Python & Installation";
    IDWriteTextFormat* pTitleFmt = theme.fontCourseTitle ? theme.fontCourseTitle.Get() : (theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontBody.Get());
    if (pTitleFmt && brushWhite && theme.dwriteFactory) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pTitleLayout;
        theme.dwriteFactory->CreateTextLayout(mainTitle, static_cast<UINT32>(wcslen(mainTitle)), pTitleFmt, contentW - (titleX - contentLeft), 200.0f, &pTitleLayout);
        if (pTitleLayout) {
            DWRITE_TEXT_METRICS tm{};
            pTitleLayout->GetMetrics(&tm);
            pRT->DrawTextLayout(D2D1::Point2F(titleX, curY), pTitleLayout.Get(), brushWhite.Get());
            curY += (std::max)(iconSize + config.ScaleF(4.0f), tm.height) + config.ScaleF(26.0f);
        } else {
            curY += iconSize + config.ScaleF(26.0f);
        }
    } else {
        curY += iconSize + config.ScaleF(26.0f);
    }

    // --- 2. Overview & Naturally Flowing Architecture Description ---
    DrawParagraph(
        L"Python is a modern, high-level, general-purpose programming language celebrated for its simple and readable syntax. Designed around the philosophy that code readability directly amplifies developer velocity, Python emphasizes clean indentation over curly braces or semicolons. This makes it an ideal first language for beginners to pick up, while still offering the depth and power demanded by industry professionals across web development, artificial intelligence, data science, and automated scripting.",
        pBodyFmt, brushProse.Get(), 16.0f
    );

    // Cross-platform statement connected directly to interpreter execution
    DrawParagraph(
        L"Because Python runs cross-platform on Windows, macOS, and Linux, code written on one operating system runs consistently anywhere. When you execute a Python program, you are interacting with an underlying interpreter that processes and executes your instructions step-by-step.",
        pBodyFmt, brushProse.Get(), 16.0f
    );

    // Streamlined execution pipeline flowing directly from the overview
    DrawParagraph(
        L"The default reference implementation of Python is CPython, written in C. When executing a Python program, CPython processes your code through a streamlined execution pipeline:\n"
        L"• Lexing & Tokenization: The source code (.py) is broken down into lexical tokens.\n"
        L"• Parsing & AST: The parser constructs an Abstract Syntax Tree (AST) representing the program structure.\n"
        L"• Bytecode Compilation: The AST is compiled into platform-independent bytecode instructions cached inside __pycache__/*.pyc for rapid startup.\n"
        L"• Python Virtual Machine (PVM): The PVM evaluation loop reads bytecode opcodes (e.g., LOAD_FAST, BINARY_OP, CALL) and maps them directly to native C execution.\n"
        L"• Memory Management & GIL: CPython combines immediate reference counting with an automated generational garbage collector (Gen 0, 1, and 2) to eliminate circular references. The Global Interpreter Lock (GIL) synchronizes native threads.",
        pBodyFmt, brushProse.Get(), 22.0f
    );

    // Installation in Phide connected seamlessly
    DrawParagraph(
        L"With an understanding of Python and how its interpreter operates, the next step is getting Python installed and configured inside Phide. You do not need to download third-party installers or edit system PATH variables manually. Phide features a built-in Runtimes Manager that handles toolchain installation and environment configuration directly inside the editor:",
        pBodyFmt, brushProse.Get(), 16.0f
    );

    // Step 1: Icon placed directly after the words "Runtimes icon"
    std::wstring s1Prefix = L"• Step 1: Open the Runtimes panel by clicking the Runtimes icon ";
    std::wstring s1Suffix = L" in the top header toolbar or pressing Ctrl+Shift+X.";

    Microsoft::WRL::ComPtr<IDWriteTextLayout> pLPrefix;
    if (theme.dwriteFactory) {
        theme.dwriteFactory->CreateTextLayout(s1Prefix.c_str(), static_cast<UINT32>(s1Prefix.length()), pBodyFmt, contentW, 100.0f, &pLPrefix);
    }
    if (pLPrefix) {
        DWRITE_TEXT_METRICS tmPref{};
        pLPrefix->GetMetrics(&tmPref);
        pRT->DrawTextLayout(D2D1::Point2F(contentLeft, curY), pLPrefix.Get(), brushProse.Get());

        // Draw ONLY the icon directly after "Runtimes icon"
        float iconBoxSize = config.ScaleF(18.0f);
        float iconX = contentLeft + tmPref.widthIncludingTrailingWhitespace;
        float iconY = curY + config.ScaleF(1.0f);
        D2D1_RECT_F rcIconBox = D2D1::RectF(iconX, iconY, iconX + iconBoxSize, iconY + iconBoxSize);
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcIconBox, config.ScaleF(3.0f), config.ScaleF(3.0f));
        if (brushBadgeBg) pRT->FillRoundedRectangle(rrc, brushBadgeBg.Get());
        if (brushBadgeBorder) pRT->DrawRoundedRectangle(rrc, brushBadgeBorder.Get(), 1.0f);
        m_icons.DrawExtensionsGrid(pRT, rcIconBox, brushWhite ? brushWhite.Get() : theme.brushTextPrimary.Get(), 1.3f);

        // Draw suffix right after the icon
        float suffX = iconX + iconBoxSize + config.ScaleF(4.0f);
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLSuffix;
        theme.dwriteFactory->CreateTextLayout(s1Suffix.c_str(), static_cast<UINT32>(s1Suffix.length()), pBodyFmt, contentW - (suffX - contentLeft), 100.0f, &pLSuffix);
        if (pLSuffix) {
            pRT->DrawTextLayout(D2D1::Point2F(suffX, curY), pLSuffix.Get(), brushProse.Get());
        }
        curY += (std::max)(tmPref.height, iconBoxSize) + config.ScaleF(16.0f);
    } else {
        DrawParagraph(s1Prefix + s1Suffix, pBodyFmt, brushProse.Get(), 16.0f);
    }

    // Step 2 with picture under it (Image 3: Runtimes panel)
    DrawParagraph(
        L"• Step 2: In the Runtimes panel, click the Python card (identified with the Python logo) to view detected environments and available version releases:",
        pBodyFmt, brushProse.Get(), 10.0f
    );

    ID2D1Bitmap* pBmpStep2 = GetLearningHubBitmap(pRT, L"step2_runtimes_panel.png");
    if (pBmpStep2) {
        D2D1_SIZE_F sz = pBmpStep2->GetSize();
        float imgW = config.ScaleF(sz.width);
        float imgH = config.ScaleF(sz.height);
        D2D1_RECT_F rcImg = D2D1::RectF(contentLeft + config.ScaleF(16.0f), curY,
                                        contentLeft + config.ScaleF(16.0f) + imgW, curY + imgH);
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcImg, config.ScaleF(6.0f), config.ScaleF(6.0f));
        if (brushBadgeBorder) pRT->DrawRoundedRectangle(rrc, brushBadgeBorder.Get(), 1.0f);
        pRT->DrawBitmap(pBmpStep2, rcImg, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        curY += imgH + config.ScaleF(18.0f);
    }

    // Step 3 with picture under it (Image 4: Python install dropdown)
    DrawParagraph(
        L"• Step 3: Choose your preferred version from the catalog (such as Python v3.15.1 or v3.15.0) and click Install. Phide will automatically configure the interpreter and set up pip:",
        pBodyFmt, brushProse.Get(), 10.0f
    );

    ID2D1Bitmap* pBmpStep3 = GetLearningHubBitmap(pRT, L"step3_python_install.png");
    if (pBmpStep3) {
        D2D1_SIZE_F sz = pBmpStep3->GetSize();
        float imgW = config.ScaleF(sz.width * 0.90f);
        float imgH = config.ScaleF(sz.height * 0.90f);
        D2D1_RECT_F rcImg = D2D1::RectF(contentLeft + config.ScaleF(16.0f), curY,
                                        contentLeft + config.ScaleF(16.0f) + imgW, curY + imgH);
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcImg, config.ScaleF(6.0f), config.ScaleF(6.0f));
        if (brushBadgeBorder) pRT->DrawRoundedRectangle(rrc, brushBadgeBorder.Get(), 1.0f);
        pRT->DrawBitmap(pBmpStep3, rcImg, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        curY += imgH + config.ScaleF(18.0f);
    }

    // Step 4
    DrawParagraph(
        L"• Step 4: You can also choose or create an isolated Workspace Virtual Environment (.venv) directly from the panel for project-specific packages.",
        pBodyFmt, brushProse.Get(), 30.0f
    );

    // Reset button bounds (no interactive button)
    m_courseOpenRuntimesBtnBounds = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);

    // --- 5. Total Height & Scroll Calculation ---
    float totalContentH = curY + m_courseScrollY - topY;
    float viewH = clientHeight - topY;
    m_courseMaxScrollY = (std::max)(0.0f, totalContentH - viewH + config.ScaleF(40.0f));

    // Vertical Scrollbar Indicator
    if (m_courseMaxScrollY > 0.0f) {
        float trackH = clientHeight - topY - config.ScaleF(16.0f);
        float thumbH = (std::max)(config.ScaleF(32.0f), (viewH / totalContentH) * trackH);
        float thumbY = topY + config.ScaleF(8.0f) + (m_courseScrollY / m_courseMaxScrollY) * (trackH - thumbH);
        D2D1_RECT_F rcThumb = D2D1::RectF(canvasLeft + canvasWidth - config.ScaleF(6.0f), thumbY,
                                         canvasLeft + canvasWidth - config.ScaleF(2.0f), thumbY + thumbH);
        if (brushScrollbar) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(rcThumb, 2.0f, 2.0f), brushScrollbar.Get());
        }
    }

    pRT->PopAxisAlignedClip();
}

int EditorCanvas::HitTestCourseView(POINT pt) const noexcept {
    return 0;
}

bool EditorCanvas::HandleCourseViewWheel(short delta) noexcept {
    if (m_courseMaxScrollY <= 0.0f) return false;
    float scrollAmount = (delta > 0) ? -60.0f : 60.0f;
    m_courseScrollY = (std::clamp)(m_courseScrollY + scrollAmount, 0.0f, m_courseMaxScrollY);
    return true;
}

bool EditorCanvas::HandleMarkdownPreviewWheel(short delta, editor::OpenDocument& doc) noexcept {
    if (doc.markdownMaxScrollY <= 0.0f) return false;
    float scrollAmount = (delta > 0) ? -60.0f : 60.0f;
    doc.markdownScrollY = (std::clamp)(doc.markdownScrollY + scrollAmount, 0.0f, doc.markdownMaxScrollY);
    return true;
}

bool EditorCanvas::HandleExtensionDetailWheel(short delta, editor::OpenDocument& doc) noexcept {
    if (doc.extensionMaxScrollY <= 0.0f) return false;
    float scrollAmount = (delta > 0) ? -60.0f : 60.0f;
    doc.extensionScrollY = (std::clamp)(doc.extensionScrollY + scrollAmount, 0.0f, doc.extensionMaxScrollY);
    return true;
}

bool EditorCanvas::HandleExtensionDetailClick(POINT pt, editor::OpenDocument& doc) {
    if (pt.y >= m_lastExtSubTabY && pt.y <= m_lastExtSubTabY + m_lastExtSubTabH) {
        if (pt.x >= m_lastExtTab0X && pt.x <= m_lastExtTab0X + m_lastExtTab0W) {
            doc.extensionSubTab = 0;
            doc.extensionScrollY = 0.0f;
            return true;
        }
        if (pt.x >= m_lastExtTab1X && pt.x <= m_lastExtTab1X + m_lastExtTab1W) {
            doc.extensionSubTab = 1;
            doc.extensionScrollY = 0.0f;
            return true;
        }
        if (pt.x >= m_lastExtTab2X && pt.x <= m_lastExtTab2X + m_lastExtTab2W) {
            doc.extensionSubTab = 2;
            doc.extensionScrollY = 0.0f;
            return true;
        }
    }

    if (pt.x >= m_lastExtDisableBtn.left && pt.x <= m_lastExtDisableBtn.right &&
        pt.y >= m_lastExtDisableBtn.top && pt.y <= m_lastExtDisableBtn.bottom) {
        doc.isDirty = !doc.isDirty; // Toggle enabled state
        return true;
    }
    return false;
}

namespace {
struct InlineSpan {
    enum class Type { Bold, Code, Italic, Link };
    Type type;
    UINT32 start;
    UINT32 length;
};

struct ParsedInlineMarkdown {
    std::wstring cleanText;
    std::vector<InlineSpan> spans;
};

static ParsedInlineMarkdown ParseInlineMarkdown(const std::wstring& raw) {
    ParsedInlineMarkdown res;
    size_t i = 0;
    const size_t n = raw.size();
    while (i < n) {
        // Inline code: `code`
        if (raw[i] == L'`') {
            size_t close = raw.find(L'`', i + 1);
            if (close != std::wstring::npos) {
                UINT32 start = static_cast<UINT32>(res.cleanText.size());
                std::wstring codeContent = raw.substr(i + 1, close - (i + 1));
                res.cleanText += codeContent;
                res.spans.push_back({ InlineSpan::Type::Code, start, static_cast<UINT32>(codeContent.size()) });
                i = close + 1;
                continue;
            }
        }
        // Bold: **text** or __text__
        if (i + 1 < n && ((raw[i] == L'*' && raw[i + 1] == L'*') || (raw[i] == L'_' && raw[i + 1] == L'_'))) {
            const wchar_t marker[3] = { raw[i], raw[i + 1], L'\0' };
            size_t close = raw.find(marker, i + 2);
            if (close != std::wstring::npos) {
                UINT32 start = static_cast<UINT32>(res.cleanText.size());
                std::wstring boldContent = raw.substr(i + 2, close - (i + 2));
                ParsedInlineMarkdown inner = ParseInlineMarkdown(boldContent);
                res.cleanText += inner.cleanText;
                res.spans.push_back({ InlineSpan::Type::Bold, start, static_cast<UINT32>(inner.cleanText.size()) });
                for (const auto& is : inner.spans) {
                    res.spans.push_back({ is.type, start + is.start, is.length });
                }
                i = close + 2;
                continue;
            }
        }
        // Link: [text](url)
        if (raw[i] == L'[') {
            size_t closeBracket = raw.find(L']', i + 1);
            if (closeBracket != std::wstring::npos && closeBracket + 1 < n && raw[closeBracket + 1] == L'(') {
                size_t closeParen = raw.find(L')', closeBracket + 2);
                if (closeParen != std::wstring::npos) {
                    UINT32 start = static_cast<UINT32>(res.cleanText.size());
                    std::wstring linkText = raw.substr(i + 1, closeBracket - (i + 1));
                    ParsedInlineMarkdown inner = ParseInlineMarkdown(linkText);
                    res.cleanText += inner.cleanText;
                    res.spans.push_back({ InlineSpan::Type::Link, start, static_cast<UINT32>(inner.cleanText.size()) });
                    for (const auto& is : inner.spans) {
                        res.spans.push_back({ is.type, start + is.start, is.length });
                    }
                    i = closeParen + 1;
                    continue;
                }
            }
        }
        // Italic: *text* or _text_ (single asterisk or underscore)
        if ((raw[i] == L'*' || raw[i] == L'_') && (i + 1 >= n || raw[i + 1] != raw[i]) && (i == 0 || raw[i - 1] != raw[i])) {
            wchar_t marker = raw[i];
            size_t close = raw.find(marker, i + 1);
            if (close != std::wstring::npos && (close + 1 >= n || raw[close + 1] != marker)) {
                UINT32 start = static_cast<UINT32>(res.cleanText.size());
                std::wstring italicContent = raw.substr(i + 1, close - (i + 1));
                ParsedInlineMarkdown inner = ParseInlineMarkdown(italicContent);
                res.cleanText += inner.cleanText;
                res.spans.push_back({ InlineSpan::Type::Italic, start, static_cast<UINT32>(inner.cleanText.size()) });
                for (const auto& is : inner.spans) {
                    res.spans.push_back({ is.type, start + is.start, is.length });
                }
                i = close + 1;
                continue;
            }
        }
        res.cleanText.push_back(raw[i]);
        ++i;
    }
    return res;
}
} // namespace

void EditorCanvas::RenderMarkdownPreview(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                         editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                         float topY, float clientHeight) {
    if (!pRT) return;

    D2D1_RECT_F rcView = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, topY + clientHeight);
    pRT->PushAxisAlignedClip(rcView, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    D2D1_COLOR_F bgColor = theme.IsDarkMode() ? D2D1::ColorF(0.10f, 0.11f, 0.13f, 1.0f) : D2D1::ColorF(0.985f, 0.988f, 0.992f, 1.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBg;
    pRT->CreateSolidColorBrush(bgColor, &brushBg);
    if (brushBg) pRT->FillRectangle(rcView, brushBg.Get());

    // 1. Top Preview Toolbar
    const float tbH = config.ScaleF(34.0f);
    D2D1_RECT_F rcToolbar = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, topY + tbH);
    D2D1_COLOR_F tbBgColor = theme.IsDarkMode() ? D2D1::ColorF(0.13f, 0.14f, 0.17f, 1.0f) : D2D1::ColorF(0.93f, 0.94f, 0.96f, 1.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTbBg;
    pRT->CreateSolidColorBrush(tbBgColor, &brushTbBg);
    if (brushTbBg) pRT->FillRectangle(rcToolbar, brushTbBg.Get());

    pRT->DrawLine(D2D1::Point2F(canvasLeft, topY + tbH), D2D1::Point2F(canvasLeft + canvasWidth, topY + tbH),
                  theme.brushBorderSubtle.Get(), 1.0f);

    // Markdown Icon in Toolbar
    float tbIconX = canvasLeft + config.ScaleF(12.0f);
    float tbIconY = topY + (tbH - config.ScaleF(16.0f)) * 0.5f;
    D2D1_RECT_F rcTbIcon = D2D1::RectF(tbIconX, tbIconY, tbIconX + config.ScaleF(16.0f), tbIconY + config.ScaleF(16.0f));
    m_icons.DrawFileIcon(pRT, theme, rcTbIcon, FileIconType::Markdown);

    // Toolbar Title & Target Filename
    std::wstring targetName = std::filesystem::path(doc.filePath).filename().wstring();
    if (targetName.empty()) targetName = L"README.md";
    std::wstring tbTitle = L"Markdown Preview: " + targetName;
    float tbTitleX = tbIconX + config.ScaleF(22.0f);
    D2D1_RECT_F rcTbTitle = D2D1::RectF(tbTitleX, topY, canvasLeft + canvasWidth * 0.95f, topY + tbH);
    pRT->DrawText(tbTitle.c_str(), static_cast<UINT32>(tbTitle.length()),
                  theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                  rcTbTitle, theme.brushTextPrimary.Get());

    // 2. Fetch Live Text (From open buffer in another split group if available, else disk)
    std::wstring markdownText;
    bool foundInBuffer = false;
    if (m_pCurrentGroups) {
        for (const auto& grp : *m_pCurrentGroups) {
            for (const auto& d : grp.docs) {
                if (!d.isSpecialView && !d.filePath.empty() && !doc.filePath.empty()) {
                    std::wstring p1 = d.filePath;
                    std::wstring p2 = doc.filePath;
                    for (auto& c : p1) { if (c == L'/') c = L'\\'; c = towlower(c); }
                    for (auto& c : p2) { if (c == L'/') c = L'\\'; c = towlower(c); }
                    if (p1 == p2 || p1.ends_with(p2) || p2.ends_with(p1)) {
                        markdownText = d.buffer.GetText();
                        foundInBuffer = true;
                        break;
                    }
                }
            }
            if (foundInBuffer) break;
        }
    }
    if (!foundInBuffer && !doc.filePath.empty()) {
        editor::Buffer tempBuf;
        if (tempBuf.LoadFromFile(doc.filePath)) {
            markdownText = tempBuf.GetText();
        }
    }
    if (markdownText.empty() && !doc.buffer.GetText().empty()) {
        markdownText = doc.buffer.GetText();
    }

    // 3. Setup Content Viewport
    D2D1_RECT_F rcContent = D2D1::RectF(canvasLeft, topY + tbH, canvasLeft + canvasWidth, topY + clientHeight);
    pRT->PushAxisAlignedClip(rcContent, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const float padX = config.ScaleF(32.0f);
    float maxReadingW = config.ScaleF(840.0f);
    float availW = canvasWidth - padX * 2.0f;
    float contentW = (availW > maxReadingW) ? maxReadingW : (std::max)(config.ScaleF(280.0f), availW);
    float contentLeft = canvasLeft + padX;
    if (availW > maxReadingW) {
        contentLeft = canvasLeft + (canvasWidth - contentW) * 0.5f;
    }

    float startY = topY + tbH + config.ScaleF(24.0f) - doc.markdownScrollY;
    float curY = startY;

    // DirectWrite fonts & theme brushes
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fontH1;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fontH2;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fontH3;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fontH4;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fontCodeBlock;

    if (theme.dwriteFactory) {
        theme.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(24.0f), L"en-US", &fontH1);
        theme.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(19.0f), L"en-US", &fontH2);
        theme.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(15.5f), L"en-US", &fontH3);
        theme.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(13.5f), L"en-US", &fontH4);
        theme.dwriteFactory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(12.5f), L"en-US", &fontCodeBlock);
    }

    IDWriteTextFormat* pBodyFmt = theme.fontCourseBody ? theme.fontCourseBody.Get() : theme.fontBody.Get();

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHead;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushProse;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCodeBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCodeBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCodeText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushQuoteBar;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushQuoteBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTableBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTableAlt;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTableBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushLink;

    if (theme.IsDarkMode()) {
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.96f, 0.98f, 1.0f), &brushHead);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.82f, 0.85f, 0.90f, 1.0f), &brushProse);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.09f, 0.11f, 1.0f), &brushCodeBg);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.23f, 0.28f, 1.0f), &brushCodeBorder);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.90f, 0.92f, 0.96f, 1.0f), &brushCodeText);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.45f, 0.65f, 1.0f), &brushQuoteBar);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.16f, 0.20f, 0.5f), &brushQuoteBg);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.16f, 0.20f, 1.0f), &brushTableBg);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.11f, 0.12f, 0.15f, 1.0f), &brushTableAlt);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.27f, 0.33f, 1.0f), &brushTableBorder);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.65f, 1.0f, 1.0f), &brushLink);
    } else {
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.15f, 0.20f, 1.0f), &brushHead);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.22f, 0.28f, 1.0f), &brushProse);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.94f, 0.95f, 0.97f, 1.0f), &brushCodeBg);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.82f, 0.85f, 0.89f, 1.0f), &brushCodeBorder);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.18f, 0.22f, 1.0f), &brushCodeText);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.50f, 0.85f, 1.0f), &brushQuoteBar);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.94f, 0.96f, 0.98f, 0.6f), &brushQuoteBg);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.94f, 0.96f, 1.0f), &brushTableBg);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.97f, 0.98f, 0.99f, 1.0f), &brushTableAlt);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.80f, 0.83f, 0.87f, 1.0f), &brushTableBorder);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.05f, 0.45f, 0.85f, 1.0f), &brushLink);
    }

    if (markdownText.empty()) {
        std::wstring emptyMsg = L"This markdown file is currently blank.\nStart typing on the left editor to preview your headings, lists, code blocks, and tables live!";
        if (theme.dwriteFactory && pBodyFmt) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pL;
            theme.dwriteFactory->CreateTextLayout(emptyMsg.c_str(), static_cast<UINT32>(emptyMsg.length()), pBodyFmt, contentW, 500.0f, &pL);
            if (pL) {
                pRT->DrawTextLayout(D2D1::Point2F(contentLeft, curY + config.ScaleF(20.0f)), pL.Get(), theme.brushTextMuted.Get());
            }
        }
    } else {
        std::wstringstream wss(markdownText);
        std::wstring line;
        bool inCodeBlock = false;
        std::wstring codeBlockLang;
        std::vector<std::wstring> codeBlockLines;
        std::vector<std::vector<std::wstring>> tableRows;

        auto FlushTable = [&]() {
            if (tableRows.empty()) return;
            size_t numCols = 0;
            for (const auto& r : tableRows) numCols = (std::max)(numCols, r.size());
            if (numCols > 0) {
                float colW = contentW / static_cast<float>(numCols);
                float rowH = config.ScaleF(28.0f);
                float tableH = static_cast<float>(tableRows.size()) * rowH;

                D2D1_RECT_F rcTbl = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + tableH);
                pRT->DrawRectangle(rcTbl, brushTableBorder.Get(), 1.0f);

                for (size_t ri = 0; ri < tableRows.size(); ++ri) {
                    float rY = curY + static_cast<float>(ri) * rowH;
                    D2D1_RECT_F rcRow = D2D1::RectF(contentLeft, rY, contentLeft + contentW, rY + rowH);
                    if (ri == 0 && brushTableBg) {
                        pRT->FillRectangle(rcRow, brushTableBg.Get());
                    } else if (ri % 2 == 1 && brushTableAlt) {
                        pRT->FillRectangle(rcRow, brushTableAlt.Get());
                    }
                    pRT->DrawLine(D2D1::Point2F(contentLeft, rY + rowH), D2D1::Point2F(contentLeft + contentW, rY + rowH), brushTableBorder.Get(), 1.0f);

                    for (size_t ci = 0; ci < tableRows[ri].size(); ++ci) {
                        float cX = contentLeft + static_cast<float>(ci) * colW;
                        if (ci > 0) {
                            pRT->DrawLine(D2D1::Point2F(cX, rY), D2D1::Point2F(cX, rY + rowH), brushTableBorder.Get(), 1.0f);
                        }
                        D2D1_RECT_F rcCell = D2D1::RectF(cX + config.ScaleF(8.0f), rY + config.ScaleF(4.0f),
                                                         cX + colW - config.ScaleF(8.0f), rY + rowH - config.ScaleF(2.0f));
                        pRT->DrawText(tableRows[ri][ci].c_str(), static_cast<UINT32>(tableRows[ri][ci].length()),
                                      (ri == 0 && fontH4) ? fontH4.Get() : pBodyFmt,
                                      rcCell, (ri == 0) ? brushHead.Get() : brushProse.Get());
                    }
                }
                curY += tableH + config.ScaleF(18.0f);
            }
            tableRows.clear();
        };

        auto RenderInlineLayout = [&](const ParsedInlineMarkdown& parsed, IDWriteTextFormat* pFormat, float x, float y, float width,
                                      Microsoft::WRL::ComPtr<IDWriteTextLayout>& outLayout, float& outHeight) {
            outHeight = 0.0f;
            if (parsed.cleanText.empty()) {
                outHeight = config.ScaleF(14.0f);
                return;
            }
            if (theme.dwriteFactory && pFormat) {
                theme.dwriteFactory->CreateTextLayout(parsed.cleanText.c_str(), static_cast<UINT32>(parsed.cleanText.length()),
                                                     pFormat, width, 10000.0f, &outLayout);
            }
            if (!outLayout) {
                outHeight = config.ScaleF(14.0f);
                return;
            }

            // Apply formatting styles for each span
            for (const auto& span : parsed.spans) {
                if (span.length == 0 || span.start + span.length > parsed.cleanText.length()) continue;
                DWRITE_TEXT_RANGE rng = { span.start, span.length };
                switch (span.type) {
                    case InlineSpan::Type::Bold:
                        outLayout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, rng);
                        break;
                    case InlineSpan::Type::Italic:
                        outLayout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, rng);
                        break;
                    case InlineSpan::Type::Link:
                        outLayout->SetUnderline(TRUE, rng);
                        break;
                    case InlineSpan::Type::Code:
                        outLayout->SetFontFamilyName(L"Consolas", rng);
                        outLayout->SetFontSize(config.fontSize > 0 ? config.ScaleF(config.fontSize * 0.88f) : config.ScaleF(12.5f), rng);
                        break;
                }
            }

            // Step 1: Draw the rounded background pills for any InlineSpan::Type::Code spans
            for (const auto& span : parsed.spans) {
                if (span.type == InlineSpan::Type::Code && span.length > 0) {
                    DWRITE_HIT_TEST_METRICS htm[16];
                    UINT32 actualHitTestCount = 0;
                    if (SUCCEEDED(outLayout->HitTestTextRange(span.start, span.length, x, y, htm, 16, &actualHitTestCount))) {
                        for (UINT32 hi = 0; hi < actualHitTestCount; ++hi) {
                            D2D1_RECT_F rcPill = D2D1::RectF(
                                htm[hi].left - config.ScaleF(3.5f),
                                htm[hi].top + config.ScaleF(1.0f),
                                htm[hi].left + htm[hi].width + config.ScaleF(3.5f),
                                htm[hi].top + htm[hi].height - config.ScaleF(1.0f)
                            );
                            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcPill, 3.5f, 3.5f);
                            if (brushCodeBg) pRT->FillRoundedRectangle(&rrc, brushCodeBg.Get());
                            if (brushCodeBorder) pRT->DrawRoundedRectangle(&rrc, brushCodeBorder.Get(), 1.0f);
                        }
                    }
                }
            }

            // Step 2: Draw the text layout
            pRT->DrawTextLayout(D2D1::Point2F(x, y), outLayout.Get(), brushProse.Get());

            DWRITE_TEXT_METRICS tm{};
            outLayout->GetMetrics(&tm);
            outHeight = tm.height;
        };

        while (std::getline(wss, line)) {
            while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.pop_back();

            // 1. Fenced Code Block Handler
            if (line.rfind(L"```", 0) == 0) {
                if (!inCodeBlock) {
                    FlushTable();
                    inCodeBlock = true;
                    codeBlockLang = line.substr(3);
                    while (!codeBlockLang.empty() && (codeBlockLang.front() == L' ' || codeBlockLang.front() == L'\t')) codeBlockLang.erase(0, 1);
                    for (auto& c : codeBlockLang) c = towupper(c);
                    codeBlockLines.clear();
                    continue;
                } else {
                    inCodeBlock = false;
                    // Render the accumulated code block
                    float lineH = config.ScaleF(18.5f);
                    float padBlock = config.ScaleF(12.0f);
                    float blockH = static_cast<float>(codeBlockLines.size()) * lineH + padBlock * 2.0f + config.ScaleF(10.0f);

                    D2D1_RECT_F rcBlock = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + blockH);
                    D2D1_ROUNDED_RECT rrcBlock = D2D1::RoundedRect(rcBlock, config.ScaleF(6.0f), config.ScaleF(6.0f));
                    if (brushCodeBg) pRT->FillRoundedRectangle(&rrcBlock, brushCodeBg.Get());
                    if (brushCodeBorder) pRT->DrawRoundedRectangle(&rrcBlock, brushCodeBorder.Get(), 1.0f);

                    // Language Tag in Top Right
                    if (!codeBlockLang.empty()) {
                        float langW = static_cast<float>(codeBlockLang.length()) * config.ScaleF(7.5f) + config.ScaleF(16.0f);
                        float langH = config.ScaleF(18.0f);
                        D2D1_ROUNDED_RECT rrcLang = D2D1::RoundedRect(
                            D2D1::RectF(rcBlock.right - langW - config.ScaleF(10.0f), rcBlock.top + config.ScaleF(8.0f),
                                        rcBlock.right - config.ScaleF(10.0f), rcBlock.top + config.ScaleF(8.0f) + langH),
                            3.0f, 3.0f);
                        if (brushCodeBorder) pRT->FillRoundedRectangle(&rrcLang, brushCodeBorder.Get());
                        pRT->DrawText(codeBlockLang.c_str(), static_cast<UINT32>(codeBlockLang.length()),
                                      theme.fontBadge ? theme.fontBadge.Get() : theme.fontMenuShortcut.Get(),
                                      rrcLang.rect, brushCodeText.Get());
                    }

                    float codeY = curY + padBlock + config.ScaleF(4.0f);
                    IDWriteTextFormat* pCodeFmt = fontCodeBlock ? fontCodeBlock.Get() : theme.fontCode.Get();
                    for (const auto& cl : codeBlockLines) {
                        D2D1_RECT_F rcCodeLine = D2D1::RectF(contentLeft + padBlock, codeY, contentLeft + contentW - padBlock, codeY + lineH);
                        pRT->DrawText(cl.c_str(), static_cast<UINT32>(cl.length()), pCodeFmt, rcCodeLine, brushCodeText.Get());
                        codeY += lineH;
                    }
                    curY += blockH + config.ScaleF(16.0f);
                    continue;
                }
            }

            if (inCodeBlock) {
                codeBlockLines.push_back(line);
                continue;
            }

            // 2. Table Rows Handler
            if (line.find(L'|') != std::wstring::npos) {
                // Check if separator row (e.g. |---|---|)
                bool isSep = true;
                for (wchar_t ch : line) {
                    if (ch != L'|' && ch != L'-' && ch != L':' && ch != L' ' && ch != L'\t') {
                        isSep = false;
                        break;
                    }
                }
                if (!isSep) {
                    std::vector<std::wstring> cells;
                    size_t s = 0;
                    while (s < line.length()) {
                        size_t p = line.find(L'|', s);
                        if (p == std::wstring::npos) break;
                        if (p > s) {
                            std::wstring cell = line.substr(s, p - s);
                            while (!cell.empty() && (cell.front() == L' ' || cell.front() == L'\t')) cell.erase(0, 1);
                            while (!cell.empty() && (cell.back() == L' ' || cell.back() == L'\t')) cell.pop_back();
                            cells.push_back(cell);
                        }
                        s = p + 1;
                    }
                    if (s < line.length()) {
                        std::wstring cell = line.substr(s);
                        while (!cell.empty() && (cell.front() == L' ' || cell.front() == L'\t')) cell.erase(0, 1);
                        while (!cell.empty() && (cell.back() == L' ' || cell.back() == L'\t')) cell.pop_back();
                        if (!cell.empty()) cells.push_back(cell);
                    }
                    if (!cells.empty()) {
                        tableRows.push_back(std::move(cells));
                        continue;
                    }
                } else {
                    continue; // Skip markdown separator row |---|---|
                }
            } else {
                FlushTable();
            }

            // 3. Horizontal Rule
            if (line == L"---" || line == L"***" || line == L"___" || line == L"----" || line == L"-----") {
                curY += config.ScaleF(10.0f);
                pRT->DrawLine(D2D1::Point2F(contentLeft, curY), D2D1::Point2F(contentLeft + contentW, curY),
                              theme.brushBorderSubtle.Get(), 1.2f);
                curY += config.ScaleF(14.0f);
                continue;
            }

            // 4. Headings (# H1, ## H2, ### H3, #### H4)
            if (line.rfind(L"#", 0) == 0) {
                size_t hCount = 0;
                while (hCount < line.length() && line[hCount] == L'#') ++hCount;
                if (hCount <= 6 && hCount < line.length() && (line[hCount] == L' ' || line[hCount] == L'\t')) {
                    std::wstring hText = line.substr(hCount);
                    while (!hText.empty() && (hText.front() == L' ' || hText.front() == L'\t')) hText.erase(0, 1);

                    IDWriteTextFormat* pFmt = (hCount == 1) ? fontH1.Get() :
                                              (hCount == 2) ? fontH2.Get() :
                                              (hCount == 3) ? fontH3.Get() : fontH4.Get();
                    if (!pFmt) pFmt = pBodyFmt;

                    ParsedInlineMarkdown parsed = ParseInlineMarkdown(hText);
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> pL;
                    float h = 0.0f;
                    RenderInlineLayout(parsed, pFmt, contentLeft, curY, contentW, pL, h);
                    curY += h + config.ScaleF(4.0f);

                    // H1 and H2 have bottom hairline divider
                    if (hCount <= 2) {
                        curY += config.ScaleF(4.0f);
                        pRT->DrawLine(D2D1::Point2F(contentLeft, curY), D2D1::Point2F(contentLeft + contentW, curY),
                                      theme.brushBorderSubtle.Get(), 1.0f);
                        curY += config.ScaleF(8.0f);
                    } else {
                        curY += config.ScaleF(6.0f);
                    }
                    continue;
                }
            }

            // 5. Blockquotes and GitHub Callout Alerts (> quote or > [!NOTE])
            if (line.rfind(L">", 0) == 0) {
                std::wstring qText = line.substr(1);
                while (!qText.empty() && (qText.front() == L' ' || qText.front() == L'\t')) qText.erase(0, 1);

                bool isAlert = false;
                std::wstring alertTitle = L"NOTE";
                D2D1_COLOR_F alertColor = D2D1::ColorF(0.20f, 0.50f, 0.90f, 1.0f); // Default Blue Note

                if (qText.rfind(L"[!NOTE]", 0) == 0) {
                    isAlert = true; alertTitle = L"NOTE"; qText = qText.substr(7);
                } else if (qText.rfind(L"[!TIP]", 0) == 0) {
                    isAlert = true; alertTitle = L"TIP"; alertColor = D2D1::ColorF(0.12f, 0.70f, 0.35f, 1.0f); qText = qText.substr(6);
                } else if (qText.rfind(L"[!IMPORTANT]", 0) == 0) {
                    isAlert = true; alertTitle = L"IMPORTANT"; alertColor = D2D1::ColorF(0.60f, 0.30f, 0.90f, 1.0f); qText = qText.substr(12);
                } else if (qText.rfind(L"[!WARNING]", 0) == 0) {
                    isAlert = true; alertTitle = L"WARNING"; alertColor = D2D1::ColorF(0.90f, 0.60f, 0.10f, 1.0f); qText = qText.substr(10);
                } else if (qText.rfind(L"[!CAUTION]", 0) == 0) {
                    isAlert = true; alertTitle = L"CAUTION"; alertColor = D2D1::ColorF(0.85f, 0.20f, 0.20f, 1.0f); qText = qText.substr(10);
                }
                while (!qText.empty() && (qText.front() == L' ' || qText.front() == L'\t')) qText.erase(0, 1);

                if (isAlert) {
                    // Render Callout Alert Card
                    float alertPad = config.ScaleF(12.0f);
                    ParsedInlineMarkdown parsedAlert = ParseInlineMarkdown(qText);
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> pL;
                    float tmH = 0.0f;
                    if (!qText.empty()) {
                        RenderInlineLayout(parsedAlert, pBodyFmt, contentLeft + alertPad, curY + alertPad + config.ScaleF(20.0f), contentW - alertPad * 2.0f - config.ScaleF(16.0f), pL, tmH);
                    }
                    float cardH = config.ScaleF(24.0f) + (std::max)(tmH, config.ScaleF(18.0f)) + alertPad * 2.0f;
                    D2D1_RECT_F rcCard = D2D1::RectF(contentLeft, curY, contentLeft + contentW, curY + cardH);
                    D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, config.ScaleF(5.0f), config.ScaleF(5.0f));

                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushAlert;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushAlertBg;
                    pRT->CreateSolidColorBrush(alertColor, &brushAlert);
                    pRT->CreateSolidColorBrush(D2D1::ColorF(alertColor.r, alertColor.g, alertColor.b, 0.12f), &brushAlertBg);

                    if (brushAlertBg) pRT->FillRoundedRectangle(&rrcCard, brushAlertBg.Get());
                    pRT->DrawLine(D2D1::Point2F(contentLeft + 2.0f, curY + 2.0f), D2D1::Point2F(contentLeft + 2.0f, curY + cardH - 2.0f),
                                  brushAlert.Get(), 3.5f);

                    // Badge Title
                    D2D1_RECT_F rcAlertTitle = D2D1::RectF(contentLeft + alertPad, curY + alertPad, contentLeft + contentW - alertPad, curY + alertPad + config.ScaleF(18.0f));
                    pRT->DrawText(alertTitle.c_str(), static_cast<UINT32>(alertTitle.length()),
                                  (fontH4 ? fontH4.Get() : pBodyFmt), rcAlertTitle, brushAlert.Get());

                    curY += cardH + config.ScaleF(14.0f);
                } else {
                    // Standard Blockquote
                    float padQuote = config.ScaleF(14.0f);
                    ParsedInlineMarkdown parsedQuote = ParseInlineMarkdown(qText);
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> pL;
                    float qH = 0.0f;
                    RenderInlineLayout(parsedQuote, pBodyFmt, contentLeft + padQuote, curY, contentW - padQuote, pL, qH);
                    float finalH = (std::max)(config.ScaleF(22.0f), qH);
                    pRT->DrawLine(D2D1::Point2F(contentLeft + 2.0f, curY), D2D1::Point2F(contentLeft + 2.0f, curY + finalH),
                                  brushQuoteBar.Get(), 3.0f);
                    curY += finalH + config.ScaleF(12.0f);
                }
                continue;
            }

            // 6. Task List (- [ ] or - [x])
            bool isTaskUnchecked = (line.rfind(L"- [ ]", 0) == 0 || line.rfind(L"* [ ]", 0) == 0);
            bool isTaskChecked   = (line.rfind(L"- [x]", 0) == 0 || line.rfind(L"- [X]", 0) == 0 ||
                                    line.rfind(L"* [x]", 0) == 0 || line.rfind(L"* [X]", 0) == 0);
            if (isTaskUnchecked || isTaskChecked) {
                std::wstring taskText = line.substr(5);
                while (!taskText.empty() && (taskText.front() == L' ' || taskText.front() == L'\t')) taskText.erase(0, 1);

                float boxSize = config.ScaleF(14.0f);
                float boxY = curY + config.ScaleF(2.0f);
                D2D1_ROUNDED_RECT rrcBox = D2D1::RoundedRect(D2D1::RectF(contentLeft, boxY, contentLeft + boxSize, boxY + boxSize), 3.0f, 3.0f);
                pRT->DrawRoundedRectangle(&rrcBox, brushCodeBorder.Get(), 1.2f);

                if (isTaskChecked) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushChecked;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.70f, 0.35f, 1.0f), &brushChecked);
                    if (brushChecked) pRT->FillRoundedRectangle(&rrcBox, brushChecked.Get());
                    // White checkmark
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushWhite;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brushWhite);
                    if (brushWhite) {
                        pRT->DrawLine(D2D1::Point2F(contentLeft + 3.0f, boxY + 7.0f), D2D1::Point2F(contentLeft + 6.0f, boxY + 10.0f), brushWhite.Get(), 1.5f);
                        pRT->DrawLine(D2D1::Point2F(contentLeft + 6.0f, boxY + 10.0f), D2D1::Point2F(contentLeft + 11.0f, boxY + 3.5f), brushWhite.Get(), 1.5f);
                    }
                }

                float textX = contentLeft + boxSize + config.ScaleF(8.0f);
                ParsedInlineMarkdown parsedTask = ParseInlineMarkdown(taskText);
                Microsoft::WRL::ComPtr<IDWriteTextLayout> pL;
                float taskH = 0.0f;
                RenderInlineLayout(parsedTask, pBodyFmt, textX, curY, contentW - (textX - contentLeft), pL, taskH);
                curY += (std::max)(boxSize, taskH) + config.ScaleF(8.0f);
                continue;
            }

            // 7. Unordered Bullet List (- , * , + )
            size_t indentSpaces = 0;
            while (indentSpaces < line.length() && line[indentSpaces] == L' ') ++indentSpaces;
            std::wstring trimmedLine = line.substr(indentSpaces);
            if (trimmedLine.rfind(L"- ", 0) == 0 || trimmedLine.rfind(L"* ", 0) == 0 || trimmedLine.rfind(L"+ ", 0) == 0) {
                std::wstring itemText = trimmedLine.substr(2);
                while (!itemText.empty() && (itemText.front() == L' ' || itemText.front() == L'\t')) itemText.erase(0, 1);

                float bulletIndent = contentLeft + static_cast<float>(indentSpaces / 2) * config.ScaleF(14.0f);
                float dotR = config.ScaleF(2.6f);
                float dotY = curY + config.ScaleF(9.0f);
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bulletIndent + dotR + 1.0f, dotY), dotR, dotR), brushHead.Get());

                float textX = bulletIndent + config.ScaleF(14.0f);
                ParsedInlineMarkdown parsedItem = ParseInlineMarkdown(itemText);
                Microsoft::WRL::ComPtr<IDWriteTextLayout> pL;
                float itemH = 0.0f;
                RenderInlineLayout(parsedItem, pBodyFmt, textX, curY, contentW - (textX - contentLeft), pL, itemH);
                curY += (std::max)(itemH, config.ScaleF(16.0f)) + config.ScaleF(6.0f);
                continue;
            }

            // 8. Ordered Numbered List (1. , 2. )
            size_t dotPos = trimmedLine.find(L". ");
            if (dotPos != std::wstring::npos && dotPos > 0 && dotPos <= 3) {
                bool allDigits = true;
                for (size_t di = 0; di < dotPos; ++di) {
                    if (!iswdigit(trimmedLine[di])) { allDigits = false; break; }
                }
                if (allDigits) {
                    std::wstring numStr = trimmedLine.substr(0, dotPos + 1);
                    std::wstring itemText = trimmedLine.substr(dotPos + 2);
                    while (!itemText.empty() && (itemText.front() == L' ' || itemText.front() == L'\t')) itemText.erase(0, 1);

                    float numW = config.ScaleF(22.0f);
                    D2D1_RECT_F rcNum = D2D1::RectF(contentLeft, curY, contentLeft + numW, curY + config.ScaleF(20.0f));
                    pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()),
                                  (fontH4 ? fontH4.Get() : pBodyFmt), rcNum, brushLink.Get());

                    float textX = contentLeft + numW + config.ScaleF(4.0f);
                    ParsedInlineMarkdown parsedNum = ParseInlineMarkdown(itemText);
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> pL;
                    float numH = 0.0f;
                    RenderInlineLayout(parsedNum, pBodyFmt, textX, curY, contentW - (textX - contentLeft), pL, numH);
                    curY += (std::max)(numH, config.ScaleF(16.0f)) + config.ScaleF(6.0f);
                    continue;
                }
            }

            // 9. Empty Line (Paragraph Gap)
            if (trimmedLine.empty()) {
                curY += config.ScaleF(12.0f);
                continue;
            }

            // 10. Standard Paragraph with Inline Formatting
            ParsedInlineMarkdown parsedPara = ParseInlineMarkdown(trimmedLine);
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pL;
            float paraH = 0.0f;
            RenderInlineLayout(parsedPara, pBodyFmt, contentLeft, curY, contentW, pL, paraH);
            curY += paraH + config.ScaleF(10.0f);
        }
        FlushTable();
    }

    pRT->PopAxisAlignedClip(); // Pop Content Viewport Clip

    // 4. Calculate Scroll Bounds
    float totalDrawnHeight = curY - startY + config.ScaleF(40.0f);
    float viewportH = clientHeight - tbH;
    doc.markdownMaxScrollY = (std::max)(0.0f, totalDrawnHeight - viewportH);
    doc.markdownScrollY = (std::clamp)(doc.markdownScrollY, 0.0f, doc.markdownMaxScrollY);

    // 5. Vertical Scrollbar
    if (doc.markdownMaxScrollY > 0.0f) {
        float sbW = config.ScaleF(8.0f);
        float sbTrackH = viewportH;
        float sbTrackX = canvasLeft + canvasWidth - sbW - config.ScaleF(2.0f);
        float sbTrackY = topY + tbH;

        float thumbRatio = viewportH / totalDrawnHeight;
        float thumbH = (std::max)(config.ScaleF(24.0f), sbTrackH * thumbRatio);
        float scrollFrac = doc.markdownScrollY / doc.markdownMaxScrollY;
        float thumbY = sbTrackY + scrollFrac * (sbTrackH - thumbH);

        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(
            D2D1::RectF(sbTrackX, thumbY, sbTrackX + sbW, thumbY + thumbH),
            config.ScaleF(3.5f), config.ScaleF(3.5f));

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushThumb;
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.20f) : D2D1::ColorF(0.12f, 0.16f, 0.24f, 0.25f), &brushThumb);
        if (brushThumb) pRT->FillRoundedRectangle(&rrcThumb, brushThumb.Get());
    }

    pRT->PopAxisAlignedClip(); // Pop Viewport Clip
}

struct ExtDetailMeta {
    std::wstring id;
    std::wstring displayName;
    std::wstring publisher;
    std::wstring version;
    std::wstring description;
    std::wstring installs;
    float rating = 4.5f;
    int ratingCount = 85;
    std::wstring categories;
    std::wstring published;
    std::wstring lastReleased;
    std::wstring repoUrl;
    std::wstring issuesUrl;
    std::wstring license;
};

static ExtDetailMeta GetExtensionMeta(const std::wstring& extId) {
    if (extId == L"bradlc.vscode-tailwindcss") {
        return {
            L"bradlc.vscode-tailwindcss",
            L"Tailwind CSS IntelliSense",
            L"Tailwind Labs",
            L"v0.14.3",
            L"Intelligent Tailwind CSS tooling for VS Code",
            L"17,890,210",
            4.7f,
            134,
            L"Linters, Snippets, Other",
            L"10/24/2019",
            L"10/29/2026",
            L"https://github.com/tailwindlabs/tailwindcss-intellisense",
            L"https://github.com/tailwindlabs/tailwindcss-intellisense/issues",
            L"MIT"
        };
    } else if (extId == L"redhat.vscode-yaml") {
        return {
            L"redhat.vscode-yaml",
            L"YAML",
            L"Red Hat",
            L"v2.1.2",
            L"YAML Language Support by Red Hat, with built-in Kubernetes syntax support",
            L"29,805,672",
            4.3f,
            75,
            L"Programming Languages, Snippets, Linters, Formatters",
            L"3/2/2018",
            L"10/23/2026",
            L"https://github.com/redhat-developer/vscode-yaml",
            L"https://github.com/redhat-developer/vscode-yaml/issues",
            L"MIT"
        };
    } else if (extId == L"eamodio.gitlens") {
        return {
            L"eamodio.gitlens",
            L"GitLens — Git supercharged",
            L"GitKraken",
            L"v2026.10.31",
            L"Supercharge Git within VS Code — Visualize code authorship at a glance via Git blame annotations and CodeLens, explore Git repositories, and more",
            L"53,666,213",
            4.5f,
            945,
            L"SCM Providers",
            L"6/15/2016",
            L"10/31/2026",
            L"https://github.com/gitkraken/vscode-gitlens",
            L"https://github.com/gitkraken/vscode-gitlens/issues",
            L"GitLens Non-Commercial Software License"
        };
    } else {
        return {
            L"dsznajder.es7-react-js-snippets",
            L"ES7+ React/Redux/React-Native snippets",
            L"rodrigovallades",
            L"v4.4.3",
            L"Extensions for React, React-Native and Redux in JS/TS with ES7+ syntax. Customizable. Built-in integration with prettier.",
            L"17,854,580",
            4.4f,
            85,
            L"Snippets",
            L"2/18/2022",
            L"2/18/2022",
            L"https://github.com/ults-io/vscode-react-javascript-snippets",
            L"https://github.com/ults-io/vscode-react-javascript-snippets/issues",
            L"MIT"
        };
    }
}

void EditorCanvas::RenderExtensionDetail(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                         editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                         float topY, float clientHeight) {
    if (!pRT) return;

    ExtDetailMeta meta = GetExtensionMeta(doc.extensionId.empty() ? doc.fileName : doc.extensionId);
    if (doc.fileName.empty() || doc.fileName == L"Untitled-1") {
        doc.fileName = L"Extension: " + meta.displayName;
    }

    D2D1_RECT_F rcView = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, topY + clientHeight);
    pRT->PushAxisAlignedClip(rcView, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Canvas background
    pRT->FillRectangle(rcView, theme.brushCanvasBg.Get());

    // Load icon bitmap if not yet cached
    if (!doc.pImageBitmap) {
        std::wstring iconFile = L"assets\\extensions\\" + meta.id + L"\\icon.png";
        doc.pImageBitmap = CanvasLoadPngBitmap(pRT, iconFile);
    }

    // DirectWrite fonts
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fontH1;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fontH2;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fontH3;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fontCodeBlock;

    if (theme.dwriteFactory) {
        theme.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(22.0f), L"en-US", &fontH1);
        theme.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(17.0f), L"en-US", &fontH2);
        theme.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(14.5f), L"en-US", &fontH3);
        theme.dwriteFactory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(12.5f), L"en-US", &fontCodeBlock);
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBlue;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushGold;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCodeBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCodeBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTableBorder;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushInputBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.58f, 0.98f, 1.0f), &brushBlue);
    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.76f, 0.03f, 1.0f), &brushGold);
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.11f, 0.12f, 0.14f, 1.0f) : D2D1::ColorF(0.94f, 0.95f, 0.97f, 1.0f), &brushCodeBg);
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.22f, 0.24f, 0.28f, 1.0f) : D2D1::ColorF(0.82f, 0.85f, 0.89f, 1.0f), &brushCodeBorder);
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.27f, 0.33f, 1.0f) : D2D1::ColorF(0.80f, 0.83f, 0.87f, 1.0f), &brushTableBorder);
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.18f, 0.19f, 0.22f, 1.0f) : D2D1::ColorF(0.92f, 0.93f, 0.95f, 1.0f), &brushInputBg);

    const float startY = topY + config.ScaleF(24.0f) - doc.extensionScrollY;
    float curY = startY;
    const float contentLeft = canvasLeft + config.ScaleF(32.0f);

    // 1. Header: Icon
    const float iconSize = config.ScaleF(64.0f);
    D2D1_RECT_F rcIcon = D2D1::RectF(contentLeft, curY, contentLeft + iconSize, curY + iconSize);
    if (doc.pImageBitmap) {
        pRT->DrawBitmap(doc.pImageBitmap.Get(), rcIcon);
    } else {
        D2D1_ROUNDED_RECT rrcIcon = D2D1::RoundedRect(rcIcon, 6.0f, 6.0f);
        if (brushInputBg) pRT->FillRoundedRectangle(&rrcIcon, brushInputBg.Get());
        m_icons.DrawExtensionsGrid(pRT, D2D1::RectF(rcIcon.left + 12.0f, rcIcon.top + 12.0f, rcIcon.right - 12.0f, rcIcon.bottom - 12.0f), theme.brushTextSecondary.Get(), 1.3f);
    }

    // 2. Header Info Beside Icon
    float infoX = contentLeft + iconSize + config.ScaleF(18.0f);
    float infoW = (std::max)(100.0f, canvasWidth - infoX - config.ScaleF(40.0f));

    // Title
    D2D1_RECT_F rcTitle = D2D1::RectF(infoX, curY - config.ScaleF(2.0f), infoX + infoW, curY + config.ScaleF(26.0f));
    pRT->DrawText(meta.displayName.c_str(), static_cast<UINT32>(meta.displayName.length()),
                  fontH1 ? fontH1.Get() : theme.fontTitle.Get(), rcTitle, theme.brushTextPrimary.Get());

    // ID, Publisher, Verified Badge, Version
    float metaLineY = curY + config.ScaleF(28.0f);
    std::wstring metaText = meta.id + L"   " + meta.publisher;
    IDWriteTextFormat* pLeadingSmall = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get();
    float metaTextW = static_cast<float>(metaText.length()) * config.ScaleF(7.0f);
    if (theme.dwriteFactory && pLeadingSmall) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayoutMeta;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(metaText.c_str(), static_cast<UINT32>(metaText.length()), pLeadingSmall, 1000.0f, 30.0f, &pLayoutMeta))) {
            DWRITE_TEXT_METRICS tm{};
            pLayoutMeta->GetMetrics(&tm);
            metaTextW = tm.widthIncludingTrailingWhitespace;
        }
    }
    D2D1_RECT_F rcMeta = D2D1::RectF(infoX, metaLineY, infoX + metaTextW, metaLineY + config.ScaleF(18.0f));
    pRT->DrawText(metaText.c_str(), static_cast<UINT32>(metaText.length()),
                  pLeadingSmall, rcMeta, theme.brushTextMuted.Get());

    // Verified badge checkmark circle
    float badgeCx = infoX + metaTextW + config.ScaleF(10.0f);
    float badgeCy = metaLineY + config.ScaleF(8.0f);
    if (brushBlue) {
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(badgeCx, badgeCy), config.ScaleF(5.5f), config.ScaleF(5.5f)), brushBlue.Get());
        // White checkmark inside
        if (theme.brushTextWhite) {
            pRT->DrawLine(D2D1::Point2F(badgeCx - config.ScaleF(2.5f), badgeCy),
                          D2D1::Point2F(badgeCx - config.ScaleF(0.5f), badgeCy + config.ScaleF(2.2f)),
                          theme.brushTextWhite.Get(), 1.2f);
            pRT->DrawLine(D2D1::Point2F(badgeCx - config.ScaleF(0.5f), badgeCy + config.ScaleF(2.2f)),
                          D2D1::Point2F(badgeCx + config.ScaleF(2.8f), badgeCy - config.ScaleF(2.0f)),
                          theme.brushTextWhite.Get(), 1.2f);
        }
    }

    // Version
    std::wstring verText = L"v" + meta.version;
    D2D1_RECT_F rcVer = D2D1::RectF(badgeCx + config.ScaleF(10.0f), metaLineY, infoX + infoW, metaLineY + config.ScaleF(18.0f));
    pRT->DrawText(verText.c_str(), static_cast<UINT32>(verText.length()),
                  pLeadingSmall, rcVer, theme.brushTextMuted.Get());

    // Subtitle / Description
    float descY = curY + config.ScaleF(48.0f);
    D2D1_RECT_F rcDesc = D2D1::RectF(infoX, descY, infoX + infoW, descY + config.ScaleF(38.0f));
    pRT->DrawText(meta.description.c_str(), static_cast<UINT32>(meta.description.length()),
                  theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                  rcDesc, theme.brushTextPrimary.Get());

    // Stats: Rating Stars + Installs
    float statsY = curY + config.ScaleF(88.0f);
    std::wstring stars = L"★★★★★";
    int filledStars = static_cast<int>(std::round(meta.rating));
    std::wstring starStr;
    for (int s = 0; s < 5; ++s) starStr += (s < filledStars) ? L"★ " : L"☆ ";
    starStr += L" (" + std::to_wstring(meta.ratingCount) + L")      ↓ " + meta.installs;
    D2D1_RECT_F rcStats = D2D1::RectF(infoX, statsY, infoX + infoW, statsY + config.ScaleF(20.0f));
    pRT->DrawText(starStr.c_str(), static_cast<UINT32>(starStr.length()),
                  pLeadingSmall, rcStats, brushGold ? brushGold.Get() : theme.brushTextPrimary.Get());

    // Action Buttons: Disable, Uninstall
    float btnY = curY + config.ScaleF(112.0f);
    m_lastExtDisableBtn = D2D1::RectF(infoX, btnY, infoX + config.ScaleF(72.0f), btnY + config.ScaleF(26.0f));
    D2D1_ROUNDED_RECT rrcDisable = D2D1::RoundedRect(m_lastExtDisableBtn, 3.0f, 3.0f);
    if (brushInputBg) pRT->FillRoundedRectangle(&rrcDisable, brushInputBg.Get());
    pRT->DrawRoundedRectangle(&rrcDisable, theme.brushBorderSubtle.Get(), 1.0f);
    const wchar_t* disText = doc.isDirty ? L"Enable" : L"Disable";
    pRT->DrawText(disText, static_cast<UINT32>(wcslen(disText)),
                  theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(),
                  m_lastExtDisableBtn, theme.brushTextPrimary.Get());

    m_lastExtUninstallBtn = D2D1::RectF(infoX + config.ScaleF(80.0f), btnY, infoX + config.ScaleF(165.0f), btnY + config.ScaleF(26.0f));
    D2D1_ROUNDED_RECT rrcUninstall = D2D1::RoundedRect(m_lastExtUninstallBtn, 3.0f, 3.0f);
    if (brushInputBg) pRT->FillRoundedRectangle(&rrcUninstall, brushInputBg.Get());
    pRT->DrawRoundedRectangle(&rrcUninstall, theme.brushBorderSubtle.Get(), 1.0f);
    const wchar_t* unText = L"Uninstall";
    pRT->DrawText(unText, static_cast<UINT32>(wcslen(unText)),
                  theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(),
                  m_lastExtUninstallBtn, theme.brushTextPrimary.Get());

    curY += config.ScaleF(150.0f);

    // 3. Subtabs Bar: DETAILS | FEATURE CONTRIBUTIONS | CHANGELOG
    m_lastExtSubTabY = curY;
    m_lastExtSubTabH = config.ScaleF(32.0f);
    m_lastExtTab0X = contentLeft;
    m_lastExtTab0W = config.ScaleF(70.0f);
    m_lastExtTab1X = m_lastExtTab0X + m_lastExtTab0W + config.ScaleF(24.0f);
    m_lastExtTab1W = config.ScaleF(170.0f);
    m_lastExtTab2X = m_lastExtTab1X + m_lastExtTab1W + config.ScaleF(24.0f);
    m_lastExtTab2W = config.ScaleF(95.0f);

    pRT->DrawLine(D2D1::Point2F(contentLeft, curY + m_lastExtSubTabH),
                  D2D1::Point2F(canvasLeft + canvasWidth - config.ScaleF(32.0f), curY + m_lastExtSubTabH),
                  theme.brushBorderSubtle.Get(), 1.0f);

    // Tab 0: DETAILS
    D2D1_RECT_F rcTab0 = D2D1::RectF(m_lastExtTab0X, curY, m_lastExtTab0X + m_lastExtTab0W, curY + m_lastExtSubTabH);
    pRT->DrawText(L"DETAILS", 7, theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(),
                  rcTab0, (doc.extensionSubTab == 0) ? (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextPrimary.Get()) : theme.brushTextMuted.Get());
    if (doc.extensionSubTab == 0 && theme.brushBrandAccent) {
        pRT->DrawLine(D2D1::Point2F(m_lastExtTab0X, curY + m_lastExtSubTabH - 1.0f),
                      D2D1::Point2F(m_lastExtTab0X + m_lastExtTab0W, curY + m_lastExtSubTabH - 1.0f),
                      theme.brushBrandAccent.Get(), 2.5f);
    }

    // Tab 1: FEATURE CONTRIBUTIONS
    D2D1_RECT_F rcTab1 = D2D1::RectF(m_lastExtTab1X, curY, m_lastExtTab1X + m_lastExtTab1W, curY + m_lastExtSubTabH);
    pRT->DrawText(L"FEATURE CONTRIBUTIONS", 21, theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(),
                  rcTab1, (doc.extensionSubTab == 1) ? (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextPrimary.Get()) : theme.brushTextMuted.Get());
    if (doc.extensionSubTab == 1 && theme.brushBrandAccent) {
        pRT->DrawLine(D2D1::Point2F(m_lastExtTab1X, curY + m_lastExtSubTabH - 1.0f),
                      D2D1::Point2F(m_lastExtTab1X + m_lastExtTab1W, curY + m_lastExtSubTabH - 1.0f),
                      theme.brushBrandAccent.Get(), 2.5f);
    }

    // Tab 2: CHANGELOG
    D2D1_RECT_F rcTab2 = D2D1::RectF(m_lastExtTab2X, curY, m_lastExtTab2X + m_lastExtTab2W, curY + m_lastExtSubTabH);
    pRT->DrawText(L"CHANGELOG", 9, theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontBody.Get(),
                  rcTab2, (doc.extensionSubTab == 2) ? (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextPrimary.Get()) : theme.brushTextMuted.Get());
    if (doc.extensionSubTab == 2 && theme.brushBrandAccent) {
        pRT->DrawLine(D2D1::Point2F(m_lastExtTab2X, curY + m_lastExtSubTabH - 1.0f),
                      D2D1::Point2F(m_lastExtTab2X + m_lastExtTab2W, curY + m_lastExtSubTabH - 1.0f),
                      theme.brushBrandAccent.Get(), 2.5f);
    }

    curY += m_lastExtSubTabH + config.ScaleF(14.0f);

    // 4. Global Enabled Notice Banner
    D2D1_RECT_F rcBanner = D2D1::RectF(contentLeft, curY, canvasLeft + canvasWidth - config.ScaleF(32.0f), curY + config.ScaleF(30.0f));
    D2D1_ROUNDED_RECT rrcBanner = D2D1::RoundedRect(rcBanner, 4.0f, 4.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBannerBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBannerBorder;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.12f), &brushBannerBg);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.35f), &brushBannerBorder);
    if (brushBannerBg) pRT->FillRoundedRectangle(&rrcBanner, brushBannerBg.Get());
    if (brushBannerBorder) pRT->DrawRoundedRectangle(&rrcBanner, brushBannerBorder.Get(), 1.0f);

    const wchar_t* bannerMsg = L"ⓘ  This extension is enabled globally.";
    D2D1_RECT_F rcBannerMsg = D2D1::RectF(rcBanner.left + config.ScaleF(12.0f), rcBanner.top, rcBanner.right, rcBanner.bottom);
    pRT->DrawText(bannerMsg, static_cast<UINT32>(wcslen(bannerMsg)),
                  theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                  rcBannerMsg, brushBlue ? brushBlue.Get() : theme.brushTextPrimary.Get());

    curY += config.ScaleF(44.0f);

    // 5. Two-Column Layout
    const float leftColW = (canvasWidth - config.ScaleF(64.0f)) * 0.72f;
    const float rightColLeft = contentLeft + leftColW + config.ScaleF(28.0f);
    const float rightColW = (std::max)(100.0f, canvasLeft + canvasWidth - rightColLeft - config.ScaleF(32.0f));

    // Render Right Column: Sidebar Metadata (Categories, Resources, More Info)
    float sideY = curY;
    const wchar_t* secCat = L"CATEGORIES";
    D2D1_RECT_F rcCatSec = D2D1::RectF(rightColLeft, sideY, rightColLeft + rightColW, sideY + config.ScaleF(18.0f));
    pRT->DrawText(secCat, static_cast<UINT32>(wcslen(secCat)),
                  theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontTitle.Get(),
                  rcCatSec, theme.brushTextMuted.Get());
    sideY += config.ScaleF(24.0f);

    // Category Pill Badge
    D2D1_RECT_F rcPill = D2D1::RectF(rightColLeft, sideY, rightColLeft + config.ScaleF(80.0f), sideY + config.ScaleF(22.0f));
    D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(rcPill, 3.0f, 3.0f);
    if (brushInputBg) pRT->FillRoundedRectangle(&rrcPill, brushInputBg.Get());
    pRT->DrawRoundedRectangle(&rrcPill, theme.brushBorderSubtle.Get(), 1.0f);
    D2D1_RECT_F rcPillText = D2D1::RectF(rcPill.left + config.ScaleF(8.0f), rcPill.top + config.ScaleF(2.0f), rcPill.right, rcPill.bottom);
    pRT->DrawText(meta.categories.c_str(), static_cast<UINT32>(meta.categories.length()),
                  pLeadingSmall, rcPillText, theme.brushTextPrimary.Get());
    sideY += config.ScaleF(36.0f);

    // Resources
    const wchar_t* secRes = L"RESOURCES";
    D2D1_RECT_F rcResSec = D2D1::RectF(rightColLeft, sideY, rightColLeft + rightColW, sideY + config.ScaleF(18.0f));
    pRT->DrawText(secRes, static_cast<UINT32>(wcslen(secRes)),
                  theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontTitle.Get(),
                  rcResSec, theme.brushTextMuted.Get());
    sideY += config.ScaleF(24.0f);

    std::wstring links[] = { L"Marketplace", L"Repository", L"Issues", L"License" };
    for (const auto& l : links) {
        D2D1_RECT_F rcLink = D2D1::RectF(rightColLeft, sideY, rightColLeft + rightColW, sideY + config.ScaleF(20.0f));
        pRT->DrawText(l.c_str(), static_cast<UINT32>(l.length()),
                      theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                      rcLink, brushBlue ? brushBlue.Get() : theme.brushTextPrimary.Get());
        sideY += config.ScaleF(22.0f);
    }
    sideY += config.ScaleF(16.0f);

    // More Info
    const wchar_t* secInfo = L"MORE INFO";
    D2D1_RECT_F rcInfoSec = D2D1::RectF(rightColLeft, sideY, rightColLeft + rightColW, sideY + config.ScaleF(18.0f));
    pRT->DrawText(secInfo, static_cast<UINT32>(wcslen(secInfo)),
                  theme.fontSectionHeader ? theme.fontSectionHeader.Get() : theme.fontTitle.Get(),
                  rcInfoSec, theme.brushTextMuted.Get());
    sideY += config.ScaleF(24.0f);

    std::pair<std::wstring, std::wstring> infoRows[] = {
        { L"Published", meta.published },
        { L"Last released", meta.lastReleased },
        { L"Identifier", meta.id }
    };
    for (const auto& row : infoRows) {
        D2D1_RECT_F rcLbl = D2D1::RectF(rightColLeft, sideY, rightColLeft + rightColW, sideY + config.ScaleF(16.0f));
        pRT->DrawText(row.first.c_str(), static_cast<UINT32>(row.first.length()),
                      pLeadingSmall, rcLbl, theme.brushTextMuted.Get());
        sideY += config.ScaleF(18.0f);
        D2D1_RECT_F rcVal = D2D1::RectF(rightColLeft, sideY, rightColLeft + rightColW, sideY + config.ScaleF(18.0f));
        pRT->DrawText(row.second.c_str(), static_cast<UINT32>(row.second.length()),
                      pLeadingSmall, rcVal, theme.brushTextPrimary.Get());
        sideY += config.ScaleF(24.0f);
    }

    // Render Left Column: Markdown content or Feature Contributions
    D2D1_RECT_F rcLeftColClip = D2D1::RectF(contentLeft, curY, contentLeft + leftColW, clientHeight);
    pRT->PushAxisAlignedClip(rcLeftColClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (doc.extensionSubTab == 1) {
        // Feature contributions view
        const wchar_t* hContrib = L"Contributed Snippets & IntelliSense Capabilities";
        D2D1_RECT_F rcContrib = D2D1::RectF(contentLeft, curY, contentLeft + leftColW, curY + config.ScaleF(28.0f));
        pRT->DrawText(hContrib, static_cast<UINT32>(wcslen(hContrib)),
                      fontH2 ? fontH2.Get() : theme.fontTitle.Get(), rcContrib, theme.brushTextPrimary.Get());
        curY += config.ScaleF(34.0f);

        struct ContribRow { std::wstring prefix; std::wstring desc; };
        std::vector<ContribRow> rows;
        if (meta.id.find(L"react") != std::wstring::npos) {
            rows = {
                { L"rafce", L"React Arrow Function Component with Export" },
                { L"rfce", L"React Function Component with Export" },
                { L"rce", L"React Class Component with Export" },
                { L"rfc", L"React Function Component" },
                { L"useState", L"React useState Hook snippet" },
                { L"useEffect", L"React useEffect Hook snippet" },
                { L"useContext", L"React useContext Hook snippet" },
                { L"useRef", L"React useRef Hook snippet" },
                { L"useCallback", L"React useCallback Hook snippet" },
                { L"useMemo", L"React useMemo Hook snippet" },
                { L"clg", L"console.log(object)" },
                { L"imp", L"import moduleName from 'module'" }
            };
        } else if (meta.id.find(L"tailwind") != std::wstring::npos) {
            rows = {
                { L"tailwindCSS.emmetCompletions", L"Enable Emmet completions within class attributes" },
                { L"tailwindCSS.includeLanguages", L"Enable Tailwind CSS completions for PHP, HTML, JS, TS, React" },
                { L"tailwindCSS.validate", L"Lint unknown utility classes and CSS directives" }
            };
        } else if (meta.id.find(L"yaml") != std::wstring::npos) {
            rows = {
                { L"yaml.schemas", L"Associate JSON/YAML schemas with Kubernetes & docker-compose manifests" },
                { L"yaml.format.enable", L"Enable or disable default YAML formatter" },
                { L"yaml.validate", L"Enable YAML syntax validation and linter warnings" }
            };
        } else {
            rows = {
                { L"gitlens.codeLens.authors.enabled", L"Specifies whether to show author CodeLens above functions" },
                { L"gitlens.currentLine.enabled", L"Specifies whether to show git blame annotation on current line" },
                { L"gitlens.views.repositories.enabled", L"Enable Repositories tree view in Source Control" }
            };
        }

        // Draw Table Header
        D2D1_RECT_F rcThPrefix = D2D1::RectF(contentLeft, curY, contentLeft + config.ScaleF(160.0f), curY + config.ScaleF(24.0f));
        D2D1_RECT_F rcThDesc = D2D1::RectF(contentLeft + config.ScaleF(170.0f), curY, contentLeft + leftColW, curY + config.ScaleF(24.0f));
        pRT->DrawText(L"Prefix / Setting", 16, theme.fontSectionHeader.Get(), rcThPrefix, theme.brushTextMuted.Get());
        pRT->DrawText(L"Description", 11, theme.fontSectionHeader.Get(), rcThDesc, theme.brushTextMuted.Get());
        curY += config.ScaleF(28.0f);
        pRT->DrawLine(D2D1::Point2F(contentLeft, curY), D2D1::Point2F(contentLeft + leftColW, curY), theme.brushBorderSubtle.Get(), 1.0f);
        curY += config.ScaleF(8.0f);

        for (const auto& r : rows) {
            D2D1_RECT_F rcPre = D2D1::RectF(contentLeft, curY, contentLeft + config.ScaleF(160.0f), curY + config.ScaleF(22.0f));
            D2D1_RECT_F rcD = D2D1::RectF(contentLeft + config.ScaleF(170.0f), curY, contentLeft + leftColW, curY + config.ScaleF(22.0f));
            pRT->DrawText(r.prefix.c_str(), static_cast<UINT32>(r.prefix.length()),
                          fontCodeBlock ? fontCodeBlock.Get() : theme.fontCode.Get(), rcPre,
                          brushBlue ? brushBlue.Get() : theme.brushTextPrimary.Get());
            pRT->DrawText(r.desc.c_str(), static_cast<UINT32>(r.desc.length()),
                          theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(), rcD,
                          theme.brushTextPrimary.Get());
            curY += config.ScaleF(26.0f);
        }
    } else {
        // DETAILS (README.md) or CHANGELOG (CHANGELOG.md)
        std::wstring mdFilename = (doc.extensionSubTab == 2) ? L"CHANGELOG.md" : L"README.md";
        std::wstring mdPath = L"assets\\extensions\\" + meta.id + L"\\" + mdFilename;
        std::wstring mdContent;
        editor::Buffer tempBuf;
        if (tempBuf.LoadFromFile(mdPath) || tempBuf.LoadFromFile(L"bin\\" + mdPath)) {
            mdContent = tempBuf.GetText();
        }

        if (mdContent.empty()) {
            mdContent = L"# " + meta.displayName + L"\n\n" + meta.description;
        }

        std::wstringstream wss(mdContent);
        std::wstring line;
        bool inCodeBlock = false;
        std::vector<std::wstring> codeBlockLines;

        while (std::getline(wss, line)) {
            if (!line.empty() && line.back() == L'\r') line.pop_back();

            // Skip raw badge images [![Badge](url)](url) or [![...
            if (line.starts_with(L"[![") || line.starts_with(L"[!")) continue;

            // Code block handling
            if (line.starts_with(L"```")) {
                if (inCodeBlock) {
                    inCodeBlock = false;
                    const float codeLineH = config.ScaleF(18.0f);
                    float blockH = static_cast<float>(codeBlockLines.size()) * codeLineH + config.ScaleF(16.0f);
                    D2D1_RECT_F rcBlock = D2D1::RectF(contentLeft, curY, contentLeft + leftColW, curY + blockH);
                    D2D1_ROUNDED_RECT rrcBlock = D2D1::RoundedRect(rcBlock, 4.0f, 4.0f);
                    if (brushCodeBg) pRT->FillRoundedRectangle(&rrcBlock, brushCodeBg.Get());
                    if (brushCodeBorder) pRT->DrawRoundedRectangle(&rrcBlock, brushCodeBorder.Get(), 1.0f);

                    float cY = curY + config.ScaleF(8.0f);
                    for (const auto& cl : codeBlockLines) {
                        D2D1_RECT_F rcCl = D2D1::RectF(contentLeft + config.ScaleF(12.0f), cY, contentLeft + leftColW - config.ScaleF(12.0f), cY + codeLineH);
                        pRT->DrawText(cl.c_str(), static_cast<UINT32>(cl.length()),
                                      fontCodeBlock ? fontCodeBlock.Get() : theme.fontCode.Get(), rcCl, theme.brushTextPrimary.Get());
                        cY += codeLineH;
                    }
                    curY += blockH + config.ScaleF(14.0f);
                    codeBlockLines.clear();
                } else {
                    inCodeBlock = true;
                    codeBlockLines.clear();
                }
                continue;
            }

            if (inCodeBlock) {
                codeBlockLines.push_back(line);
                continue;
            }

            if (line.empty()) {
                curY += config.ScaleF(8.0f);
                continue;
            }

            // Headings
            if (line.starts_with(L"# ")) {
                std::wstring hText = line.substr(2);
                D2D1_RECT_F rcH = D2D1::RectF(contentLeft, curY, contentLeft + leftColW, curY + config.ScaleF(32.0f));
                pRT->DrawText(hText.c_str(), static_cast<UINT32>(hText.length()), fontH1.Get(), rcH, theme.brushTextPrimary.Get());
                curY += config.ScaleF(34.0f);
                pRT->DrawLine(D2D1::Point2F(contentLeft, curY), D2D1::Point2F(contentLeft + leftColW, curY), theme.brushBorderSubtle.Get(), 1.0f);
                curY += config.ScaleF(10.0f);
                continue;
            }
            if (line.starts_with(L"## ")) {
                std::wstring hText = line.substr(3);
                D2D1_RECT_F rcH = D2D1::RectF(contentLeft, curY, contentLeft + leftColW, curY + config.ScaleF(26.0f));
                pRT->DrawText(hText.c_str(), static_cast<UINT32>(hText.length()), fontH2.Get(), rcH, theme.brushTextPrimary.Get());
                curY += config.ScaleF(28.0f);
                pRT->DrawLine(D2D1::Point2F(contentLeft, curY), D2D1::Point2F(contentLeft + leftColW, curY), theme.brushBorderSubtle.Get(), 0.8f);
                curY += config.ScaleF(8.0f);
                continue;
            }
            if (line.starts_with(L"### ")) {
                std::wstring hText = line.substr(4);
                D2D1_RECT_F rcH = D2D1::RectF(contentLeft, curY, contentLeft + leftColW, curY + config.ScaleF(22.0f));
                pRT->DrawText(hText.c_str(), static_cast<UINT32>(hText.length()), fontH3.Get(), rcH, theme.brushTextPrimary.Get());
                curY += config.ScaleF(26.0f);
                continue;
            }

            // Bullet list item
            if (line.starts_with(L"- ") || line.starts_with(L"* ")) {
                std::wstring itemText = line.substr(2);
                float bulletCx = contentLeft + config.ScaleF(6.0f);
                float bulletCy = curY + config.ScaleF(9.0f);
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bulletCx, bulletCy), config.ScaleF(2.5f), config.ScaleF(2.5f)), theme.brushTextMuted.Get());

                D2D1_RECT_F rcItem = D2D1::RectF(contentLeft + config.ScaleF(16.0f), curY, contentLeft + leftColW, curY + config.ScaleF(20.0f));
                pRT->DrawText(itemText.c_str(), static_cast<UINT32>(itemText.length()),
                              theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(), rcItem, theme.brushTextPrimary.Get());
                curY += config.ScaleF(22.0f);
                continue;
            }

            // Regular paragraph line
            D2D1_RECT_F rcLine = D2D1::RectF(contentLeft, curY, contentLeft + leftColW, curY + config.ScaleF(20.0f));
            pRT->DrawText(line.c_str(), static_cast<UINT32>(line.length()),
                          theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(), rcLine, theme.brushTextPrimary.Get());
            curY += config.ScaleF(20.0f);
        }
    }

    pRT->PopAxisAlignedClip();

    // Determine total content height
    float maxColBottom = (std::max)(curY, sideY);
    float totalDrawnHeight = maxColBottom - startY + config.ScaleF(60.0f);
    float viewportH = clientHeight - topY;
    doc.extensionMaxScrollY = (std::max)(0.0f, totalDrawnHeight - viewportH);
    doc.extensionScrollY = (std::clamp)(doc.extensionScrollY, 0.0f, doc.extensionMaxScrollY);

    // 6. Scrollbar on Right
    if (doc.extensionMaxScrollY > 0.0f) {
        float sbW = config.ScaleF(8.0f);
        float sbTrackH = viewportH;
        float sbTrackX = canvasLeft + canvasWidth - sbW - config.ScaleF(2.0f);
        float sbTrackY = topY;

        float thumbRatio = viewportH / totalDrawnHeight;
        float thumbH = (std::max)(config.ScaleF(24.0f), sbTrackH * thumbRatio);
        float scrollFrac = doc.extensionScrollY / doc.extensionMaxScrollY;
        float thumbY = sbTrackY + scrollFrac * (sbTrackH - thumbH);

        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(
            D2D1::RectF(sbTrackX, thumbY, sbTrackX + sbW, thumbY + thumbH),
            config.ScaleF(3.5f), config.ScaleF(3.5f));

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushThumb;
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.20f) : D2D1::ColorF(0.12f, 0.16f, 0.24f, 0.25f), &brushThumb);
        if (brushThumb) pRT->FillRoundedRectangle(&rrcThumb, brushThumb.Get());
    }

    pRT->PopAxisAlignedClip(); // Pop Viewport Clip
}

bool EditorCanvas::HandleLearningChar(wchar_t ch) {
    if (!m_isLearningSearchFocused) return false;
    if (ch >= 32 && ch != 127) {
        m_learningSearchQuery.push_back(ch);
        m_learningScrollY = 0.0f;
        return true;
    }
    return false;
}

bool EditorCanvas::HandleLearningKeyDown(WPARAM key) {
    if (!m_isLearningSearchFocused) return false;
    if (key == VK_BACK) {
        if (!m_learningSearchQuery.empty()) {
            m_learningSearchQuery.pop_back();
            m_learningScrollY = 0.0f;
            return true;
        }
    } else if (key == VK_ESCAPE) {
        m_learningSearchQuery.clear();
        m_isLearningSearchFocused = false;
        m_learningScrollY = 0.0f;
        return true;
    }
    return false;
}

bool EditorCanvas::HandleStudentMouseMove(POINT pt) {
    auto PointInRect = [](POINT p, const D2D1_RECT_F& r) -> bool {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    };

    int oldHoverCard = m_hoveredLearningCard;
    int oldHoverFilter = m_hoveredLearningFilter;
    bool oldHoverSearch = m_hoveredLearningSearch;
    bool oldHoverSort = m_hoveredLearningSort;

    m_hoveredLearningSearch = PointInRect(pt, m_learningSearchBounds);
    m_hoveredLearningSort = PointInRect(pt, m_learningSortBounds);

    m_hoveredLearningFilter = -1;
    for (int i = 0; i < 5; ++i) {
        if (PointInRect(pt, m_learningFilterBounds[i])) {
            m_hoveredLearningFilter = i;
            break;
        }
    }

    m_hoveredLearningCard = -1;
    for (const auto& card : m_renderedCourseCards) {
        if (PointInRect(pt, card.bounds)) {
            m_hoveredLearningCard = static_cast<int>(card.courseIndex);
            break;
        }
    }

    return (oldHoverCard != m_hoveredLearningCard ||
            oldHoverFilter != m_hoveredLearningFilter ||
            oldHoverSearch != m_hoveredLearningSearch ||
            oldHoverSort != m_hoveredLearningSort);
}

bool EditorCanvas::HandleStudentMouseWheel(short delta) {
    if (m_learningMaxScrollY <= 0.0f) return false;
    float step = 45.0f;
    float oldScroll = m_learningScrollY;
    if (delta > 0) {
        m_learningScrollY = (std::max)(0.0f, m_learningScrollY - step);
    } else {
        m_learningScrollY = (std::min)(m_learningMaxScrollY, m_learningScrollY + step);
    }
    return (m_learningScrollY != oldScroll);
}

int EditorCanvas::HitTestStudentView(POINT pt) const {
    auto PointInRect = [](POINT p, const D2D1_RECT_F& r) -> bool {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    };

    if (PointInRect(pt, m_learningSearchBounds)) {
        return 50;
    }

    for (int i = 0; i < 5; ++i) {
        if (PointInRect(pt, m_learningFilterBounds[i])) {
            return 60 + i;
        }
    }

    if (PointInRect(pt, m_learningSortBounds)) {
        return 70;
    }

    for (const auto& card : m_renderedCourseCards) {
        if (PointInRect(pt, card.bounds)) {
            return 100 + static_cast<int>(card.courseIndex);
        }
    }

    // Quick sandbox buttons (backward compatibility if needed)
    if (PointInRect(pt, m_studentQuickNbBtn)) return 10;
    if (PointInRect(pt, m_studentQuickScratchBtn)) return 11;
    if (PointInRect(pt, m_studentQuickDbBtn)) return 12;

    return -1;
}

const LearningCourseDef& EditorCanvas::GetLearningCourse(size_t index) const {
    static LearningCourseDef s_empty{};
    if (index < m_learningCourses.size()) {
        return m_learningCourses[index];
    }
    return s_empty;
}

void EditorCanvas::RenderRefactorPopup(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientHeight) {
    if (!pRT || m_refactorMessage.empty()) return;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;

    if (theme.IsDarkMode()) {
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.145f, 0.145f, 0.155f, 0.98f), &bgBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.478f, 0.800f, 1.0f), &borderBrush); // #007ACC
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.88f, 0.88f, 0.88f, 1.0f), &textBrush);
    } else {
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.96f, 0.96f, 0.98f), &bgBrush);
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.478f, 0.800f, 1.0f), &borderBrush); // #007ACC
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f), &textBrush);
    }

    if (!bgBrush || !borderBrush || !textBrush) return;

    float textW = 160.0f;
    float textH = 18.0f;
    IDWriteTextFormat* pFmt = theme.fontMenu ? theme.fontMenu.Get() : theme.fontBody.Get();

    if (theme.dwriteFactory && pFmt) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                m_refactorMessage.c_str(),
                static_cast<UINT32>(m_refactorMessage.length()),
                pFmt, 400.0f, 100.0f, &pLayout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                textW = tm.widthIncludingTrailingWhitespace;
                textH = tm.height;
            }
        }
    }

    const float padX = config.ScaleF(12.0f);
    const float padY = config.ScaleF(5.0f);
    const float popupW = textW + padX * 2.0f;
    const float popupH = textH + padY * 2.0f;
    const float arrowW = config.ScaleF(10.0f);
    const float arrowH = config.ScaleF(6.0f);

    float tipX = m_lastCaretX;
    if (tipX < 20.0f) tipX = 200.0f;
    float tipY = m_lastCaretY;
    if (tipY < 40.0f) tipY = 150.0f;

    float popupLeft = tipX - popupW * 0.5f;
    if (popupLeft < config.ScaleF(10.0f)) popupLeft = config.ScaleF(10.0f);

    bool flipBelow = (tipY - popupH - arrowH < config.ScaleF(60.0f));
    float popupTop = 0.0f;
    float popupBottom = 0.0f;

    if (!flipBelow) {
        popupBottom = tipY - arrowH;
        popupTop = popupBottom - popupH;
    } else {
        popupTop = tipY + config.ScaleF(20.0f) + arrowH;
        popupBottom = popupTop + popupH;
    }

    D2D1_RECT_F rcBox = D2D1::RectF(popupLeft, popupTop, popupLeft + popupW, popupBottom);
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcBox, 4.0f, 4.0f);

    // Draw main popover balloon body
    pRT->FillRoundedRectangle(rrc, bgBrush.Get());
    pRT->DrawRoundedRectangle(rrc, borderBrush.Get(), 1.2f);

    // Draw pointer triangle
    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (pFactory) {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> pTri;
        if (SUCCEEDED(pFactory->CreatePathGeometry(&pTri))) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(pTri->Open(&sink))) {
                if (!flipBelow) {
                    // Downward triangle pointing at caret
                    sink->BeginFigure(D2D1::Point2F(tipX - arrowW * 0.5f, popupBottom - 0.5f), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(tipX + arrowW * 0.5f, popupBottom - 0.5f));
                    sink->AddLine(D2D1::Point2F(tipX, tipY));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                } else {
                    // Upward triangle pointing at caret
                    sink->BeginFigure(D2D1::Point2F(tipX - arrowW * 0.5f, popupTop + 0.5f), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(tipX + arrowW * 0.5f, popupTop + 0.5f));
                    sink->AddLine(D2D1::Point2F(tipX, tipY + config.ScaleF(20.0f)));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                }
                sink->Close();
                pRT->FillGeometry(pTri.Get(), bgBrush.Get());
                pRT->DrawGeometry(pTri.Get(), borderBrush.Get(), 1.2f);
            }
        }
    }

    // Draw popover message text
    D2D1_RECT_F rcText = D2D1::RectF(popupLeft + padX, popupTop + padY, popupLeft + popupW - padX, popupBottom - padY);
    pRT->DrawText(
        m_refactorMessage.c_str(),
        static_cast<UINT32>(m_refactorMessage.length()),
        pFmt,
        rcText,
        textBrush.Get()
    );
}

void EditorCanvas::ShowAutocomplete(const std::vector<editor::IntelliSenseItem>& items, size_t selectedIndex) {
    m_autocompleteScrollOffset = 0;
    m_autocompleteItems = items;
    m_autocompleteSelectedIdx = (selectedIndex < items.size()) ? selectedIndex : 0;
    m_showAutocomplete = !items.empty();
}

bool EditorCanvas::HandleAutocompleteMouseWheel(short delta) noexcept {
    if (!m_showAutocomplete || m_autocompleteItems.empty()) return false;
    const size_t maxVisible = 8;
    if (m_autocompleteItems.size() <= maxVisible) return false;
    size_t maxScroll = m_autocompleteItems.size() - maxVisible;

    int step = (delta > 0) ? -2 : 2;
    int newOffset = static_cast<int>(m_autocompleteScrollOffset) + step;
    if (newOffset < 0) newOffset = 0;
    if (newOffset > static_cast<int>(maxScroll)) newOffset = static_cast<int>(maxScroll);
    m_autocompleteScrollOffset = static_cast<size_t>(newOffset);
    return true;
}

void EditorCanvas::SetAutocompleteSelectedIndex(size_t idx) noexcept {
    if (idx < m_autocompleteItems.size()) {
        m_autocompleteSelectedIdx = idx;
        const size_t maxVisible = 8;
        if (m_autocompleteSelectedIdx < m_autocompleteScrollOffset) {
            m_autocompleteScrollOffset = m_autocompleteSelectedIdx;
        } else if (m_autocompleteSelectedIdx >= m_autocompleteScrollOffset + maxVisible) {
            m_autocompleteScrollOffset = m_autocompleteSelectedIdx - maxVisible + 1;
        }
    }
}

void EditorCanvas::SelectNextAutocomplete() noexcept {
    if (m_autocompleteItems.empty()) return;
    m_autocompleteSelectedIdx = (m_autocompleteSelectedIdx + 1) % m_autocompleteItems.size();
    const size_t maxVisible = 8;
    if (m_autocompleteSelectedIdx < m_autocompleteScrollOffset) {
        m_autocompleteScrollOffset = m_autocompleteSelectedIdx;
    } else if (m_autocompleteSelectedIdx >= m_autocompleteScrollOffset + maxVisible) {
        m_autocompleteScrollOffset = m_autocompleteSelectedIdx - maxVisible + 1;
    }
}

void EditorCanvas::SelectPrevAutocomplete() noexcept {
    if (m_autocompleteItems.empty()) return;
    if (m_autocompleteSelectedIdx == 0) {
        m_autocompleteSelectedIdx = m_autocompleteItems.size() - 1;
    } else {
        m_autocompleteSelectedIdx--;
    }
    const size_t maxVisible = 8;
    if (m_autocompleteSelectedIdx < m_autocompleteScrollOffset) {
        m_autocompleteScrollOffset = m_autocompleteSelectedIdx;
    } else if (m_autocompleteSelectedIdx >= m_autocompleteScrollOffset + maxVisible) {
        m_autocompleteScrollOffset = m_autocompleteSelectedIdx - maxVisible + 1;
    }
}

const editor::IntelliSenseItem* EditorCanvas::GetSelectedAutocompleteItem() const noexcept {
    if (m_autocompleteItems.empty() || m_autocompleteSelectedIdx >= m_autocompleteItems.size()) {
        return nullptr;
    }
    return &m_autocompleteItems[m_autocompleteSelectedIdx];
}

bool EditorCanvas::HitTestAutocomplete(POINT pt, int& outIdx) const {
    outIdx = -1;
    if (!m_showAutocomplete || m_autocompleteItems.empty()) return false;
    if (pt.x < m_autocompleteBounds.left || pt.x > m_autocompleteBounds.right ||
        pt.y < m_autocompleteBounds.top || pt.y > m_autocompleteBounds.bottom) {
        return false;
    }

    float itemsTop = m_autocompleteBounds.top + 4.0f;
    float itemsBottom = m_autocompleteBounds.bottom - 26.0f;
    if (pt.y >= itemsTop && pt.y < itemsBottom) {
        size_t visibleCount = (std::min)(m_autocompleteItems.size(), static_cast<size_t>(8));
        if (visibleCount > 0) {
            float itemH = (itemsBottom - itemsTop) / static_cast<float>(visibleCount);
            size_t scrollOffset = m_autocompleteScrollOffset;



            int relIdx = static_cast<int>((pt.y - itemsTop) / itemH);
            if (relIdx >= 0 && relIdx < static_cast<int>(visibleCount)) {
                size_t actualIdx = scrollOffset + static_cast<size_t>(relIdx);
                if (actualIdx < m_autocompleteItems.size()) {
                    outIdx = static_cast<int>(actualIdx);
                }
            }
        }
    }
    return true;
}

void EditorCanvas::RenderAutocompletePopup(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                           float clientWidth, float clientHeight) {
    if (!pRT || !m_showAutocomplete || m_autocompleteItems.empty()) return;

    const float itemH = config.ScaleF(24.0f);
    const float footerH = config.ScaleF(26.0f);
    const size_t maxVisible = 8;
    const size_t totalItems = m_autocompleteItems.size();
    const size_t visibleCount = (std::min)(totalItems, maxVisible);

    // VS Code IntelliSense card dimensions (Image 5 benchmark: 440px wide for ample breathing room)
    float popupW = config.ScaleF(440.0f);
    if (popupW > clientWidth - config.ScaleF(24.0f)) {
        popupW = (std::max)(config.ScaleF(300.0f), clientWidth - config.ScaleF(24.0f));
    }
    const float popupH = static_cast<float>(visibleCount) * itemH + footerH + config.ScaleF(8.0f);

    float popupX = m_lastCaretX;
    if (popupX + popupW > clientWidth - config.ScaleF(12.0f)) {
        popupX = (std::max)(config.ScaleF(10.0f), clientWidth - popupW - config.ScaleF(12.0f));
    }
    float popupY = m_lastCaretY + config.ScaleF(22.0f);
    if (popupY + popupH > clientHeight - config.ScaleF(10.0f)) {
        popupY = (std::max)(config.ScaleF(35.0f), m_lastCaretY - popupH - config.ScaleF(4.0f));
    }

    m_autocompleteBounds = D2D1::RectF(popupX, popupY, popupX + popupW, popupY + popupH);

    // 1. Elevated soft drop shadow (Image 5)
    for (int s = 1; s <= 3; ++s) {
        float expand = static_cast<float>(s) * config.ScaleF(1.5f);
        float alpha = 0.03f + static_cast<float>(s) * 0.025f;
        D2D1_RECT_F rcShadow = D2D1::RectF(
            popupX - expand, popupY - expand * 0.5f + config.ScaleF(2.0f),
            popupX + popupW + expand, popupY + popupH + expand + config.ScaleF(2.0f)
        );
        D2D1_ROUNDED_RECT rrcShadow = D2D1::RoundedRect(rcShadow, 6.0f + expand, 6.0f + expand);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadowBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, alpha), &shadowBrush);
        if (shadowBrush) pRT->FillRoundedRectangle(rrcShadow, shadowBrush.Get());
    }

    // 2. VS Code Card Body (#1e1e1e / #252526 dark card with subtle #3c3c3c border)
    D2D1_ROUNDED_RECT rrcPopup = D2D1::RoundedRect(m_autocompleteBounds, 6.0f, 6.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.12f, 0.13f, 0.98f) : D2D1::ColorF(0.97f, 0.97f, 0.98f, 0.98f),
        &bgBrush
    );
    pRT->CreateSolidColorBrush(
        theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.24f, 0.26f, 1.0f) : D2D1::ColorF(0.80f, 0.82f, 0.85f, 1.0f),
        &borderBrush
    );

    if (bgBrush) pRT->FillRoundedRectangle(rrcPopup, bgBrush.Get());
    if (borderBrush) pRT->DrawRoundedRectangle(rrcPopup, borderBrush.Get(), 1.0f);

    // Compute scroll window
    if (m_autocompleteSelectedIdx < m_autocompleteScrollOffset) {
        m_autocompleteScrollOffset = m_autocompleteSelectedIdx;
    } else if (m_autocompleteSelectedIdx >= m_autocompleteScrollOffset + visibleCount) {
        m_autocompleteScrollOffset = m_autocompleteSelectedIdx - visibleCount + 1;
    }
    if (totalItems > visibleCount) {
        if (m_autocompleteScrollOffset > totalItems - visibleCount) {
            m_autocompleteScrollOffset = totalItems - visibleCount;
        }
    } else {
        m_autocompleteScrollOffset = 0;
    }
    size_t scrollOffset = m_autocompleteScrollOffset;

    const bool hasScrollbar = (totalItems > visibleCount);
    float itemY = popupY + config.ScaleF(4.0f);

    for (size_t i = 0; i < visibleCount; ++i) {
        size_t itemIdx = scrollOffset + i;
        if (itemIdx >= totalItems) break;

        const auto& item = m_autocompleteItems[itemIdx];
        bool isSelected = (itemIdx == m_autocompleteSelectedIdx);

        D2D1_RECT_F rcItem = D2D1::RectF(popupX + config.ScaleF(4.0f), itemY, popupX + popupW - config.ScaleF(4.0f), itemY + itemH);

        // 3. Selection row highlight: Subtle VS Code blue #094771 / #264f78 (Image 5)
        if (isSelected) {
            D2D1_ROUNDED_RECT rrcSel = D2D1::RoundedRect(rcItem, 3.5f, 3.5f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.04f, 0.28f, 0.48f, 0.95f) : D2D1::ColorF(0.80f, 0.89f, 0.98f, 0.95f),
                &selBrush
            );
            if (selBrush) pRT->FillRoundedRectangle(rrcSel, selBrush.Get());
        }

        // Strict clipping for this item row: GUARANTEES no text leaks past row bounds
        pRT->PushAxisAlignedClip(rcItem, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        // 4. Icon Badge on Left (Image 5)
        float iconSize = config.ScaleF(16.0f);
        float iconX = popupX + config.ScaleF(8.0f);
        float iconY = itemY + (itemH - iconSize) / 2.0f;
        D2D1_RECT_F rcIcon = D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize);

        D2D1_COLOR_F badgeColor = D2D1::ColorF(0.6f, 0.6f, 0.6f);
        const wchar_t* badgeLetter = L"ⓚ";
        switch (item.kind) {
            case editor::CompletionKind::Property:
                badgeColor = D2D1::ColorF(0.75f, 0.52f, 0.99f); badgeLetter = L"ⓟ"; break;
            case editor::CompletionKind::Method:
                badgeColor = D2D1::ColorF(0.51f, 0.55f, 0.97f); badgeLetter = L"ⓜ"; break;
            case editor::CompletionKind::Function:
                badgeColor = D2D1::ColorF(0.65f, 0.54f, 0.98f); badgeLetter = L"ⓕ"; break;
            case editor::CompletionKind::Variable:
                badgeColor = D2D1::ColorF(0.38f, 0.65f, 0.98f); badgeLetter = L"ⓥ"; break;
            case editor::CompletionKind::Keyword:
                badgeColor = D2D1::ColorF(0.96f, 0.45f, 0.71f); badgeLetter = L"ⓚ"; break;
            case editor::CompletionKind::Tag:
                badgeColor = D2D1::ColorF(0.29f, 0.87f, 0.50f); badgeLetter = L"ⓣ"; break;
            case editor::CompletionKind::Attribute:
                badgeColor = D2D1::ColorF(0.98f, 0.57f, 0.24f); badgeLetter = L"ⓐ"; break;
            case editor::CompletionKind::Snippet:
                badgeColor = D2D1::ColorF(0.22f, 0.74f, 0.97f); badgeLetter = L"ⓢ"; break;
            case editor::CompletionKind::Class:
                badgeColor = D2D1::ColorF(0.95f, 0.65f, 0.20f); badgeLetter = L"ⓒ"; break;
            case editor::CompletionKind::Constant:
                badgeColor = D2D1::ColorF(0.40f, 0.75f, 0.95f); badgeLetter = L"ⓒ"; break;
            case editor::CompletionKind::Module:
                badgeColor = D2D1::ColorF(0.85f, 0.45f, 0.85f); badgeLetter = L"ⓜ"; break;
            case editor::CompletionKind::Folder:
            case editor::CompletionKind::File:
                break;
        }

        // Render genuine explorer-style icons for Folders & Files, or badges for symbols (Images 4 & 5)
        if (item.kind == editor::CompletionKind::Color) {
            float swatchW = config.ScaleF(13.0f);
            float swatchH = config.ScaleF(13.0f);
            float sX = iconX + (iconSize - swatchW) / 2.0f;
            float sY = iconY + (iconSize - swatchH) / 2.0f;
            D2D1_RECT_F rcSwatch = D2D1::RectF(sX, sY, sX + swatchW, sY + swatchH);
            D2D1_ROUNDED_RECT rrcSwatch = D2D1::RoundedRect(rcSwatch, 2.0f, 2.0f);

            uint32_t c = item.colorHex;
            float r = ((c >> 16) & 0xFF) / 255.0f;
            float g = ((c >> 8) & 0xFF) / 255.0f;
            float b = (c & 0xFF) / 255.0f;

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush;
            pRT->CreateSolidColorBrush(D2D1::ColorF(r, g, b, 1.0f), &fillBrush);
            if (fillBrush) pRT->FillRoundedRectangle(rrcSwatch, fillBrush.Get());

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
            pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f), &borderBrush);
            if (borderBrush) pRT->DrawRoundedRectangle(rrcSwatch, borderBrush.Get(), 1.0f);
        } else if (item.kind == editor::CompletionKind::Folder) {
            auto setiInfo = theme.GetSetiIcon(item.label, true, false);
            if (theme.fontSeti && setiInfo.pBrush) {
                pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), rcIcon, setiInfo.pBrush);
            } else {
                m_icons.DrawFileIcon(pRT, theme, rcIcon, FileIconType::Folder);
            }
        } else if (item.kind == editor::CompletionKind::File) {
            auto setiInfo = theme.GetSetiIcon(item.label, false, false);
            if (theme.fontSeti && setiInfo.pBrush) {
                pRT->DrawText(&setiInfo.glyph, 1, theme.fontSeti.Get(), rcIcon, setiInfo.pBrush);
            } else {
                auto fType = LucideIcons::GetIconTypeForPath(item.label, false, false);
                m_icons.DrawFileIcon(pRT, theme, rcIcon, fType);
            }
        } else {
            // Pill container for symbol badge (ⓟ, ⓜ, ⓕ, ⓥ, ⓚ, ⓣ, ⓐ, ⓢ)
            D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(rcIcon, 3.0f, 3.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBgBrush;
            pRT->CreateSolidColorBrush(D2D1::ColorF(badgeColor.r, badgeColor.g, badgeColor.b, 0.15f), &badgeBgBrush);
            if (badgeBgBrush) pRT->FillRoundedRectangle(rrcBadge, badgeBgBrush.Get());

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBrush;
            pRT->CreateSolidColorBrush(badgeColor, &badgeBrush);
            if (badgeBrush) {
                pRT->DrawText(badgeLetter, 1, theme.fontMenu ? theme.fontMenu.Get() : theme.fontBody.Get(), rcIcon, badgeBrush.Get());
            }
        }

        // 5. Origin Tag on Far Right (e.g. "builtins", "snippets", "html", "css", right-aligned)
        float originW = config.ScaleF(76.0f);
        float originRight = popupX + popupW - config.ScaleF(hasScrollbar ? 16.0f : 10.0f);
        float originLeft = originRight - originW;

        std::wstring originText = item.origin;
        if (originText.empty()) {
            switch (item.kind) {
                case editor::CompletionKind::Snippet: originText = L"snippets"; break;
                case editor::CompletionKind::Tag:
                case editor::CompletionKind::Attribute: originText = L"html"; break;
                case editor::CompletionKind::Folder:
                case editor::CompletionKind::File: originText = L"path"; break;
                case editor::CompletionKind::Property: originText = L"css"; break;
                case editor::CompletionKind::Method:
                case editor::CompletionKind::Function:
                case editor::CompletionKind::Variable: originText = L"builtins"; break;
                case editor::CompletionKind::Keyword: originText = L"keyword"; break;
                default: originText = L"code"; break;
            }
        }

        D2D1_RECT_F rcOrigin = D2D1::RectF(originLeft, itemY + config.ScaleF(2.0f), originRight, itemY + itemH);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> originBrush;
        pRT->CreateSolidColorBrush(
            isSelected
                ? (theme.IsDarkMode() ? D2D1::ColorF(0.70f, 0.78f, 0.88f) : D2D1::ColorF(0.35f, 0.45f, 0.60f))
                : (theme.IsDarkMode() ? D2D1::ColorF(0.50f, 0.52f, 0.56f) : D2D1::ColorF(0.55f, 0.57f, 0.62f)),
            &originBrush
        );
        if (originBrush) {
            // fontMenuShortcut has DWRITE_TEXT_ALIGNMENT_TRAILING for right-alignment
            pRT->DrawText(originText.c_str(), static_cast<UINT32>(originText.length()),
                          theme.fontMenuShortcut ? theme.fontMenuShortcut.Get() : theme.fontBody.Get(),
                          rcOrigin, originBrush.Get());
        }

        // 6. Label Text with Measured Width & Highlight (Image 5)
        float labelX = iconX + iconSize + config.ScaleF(8.0f);
        float maxLabelAvailW = originLeft - labelX - config.ScaleF(12.0f);

        float measuredLabelW = 0.0f;
        if (theme.dwriteFactory && theme.fontMenuItem) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pMeasureLayout;
            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                    item.label.c_str(), static_cast<UINT32>(item.label.length()),
                    theme.fontMenuItem.Get(), (std::max)(100.0f, maxLabelAvailW), itemH, &pMeasureLayout))) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(pMeasureLayout->GetMetrics(&tm))) {
                    measuredLabelW = tm.widthIncludingTrailingWhitespace;
                }
            }
        }
        if (measuredLabelW <= 0.0f) {
            measuredLabelW = static_cast<float>(item.label.length()) * config.ScaleF(7.5f);
        }

        float actualLabelW = (std::min)(measuredLabelW + config.ScaleF(2.0f), maxLabelAvailW * 0.55f);
        D2D1_RECT_F rcLabel = D2D1::RectF(labelX, itemY + config.ScaleF(2.0f), labelX + actualLabelW, itemY + itemH);
        ID2D1Brush* textBrush = isSelected ? (theme.IsDarkMode() ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get()) : theme.brushTextPrimary.Get();

        // Highlight matched characters within item label
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pTextLayout;
        if (theme.dwriteFactory && theme.fontMenuItem) {
            theme.dwriteFactory->CreateTextLayout(
                item.label.c_str(),
                static_cast<UINT32>(item.label.length()),
                theme.fontMenuItem.Get(),
                actualLabelW + config.ScaleF(4.0f),
                itemH,
                &pTextLayout
            );
        }

        if (pTextLayout) {
            if (item.matchLen > 0 && item.matchStart < item.label.length()) {
                UINT32 mLen = static_cast<UINT32>((std::min)(item.matchLen, item.label.length() - item.matchStart));
                DWRITE_TEXT_RANGE matchRange = { static_cast<UINT32>(item.matchStart), mLen };
                pTextLayout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, matchRange);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hlBrush;
                if (isSelected) {
                    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.00f, 0.30f, 0.75f), &hlBrush);
                } else {
                    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.22f, 0.62f, 1.0f) : D2D1::ColorF(0.0f, 0.45f, 0.88f), &hlBrush); // subtle VS Code blue
                }
                if (hlBrush) {
                    pTextLayout->SetDrawingEffect(hlBrush.Get(), matchRange);
                }
            }
            pRT->DrawTextLayout(D2D1::Point2F(labelX, itemY + config.ScaleF(2.0f)), pTextLayout.Get(), textBrush);
        } else {
            pRT->DrawText(item.label.c_str(), static_cast<UINT32>(item.label.length()),
                          theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                          rcLabel, textBrush);
        }

        // 7. Detail text with strict clipping (e.g. "(values, sep, end, file, flush)" or "CSSStyleDeclaration.border")
        float detailX = labelX + actualLabelW + config.ScaleF(8.0f);
        float detailRight = originLeft - config.ScaleF(6.0f);
        if (!item.detail.empty() && detailRight > detailX + config.ScaleF(10.0f)) {
            D2D1_RECT_F rcDetail = D2D1::RectF(detailX, itemY + config.ScaleF(2.0f), detailRight, itemY + itemH);
            pRT->PushAxisAlignedClip(rcDetail, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            ID2D1Brush* detailBrush = isSelected
                ? (theme.IsDarkMode() ? theme.brushTextSecondary.Get() : theme.brushTextPrimary.Get())
                : theme.brushTextMuted.Get();
            pRT->DrawText(item.detail.c_str(), static_cast<UINT32>(item.detail.length()),
                          theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                          rcDetail, detailBrush);
            pRT->PopAxisAlignedClip();
        }

        pRT->PopAxisAlignedClip(); // End item row clip
        itemY += itemH;
    }

    // 8. Vertical scrollbar on right if totalItems > visibleCount
    if (totalItems > visibleCount) {
        float sbW = config.ScaleF(4.0f);
        float sbPad = config.ScaleF(3.0f);
        float sbRight = popupX + popupW - sbPad;
        float sbLeft = sbRight - sbW;
        float sbTop = popupY + config.ScaleF(4.0f);
        float sbH = static_cast<float>(visibleCount) * itemH;

        D2D1_ROUNDED_RECT rrcSbTrack = D2D1::RoundedRect(D2D1::RectF(sbLeft, sbTop, sbRight, sbTop + sbH), 2.0f, 2.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.15f), &trackBrush);
        if (trackBrush) pRT->FillRoundedRectangle(rrcSbTrack, trackBrush.Get());

        float maxScroll = static_cast<float>(totalItems - visibleCount);
        float thumbH = (std::max)(config.ScaleF(14.0f), sbH * (static_cast<float>(visibleCount) / static_cast<float>(totalItems)));
        float thumbY = sbTop + (static_cast<float>(m_autocompleteScrollOffset) / maxScroll) * (sbH - thumbH);

        D2D1_ROUNDED_RECT rrcSbThumb = D2D1::RoundedRect(D2D1::RectF(sbLeft, thumbY, sbRight, thumbY + thumbH), 2.0f, 2.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> thumbBrush;
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.50f, 0.52f, 0.58f, 0.7f) : D2D1::ColorF(0.60f, 0.62f, 0.68f, 0.8f), &thumbBrush);
        if (thumbBrush) pRT->FillRoundedRectangle(rrcSbThumb, thumbBrush.Get());
    }

    // 9. Bottom Tip Bar (Image 5 benchmark: clipped, clean separator, Next Tip & ⋮ icon)
    float footerY = itemY + config.ScaleF(2.0f);
    pRT->DrawLine(
        D2D1::Point2F(popupX + config.ScaleF(4.0f), footerY),
        D2D1::Point2F(popupX + popupW - config.ScaleF(4.0f), footerY),
        borderBrush.Get(),
        0.8f
    );

    D2D1_RECT_F rcFooter = D2D1::RectF(popupX + config.ScaleF(4.0f), footerY, popupX + popupW - config.ScaleF(4.0f), popupY + popupH);
    pRT->PushAxisAlignedClip(rcFooter, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Left Tip Text
    const wchar_t* tipText = L"Press Tab to insert \u2022 \u2191\u2193 to navigate";
    D2D1_RECT_F rcTip = D2D1::RectF(popupX + config.ScaleF(8.0f), footerY + config.ScaleF(3.0f), popupX + popupW - config.ScaleF(88.0f), popupY + popupH);
    pRT->DrawText(tipText, static_cast<UINT32>(wcslen(tipText)),
                  theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                  rcTip, theme.brushTextMuted.Get());

    // Right "Next Tip" link
    const wchar_t* nextTipText = L"Next Tip";
    D2D1_RECT_F rcNextTip = D2D1::RectF(popupX + popupW - config.ScaleF(84.0f), footerY + config.ScaleF(3.0f), popupX + popupW - config.ScaleF(24.0f), popupY + popupH);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> linkBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.60f, 0.95f), &linkBrush);
    if (linkBrush) {
        pRT->DrawText(nextTipText, static_cast<UINT32>(wcslen(nextTipText)),
                      theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                      rcNextTip, linkBrush.Get());
    }

    // Right "⋮" menu dots
    const wchar_t* dotsText = L"\u22EE";
    D2D1_RECT_F rcDots = D2D1::RectF(popupX + popupW - config.ScaleF(20.0f), footerY + config.ScaleF(3.0f), popupX + popupW - config.ScaleF(6.0f), popupY + popupH);
    pRT->DrawText(dotsText, 1,
                  theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get(),
                  rcDots, theme.brushTextMuted.Get());

    pRT->PopAxisAlignedClip(); // End footer clip

    // 10. Documentation Sidecar Flyout Card (Dynamic width, height, word-wrap, and strict clipping)
    if (m_autocompleteSelectedIdx < m_autocompleteItems.size()) {
        const auto& selItem = m_autocompleteItems[m_autocompleteSelectedIdx];
        bool hasRichDoc = !selItem.documentation.empty() || selItem.isColor || (selItem.detail.length() > 38 || selItem.detail.find(L'\n') != std::wstring::npos);
        if (hasRichDoc) {
            float padX = config.ScaleF(12.0f);
            float padY = config.ScaleF(10.0f);

            // Determine whether to place flyout card to the right or left of the autocomplete menu
            float spaceRight = clientWidth - (popupX + popupW + config.ScaleF(6.0f)) - config.ScaleF(12.0f);
            float spaceLeft  = popupX - config.ScaleF(16.0f);
            bool placeOnRight = (spaceRight >= config.ScaleF(280.0f) || spaceRight >= spaceLeft);
            float maxAvailW = placeOnRight ? spaceRight : spaceLeft;
            maxAvailW = (std::max)(config.ScaleF(260.0f), (std::min)(maxAvailW, config.ScaleF(560.0f)));

            float textWrapW = maxAvailW - padX * 2.0f;

            // Pre-measure detail layout (signature or CSS property) with word-wrapping
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pDetailLayout;
            float detailH = 0.0f;
            float maxMeasuredW = config.ScaleF(260.0f);
            if (!selItem.detail.empty() && theme.dwriteFactory && (theme.fontCode || theme.fontBody)) {
                IDWriteTextFormat* pFmt = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();
                if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                        selItem.detail.c_str(), static_cast<UINT32>(selItem.detail.length()),
                        pFmt, textWrapW, 2000.0f, &pDetailLayout))) {
                    pDetailLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                    DWRITE_TEXT_METRICS tm;
                    if (SUCCEEDED(pDetailLayout->GetMetrics(&tm))) {
                        detailH = tm.height;
                        maxMeasuredW = (std::max)(maxMeasuredW, tm.width + padX * 2.0f);
                    }
                }
            }

            // Pre-measure documentation layout with word-wrapping
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pDocLayout;
            float docTextH = 0.0f;
            if (!selItem.documentation.empty() && theme.dwriteFactory && (theme.fontMenuItem || theme.fontBody)) {
                IDWriteTextFormat* pFmt = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get();
                if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                        selItem.documentation.c_str(), static_cast<UINT32>(selItem.documentation.length()),
                        pFmt, textWrapW, 2000.0f, &pDocLayout))) {
                    pDocLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                    DWRITE_TEXT_METRICS tm;
                    if (SUCCEEDED(pDocLayout->GetMetrics(&tm))) {
                        docTextH = tm.height;
                        maxMeasuredW = (std::max)(maxMeasuredW, tm.width + padX * 2.0f);
                    }
                }
            }

            float docW = (std::min)(maxAvailW, (std::max)(config.ScaleF(280.0f), maxMeasuredW));
            float docH = padY * 2.0f;
            if (pDetailLayout) docH += detailH + config.ScaleF(6.0f);
            if (selItem.isColor) docH += config.ScaleF(22.0f);
            if (pDocLayout) docH += docTextH + config.ScaleF(6.0f);
            docH = (std::min)(docH, clientHeight - config.ScaleF(30.0f));

            float docX = placeOnRight ? (popupX + popupW + config.ScaleF(4.0f)) : (popupX - docW - config.ScaleF(4.0f));
            if (docX < config.ScaleF(6.0f)) docX = config.ScaleF(6.0f);
            if (docX + docW > clientWidth - config.ScaleF(6.0f)) docW = clientWidth - docX - config.ScaleF(6.0f);

            float docY = popupY;
            if (docY + docH > clientHeight - config.ScaleF(10.0f)) {
                docY = (std::max)(config.ScaleF(35.0f), clientHeight - docH - config.ScaleF(10.0f));
            }

            D2D1_RECT_F rcDoc = D2D1::RectF(docX, docY, docX + docW, docY + docH);
            D2D1_ROUNDED_RECT rrcDoc = D2D1::RoundedRect(rcDoc, 6.0f, 6.0f);

            // Elevated drop shadow
            for (int s = 1; s <= 2; ++s) {
                float expand = static_cast<float>(s) * config.ScaleF(1.5f);
                D2D1_RECT_F rcDocShadow = D2D1::RectF(
                    docX - expand, docY - expand * 0.5f + config.ScaleF(2.0f),
                    docX + docW + expand, docY + docH + expand + config.ScaleF(2.0f)
                );
                D2D1_ROUNDED_RECT rrcDocShadow = D2D1::RoundedRect(rcDocShadow, 6.0f + expand, 6.0f + expand);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadowBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f), &shadowBrush);
                if (shadowBrush) pRT->FillRoundedRectangle(rrcDocShadow, shadowBrush.Get());
            }

            // Card background & border
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> docBgBrush;
            pRT->CreateSolidColorBrush(
                theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.98f) : D2D1::ColorF(0.96f, 0.96f, 0.98f, 0.98f),
                &docBgBrush
            );
            if (docBgBrush) pRT->FillRoundedRectangle(rrcDoc, docBgBrush.Get());
            pRT->DrawRoundedRectangle(rrcDoc, borderBrush.Get(), 1.0f);

            // Strict clip ensures NO text or swatches leak outside the card
            pRT->PushAxisAlignedClip(rcDoc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            float curDocY = docY + padY;

            // Render Detail (signature or CSS rule)
            if (pDetailLayout) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> propBrush;
                pRT->CreateSolidColorBrush(
                    theme.IsDarkMode() ? D2D1::ColorF(0.61f, 0.86f, 1.0f) : D2D1::ColorF(0.0f, 0.40f, 0.80f),
                    &propBrush
                );
                pRT->DrawTextLayout(D2D1::Point2F(docX + padX, curDocY), pDetailLayout.Get(), propBrush ? propBrush.Get() : theme.brushTextPrimary.Get());
                curDocY += detailH + config.ScaleF(6.0f);
            }

            // Render Color Swatch + Hex value if applicable
            if (selItem.isColor) {
                float sW = config.ScaleF(13.0f);
                float sH = config.ScaleF(13.0f);
                float sY = curDocY + config.ScaleF(2.0f);
                D2D1_RECT_F rcDocSwatch = D2D1::RectF(docX + padX, sY, docX + padX + sW, sY + sH);
                D2D1_ROUNDED_RECT rrcDocSwatch = D2D1::RoundedRect(rcDocSwatch, 2.0f, 2.0f);

                uint32_t c = selItem.colorHex;
                float r = ((c >> 16) & 0xFF) / 255.0f;
                float g = ((c >> 8) & 0xFF) / 255.0f;
                float b = (c & 0xFF) / 255.0f;

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> docSwatchBrush;
                pRT->CreateSolidColorBrush(D2D1::ColorF(r, g, b, 1.0f), &docSwatchBrush);
                if (docSwatchBrush) pRT->FillRoundedRectangle(rrcDocSwatch, docSwatchBrush.Get());

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> docSwatchBorder;
                pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.3f), &docSwatchBorder);
                if (docSwatchBorder) pRT->DrawRoundedRectangle(rrcDocSwatch, docSwatchBorder.Get(), 1.0f);

                std::wstring hexStr = selItem.documentation;
                if (hexStr.empty()) {
                    wchar_t buf[16];
                    swprintf_s(buf, L"#%06x", c);
                    hexStr = buf;
                }
                D2D1_RECT_F rcHex = D2D1::RectF(docX + padX + sW + config.ScaleF(8.0f), curDocY, docX + docW - padX, curDocY + config.ScaleF(20.0f));
                pRT->DrawText(
                    hexStr.c_str(), static_cast<UINT32>(hexStr.length()),
                    theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get(),
                    rcHex, theme.brushTextSecondary.Get()
                );
                curDocY += config.ScaleF(22.0f);
            }

            // Render Documentation description
            if (pDocLayout) {
                pRT->DrawTextLayout(D2D1::Point2F(docX + padX, curDocY), pDocLayout.Get(), theme.brushTextMuted.Get());
            }

            pRT->PopAxisAlignedClip();
        }
    }
}

void EditorCanvas::RenderCanvasThemeEffects(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                            float canvasLeft, float canvasWidth, float topY, float clientHeight) {
    if (!pRT || canvasWidth <= 20.0f || clientHeight <= topY) return;

    ThemeMode mode = theme.GetThemeMode();
    if (mode == ThemeMode::RetroGrid80s) {
        // Synthwave 80s 3D Perspective Wireframe Grid (Image 5)
        const float gridH = (std::min)(350.0f, (clientHeight - topY) * 0.55f);
        const float horizonY = clientHeight - gridH;
        const float centerX = canvasLeft + canvasWidth * 0.5f;

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushNeonPink;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushNeonCyan;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHorizon;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.165f, 0.522f, 0.28f), &brushNeonPink); // #FF2A85
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.941f, 1.0f, 0.22f), &brushNeonCyan);   // #00F0FF
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.165f, 0.522f, 0.45f), &brushHorizon);

        if (brushHorizon) {
            pRT->DrawLine(D2D1::Point2F(canvasLeft, horizonY), D2D1::Point2F(canvasLeft + canvasWidth, horizonY), brushHorizon.Get(), 1.5f);
        }

        // Perspective depth lines converging toward vanishing point
        if (brushNeonPink) {
            const int numLines = 14;
            for (int i = -numLines; i <= numLines; ++i) {
                float bottomX = centerX + static_cast<float>(i) * (canvasWidth / static_cast<float>(numLines * 1.5f));
                float topX = centerX + static_cast<float>(i) * (canvasWidth * 0.05f / static_cast<float>(numLines));
                pRT->DrawLine(D2D1::Point2F(topX, horizonY), D2D1::Point2F(bottomX, clientHeight), brushNeonPink.Get(), 1.0f);
            }
        }

        // Horizontal perspective lines with quadratic spacing
        if (brushNeonCyan) {
            const int numHoriz = 10;
            for (int h = 1; h <= numHoriz; ++h) {
                float t = static_cast<float>(h) / static_cast<float>(numHoriz);
                float nonLinearT = t * t;
                float lineY = horizonY + nonLinearT * gridH;
                float alpha = 0.10f + 0.25f * t;
                brushNeonCyan->SetOpacity(alpha);
                pRT->DrawLine(D2D1::Point2F(canvasLeft, lineY), D2D1::Point2F(canvasLeft + canvasWidth, lineY), brushNeonCyan.Get(), 1.0f);
            }
        }
    } else if (mode == ThemeMode::CosmicStarfield) {
        // Deep Space Cosmic Twinkling Stars & Particles
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushStar;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.92f, 1.0f, 0.6f), &brushStar);
        if (brushStar) {
            const int starCount = 50;
            for (int i = 0; i < starCount; ++i) {
                float seedX = static_cast<float>((i * 1973 + 13) % 997) / 997.0f;
                float seedY = static_cast<float>((i * 4729 + 17) % 991) / 991.0f;
                float seedR = static_cast<float>((i * 7919 + 23) % 100) / 100.0f;

                float starX = canvasLeft + seedX * canvasWidth;
                float starY = topY + seedY * (clientHeight - topY);
                float starRadius = (seedR > 0.85f) ? 1.8f : ((seedR > 0.5f) ? 1.2f : 0.8f);
                float starAlpha = (seedR > 0.85f) ? 0.75f : ((seedR > 0.5f) ? 0.50f : 0.30f);

                if (i % 3 == 0) {
                    brushStar->SetColor(D2D1::ColorF(0.655f, 0.545f, 0.980f, starAlpha)); // Nebula Purple
                } else if (i % 3 == 1) {
                    brushStar->SetColor(D2D1::ColorF(0.380f, 0.750f, 1.000f, starAlpha)); // Cyan Star
                } else {
                    brushStar->SetColor(D2D1::ColorF(1.000f, 0.950f, 0.800f, starAlpha)); // Pale Gold Star
                }
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(starX, starY), starRadius, starRadius), brushStar.Get());
            }
        }
    } else if (mode == ThemeMode::MatrixDigitalRain) {
        // Matrix Green Terminal Code Drops
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushMatrix;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 1.0f, 0.4f, 0.35f), &brushMatrix);
        if (brushMatrix) {
            const int columnCount = 28;
            for (int c = 0; c < columnCount; ++c) {
                float colX = canvasLeft + (static_cast<float>(c) + 0.5f) * (canvasWidth / static_cast<float>(columnCount));
                float seedY = static_cast<float>((c * 317 + 41) % 997) / 997.0f;
                float streamLen = 80.0f + static_cast<float>((c * 191) % 120);
                float startY = topY + seedY * (clientHeight - topY);

                brushMatrix->SetColor(D2D1::ColorF(0.0f, 1.0f, 0.4f, 0.12f));
                pRT->DrawLine(D2D1::Point2F(colX, startY), D2D1::Point2F(colX, (std::min)(clientHeight, startY + streamLen)), brushMatrix.Get(), 1.5f);

                brushMatrix->SetColor(D2D1::ColorF(0.8f, 1.0f, 0.867f, 0.45f));
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(colX, (std::min)(clientHeight - 2.0f, startY + streamLen)), 1.8f, 1.8f), brushMatrix.Get());
            }
        }
    } else if (mode == ThemeMode::AuroraBorealis) {
        // Shimmering Arctic Aurora Polar Teal Curtains
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushAurora;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.176f, 0.831f, 0.749f, 0.08f), &brushAurora);
        if (brushAurora) {
            float auroraMidY = topY + (clientHeight - topY) * 0.35f;
            for (int w = 0; w < 4; ++w) {
                float yOff = static_cast<float>(w) * 16.0f;
                brushAurora->SetColor(D2D1::ColorF(0.176f, 0.831f, 0.749f, 0.06f - w * 0.012f));
                pRT->DrawLine(D2D1::Point2F(canvasLeft, auroraMidY + yOff), D2D1::Point2F(canvasLeft + canvasWidth, auroraMidY - yOff * 0.5f), brushAurora.Get(), 8.0f);
            }
        }
    } else if (mode == ThemeMode::EmeraldForest) {
        // Glowing Fireflies Particles in deep emerald jungle
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushFirefly;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.639f, 0.902f, 0.208f, 0.5f), &brushFirefly); // #A3E635
        if (brushFirefly) {
            const int fireflyCount = 24;
            for (int f = 0; f < fireflyCount; ++f) {
                float fx = canvasLeft + (static_cast<float>((f * 617 + 11) % 991) / 991.0f) * canvasWidth;
                float fy = topY + (static_cast<float>((f * 883 + 19) % 983) / 983.0f) * (clientHeight - topY);
                float glowR = 3.5f + static_cast<float>((f * 29) % 4);
                brushFirefly->SetColor(D2D1::ColorF(0.639f, 0.902f, 0.208f, 0.15f));
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fx, fy), glowR, glowR), brushFirefly.Get());
                brushFirefly->SetColor(D2D1::ColorF(0.85f, 1.0f, 0.4f, 0.65f));
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fx, fy), 1.2f, 1.2f), brushFirefly.Get());
            }
        }
    } else if (mode == ThemeMode::SunsetGlow) {
        // Sunset Horizon Glow at the bottom
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDusk;
        pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.369f, 0.494f, 0.08f), &brushDusk); // Coral #FF5E7E
        if (brushDusk) {
            float duskH = (clientHeight - topY) * 0.35f;
            float duskY = clientHeight - duskH;
            D2D1_RECT_F rcDusk = D2D1::RectF(canvasLeft, duskY, canvasLeft + canvasWidth, clientHeight);
            pRT->FillRectangle(rcDusk, brushDusk.Get());
            brushDusk->SetColor(D2D1::ColorF(1.0f, 0.459f, 0.337f, 0.18f)); // Amber #FF7556
            pRT->DrawLine(D2D1::Point2F(canvasLeft, duskY), D2D1::Point2F(canvasLeft + canvasWidth, duskY), brushDusk.Get(), 1.0f);
        }
    }
}

void EditorCanvas::RenderSyntaxTokensLine(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                         const editor::Buffer& buffer, size_t lineIdx,
                                         float codeLeft, float lineY, float maxRight,
                                         float lineH, float charW, IDWriteTextFormat* pCodeFmt,
                                         ID2D1Brush* defaultTextBrush,
                                         const std::wstring& ext) {
    std::wstring lineStr = buffer.GetLine(lineIdx);
    if (lineStr.empty() || codeLeft >= maxRight) return;

    auto tokens = buffer.GetTokensForLine(lineIdx, ext);
    if (theme.dwriteFactory && pCodeFmt) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLineLayout;
        float availW = (std::max)(1200.0f, maxRight - codeLeft + 500.0f);
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                lineStr.c_str(),
                static_cast<UINT32>(lineStr.length()),
                pCodeFmt,
                availW,
                lineH,
                &pLineLayout))) {
            for (const auto& tok : tokens) {
                if (tok.startCol + tok.length > lineStr.length()) continue;
                ID2D1Brush* brush = defaultTextBrush ? defaultTextBrush : theme.brushTextPrimary.Get();
                switch (tok.type) {
                    case editor::TokenType::Keyword:   brush = theme.brushSyntaxKeyword.Get(); break;
                    case editor::TokenType::Type:      brush = theme.brushSyntaxType.Get(); break;
                    case editor::TokenType::String:    brush = theme.brushSyntaxString.Get(); break;
                    case editor::TokenType::Comment:   brush = theme.brushSyntaxComment.Get(); break;
                    case editor::TokenType::Number:    brush = theme.brushSyntaxNumber.Get(); break;
                    case editor::TokenType::Operator:  brush = theme.brushSyntaxOperator.Get(); break;
                    case editor::TokenType::Tag:       brush = theme.brushSyntaxTag.Get(); break;
                    case editor::TokenType::Attribute: brush = theme.brushSyntaxAttribute.Get(); break;
                    default: break;
                }
                if (!brush) brush = theme.brushTextPrimary.Get();
                DWRITE_TEXT_RANGE range = { static_cast<UINT32>(tok.startCol), static_cast<UINT32>(tok.length) };
                pLineLayout->SetDrawingEffect(brush, range);
            }
            pRT->DrawTextLayout(D2D1::Point2F(codeLeft, lineY), pLineLayout.Get(), defaultTextBrush ? defaultTextBrush : theme.brushTextPrimary.Get());
            return;
        }
    }

    if (tokens.empty()) {
        D2D1_RECT_F rcLine = D2D1::RectF(codeLeft, lineY, maxRight, lineY + lineH);
        pRT->DrawText(lineStr.c_str(), static_cast<UINT32>(lineStr.length()), pCodeFmt, rcLine,
                      defaultTextBrush ? defaultTextBrush : theme.brushTextPrimary.Get());
        return;
    }

    for (const auto& tok : tokens) {
        if (tok.startCol + tok.length > lineStr.length()) continue;
        float tokenX = codeLeft + static_cast<float>(tok.startCol) * charW;
        float tokenW = static_cast<float>(tok.length) * charW;
        if (tokenX >= maxRight) break;

        D2D1_RECT_F rcTok = D2D1::RectF(tokenX, lineY, (std::min)(tokenX + tokenW + config.ScaleF(4.0f), maxRight), lineY + lineH);

        ID2D1Brush* brush = defaultTextBrush ? defaultTextBrush : theme.brushTextPrimary.Get();
        switch (tok.type) {
            case editor::TokenType::Keyword:   brush = theme.brushSyntaxKeyword.Get(); break;
            case editor::TokenType::Type:      brush = theme.brushSyntaxType.Get(); break;
            case editor::TokenType::String:    brush = theme.brushSyntaxString.Get(); break;
            case editor::TokenType::Comment:   brush = theme.brushSyntaxComment.Get(); break;
            case editor::TokenType::Number:    brush = theme.brushSyntaxNumber.Get(); break;
            case editor::TokenType::Operator:  brush = theme.brushSyntaxOperator.Get(); break;
            case editor::TokenType::Tag:       brush = theme.brushSyntaxTag.Get(); break;
            case editor::TokenType::Attribute: brush = theme.brushSyntaxAttribute.Get(); break;
            default: break;
        }
        if (!brush) brush = theme.brushTextPrimary.Get();

        pRT->DrawText(
            lineStr.c_str() + tok.startCol,
            static_cast<UINT32>(tok.length),
            pCodeFmt,
            rcTok,
            brush
        );
    }
}

void EditorCanvas::RenderDiffView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                  editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                  float topY, float clientHeight, bool isFocused) {
    const float subHeaderH = config.ScaleF(24.0f);
    const float dividerW = config.ScaleF(32.0f);
    const float scrollbarW = config.ScaleF(12.0f);
    const float halfW = (canvasWidth - dividerW - scrollbarW) / 2.0f;
    const float gutterW = config.ScaleF(46.0f);
    const float lineH = config.ScaleF(std::round(config.fontSize * config.lineHeightMultiplier));
    const float charW = (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.8f);

    const float leftX = canvasLeft;
    const float dividerX = leftX + halfW;
    const float rightX = dividerX + dividerW;

    // Brushes for diff styling
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRedBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.18f, 0.20f, 0.28f), &brushDiffRedBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRedWord;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.75f, 0.22f, 0.25f, 0.50f), &brushDiffRedWord);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRedBar;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.878f, 0.424f, 0.459f, 1.0f), &brushDiffRedBar);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreenBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.45f, 0.20f, 0.28f), &brushDiffGreenBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreenWord;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.65f, 0.25f, 0.50f), &brushDiffGreenWord);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreenBar;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.596f, 0.765f, 0.475f, 1.0f), &brushDiffGreenBar);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBridgeModBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.20f, 0.25f, 0.35f), &brushBridgeModBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBridgeModBorder;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.75f, 0.25f, 0.30f, 0.60f), &brushBridgeModBorder);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBridgeAddBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.45f, 0.25f, 0.35f), &brushBridgeAddBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBridgeAddBorder;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.65f, 0.35f, 0.60f), &brushBridgeAddBorder);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDividerBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.10f, 0.12f, 0.85f), &brushDividerBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillRoBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.24f, 0.26f, 0.9f), &brushPillRoBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushPillEditBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.055f, 0.388f, 0.612f, 0.9f), &brushPillEditBg);

    IDWriteTextFormat* pCodeFmt = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();
    IDWriteTextFormat* pLabelFmt = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get();

    // =========================================================================
    // 1. Sub-headers (Left = HEAD / Read-only, Right = Working Tree / Editable)
    // =========================================================================
    D2D1_RECT_F rcLeftSub = D2D1::RectF(leftX, topY, dividerX, topY + subHeaderH);
    pRT->FillRectangle(rcLeftSub, theme.brushHeaderBg.Get());
    pRT->DrawLine(D2D1::Point2F(leftX, topY + subHeaderH), D2D1::Point2F(dividerX, topY + subHeaderH), theme.brushBorderSubtle.Get(), 1.0f);

    std::wstring leftTitle = doc.diffOriginalTitle.empty() ? (doc.fileName + L" (HEAD)") : doc.diffOriginalTitle;
    D2D1_RECT_F rcLeftTitle = D2D1::RectF(leftX + config.ScaleF(12.0f), topY + 2.0f, dividerX - config.ScaleF(80.0f), topY + subHeaderH - 2.0f);
    pRT->DrawText(leftTitle.c_str(), static_cast<UINT32>(leftTitle.length()),
                  pLabelFmt, rcLeftTitle, theme.brushTextPrimary.Get());

    // Left [Read-only] badge
    float roW = config.ScaleF(74.0f);
    float roH = config.ScaleF(18.0f);
    D2D1_ROUNDED_RECT rrcRo = D2D1::RoundedRect(
        D2D1::RectF(dividerX - roW - config.ScaleF(8.0f), topY + (subHeaderH - roH) / 2.0f, dividerX - config.ScaleF(8.0f), topY + (subHeaderH + roH) / 2.0f),
        3.0f, 3.0f
    );
    pRT->FillRoundedRectangle(rrcRo, brushPillRoBg.Get());
    pRT->DrawText(L"Read-only", 9, pLabelFmt,
                  D2D1::RectF(rrcRo.rect.left + 5.0f, rrcRo.rect.top, rrcRo.rect.right, rrcRo.rect.bottom),
                  theme.brushTextMuted.Get());

    // Right Sub-header
    D2D1_RECT_F rcRightSub = D2D1::RectF(rightX, topY, rightX + halfW, topY + subHeaderH);
    pRT->FillRectangle(rcRightSub, theme.brushHeaderBg.Get());
    pRT->DrawLine(D2D1::Point2F(rightX, topY + subHeaderH), D2D1::Point2F(rightX + halfW, topY + subHeaderH), theme.brushBorderSubtle.Get(), 1.0f);

    std::wstring rightTitle = doc.diffWorkingTitle.empty() ? (doc.fileName + L" (Working Tree)") : doc.diffWorkingTitle;
    D2D1_RECT_F rcRightTitle = D2D1::RectF(rightX + config.ScaleF(12.0f), topY + 2.0f, rightX + halfW - config.ScaleF(80.0f), topY + subHeaderH - 2.0f);
    pRT->DrawText(rightTitle.c_str(), static_cast<UINT32>(rightTitle.length()),
                  pLabelFmt, rcRightTitle, theme.brushTextPrimary.Get());

    // Right [Editable] badge (or Read-only if commit diff)
    const wchar_t* rightBadgeText = doc.isDiffRightReadOnly ? L"Read-only" : L"Editable";
    float editW = config.ScaleF(74.0f);
    D2D1_ROUNDED_RECT rrcEdit = D2D1::RoundedRect(
        D2D1::RectF(rightX + halfW - editW - config.ScaleF(8.0f), topY + (subHeaderH - roH) / 2.0f, rightX + halfW - config.ScaleF(8.0f), topY + (subHeaderH + roH) / 2.0f),
        3.0f, 3.0f
    );
    pRT->FillRoundedRectangle(rrcEdit, doc.isDiffRightReadOnly ? brushPillRoBg.Get() : brushPillEditBg.Get());
    pRT->DrawText(rightBadgeText, static_cast<UINT32>(wcslen(rightBadgeText)), theme.fontButtonCenter ? theme.fontButtonCenter.Get() : pLabelFmt,
                  rrcEdit.rect,
                  doc.isDiffRightReadOnly ? theme.brushTextMuted.Get() : theme.brushTextWhite.Get());

    // =========================================================================
    // 2. Code Area Layout & Scrolling
    // =========================================================================
    float codeTopY = topY + subHeaderH;
    float codeAreaH = clientHeight - codeTopY;
    size_t visibleCount = (lineH > 0.0f) ? static_cast<size_t>(codeAreaH / lineH) : 20;

    size_t leftTotal = doc.diffOriginalBuffer.GetLineCount();
    size_t rightTotal = doc.buffer.GetLineCount();
    size_t maxLines = (std::max)(leftTotal, rightTotal);
    size_t maxScroll = (maxLines > visibleCount) ? (maxLines - visibleCount) : 0;
    if (doc.scrollLine > maxScroll) doc.scrollLine = maxScroll;

    // =========================================================================
    // 3. Middle Divider (Connecting bridges between diff hunks) (Images 3 & 4)
    // =========================================================================
    D2D1_RECT_F rcDivider = D2D1::RectF(dividerX, codeTopY, dividerX + dividerW, clientHeight);
    pRT->FillRectangle(rcDivider, brushDividerBg.Get());
    pRT->DrawLine(D2D1::Point2F(dividerX, codeTopY), D2D1::Point2F(dividerX, clientHeight), theme.brushBorderSubtle.Get(), 1.0f);
    pRT->DrawLine(D2D1::Point2F(dividerX + dividerW, codeTopY), D2D1::Point2F(dividerX + dividerW, clientHeight), theme.brushBorderSubtle.Get(), 1.0f);

    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);

    pRT->PushAxisAlignedClip(rcDivider, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (const auto& hunk : doc.diffHunks) {
        float yLStart = codeTopY + (static_cast<float>(hunk.leftStart) - static_cast<float>(doc.scrollLine)) * lineH;
        float yLEnd = yLStart + static_cast<float>(hunk.leftCount) * lineH;
        if (hunk.leftCount == 0) yLEnd = yLStart;

        float yRStart = codeTopY + (static_cast<float>(hunk.rightStart) - static_cast<float>(doc.scrollLine)) * lineH;
        float yREnd = yRStart + static_cast<float>(hunk.rightCount) * lineH;
        if (hunk.rightCount == 0) yREnd = yRStart;

        if (pFactory && ((yLEnd >= codeTopY && yLStart <= clientHeight) || (yREnd >= codeTopY && yRStart <= clientHeight))) {
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> pBridge;
            if (SUCCEEDED(pFactory->CreatePathGeometry(&pBridge))) {
                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(pBridge->Open(&sink))) {
                    sink->BeginFigure(D2D1::Point2F(dividerX, yLStart), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddBezier(D2D1::BezierSegment(
                        D2D1::Point2F(dividerX + dividerW * 0.45f, yLStart),
                        D2D1::Point2F(dividerX + dividerW * 0.55f, yRStart),
                        D2D1::Point2F(dividerX + dividerW, yRStart)
                    ));
                    sink->AddLine(D2D1::Point2F(dividerX + dividerW, yREnd));
                    sink->AddBezier(D2D1::BezierSegment(
                        D2D1::Point2F(dividerX + dividerW * 0.55f, yREnd),
                        D2D1::Point2F(dividerX + dividerW * 0.45f, yLEnd),
                        D2D1::Point2F(dividerX, yLEnd)
                    ));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();

                    bool isAdd = (hunk.type == editor::OpenDocument::DiffHunkItem::HunkType::Added);
                    pRT->FillGeometry(pBridge.Get(), isAdd ? brushBridgeAddBg.Get() : brushBridgeModBg.Get());
                    pRT->DrawGeometry(pBridge.Get(), isAdd ? brushBridgeAddBorder.Get() : brushBridgeModBorder.Get(), 1.0f);
                }
            }
        }
    }
    pRT->PopAxisAlignedClip();

    // =========================================================================
    // 4. Left Pane (Original / HEAD - Read Only)
    // =========================================================================
    D2D1_RECT_F rcLeftClip = D2D1::RectF(leftX, codeTopY, dividerX, clientHeight);
    pRT->PushAxisAlignedClip(rcLeftClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Gutter Background
    D2D1_RECT_F rcLeftGutter = D2D1::RectF(leftX, codeTopY, leftX + gutterW, clientHeight);
    pRT->FillRectangle(rcLeftGutter, theme.brushCanvasBg.Get());

    size_t endLeftV = (std::min)(leftTotal, doc.scrollLine + visibleCount + 2);
    for (size_t v = doc.scrollLine; v < endLeftV; ++v) {
        float lineY = codeTopY + (v - doc.scrollLine) * lineH;
        if (lineY + lineH < codeTopY || lineY > clientHeight) continue;

        bool isChanged = doc.diffDeletedLines.count(v) > 0;
        if (isChanged) {
            // Full line red background
            D2D1_RECT_F rcLine = D2D1::RectF(leftX + gutterW, lineY, dividerX, lineY + lineH);
            pRT->FillRectangle(rcLine, brushDiffRedBg.Get());

            // Red vertical bar in gutter
            D2D1_RECT_F rcBar = D2D1::RectF(leftX + gutterW - config.ScaleF(3.0f), lineY, leftX + gutterW, lineY + lineH);
            pRT->FillRectangle(rcBar, brushDiffRedBar.Get());
        }

        // Line number (11- if changed)
        std::wstring numStr = std::to_wstring(v + 1) + (isChanged ? L"-" : L"");
        D2D1_RECT_F rcNum = D2D1::RectF(leftX + config.ScaleF(4.0f), lineY, leftX + gutterW - config.ScaleF(10.0f), lineY + lineH);
        pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()), theme.fontMenuShortcut.Get(),
                      rcNum, isChanged ? brushDiffRedBar.Get() : theme.brushLineNumber.Get());

        // Line text with syntax highlighting
        float textX = leftX + gutterW + config.ScaleF(12.0f);
        RenderSyntaxTokensLine(pRT, theme, config, doc.diffOriginalBuffer, v, textX, lineY, dividerX, lineH, charW, pCodeFmt, isChanged ? brushDiffRedBar.Get() : theme.brushTextPrimary.Get());
    }
    pRT->PopAxisAlignedClip();

    // =========================================================================
    // 5. Right Pane (Working Tree - EDITABLE) (Images 2, 3, 4)
    // =========================================================================
    D2D1_RECT_F rcRightClip = D2D1::RectF(rightX, codeTopY, rightX + halfW, clientHeight);
    pRT->PushAxisAlignedClip(rcRightClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Gutter Background
    D2D1_RECT_F rcRightGutter = D2D1::RectF(rightX, codeTopY, rightX + gutterW, clientHeight);
    pRT->FillRectangle(rcRightGutter, theme.brushCanvasBg.Get());

    auto cursor = doc.buffer.GetCursor();
    bool isRightActive = (doc.diffActivePane == 1 && isFocused);

    size_t endRightV = (std::min)(rightTotal, doc.scrollLine + visibleCount + 2);
    for (size_t v = doc.scrollLine; v < endRightV; ++v) {
        float lineY = codeTopY + (v - doc.scrollLine) * lineH;
        if (lineY + lineH < codeTopY || lineY > clientHeight) continue;

        // Active line highlight
        if (isRightActive && v == cursor.line) {
            D2D1_RECT_F rcActive = D2D1::RectF(rightX + gutterW, lineY, rightX + halfW, lineY + lineH);
            pRT->FillRectangle(rcActive, theme.brushCurrentLineBg.Get());
        }

        bool isChanged = doc.diffAddedLines.count(v) > 0;
        if (isChanged) {
            // Full line green background
            D2D1_RECT_F rcLine = D2D1::RectF(rightX + gutterW, lineY, rightX + halfW, lineY + lineH);
            pRT->FillRectangle(rcLine, brushDiffGreenBg.Get());

            // Green vertical bar in gutter
            D2D1_RECT_F rcBar = D2D1::RectF(rightX + gutterW - config.ScaleF(3.0f), lineY, rightX + gutterW, lineY + lineH);
            pRT->FillRectangle(rcBar, brushDiffGreenBar.Get());
        }

        // Line number (11+ if changed)
        std::wstring numStr = std::to_wstring(v + 1) + (isChanged ? L"+" : L"");
        D2D1_RECT_F rcNum = D2D1::RectF(rightX + config.ScaleF(4.0f), lineY, rightX + gutterW - config.ScaleF(10.0f), lineY + lineH);
        pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()), theme.fontMenuShortcut.Get(),
                      rcNum, isChanged ? brushDiffGreenBar.Get() : ((isRightActive && v == cursor.line) ? theme.brushTextPrimary.Get() : theme.brushLineNumber.Get()));

        // Line text with syntax highlighting
        float textX = rightX + gutterW + config.ScaleF(12.0f);
        RenderSyntaxTokensLine(pRT, theme, config, doc.buffer, v, textX, lineY, rightX + halfW, lineH, charW, pCodeFmt, isChanged ? brushDiffGreenBar.Get() : theme.brushTextPrimary.Get());
    }

    // Thin Deletion Line Indicators on Right Pane (Image 4 Request)
    // Instead of skipping a line to show changes, draw a thin horizontal line marker!
    for (const auto& hunk : doc.diffHunks) {
        if (hunk.type == editor::OpenDocument::DiffHunkItem::HunkType::Deleted && hunk.rightCount == 0) {
            if (hunk.rightStart >= doc.scrollLine && hunk.rightStart <= doc.scrollLine + visibleCount + 1) {
                float thinY = codeTopY + (static_cast<float>(hunk.rightStart) - static_cast<float>(doc.scrollLine)) * lineH;
                // Thin horizontal red deletion marker
                pRT->DrawLine(
                    D2D1::Point2F(rightX + gutterW, thinY),
                    D2D1::Point2F(rightX + halfW, thinY),
                    brushDiffRedBar.Get(), 2.2f
                );
                // Small indicator triangle in gutter
                float triX = rightX + gutterW - config.ScaleF(10.0f);
                pRT->DrawLine(D2D1::Point2F(triX, thinY - 3.0f), D2D1::Point2F(triX + 4.5f, thinY), brushDiffRedBar.Get(), 1.6f);
                pRT->DrawLine(D2D1::Point2F(triX + 4.5f, thinY), D2D1::Point2F(triX, thinY + 3.0f), brushDiffRedBar.Get(), 1.6f);
            }
        }
    }

    // Blinking Caret '|' on Editable Right Pane (Image 2)
    if (isRightActive && !doc.isDiffRightReadOnly && cursor.line >= doc.scrollLine && cursor.line <= doc.scrollLine + visibleCount) {
        float caretY = codeTopY + (cursor.line - doc.scrollLine) * lineH;
        float caretX = rightX + gutterW + config.ScaleF(12.0f) + static_cast<float>(cursor.col) * charW;
        m_lastCaretX = caretX;
        m_lastCaretY = caretY;
        if (m_caretVisible && caretX < rightX + halfW) {
            ID2D1Brush* caretBrush = theme.brushTextPrimary.Get();
            const std::wstring& style = config.cursorStyle;
            if (style == L"block") {
                D2D1_RECT_F rcBlock = D2D1::RectF(caretX, caretY + config.ScaleF(1.0f), caretX + charW, caretY + lineH - config.ScaleF(1.0f));
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> blockFill;
                pRT->CreateSolidColorBrush(
                    theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.65f) : D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.65f),
                    &blockFill
                );
                pRT->FillRectangle(rcBlock, blockFill ? blockFill.Get() : caretBrush);
                pRT->DrawRectangle(rcBlock, caretBrush, 1.0f);
            } else if (style == L"underline") {
                float underY = caretY + lineH - config.ScaleF(2.5f);
                pRT->DrawLine(D2D1::Point2F(caretX, underY), D2D1::Point2F(caretX + charW, underY), caretBrush, 2.5f);
            } else {
                pRT->DrawLine(
                    D2D1::Point2F(caretX, caretY + config.ScaleF(2.0f)),
                    D2D1::Point2F(caretX, caretY + lineH - config.ScaleF(2.0f)),
                    caretBrush, 1.5f
                );
            }
        }
    }

    pRT->PopAxisAlignedClip();

    // =========================================================================
    // 6. Vertical Scrollbar on Far Right
    // =========================================================================
    float sbLeft = rightX + halfW;
    D2D1_RECT_F rcTrack = D2D1::RectF(sbLeft, codeTopY, sbLeft + scrollbarW, clientHeight);
    doc.scrollbarTrack = rcTrack;
    pRT->FillRectangle(rcTrack, theme.brushCanvasBg.Get());

    if (maxLines > visibleCount) {
        float thumbRatio = (static_cast<float>(visibleCount) / static_cast<float>(maxLines));
        float thumbH = (std::max)(config.ScaleF(24.0f), codeAreaH * thumbRatio);
        float scrollPct = (maxScroll > 0) ? (static_cast<float>(doc.scrollLine) / static_cast<float>(maxScroll)) : 0.0f;
        float thumbY = codeTopY + (codeAreaH - thumbH) * scrollPct;
        doc.scrollbarThumb = D2D1::RectF(sbLeft + 2.0f, thumbY, sbLeft + scrollbarW - 2.0f, thumbY + thumbH);
        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(doc.scrollbarThumb, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrcThumb, theme.brushBorderSubtle.Get());
    }
}

void EditorCanvas::RenderDiffBinaryView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                        editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                        float topY, float clientHeight) {
    const float subHeaderH = config.ScaleF(24.0f);
    const float dividerW = config.ScaleF(32.0f);
    const float halfW = (canvasWidth - dividerW) / 2.0f;
    const float leftX = canvasLeft;
    const float dividerX = leftX + halfW;
    const float rightX = dividerX + dividerW;

    IDWriteTextFormat* pLabelFmt = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get();

    // Sub-headers
    D2D1_RECT_F rcLeftSub = D2D1::RectF(leftX, topY, dividerX, topY + subHeaderH);
    pRT->FillRectangle(rcLeftSub, theme.brushHeaderBg.Get());
    pRT->DrawLine(D2D1::Point2F(leftX, topY + subHeaderH), D2D1::Point2F(dividerX, topY + subHeaderH), theme.brushBorderSubtle.Get(), 1.0f);
    std::wstring leftTitle = doc.diffOriginalTitle.empty() ? (doc.fileName + L" (HEAD)") : doc.diffOriginalTitle;
    pRT->DrawText(leftTitle.c_str(), static_cast<UINT32>(leftTitle.length()), pLabelFmt,
                  D2D1::RectF(leftX + config.ScaleF(12.0f), topY + 2.0f, dividerX - config.ScaleF(80.0f), topY + subHeaderH - 2.0f),
                  theme.brushTextPrimary.Get());

    D2D1_RECT_F rcRightSub = D2D1::RectF(rightX, topY, rightX + halfW, topY + subHeaderH);
    pRT->FillRectangle(rcRightSub, theme.brushHeaderBg.Get());
    pRT->DrawLine(D2D1::Point2F(rightX, topY + subHeaderH), D2D1::Point2F(rightX + halfW, topY + subHeaderH), theme.brushBorderSubtle.Get(), 1.0f);
    std::wstring rightTitle = doc.diffWorkingTitle.empty() ? (doc.fileName + L" (Working Tree)") : doc.diffWorkingTitle;
    pRT->DrawText(rightTitle.c_str(), static_cast<UINT32>(rightTitle.length()), pLabelFmt,
                  D2D1::RectF(rightX + config.ScaleF(12.0f), topY + 2.0f, rightX + halfW - config.ScaleF(80.0f), topY + subHeaderH - 2.0f),
                  theme.brushTextPrimary.Get());

    // Center Banner: Binary File Not Shown
    float contentTop = topY + subHeaderH;
    float contentH = clientHeight - contentTop;
    float centerY = contentTop + contentH * 0.40f;
    float centerX = canvasLeft + canvasWidth * 0.5f;

    // Card background
    float cardW = config.ScaleF(460.0f);
    float cardH = config.ScaleF(130.0f);
    D2D1_RECT_F rcCard = D2D1::RectF(centerX - cardW * 0.5f, centerY - cardH * 0.5f, centerX + cardW * 0.5f, centerY + cardH * 0.5f);
    D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcCard, 8.0f, 8.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCardBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.95f), &brushCardBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBadge;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.45f, 0.95f, 1.0f), &brushBadge);

    pRT->FillRoundedRectangle(rrcCard, brushCardBg.Get());
    pRT->DrawRoundedRectangle(rrcCard, theme.brushBorderSubtle.Get(), 1.0f);

    // Pill badge: [BINARY]
    D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(
        D2D1::RectF(centerX - config.ScaleF(36.0f), rcCard.top + config.ScaleF(14.0f), centerX + config.ScaleF(36.0f), rcCard.top + config.ScaleF(32.0f)),
        3.0f, 3.0f
    );
    pRT->FillRoundedRectangle(rrcPill, brushBadge.Get());
    const wchar_t* pillText = L"BINARY";
    pRT->DrawText(pillText, 6, theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : pLabelFmt,
                  rrcPill.rect, theme.brushTextWhite.Get());

    // Headline: "Binary file not shown"
    const wchar_t* headline = L"Binary file not shown";
    D2D1_RECT_F rcHead = D2D1::RectF(rcCard.left, rcCard.top + config.ScaleF(38.0f), rcCard.right, rcCard.top + config.ScaleF(66.0f));
    pRT->DrawText(headline, static_cast<UINT32>(wcslen(headline)),
                  theme.fontTitle.Get() ? theme.fontTitle.Get() : theme.fontBody.Get(),
                  rcHead, theme.brushTextPrimary.Get());

    // Subtitle
    std::wstring targetName = doc.diffBinaryFileName.empty() ? doc.fileName : doc.diffBinaryFileName;
    std::wstring sub = L"The file '" + targetName + L"' is binary or an executable and cannot be compared as text.";
    D2D1_RECT_F rcSub = D2D1::RectF(rcCard.left + config.ScaleF(16.0f), rcCard.top + config.ScaleF(72.0f), rcCard.right - config.ScaleF(16.0f), rcCard.bottom - config.ScaleF(10.0f));
    pRT->DrawText(sub.c_str(), static_cast<UINT32>(sub.length()),
                  theme.fontBody.Get(), rcSub, theme.brushTextSecondary.Get());
}

void EditorCanvas::RenderMultiFileDiffView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                           editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                           float topY, float clientHeight, bool isPaneFocused) {
    (void)isPaneFocused;
    const float headerBarH = config.ScaleF(28.0f);
    const float lineH = config.ScaleF(std::round(config.fontSize * config.lineHeightMultiplier));
    const float gutterW = config.ScaleF(46.0f);
    const float scrollbarW = config.ScaleF(12.0f);
    const float dividerW = config.ScaleF(24.0f);
    const float usableW = canvasWidth - scrollbarW;
    const float halfW = (usableW - dividerW) * 0.5f;

    const float leftX = canvasLeft;
    const float dividerX = leftX + halfW;
    const float rightX = dividerX + dividerW;

    IDWriteTextFormat* pCodeFmt = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();
    IDWriteTextFormat* pLabelFmt = theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontBody.Get();

    // Clip rendering to content area
    pRT->PushAxisAlignedClip(D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, clientHeight), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Calculate total visual rows for scrollbar
    size_t totalVisualRows = 0;
    for (const auto& entry : doc.multiFileDiffs) {
        totalVisualRows += 2; // header bar (~1.4 rows)
        if (!entry.isCollapsed) {
            if (entry.isBinary) {
                totalVisualRows += 3;
            } else {
                size_t nLeft = entry.originalBuffer.GetLineCount();
                size_t nRight = entry.workingBuffer.GetLineCount();
                totalVisualRows += (std::max)(nLeft, nRight);
            }
        }
    }
    if (totalVisualRows == 0) totalVisualRows = 1;

    // Viewport row capacity
    float viewH = clientHeight - topY;
    size_t visibleRows = static_cast<size_t>(viewH / lineH);
    if (visibleRows < 2) visibleRows = 2;
    if (doc.scrollLine > totalVisualRows) doc.scrollLine = (totalVisualRows > visibleRows) ? (totalVisualRows - visibleRows) : 0;

    // Virtual Y start based on scrollLine
    float curY = topY - static_cast<float>(doc.scrollLine) * lineH;

    // Common brushes for diff highlights
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRedBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.18f, 0.20f, 0.28f), &brushDiffRedBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffRedBar;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.878f, 0.424f, 0.459f, 1.0f), &brushDiffRedBar);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreenBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.45f, 0.20f, 0.28f), &brushDiffGreenBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushDiffGreenBar;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.40f, 0.78f, 0.45f, 1.0f), &brushDiffGreenBar);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHeaderBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.14f, 0.14f, 0.16f, 1.0f), &brushHeaderBg);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHeaderBorder;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.24f, 0.26f, 1.0f), &brushHeaderBorder);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushHiddenLinesBg;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.16f, 0.16f, 0.18f, 0.85f), &brushHiddenLinesBg);

    for (size_t fIdx = 0; fIdx < doc.multiFileDiffs.size(); ++fIdx) {
        auto& entry = doc.multiFileDiffs[fIdx];

        // 1. Render Collapsible File Header Banner (Image 4)
        entry.headerBounds = D2D1::RectF(canvasLeft, curY, canvasLeft + usableW, curY + headerBarH);

        if (curY + headerBarH >= topY && curY <= clientHeight) {
            pRT->FillRectangle(entry.headerBounds, brushHeaderBg.Get());
            pRT->DrawLine(D2D1::Point2F(canvasLeft, curY), D2D1::Point2F(canvasLeft + usableW, curY), brushHeaderBorder.Get(), 1.0f);
            pRT->DrawLine(D2D1::Point2F(canvasLeft, curY + headerBarH), D2D1::Point2F(canvasLeft + usableW, curY + headerBarH), brushHeaderBorder.Get(), 1.0f);

            // Expand/collapse chevron
            const wchar_t* chv = entry.isCollapsed ? L"›" : L"⌄";
            D2D1_RECT_F rcChv = D2D1::RectF(canvasLeft + config.ScaleF(10.0f), curY + config.ScaleF(2.0f), canvasLeft + config.ScaleF(24.0f), curY + headerBarH);
            pRT->DrawText(chv, 1, pLabelFmt, rcChv, theme.brushTextMuted.Get());

            // File icon representation (e.g. C- for C++, js for JS, etc.)
            float textX = canvasLeft + config.ScaleF(28.0f);
            float iconW = config.ScaleF(18.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushIcon;
            if (entry.fileName.ends_with(L".hpp") || entry.fileName.ends_with(L".h")) {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.65f, 0.95f, 1.0f), &brushIcon);
            } else if (entry.fileName.ends_with(L".cpp") || entry.fileName.ends_with(L".c")) {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.50f, 0.90f, 1.0f), &brushIcon);
            } else if (entry.fileName.ends_with(L".json") || entry.fileName.ends_with(L".yml") || entry.fileName.ends_with(L".yaml")) {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.75f, 0.25f, 1.0f), &brushIcon);
            } else {
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.70f, 0.70f, 0.75f, 1.0f), &brushIcon);
            }
            D2D1_RECT_F rcIcon = D2D1::RectF(textX, curY + config.ScaleF(5.0f), textX + iconW, curY + config.ScaleF(23.0f));
            pRT->DrawRectangle(rcIcon, brushIcon.Get(), 1.2f);
            textX += iconW + config.ScaleF(8.0f);

            // File Name (bold white)
            float nameW = static_cast<float>(entry.fileName.length()) * config.ScaleF(8.0f);
            D2D1_RECT_F rcName = D2D1::RectF(textX, curY + config.ScaleF(4.0f), textX + nameW, curY + headerBarH);
            pRT->DrawText(entry.fileName.c_str(), static_cast<UINT32>(entry.fileName.length()), pLabelFmt, rcName, theme.brushTextPrimary.Get());
            textX += nameW + config.ScaleF(10.0f);

            // Relative directory (dimmed gray, matching Image 4)
            if (!entry.relDir.empty()) {
                D2D1_RECT_F rcDir = D2D1::RectF(textX, curY + config.ScaleF(4.0f), canvasLeft + usableW - config.ScaleF(120.0f), curY + headerBarH);
                pRT->DrawText(entry.relDir.c_str(), static_cast<UINT32>(entry.relDir.length()), pLabelFmt, rcDir, theme.brushTextMuted.Get());
            }

            // Right-side stats/pills
            if (entry.isBinary) {
                float badgeW = config.ScaleF(56.0f);
                float badgeH = config.ScaleF(16.0f);
                D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(
                    D2D1::RectF(canvasLeft + usableW - badgeW - config.ScaleF(12.0f), curY + (headerBarH - badgeH) * 0.5f,
                                canvasLeft + usableW - config.ScaleF(12.0f), curY + (headerBarH + badgeH) * 0.5f),
                    3.0f, 3.0f
                );
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBadge;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.45f, 0.95f, 1.0f), &brushBadge);
                pRT->FillRoundedRectangle(rrcPill, brushBadge.Get());
                const wchar_t* bText = L"BINARY";
                pRT->DrawText(bText, 6, pLabelFmt, rrcPill.rect, theme.brushTextWhite.Get());
            } else {
                size_t nAdds = entry.addedLines.size();
                size_t nDels = entry.deletedLines.size();
                std::wstring statStr = L"+" + std::to_wstring(nAdds) + L" -" + std::to_wstring(nDels);
                D2D1_RECT_F rcStat = D2D1::RectF(canvasLeft + usableW - config.ScaleF(110.0f), curY + config.ScaleF(4.0f),
                                                 canvasLeft + usableW - config.ScaleF(14.0f), curY + headerBarH);
                pRT->DrawText(statStr.c_str(), static_cast<UINT32>(statStr.length()), pLabelFmt, rcStat, brushDiffGreenBar.Get());
            }
        }
        curY += headerBarH;

        // 2. Render File Diff Body (if expanded)
        if (!entry.isCollapsed) {
            if (entry.isBinary) {
                // Binary placeholder box
                float boxH = config.ScaleF(44.0f);
                if (curY + boxH >= topY && curY <= clientHeight) {
                    D2D1_RECT_F rcBox = D2D1::RectF(canvasLeft, curY, canvasLeft + usableW, curY + boxH);
                    pRT->FillRectangle(rcBox, theme.brushCanvasBg.Get());
                    const wchar_t* binNotice = L"Binary file not shown";
                    D2D1_RECT_F rcNotice = D2D1::RectF(canvasLeft + config.ScaleF(40.0f), curY + config.ScaleF(12.0f), canvasLeft + usableW, curY + boxH);
                    pRT->DrawText(binNotice, static_cast<UINT32>(wcslen(binNotice)), pLabelFmt, rcNotice, theme.brushTextMuted.Get());
                }
                curY += boxH;
            } else {
                size_t nLeft = entry.originalBuffer.GetLineCount();
                size_t nRight = entry.workingBuffer.GetLineCount();
                size_t maxLines = (std::max)(nLeft, nRight);

                // Render side-by-side lines
                size_t l = 0;
                while (l < maxLines) {
                    // Check for consecutive unchanged lines to show hidden lines banner (Image 4)
                    bool isLeftDel = (l < nLeft && entry.deletedLines.contains(l));
                    bool isRightAdd = (l < nRight && entry.addedLines.contains(l));

                    if (!isLeftDel && !isRightAdd) {
                        size_t unchangedCount = 0;
                        size_t peek = l;
                        while (peek < maxLines) {
                            bool pd = (peek < nLeft && entry.deletedLines.contains(peek));
                            bool pa = (peek < nRight && entry.addedLines.contains(peek));
                            if (pd || pa) break;
                            unchangedCount++;
                            peek++;
                        }

                        if (unchangedCount > 8) {
                            // Render Hidden Lines Bar (Image 4)
                            float barH = lineH;
                            if (curY + barH >= topY && curY <= clientHeight) {
                                D2D1_RECT_F rcBar = D2D1::RectF(canvasLeft, curY, canvasLeft + usableW, curY + barH);
                                pRT->FillRectangle(rcBar, brushHiddenLinesBg.Get());
                                pRT->DrawLine(D2D1::Point2F(canvasLeft, curY), D2D1::Point2F(canvasLeft + usableW, curY), brushHeaderBorder.Get(), 0.8f);
                                pRT->DrawLine(D2D1::Point2F(canvasLeft, curY + barH), D2D1::Point2F(canvasLeft + usableW, curY + barH), brushHeaderBorder.Get(), 0.8f);

                                std::wstring hidText = L"❊  " + std::to_wstring(unchangedCount) + L" hidden lines  ❊";
                                D2D1_RECT_F rcHidText = D2D1::RectF(canvasLeft + gutterW, curY + 1.0f, canvasLeft + usableW, curY + barH);
                                pRT->DrawText(hidText.c_str(), static_cast<UINT32>(hidText.length()), pLabelFmt, rcHidText, theme.brushTextMuted.Get());
                            }
                            curY += barH;
                            l += unchangedCount;
                            continue;
                        }
                    }

                    if (curY + lineH >= topY && curY <= clientHeight) {
                        // Left pane line (Parent commit)
                        if (l < nLeft) {
                            D2D1_RECT_F rcLeftLine = D2D1::RectF(leftX, curY, dividerX, curY + lineH);
                            if (isLeftDel) {
                                pRT->FillRectangle(rcLeftLine, brushDiffRedBg.Get());
                                pRT->FillRectangle(D2D1::RectF(leftX, curY, leftX + 3.0f, curY + lineH), brushDiffRedBar.Get());
                            }
                            // Gutter number
                            std::wstring num = std::to_wstring(l + 1);
                            D2D1_RECT_F rcGutter = D2D1::RectF(leftX + 2.0f, curY + 1.0f, leftX + gutterW - 6.0f, curY + lineH);
                            pRT->DrawText(num.c_str(), static_cast<UINT32>(num.length()), pCodeFmt, rcGutter, theme.brushLineNumber.Get());

                            // Text with syntax highlighting
                            float textX = leftX + gutterW + config.ScaleF(8.0f);
                            RenderSyntaxTokensLine(pRT, theme, config, entry.originalBuffer, l, textX, curY + 1.0f, dividerX, lineH, config.ScaleF(7.8f), pCodeFmt, isLeftDel ? brushDiffRedBar.Get() : theme.brushTextPrimary.Get());
                        }

                        // Right pane line (This commit)
                        if (l < nRight) {
                            D2D1_RECT_F rcRightLine = D2D1::RectF(rightX, curY, rightX + halfW, curY + lineH);
                            if (isRightAdd) {
                                pRT->FillRectangle(rcRightLine, brushDiffGreenBg.Get());
                                pRT->FillRectangle(D2D1::RectF(rightX, curY, rightX + 3.0f, curY + lineH), brushDiffGreenBar.Get());
                            }
                            // Gutter number
                            std::wstring num = std::to_wstring(l + 1);
                            D2D1_RECT_F rcGutter = D2D1::RectF(rightX + 2.0f, curY + 1.0f, rightX + gutterW - 6.0f, curY + lineH);
                            pRT->DrawText(num.c_str(), static_cast<UINT32>(num.length()), pCodeFmt, rcGutter, theme.brushLineNumber.Get());

                            // Text with syntax highlighting
                            float textX = rightX + gutterW + config.ScaleF(8.0f);
                            RenderSyntaxTokensLine(pRT, theme, config, entry.workingBuffer, l, textX, curY + 1.0f, rightX + halfW, lineH, config.ScaleF(7.8f), pCodeFmt, isRightAdd ? brushDiffGreenBar.Get() : theme.brushTextPrimary.Get());
                        }

                        // Divider between left and right
                        pRT->DrawLine(D2D1::Point2F(dividerX + dividerW * 0.5f, curY), D2D1::Point2F(dividerX + dividerW * 0.5f, curY + lineH), brushHeaderBorder.Get(), 0.5f);
                    }
                    curY += lineH;
                    l++;
                }
            }
        }
    }

    // Scrollbar Track & Thumb
    float trackLeft = canvasLeft + canvasWidth - scrollbarW;
    doc.scrollbarTrack = D2D1::RectF(trackLeft, topY, canvasLeft + canvasWidth, clientHeight);
    pRT->FillRectangle(doc.scrollbarTrack, theme.brushHeaderBg.Get());

    if (totalVisualRows > visibleRows) {
        float ratio = static_cast<float>(visibleRows) / static_cast<float>(totalVisualRows);
        float thumbH = (std::max)(viewH * ratio, config.ScaleF(24.0f));
        float scrollRange = viewH - thumbH;
        float progress = static_cast<float>(doc.scrollLine) / static_cast<float>(totalVisualRows - visibleRows);
        float thumbY = topY + progress * scrollRange;

        doc.scrollbarThumb = D2D1::RectF(trackLeft + 2.0f, thumbY, trackLeft + scrollbarW - 2.0f, thumbY + thumbH);
        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(doc.scrollbarThumb, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrcThumb, theme.brushBorderSubtle.Get());
    }

    pRT->PopAxisAlignedClip();
}

const EditorCanvas::ColorSwatchHit* EditorCanvas::HitTestColorSwatch(POINT pt) const {
    float x = static_cast<float>(pt.x);
    float y = static_cast<float>(pt.y);
    for (const auto& s : m_visibleColorSwatches) {
        if (x >= s.bounds.left - 3.0f && x <= s.bounds.right + 3.0f &&
            y >= s.bounds.top - 3.0f && y <= s.bounds.bottom + 3.0f) {
            return &s;
        }
    }
    return nullptr;
}

bool EditorCanvas::HitTestCrossFilePill(POINT pt) const {
    if (!m_prediction.isValid || !m_prediction.isCrossFile) return false;
    return (pt.x >= m_crossFilePillBounds.left && pt.x <= m_crossFilePillBounds.right &&
            pt.y >= m_crossFilePillBounds.top && pt.y <= m_crossFilePillBounds.bottom);
}

} // namespace phide::ui
