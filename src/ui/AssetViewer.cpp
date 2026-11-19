/**
 * @file AssetViewer.cpp
 * @brief Implementation of the built-in Raster Image, SVG Live Split Preview, and Font Waterfall viewer (#5).
 */

#include "phide/ui/AssetViewer.hpp"
#include "phide/editor/OpenDocument.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace phide::ui {

using Microsoft::WRL::ComPtr;

namespace {

static bool PtInRectF(POINT pt, const D2D1_RECT_F& r) noexcept {
    return static_cast<float>(pt.x) >= r.left && static_cast<float>(pt.x) <= r.right &&
           static_cast<float>(pt.y) >= r.top  && static_cast<float>(pt.y) <= r.bottom;
}

static std::wstring ToUpperExt(const std::wstring& path) {
    auto dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring ext = path.substr(dot + 1);
    for (auto& ch : ext) ch = static_cast<wchar_t>(towupper(ch));
    return ext;
}

static std::wstring FormatByteSize(uint64_t bytes) {
    wchar_t buf[64]{};
    if (bytes < 1024u) {
        swprintf_s(buf, L"%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024u * 1024u) {
        swprintf_s(buf, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        swprintf_s(buf, L"%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buf;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

static bool CopyWideTextToClipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty()) return false;
    if (!OpenClipboard(hwnd)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    void* pDst = GlobalLock(hMem);
    if (!pDst) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }
    std::memcpy(pDst, text.c_str(), bytes);
    GlobalUnlock(hMem);
    if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

static std::string EncodeBase64(const std::vector<uint8_t>& data) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t b0 = data[i];
        uint32_t b1 = (i + 1 < data.size()) ? data[i + 1] : 0;
        uint32_t b2 = (i + 2 < data.size()) ? data[i + 2] : 0;
        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kTable[(triple >> 18) & 0x3F]);
        out.push_back(kTable[(triple >> 12) & 0x3F]);
        out.push_back((i + 1 < data.size()) ? kTable[(triple >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < data.size()) ? kTable[triple & 0x3F] : '=');
    }
    return out;
}

static std::wstring MimeTypeForAssetPath(const std::wstring& path) {
    std::wstring ext = ToUpperExt(path);
    if (ext == L"PNG") return L"image/png";
    if (ext == L"JPG" || ext == L"JPEG") return L"image/jpeg";
    if (ext == L"GIF") return L"image/gif";
    if (ext == L"WEBP") return L"image/webp";
    if (ext == L"BMP") return L"image/bmp";
    if (ext == L"ICO") return L"image/x-icon";
    if (ext == L"SVG") return L"image/svg+xml";
    if (ext == L"TTF") return L"font/ttf";
    if (ext == L"OTF") return L"font/otf";
    if (ext == L"WOFF") return L"font/woff";
    if (ext == L"WOFF2") return L"font/woff2";
    return L"application/octet-stream";
}

static uint16_t ReadBE16(const std::vector<uint8_t>& buf, size_t off) noexcept {
    if (off + 2 > buf.size()) return 0;
    return static_cast<uint16_t>((static_cast<uint16_t>(buf[off]) << 8) | buf[off + 1]);
}

static uint32_t ReadBE32(const std::vector<uint8_t>& buf, size_t off) noexcept {
    if (off + 4 > buf.size()) return 0;
    return (static_cast<uint32_t>(buf[off]) << 24) |
           (static_cast<uint32_t>(buf[off + 1]) << 16) |
           (static_cast<uint32_t>(buf[off + 2]) << 8) |
           static_cast<uint32_t>(buf[off + 3]);
}

/**
 * @brief Extracts SFNT metadata (family, subfamily, version, designer, license), glyph count,
 *        unitsPerEm, fixed-pitch flag, and OpenType GSUB/GPOS feature tags from raw TTF/OTF bytes.
 */
static void ParseSfntMetadata(const std::vector<uint8_t>& bytes, AssetViewerState& st) {
    if (bytes.size() < 12) return;
    const uint32_t sfntTag = ReadBE32(bytes, 0);
    // TrueType (0x00010000 / 'true') or OpenType CFF ('OTTO')
    if (sfntTag != 0x00010000u && sfntTag != 0x4F54544Fu && sfntTag != 0x74727565u) return;

    const uint16_t numTables = ReadBE16(bytes, 4);
    if (numTables == 0 || 12u + static_cast<size_t>(numTables) * 16u > bytes.size()) return;

    struct TableLoc { uint32_t off = 0; uint32_t len = 0; };
    TableLoc nameTbl{}, headTbl{}, maxpTbl{}, postTbl{}, gsubTbl{}, gposTbl{};
    for (uint16_t i = 0; i < numTables; ++i) {
        size_t recOff = 12u + static_cast<size_t>(i) * 16u;
        uint32_t tag = ReadBE32(bytes, recOff);
        uint32_t off = ReadBE32(bytes, recOff + 8);
        uint32_t len = ReadBE32(bytes, recOff + 12);
        if (static_cast<uint64_t>(off) + len > bytes.size()) continue;
        if (tag == 0x6E616D65u) nameTbl = { off, len };      // 'name'
        else if (tag == 0x68656164u) headTbl = { off, len }; // 'head'
        else if (tag == 0x6D617870u) maxpTbl = { off, len }; // 'maxp'
        else if (tag == 0x706F7374u) postTbl = { off, len }; // 'post'
        else if (tag == 0x47535542u) gsubTbl = { off, len }; // 'GSUB'
        else if (tag == 0x47504F53u) gposTbl = { off, len }; // 'GPOS'
    }

    if (headTbl.len >= 20) st.fontUnitsPerEm = ReadBE16(bytes, headTbl.off + 18);
    if (maxpTbl.len >= 6)  st.fontNumGlyphs  = ReadBE16(bytes, maxpTbl.off + 4);
    if (postTbl.len >= 16) st.fontIsFixedPitch = (ReadBE32(bytes, postTbl.off + 12) != 0);

    if (nameTbl.len >= 6) {
        const uint16_t count = ReadBE16(bytes, nameTbl.off + 2);
        const uint16_t stringOffset = ReadBE16(bytes, nameTbl.off + 4);
        const size_t recordsBase = static_cast<size_t>(nameTbl.off) + 6u;
        const size_t storageBase = static_cast<size_t>(nameTbl.off) + stringOffset;
        if (storageBase <= bytes.size()) {
            for (uint16_t i = 0; i < count; ++i) {
                size_t rOff = recordsBase + static_cast<size_t>(i) * 12u;
                if (rOff + 12u > bytes.size()) break;
                uint16_t platformId = ReadBE16(bytes, rOff);
                uint16_t nameId = ReadBE16(bytes, rOff + 6);
                uint16_t len = ReadBE16(bytes, rOff + 8);
                uint16_t off = ReadBE16(bytes, rOff + 10);
                if (nameId != 1 && nameId != 2 && nameId != 5 && nameId != 8 &&
                    nameId != 9 && nameId != 13 && nameId != 16 && nameId != 17) {
                    continue;
                }
                size_t strStart = storageBase + off;
                if (strStart + len > bytes.size() || len == 0) continue;

                std::wstring decoded;
                if (platformId == 3 || platformId == 0) {
                    for (size_t k = 0; k + 1 < len; k += 2) {
                        wchar_t wc = static_cast<wchar_t>((static_cast<uint16_t>(bytes[strStart + k]) << 8) |
                                                          bytes[strStart + k + 1]);
                        if (wc != L'\0') decoded.push_back(wc);
                    }
                } else if (platformId == 1) {
                    for (size_t k = 0; k < len; ++k) {
                        if (bytes[strStart + k] != 0) decoded.push_back(static_cast<wchar_t>(bytes[strStart + k]));
                    }
                }
                if (decoded.empty()) continue;
                if (nameId == 16 || (nameId == 1 && st.fontFamilyName.empty())) st.fontFamilyName = decoded;
                else if (nameId == 17 || (nameId == 2 && st.fontSubfamilyName.empty())) st.fontSubfamilyName = decoded;
                else if (nameId == 5 && st.fontVersionStr.empty()) st.fontVersionStr = decoded;
                else if ((nameId == 8 || nameId == 9) && st.fontManufacturerStr.empty()) st.fontManufacturerStr = decoded;
                else if (nameId == 13 && st.fontLicenseStr.empty()) st.fontLicenseStr = decoded;
            }
        }
    }

    // Parse GSUB / GPOS FeatureList 4-char OpenType tags
    std::vector<uint32_t> seenTags;
    auto collectOtFeatures = [&](const TableLoc& tbl) {
        if (tbl.len < 10) return;
        const uint16_t featListRel = ReadBE16(bytes, tbl.off + 6);
        const size_t featListOff = static_cast<size_t>(tbl.off) + featListRel;
        if (featListOff + 2u > bytes.size()) return;
        const uint16_t featCount = ReadBE16(bytes, featListOff);
        const uint16_t capped = (std::min<uint16_t>)(featCount, 128u);
        for (uint16_t i = 0; i < capped; ++i) {
            size_t recOff = featListOff + 2u + static_cast<size_t>(i) * 6u;
            if (recOff + 6u > bytes.size()) break;
            uint32_t rawTagBE = ReadBE32(bytes, recOff);
            char c0 = static_cast<char>((rawTagBE >> 24) & 0xFFu);
            char c1 = static_cast<char>((rawTagBE >> 16) & 0xFFu);
            char c2 = static_cast<char>((rawTagBE >> 8) & 0xFFu);
            char c3 = static_cast<char>(rawTagBE & 0xFFu);
            if (c0 < 0x20 || c0 > 0x7E || c1 < 0x20 || c1 > 0x7E ||
                c2 < 0x20 || c2 > 0x7E || c3 < 0x20 || c3 > 0x7E) {
                continue;
            }
            // DirectWrite DWRITE_MAKE_OPENTYPE_TAG(a,b,c,d) stores little-endian 4CC
            uint32_t dwTag = DWRITE_MAKE_OPENTYPE_TAG(c0, c1, c2, c3);
            if (std::find(seenTags.begin(), seenTags.end(), dwTag) != seenTags.end()) continue;
            seenTags.push_back(dwTag);

            std::wstring tagW;
            tagW.push_back(static_cast<wchar_t>(c0));
            tagW.push_back(static_cast<wchar_t>(c1));
            tagW.push_back(static_cast<wchar_t>(c2));
            tagW.push_back(static_cast<wchar_t>(c3));
            AssetViewerState::FontFeatureToggle ft;
            ft.tag = std::move(tagW);
            ft.tagFourCC = dwTag;
            ft.enabled = (ft.tag == L"calt" || ft.tag == L"liga" || ft.tag == L"kern" || ft.tag == L"clig");
            st.fontOtFeatures.push_back(std::move(ft));
            if (st.fontOtFeatures.size() >= 18u) break;
        }
    };
    collectOtFeatures(gsubTbl);
    collectOtFeatures(gposTbl);
}

static void SetToast(AssetViewerState& st, const std::wstring& msg) {
    st.toastMessage = msg;
    st.toastExpireTick = GetTickCount64() + 2200ull;
}

/**
 * @brief Extracts top 6 quantized dominant colors from a 32bpp PBGRA WIC bitmap using bounded sampling.
 */
static void ExtractDominantColors(IWICBitmapSource* pPbgra, uint32_t w, uint32_t h,
                                  std::vector<DominantColorSwatch>& outSwatches) {
    outSwatches.clear();
    if (!pPbgra || w == 0 || h == 0) return;

    // Cap decoded sampling buffer cleanly to at most ~256x256 via row/col stride
    const uint32_t stepX = (std::max)(1u, w / 64u);
    const uint32_t stepY = (std::max)(1u, h / 64u);
    const size_t stride = static_cast<size_t>(w) * 4u;
    if (stride > 64u * 1024u * 1024u) return;

    std::vector<uint8_t> rowBuf(stride);
    struct Bucket {
        uint64_t rSum = 0;
        uint64_t gSum = 0;
        uint64_t bSum = 0;
        uint32_t count = 0;
    };
    std::unordered_map<uint32_t, Bucket> buckets;

    for (uint32_t y = 0; y < h; y += stepY) {
        WICRect rc{ 0, static_cast<INT>(y), static_cast<INT>(w), 1 };
        if (FAILED(pPbgra->CopyPixels(&rc, static_cast<UINT>(stride), static_cast<UINT>(stride), rowBuf.data()))) {
            continue;
        }
        for (uint32_t x = 0; x < w; x += stepX) {
            const size_t idx = static_cast<size_t>(x) * 4u;
            uint8_t b = rowBuf[idx + 0];
            uint8_t g = rowBuf[idx + 1];
            uint8_t r = rowBuf[idx + 2];
            uint8_t a = rowBuf[idx + 3];
            if (a < 32) continue; // Ignore transparent background pixels

            // Un-premultiply approximate RGB for accurate hex reporting
            if (a < 255 && a > 0) {
                r = static_cast<uint8_t>((std::min)(255u, (static_cast<uint32_t>(r) * 255u) / a));
                g = static_cast<uint8_t>((std::min)(255u, (static_cast<uint32_t>(g) * 255u) / a));
                b = static_cast<uint8_t>((std::min)(255u, (static_cast<uint32_t>(b) * 255u) / a));
            }
            // 5-bit per channel quantization key (32x32x32 buckets)
            uint32_t key = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
            auto& bk = buckets[key];
            bk.rSum += r;
            bk.gSum += g;
            bk.bSum += b;
            bk.count++;
        }
    }

    std::vector<DominantColorSwatch> all;
    all.reserve(buckets.size());
    for (const auto& kv : buckets) {
        if (kv.second.count == 0) continue;
        DominantColorSwatch sw;
        sw.r = static_cast<uint8_t>(kv.second.rSum / kv.second.count);
        sw.g = static_cast<uint8_t>(kv.second.gSum / kv.second.count);
        sw.b = static_cast<uint8_t>(kv.second.bSum / kv.second.count);
        sw.sampleCount = kv.second.count;
        wchar_t hex[16]{};
        swprintf_s(hex, L"#%02X%02X%02X", sw.r, sw.g, sw.b);
        sw.hexString = hex;
        all.push_back(std::move(sw));
    }

    std::sort(all.begin(), all.end(), [](const DominantColorSwatch& a, const DominantColorSwatch& b) {
        return a.sampleCount > b.sampleCount;
    });

    // Greedily pick up to 6 visually distinct swatches (Euclidean distance >= 32)
    for (const auto& cand : all) {
        bool tooClose = false;
        for (const auto& chosen : outSwatches) {
            int dr = static_cast<int>(cand.r) - static_cast<int>(chosen.r);
            int dg = static_cast<int>(cand.g) - static_cast<int>(chosen.g);
            int db = static_cast<int>(cand.b) - static_cast<int>(chosen.b);
            if (dr * dr + dg * dg + db * db < 32 * 32) {
                tooClose = true;
                break;
            }
        }
        if (!tooClose) {
            outSwatches.push_back(cand);
            if (outSwatches.size() >= 6) break;
        }
    }
}

static bool ParseAttrValue(const std::wstring& tag, const std::wstring& attrName, std::wstring& outVal) {
    size_t pos = 0;
    while ((pos = tag.find(attrName, pos)) != std::wstring::npos) {
        if (pos > 0 && (iswalnum(tag[pos - 1]) || tag[pos - 1] == L'-' || tag[pos - 1] == L'_')) {
            pos += attrName.size();
            continue;
        }
        size_t after = pos + attrName.size();
        while (after < tag.size() && iswspace(tag[after])) ++after;
        if (after >= tag.size() || tag[after] != L'=') {
            pos = after;
            continue;
        }
        ++after;
        while (after < tag.size() && iswspace(tag[after])) ++after;
        if (after >= tag.size()) return false;
        wchar_t q = tag[after];
        if (q != L'"' && q != L'\'') return false;
        size_t endQ = tag.find(q, after + 1);
        if (endQ == std::wstring::npos) return false;
        outVal = tag.substr(after + 1, endQ - (after + 1));
        return true;
    }
    return false;
}

static bool ParseSvgDimensions(const std::wstring& svgText, float& outW, float& outH) {
    outW = 256.0f;
    outH = 256.0f;
    size_t svgPos = svgText.find(L"<svg");
    if (svgPos == std::wstring::npos) return false;
    size_t svgEnd = svgText.find(L'>', svgPos);
    if (svgEnd == std::wstring::npos) return false;
    std::wstring hdr = svgText.substr(svgPos, svgEnd - svgPos + 1);

    std::wstring vb;
    if (ParseAttrValue(hdr, L"viewBox", vb)) {
        for (auto& ch : vb) {
            if (ch == L',') ch = L' ';
        }
        std::wistringstream iss(vb);
        float vx = 0, vy = 0, vw = 0, vh = 0;
        if (iss >> vx >> vy >> vw >> vh && vw > 0.0f && vh > 0.0f) {
            outW = vw;
            outH = vh;
            return true;
        }
    }

    std::wstring wStr, hStr;
    if (ParseAttrValue(hdr, L"width", wStr) && ParseAttrValue(hdr, L"height", hStr)) {
        try {
            float pw = std::stof(wStr);
            float ph = std::stof(hStr);
            if (pw > 0.0f && ph > 0.0f) {
                outW = pw;
                outH = ph;
                return true;
            }
        } catch (...) {}
    }
    return true;
}

static std::shared_ptr<AssetViewerState> EnsureAssetState(phide::editor::OpenDocument& doc) {
    if (!doc.assetView) {
        doc.assetView = std::make_shared<AssetViewerState>();
    }
    return doc.assetView;
}

static float MeasureUiTextWidth(Theme& theme, const app::AppConfig& config,
                                IDWriteTextFormat* pFmt, const wchar_t* text, float fallbackPerChar) {
    if (!text || !*text) return 0.0f;
    const size_t len = wcslen(text);
    if (theme.dwriteFactory && pFmt) {
        ComPtr<IDWriteTextLayout> layout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(text, static_cast<UINT32>(len), pFmt, 2000.0f, 100.0f, &layout)) && layout) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(layout->GetMetrics(&tm))) {
                return tm.widthIncludingTrailingWhitespace;
            }
        }
    }
    return static_cast<float>(len) * config.ScaleF(fallbackPerChar);
}

static IDWriteTextFormat* GetAssetBtnFont(Theme& theme) noexcept {
    if (theme.fontButtonCenter) return theme.fontButtonCenter.Get();
    if (theme.fontMenu) return theme.fontMenu.Get();
    return theme.fontBody.Get();
}

static float CalcToolbarButtonWidth(Theme& theme, const app::AppConfig& config,
                                    const wchar_t* label, float minW = 34.0f) {
    return (std::max)(config.ScaleF(minW),
                      MeasureUiTextWidth(theme, config, GetAssetBtnFont(theme), label, 7.6f) + config.ScaleF(22.0f));
}

static void DrawToolbarButton(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                              const D2D1_RECT_F& rc, const wchar_t* label, bool active, bool hovered,
                              ID2D1Brush* bgNormal, ID2D1Brush* bgHover, ID2D1Brush* borderBrush) {
    if (rc.right <= rc.left + 2.0f) return;
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rc, config.ScaleF(5.0f), config.ScaleF(5.0f));
    ID2D1Brush* fill = active ? theme.brushBrandAccent.Get() : (hovered ? bgHover : bgNormal);
    if (fill) pRT->FillRoundedRectangle(rrc, fill);
    if (borderBrush) pRT->DrawRoundedRectangle(rrc, active ? theme.brushBrandAccent.Get() : borderBrush, hovered ? 1.3f : 1.0f);

    IDWriteTextFormat* pFmt = GetAssetBtnFont(theme);
    ID2D1Brush* txtBrush = active
        ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
        : theme.brushTextPrimary.Get();
    if (pFmt && txtBrush) {
        DWRITE_TEXT_ALIGNMENT oldAlign = pFmt->GetTextAlignment();
        DWRITE_PARAGRAPH_ALIGNMENT oldPara = pFmt->GetParagraphAlignment();
        pFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        pRT->DrawText(label, static_cast<UINT32>(wcslen(label)), pFmt, rc, txtBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        pFmt->SetTextAlignment(oldAlign);
        pFmt->SetParagraphAlignment(oldPara);
    }
}

static void PopulateFontGlyphGrid(AssetViewerState& st) {
    st.fontGlyphCells.clear();
    if (!st.customFontFace) return;

    auto appendCp = [&](uint32_t cp) {
        if (st.fontGlyphCells.size() >= 600u) return;
        if (!st.customFontFace->HasCharacter(cp)) return;

        AssetViewerState::FontGlyphCell cell;
        cell.codepoint = cp;
        if (cp <= 0xFFFFu) {
            cell.glyphChar.push_back(static_cast<wchar_t>(cp));
        } else {
            uint32_t v = cp - 0x10000u;
            cell.glyphChar.push_back(static_cast<wchar_t>(0xD800u + (v >> 10)));
            cell.glyphChar.push_back(static_cast<wchar_t>(0xDC00u + (v & 0x3FFu)));
        }
        wchar_t hexBuf[16]{};
        swprintf_s(hexBuf, L"U+%04X", cp);
        cell.hexCode = hexBuf;
        st.fontGlyphCells.push_back(std::move(cell));
    };

    for (uint32_t cp = 0x0021u; cp <= 0x007Eu; ++cp) appendCp(cp);
    for (uint32_t cp = 0x00A1u; cp <= 0x017Fu; ++cp) appendCp(cp);
    for (uint32_t cp = 0x2010u; cp <= 0x203Au; ++cp) appendCp(cp);
    for (uint32_t cp = 0x2190u; cp <= 0x21BBu; ++cp) appendCp(cp);
    for (uint32_t cp = 0x2500u; cp <= 0x254Bu; ++cp) appendCp(cp);
    appendCp(0x2260u);
    appendCp(0x2264u);
    appendCp(0x2265u);
}

static void DrawCheckerboardRect(ID2D1RenderTarget* pRT, const D2D1_RECT_F& rc, float cellPx,
                                 bool darkMode) {
    ComPtr<ID2D1SolidColorBrush> brA, brB;
    pRT->CreateSolidColorBrush(darkMode ? D2D1::ColorF(0x1E222A) : D2D1::ColorF(0xFFFFFF), &brA);
    pRT->CreateSolidColorBrush(darkMode ? D2D1::ColorF(0x282D37) : D2D1::ColorF(0xE6E9EF), &brB);
    if (!brA || !brB) return;

    pRT->FillRectangle(rc, brA.Get());
    pRT->PushAxisAlignedClip(rc, D2D1_ANTIALIAS_MODE_ALIASED);
    const float cell = (std::max)(6.0f, cellPx);
    int rowIdx = 0;
    for (float y = rc.top; y < rc.bottom; y += cell, ++rowIdx) {
        int colIdx = rowIdx & 1;
        for (float x = rc.left; x < rc.right; x += cell, ++colIdx) {
            if ((colIdx & 1) != 0) {
                pRT->FillRectangle(D2D1::RectF(x, y, (std::min)(rc.right, x + cell), (std::min)(rc.bottom, y + cell)), brB.Get());
            }
        }
    }
    pRT->PopAxisAlignedClip();
}

} // namespace

AssetViewerState::~AssetViewerState() {
    DiscardDeviceResources();
    ReleaseFontResources();
}

void AssetViewerState::DiscardDeviceResources() noexcept {
    svgDoc.Reset();
    cachedSvgRT = nullptr;
    cachedSvgBufferVersion = UINT64_MAX;
}

void AssetViewerState::ReleaseFontResources() noexcept {
    // Release font face and collection before unregistering loader so DirectWrite holds no active file refs
    customFontFace.Reset();
    customFontCollection.Reset();
    if (dwriteFactory5 && memFontLoader) {
        dwriteFactory5->UnregisterFontFileLoader(memFontLoader.Get());
    }
    memFontLoader.Reset();
    dwriteFactory5.Reset();
    std::vector<uint8_t>().swap(rawFontBytes);
    fontGlyphCells.clear();
    fontOtFeatures.clear();
}

bool AssetViewerRenderer::IsFontExtension(const std::wstring& extLower) noexcept {
    return extLower == L".ttf" || extLower == L".otf" ||
           extLower == L".woff" || extLower == L".woff2";
}

void AssetViewerRenderer::RenderRasterImageView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                                phide::editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                                float topY, float clientHeight) {
    if (!pRT || canvasWidth <= 20.0f || clientHeight <= topY + 20.0f) return;
    auto st = EnsureAssetState(doc);

    if (st->loadedFilePath != doc.filePath) {
        st->loadedFilePath = doc.filePath;
        st->imageMetadataLoaded = false;
        st->dominantColors.clear();
        st->bitsPerPixel = 0;
        st->pixelFormatName.clear();
    }

    // Load WIC bitmap + pixel metadata + quantized dominant color palette once
    if ((!doc.pImageBitmap || !st->imageMetadataLoaded) && !doc.filePath.empty()) {
        ComPtr<IWICImagingFactory> pWicFactory;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&pWicFactory))) && pWicFactory) {
            ComPtr<IWICBitmapDecoder> pDecoder;
            if (SUCCEEDED(pWicFactory->CreateDecoderFromFilename(doc.filePath.c_str(), nullptr, GENERIC_READ,
                                                                 WICDecodeMetadataCacheOnDemand, &pDecoder)) && pDecoder) {
                ComPtr<IWICBitmapFrameDecode> pFrame;
                if (SUCCEEDED(pDecoder->GetFrame(0, &pFrame)) && pFrame) {
                    pFrame->GetSize(&doc.imageWidth, &doc.imageHeight);

                    WICPixelFormatGUID pixFmt{};
                    if (SUCCEEDED(pFrame->GetPixelFormat(&pixFmt))) {
                        ComPtr<IWICComponentInfo> pCompInfo;
                        if (SUCCEEDED(pWicFactory->CreateComponentInfo(pixFmt, &pCompInfo)) && pCompInfo) {
                            ComPtr<IWICPixelFormatInfo> pFmtInfo;
                            if (SUCCEEDED(pCompInfo.As(&pFmtInfo)) && pFmtInfo) {
                                UINT bpp = 0;
                                if (SUCCEEDED(pFmtInfo->GetBitsPerPixel(&bpp))) st->bitsPerPixel = bpp;
                                UINT len = 0;
                                pFmtInfo->GetFriendlyName(0, nullptr, &len);
                                if (len > 0 && len < 128) {
                                    std::wstring fmtName(len, L'\0');
                                    if (SUCCEEDED(pFmtInfo->GetFriendlyName(len, fmtName.data(), &len))) {
                                        while (!fmtName.empty() && fmtName.back() == L'\0') fmtName.pop_back();
                                        st->pixelFormatName = fmtName;
                                    }
                                }
                            }
                        }
                    }

                    ComPtr<IWICFormatConverter> pConverter;
                    if (SUCCEEDED(pWicFactory->CreateFormatConverter(&pConverter)) && pConverter) {
                        if (SUCCEEDED(pConverter->Initialize(pFrame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                                             WICBitmapDitherTypeNone, nullptr, 0.0,
                                                             WICBitmapPaletteTypeCustom))) {
                            if (!doc.pImageBitmap) {
                                pRT->CreateBitmapFromWicBitmap(pConverter.Get(), nullptr, &doc.pImageBitmap);
                            }
                            if (!st->imageMetadataLoaded) {
                                ExtractDominantColors(pConverter.Get(), doc.imageWidth, doc.imageHeight, st->dominantColors);
                                st->imageMetadataLoaded = true;
                            }
                        }
                    }
                }
            }
        }
        if (doc.imageFileSize == 0 && !doc.filePath.empty()) {
            std::error_code ec;
            doc.imageFileSize = std::filesystem::file_size(doc.filePath, ec);
        }
        st->fileSizeBytes = doc.imageFileSize;
    }

    const bool dark = theme.IsDarkMode();
    ComPtr<ID2D1SolidColorBrush> brToolbarBg, brBtnBg, brBtnHover, brBorder;
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x181B22) : D2D1::ColorF(0xEEF1F6), &brToolbarBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x232834) : D2D1::ColorF(0xFFFFFF), &brBtnBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x2F3646) : D2D1::ColorF(0xDCE3EE), &brBtnHover);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x333A48) : D2D1::ColorF(0xBCC4D4), &brBorder);

    // Top Asset Toolbar (clean 12px UI buttons with comfortable sizing)
    const float barH = config.ScaleF(44.0f);
    st->rcToolbar = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, topY + barH);
    if (brToolbarBg) pRT->FillRectangle(st->rcToolbar, brToolbarBg.Get());
    pRT->DrawLine(D2D1::Point2F(canvasLeft, st->rcToolbar.bottom),
                  D2D1::Point2F(canvasLeft + canvasWidth, st->rcToolbar.bottom),
                  theme.brushBorderSubtle.Get(), 1.0f);

    st->rcBtnZoomOut = {};
    st->rcBtnZoomFit = {};
    st->rcBtnZoom100 = {};
    st->rcBtnZoomIn = {};
    st->rcBtnChecker = {};
    st->rcBtnCopyImgTag = {};
    st->rcBtnCopyBase64 = {};
    st->rcBtnCopyFontFace = {};
    st->rcBtnCopyLinkTag = {};
    st->rcBtnToggleSvgPreview = {};
    st->rcFontSampleBox = {};

    const float pad = config.ScaleF(12.0f);
    const float btnH = config.ScaleF(30.0f);
    const float btnY = topY + (barH - btnH) * 0.5f;
    float bx = canvasLeft + pad;
    const float maxR = canvasLeft + canvasWidth - pad;

    auto addBtn = [&](D2D1_RECT_F& outRc, const wchar_t* lbl, int id, float minW, bool active = false) {
        const float w = CalcToolbarButtonWidth(theme, config, lbl, minW);
        if (bx + w > maxR) { outRc = {}; return; }
        outRc = D2D1::RectF(bx, btnY, bx + w, btnY + btnH);
        DrawToolbarButton(pRT, theme, config, outRc, lbl, active, st->hoveredButtonId == id,
                          brBtnBg.Get(), brBtnHover.Get(), brBorder.Get());
        bx += w + config.ScaleF(7.0f);
    };

    addBtn(st->rcBtnZoomOut,    L"-",           1, 32.0f);
    addBtn(st->rcBtnZoomFit,    L"Fit",         2, 46.0f, st->imageZoom <= 0.0f);
    addBtn(st->rcBtnZoom100,    L"100%",        3, 56.0f, std::abs(st->imageZoom - 1.0f) < 0.01f);
    addBtn(st->rcBtnZoomIn,     L"+",           4, 32.0f);
    addBtn(st->rcBtnChecker,    L"Grid",        5, 62.0f, st->showCheckerboard);
    addBtn(st->rcBtnCopyImgTag, L"Copy <img>",  6, 94.0f);
    addBtn(st->rcBtnCopyBase64, L"Copy Base64", 7, 102.0f);

    // Dominant color swatches on the right side of the toolbar (when width permits)
    const float swSz = config.ScaleF(18.0f);
    const float swY = topY + (barH - swSz) * 0.5f;
    float sx = maxR;
    for (int i = static_cast<int>(st->dominantColors.size()) - 1; i >= 0; --i) {
        sx -= swSz;
        if (sx < bx + config.ScaleF(8.0f)) {
            st->dominantColors[static_cast<size_t>(i)].bounds = {};
            continue;
        }
        D2D1_RECT_F rcSw = D2D1::RectF(sx, swY, sx + swSz, swY + swSz);
        st->dominantColors[static_cast<size_t>(i)].bounds = rcSw;
        ComPtr<ID2D1SolidColorBrush> brSw;
        const auto& c = st->dominantColors[static_cast<size_t>(i)];
        pRT->CreateSolidColorBrush(D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f), &brSw);
        D2D1_ROUNDED_RECT rrcSw = D2D1::RoundedRect(rcSw, config.ScaleF(3.0f), config.ScaleF(3.0f));
        if (brSw) pRT->FillRoundedRectangle(rrcSw, brSw.Get());
        pRT->DrawRoundedRectangle(rrcSw,
                                  (st->hoveredSwatchIdx == i) ? theme.brushBrandAccent.Get() : brBorder.Get(),
                                  (st->hoveredSwatchIdx == i) ? 1.8f : 1.0f);
        sx -= config.ScaleF(5.0f);
    }

    // Main Preview Area + Bottom Metadata Strip
    const float metaH = config.ScaleF(34.0f);
    const float viewTop = st->rcToolbar.bottom;
    const float viewBot = (std::max)(viewTop + 20.0f, clientHeight - metaH);
    const D2D1_RECT_F rcPreviewArea = D2D1::RectF(canvasLeft, viewTop, canvasLeft + canvasWidth, viewBot);

    pRT->PushAxisAlignedClip(rcPreviewArea, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (doc.pImageBitmap && doc.imageWidth > 0 && doc.imageHeight > 0) {
        const float availW = (std::max)(20.0f, canvasWidth - config.ScaleF(48.0f));
        const float availH = (std::max)(20.0f, (viewBot - viewTop) - config.ScaleF(36.0f));
        float scale = st->imageZoom;
        if (scale <= 0.0f) {
            scale = 1.0f;
            if (static_cast<float>(doc.imageWidth) > availW || static_cast<float>(doc.imageHeight) > availH) {
                scale = (std::min)(availW / static_cast<float>(doc.imageWidth),
                                   availH / static_cast<float>(doc.imageHeight));
            }
        }
        const float drawW = static_cast<float>(doc.imageWidth) * scale;
        const float drawH = static_cast<float>(doc.imageHeight) * scale;
        const float drawX = canvasLeft + (canvasWidth - drawW) * 0.5f;
        const float drawY = viewTop + ((viewBot - viewTop) - drawH) * 0.5f;
        D2D1_RECT_F imgRect = D2D1::RectF(drawX, drawY, drawX + drawW, drawY + drawH);

        if (st->showCheckerboard) {
            DrawCheckerboardRect(pRT, imgRect, config.ScaleF(10.0f), dark);
        }
        pRT->DrawRectangle(D2D1::RectF(drawX - 1.0f, drawY - 1.0f, drawX + drawW + 1.0f, drawY + drawH + 1.0f),
                           theme.brushBorderSubtle.Get(), 1.0f);
        pRT->DrawBitmap(doc.pImageBitmap.Get(), imgRect, 1.0f,
                        scale >= 2.0f ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else if (theme.fontBody && theme.brushTextMuted) {
        pRT->DrawText(L"Unable to decode image file.", 28, theme.fontBody.Get(), rcPreviewArea, theme.brushTextMuted.Get());
    }
    pRT->PopAxisAlignedClip();

    // Bottom Metadata Bar
    D2D1_RECT_F rcMeta = D2D1::RectF(canvasLeft, viewBot, canvasLeft + canvasWidth, clientHeight);
    if (brToolbarBg) pRT->FillRectangle(rcMeta, brToolbarBg.Get());
    pRT->DrawLine(D2D1::Point2F(canvasLeft, viewBot), D2D1::Point2F(canvasLeft + canvasWidth, viewBot),
                  theme.brushBorderSubtle.Get(), 1.0f);

    std::wstring info = std::to_wstring(doc.imageWidth) + L" \u00D7 " + std::to_wstring(doc.imageHeight) + L" px";
    std::wstring ext = ToUpperExt(doc.filePath);
    if (!ext.empty()) info += L"  |  " + ext;
    if (st->bitsPerPixel > 0) info += L"  |  " + std::to_wstring(st->bitsPerPixel) + L"-bit";
    if (!st->pixelFormatName.empty()) info += L" (" + st->pixelFormatName + L")";
    info += L"  |  " + FormatByteSize(doc.imageFileSize);
    if (st->hoveredSwatchIdx >= 0 && static_cast<size_t>(st->hoveredSwatchIdx) < st->dominantColors.size()) {
        info += L"  |  Swatch: " + st->dominantColors[static_cast<size_t>(st->hoveredSwatchIdx)].hexString + L" (Click to copy)";
    }
    if (!st->toastMessage.empty() && GetTickCount64() < st->toastExpireTick) {
        info = st->toastMessage;
    }

    IDWriteTextFormat* pMetaFmt = theme.fontBody ? theme.fontBody.Get() : theme.fontMenu.Get();
    if (pMetaFmt && theme.brushTextSecondary) {
        D2D1_RECT_F rcMetaTxt = D2D1::RectF(canvasLeft + pad, viewBot + config.ScaleF(4.0f),
                                            canvasLeft + canvasWidth - pad, clientHeight - config.ScaleF(4.0f));
        pRT->DrawText(info.c_str(), static_cast<UINT32>(info.size()), pMetaFmt, rcMetaTxt,
                      theme.brushTextSecondary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

void AssetViewerRenderer::RenderSvgPreviewPane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                               phide::editor::OpenDocument& doc, float paneLeft, float paneWidth,
                                               float topY, float clientHeight) {
    if (!pRT || paneWidth <= 24.0f || clientHeight <= topY + 24.0f) return;
    auto st = EnsureAssetState(doc);

    const bool dark = theme.IsDarkMode();
    ComPtr<ID2D1SolidColorBrush> brBg, brToolbarBg, brBtnBg, brBtnHover, brBorder;
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x13161C) : D2D1::ColorF(0xF5F7FA), &brBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x181B22) : D2D1::ColorF(0xEEF1F6), &brToolbarBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x232834) : D2D1::ColorF(0xFFFFFF), &brBtnBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x2F3646) : D2D1::ColorF(0xDCE3EE), &brBtnHover);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x333A48) : D2D1::ColorF(0xBCC4D4), &brBorder);

    D2D1_RECT_F rcPane = D2D1::RectF(paneLeft, topY, paneLeft + paneWidth, clientHeight);
    if (brBg) pRT->FillRectangle(rcPane, brBg.Get());
    pRT->DrawLine(D2D1::Point2F(paneLeft, topY), D2D1::Point2F(paneLeft, clientHeight),
                  theme.brushBorderSubtle.Get(), 1.0f);

    const float barH = config.ScaleF(42.0f);
    D2D1_RECT_F rcBar = D2D1::RectF(paneLeft + 1.0f, topY, paneLeft + paneWidth, topY + barH);
    if (brToolbarBg) pRT->FillRectangle(rcBar, brToolbarBg.Get());
    pRT->DrawLine(D2D1::Point2F(paneLeft, rcBar.bottom), D2D1::Point2F(paneLeft + paneWidth, rcBar.bottom),
                  theme.brushBorderSubtle.Get(), 1.0f);

    st->rcBtnChecker = {};
    st->rcBtnCopyImgTag = {};

    const float pad = config.ScaleF(12.0f);
    const float btnH = config.ScaleF(28.0f);
    const float btnY = topY + (barH - btnH) * 0.5f;

    float rightX = paneLeft + paneWidth - pad;
    const float gridW = CalcToolbarButtonWidth(theme, config, L"Grid", 60.0f);
    const float copyW = CalcToolbarButtonWidth(theme, config, L"Copy SVG", 86.0f);

    IDWriteTextFormat* pLblFmt = theme.fontBody ? theme.fontBody.Get() : theme.fontMenu.Get();
    const float titleMaxR = (std::max)(paneLeft + pad + config.ScaleF(40.0f),
                                       rightX - gridW - copyW - config.ScaleF(22.0f));
    if (pLblFmt && theme.brushTextPrimary) {
        std::wstring svgHdr = L"SVG Live Preview";
        if (!st->toastMessage.empty() && GetTickCount64() < st->toastExpireTick) svgHdr = st->toastMessage;
        DWRITE_PARAGRAPH_ALIGNMENT oldPa = pLblFmt->GetParagraphAlignment();
        pLblFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        pRT->DrawText(svgHdr.c_str(), static_cast<UINT32>(svgHdr.size()), pLblFmt,
                      D2D1::RectF(paneLeft + pad, btnY, titleMaxR, btnY + btnH),
                      theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        pLblFmt->SetParagraphAlignment(oldPa);
    }

    if (rightX - gridW > paneLeft + config.ScaleF(90.0f)) {
        st->rcBtnChecker = D2D1::RectF(rightX - gridW, btnY, rightX, btnY + btnH);
        DrawToolbarButton(pRT, theme, config, st->rcBtnChecker, L"Grid", st->showCheckerboard,
                          st->hoveredButtonId == 5, brBtnBg.Get(), brBtnHover.Get(), brBorder.Get());
        rightX -= gridW + config.ScaleF(8.0f);
    }
    if (rightX - copyW > paneLeft + config.ScaleF(90.0f)) {
        st->rcBtnCopyImgTag = D2D1::RectF(rightX - copyW, btnY, rightX, btnY + btnH);
        DrawToolbarButton(pRT, theme, config, st->rcBtnCopyImgTag, L"Copy SVG", false,
                          st->hoveredButtonId == 6, brBtnBg.Get(), brBtnHover.Get(), brBorder.Get());
    }

    // Re-parse SVG document whenever buffer version or render target changes
    const uint64_t ver = doc.buffer.GetVersion();
    if (st->cachedSvgRT != pRT || st->cachedSvgBufferVersion != ver) {
        st->cachedSvgRT = pRT;
        st->cachedSvgBufferVersion = ver;
        st->svgDoc.Reset();
        st->svgParseError.clear();
        st->svgParseAttempted = true;

        std::wstring svgText = doc.buffer.GetText();
        if (svgText.size() > AssetViewerState::kMaxSvgChars) {
            st->svgParseError = L"SVG exceeds 2 MB live preview limit.";
        } else {
            ParseSvgDimensions(svgText, st->svgIntrinsicW, st->svgIntrinsicH);
            std::string utf8 = WideToUtf8(svgText);
            if (!utf8.empty()) {
                ComPtr<ID2D1DeviceContext5> dc5;
                if (SUCCEEDED(pRT->QueryInterface(IID_PPV_ARGS(&dc5))) && dc5) {
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, utf8.size());
                    if (hMem) {
                        void* pMem = GlobalLock(hMem);
                        if (pMem) {
                            std::memcpy(pMem, utf8.data(), utf8.size());
                            GlobalUnlock(hMem);
                            ComPtr<IStream> pStream;
                            if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &pStream)) && pStream) {
                                HRESULT hrSvg = dc5->CreateSvgDocument(
                                    pStream.Get(),
                                    D2D1::SizeF(st->svgIntrinsicW, st->svgIntrinsicH),
                                    &st->svgDoc);
                                if (FAILED(hrSvg)) {
                                    st->svgParseError = L"Invalid SVG XML syntax — edit left pane to fix.";
                                }
                            } else {
                                GlobalFree(hMem);
                            }
                        } else {
                            GlobalFree(hMem);
                        }
                    }
                } else {
                    st->svgParseError = L"Direct2D 1.3 SVG renderer unavailable on this adapter.";
                }
            } else {
                st->svgParseError = L"Empty SVG document.";
            }
        }
    }

    const D2D1_RECT_F rcView = D2D1::RectF(paneLeft + 1.0f, rcBar.bottom, paneLeft + paneWidth, clientHeight);
    pRT->PushAxisAlignedClip(rcView, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (st->svgDoc) {
        ComPtr<ID2D1DeviceContext5> dc5;
        if (SUCCEEDED(pRT->QueryInterface(IID_PPV_ARGS(&dc5))) && dc5) {
            const float availW = (std::max)(24.0f, paneWidth - config.ScaleF(36.0f));
            const float availH = (std::max)(24.0f, (clientHeight - rcBar.bottom) - config.ScaleF(36.0f));
            const float sw = (std::max)(1.0f, st->svgIntrinsicW);
            const float sh = (std::max)(1.0f, st->svgIntrinsicH);
            const float scale = (std::min)(availW / sw, availH / sh);
            const float drawW = sw * scale;
            const float drawH = sh * scale;
            const float drawX = paneLeft + (paneWidth - drawW) * 0.5f;
            const float drawY = rcBar.bottom + ((clientHeight - rcBar.bottom) - drawH) * 0.5f;
            const D2D1_RECT_F rcDraw = D2D1::RectF(drawX, drawY, drawX + drawW, drawY + drawH);

            if (st->showCheckerboard) {
                DrawCheckerboardRect(pRT, rcDraw, config.ScaleF(10.0f), dark);
            }
            pRT->DrawRectangle(rcDraw, theme.brushBorderSubtle.Get(), 1.0f);

            D2D1_MATRIX_3X2_F prevXform;
            pRT->GetTransform(&prevXform);
            pRT->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
                              D2D1::Matrix3x2F::Translation(drawX, drawY) * prevXform);
            dc5->DrawSvgDocument(st->svgDoc.Get());
            pRT->SetTransform(prevXform);
        }
    } else if (!st->svgParseError.empty() && pLblFmt && theme.brushTextMuted) {
        pRT->DrawText(st->svgParseError.c_str(), static_cast<UINT32>(st->svgParseError.size()),
                      pLblFmt, D2D1::RectF(rcView.left + pad, rcView.top + pad, rcView.right - pad, rcView.bottom - pad),
                      theme.brushTextMuted.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    pRT->PopAxisAlignedClip();
}

void AssetViewerRenderer::RenderFontAssetView(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                              phide::editor::OpenDocument& doc, float canvasLeft, float canvasWidth,
                                              float topY, float clientHeight) {
    if (!pRT || canvasWidth <= 20.0f || clientHeight <= topY + 20.0f) return;
    auto st = EnsureAssetState(doc);

    if (st->loadedFilePath != doc.filePath) {
        st->ReleaseFontResources();
        st->loadedFilePath = doc.filePath;
        st->fontLoadAttempted = false;
        st->fontRenderSupported = false;
        st->fontFamilyName.clear();
        st->fontSubfamilyName.clear();
        st->fontVersionStr.clear();
        st->fontManufacturerStr.clear();
        st->fontLicenseStr.clear();
        st->fontNumGlyphs = 0;
        st->fontUnitsPerEm = 1000;
        st->fontIsFixedPitch = false;
        st->fontStatusNotice.clear();
        st->fontScrollY = 0.0f;
        st->fontTab = AssetViewerState::FontViewTab::Waterfall;
    }

    if (!st->fontLoadAttempted && !doc.filePath.empty()) {
        st->fontLoadAttempted = true;
        st->fontFormatLabel = ToUpperExt(doc.filePath);
        std::error_code ec;
        st->fileSizeBytes = std::filesystem::file_size(doc.filePath, ec);
        if (st->fileSizeBytes > 0 && st->fileSizeBytes <= AssetViewerState::kMaxFontFileBytes) {
            std::ifstream ifs(doc.filePath, std::ios::binary);
            if (ifs.is_open()) {
                st->rawFontBytes.resize(static_cast<size_t>(st->fileSizeBytes));
                ifs.read(reinterpret_cast<char*>(st->rawFontBytes.data()),
                         static_cast<std::streamsize>(st->fileSizeBytes));
                ParseSfntMetadata(st->rawFontBytes, *st);

                if ((st->fontFormatLabel == L"TTF" || st->fontFormatLabel == L"OTF") && theme.dwriteFactory) {
                    if (SUCCEEDED(theme.dwriteFactory.As(&st->dwriteFactory5)) && st->dwriteFactory5) {
                        if (SUCCEEDED(st->dwriteFactory5->CreateInMemoryFontFileLoader(&st->memFontLoader)) &&
                            st->memFontLoader &&
                            SUCCEEDED(st->dwriteFactory5->RegisterFontFileLoader(st->memFontLoader.Get()))) {
                            ComPtr<IDWriteFontFile> pFontFile;
                            if (SUCCEEDED(st->memFontLoader->CreateInMemoryFontFileReference(
                                    st->dwriteFactory5.Get(),
                                    st->rawFontBytes.data(),
                                    static_cast<UINT32>(st->rawFontBytes.size()),
                                    nullptr,
                                    &pFontFile)) && pFontFile) {
                                ComPtr<IDWriteFontSetBuilder> pBuilder;
                                ComPtr<IDWriteFontSetBuilder1> pBuilder1;
                                if (SUCCEEDED(st->dwriteFactory5->CreateFontSetBuilder(&pBuilder)) &&
                                    SUCCEEDED(pBuilder.As(&pBuilder1)) && pBuilder1 &&
                                    SUCCEEDED(pBuilder1->AddFontFile(pFontFile.Get()))) {
                                    ComPtr<IDWriteFontSet> pSet;
                                    if (SUCCEEDED(pBuilder1->CreateFontSet(&pSet)) && pSet) {
                                        if (SUCCEEDED(st->dwriteFactory5->CreateFontCollectionFromFontSet(
                                                pSet.Get(), &st->customFontCollection)) && st->customFontCollection) {
                                            UINT32 famCount = st->customFontCollection->GetFontFamilyCount();
                                            if (famCount > 0) {
                                                ComPtr<IDWriteFontFamily> pFam;
                                                if (SUCCEEDED(st->customFontCollection->GetFontFamily(0, &pFam)) && pFam) {
                                                    ComPtr<IDWriteLocalizedStrings> pNames;
                                                    if (SUCCEEDED(pFam->GetFamilyNames(&pNames)) && pNames && pNames->GetCount() > 0) {
                                                        UINT32 nameLen = 0;
                                                        pNames->GetStringLength(0, &nameLen);
                                                        if (nameLen > 0 && nameLen < 256) {
                                                            std::wstring dwName(nameLen + 1, L'\0');
                                                            if (SUCCEEDED(pNames->GetString(0, dwName.data(), nameLen + 1))) {
                                                                while (!dwName.empty() && dwName.back() == L'\0') dwName.pop_back();
                                                                st->fontFamilyName = dwName;
                                                            }
                                                        }
                                                    }
                                                    ComPtr<IDWriteFont> pFont;
                                                    if (SUCCEEDED(pFam->GetFont(0, &pFont)) && pFont) {
                                                        ComPtr<IDWriteFontFace> pBaseFace;
                                                        if (SUCCEEDED(pFont->CreateFontFace(&pBaseFace)) && pBaseFace) {
                                                            if (SUCCEEDED(pBaseFace.As(&st->customFontFace)) && st->customFontFace) {
                                                                if (st->fontNumGlyphs == 0) {
                                                                    st->fontNumGlyphs = st->customFontFace->GetGlyphCount();
                                                                }
                                                                PopulateFontGlyphGrid(*st);
                                                            }
                                                        }
                                                    }
                                                }
                                                st->fontRenderSupported = true;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else {
                    st->fontStatusNotice = L"Compressed webfont (" + st->fontFormatLabel +
                                           L") \u2014 use Copy @font-face CSS to embed directly in stylesheets.";
                }
            }
        }
        if (st->fontFamilyName.empty()) {
            st->fontFamilyName = std::filesystem::path(doc.filePath).stem().wstring();
        }
        if (st->fontSubfamilyName.empty()) {
            st->fontSubfamilyName = L"Regular";
        }
    }

    const bool dark = theme.IsDarkMode();
    ComPtr<ID2D1SolidColorBrush> brBg, brCard, brCardAlt, brBorder, brBtnBg, brBtnHover, brAccentSoft;
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x111318) : D2D1::ColorF(0xF8FAFC), &brBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x181C24) : D2D1::ColorF(0xFFFFFF), &brCard);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x202531) : D2D1::ColorF(0xEEF2F7), &brCardAlt);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x2E3545) : D2D1::ColorF(0xCBD5E1), &brBorder);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x242A38) : D2D1::ColorF(0xFFFFFF), &brBtnBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x31394D) : D2D1::ColorF(0xDCE3EE), &brBtnHover);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0x2563EB, 0.22f) : D2D1::ColorF(0x2563EB, 0.12f), &brAccentSoft);

    D2D1_RECT_F rcCanvas = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, clientHeight);
    if (brBg) pRT->FillRectangle(rcCanvas, brBg.Get());

    IDWriteTextFormat* pUiFmt   = theme.fontBody ? theme.fontBody.Get() : theme.fontMenu.Get();
    IDWriteTextFormat* pSubFmt  = theme.fontMenu ? theme.fontMenu.Get() : pUiFmt;
    IDWriteTextFormat* pCodeFmt = theme.fontCode ? theme.fontCode.Get() : pSubFmt;
    ID2D1Brush* txtPri   = theme.brushTextPrimary ? theme.brushTextPrimary.Get() : theme.brushTextWhite.Get();
    ID2D1Brush* txtSec   = theme.brushTextSecondary ? theme.brushTextSecondary.Get() : txtPri;
    ID2D1Brush* txtMuted = theme.brushTextMuted ? theme.brushTextMuted.Get() : txtSec;

    IDWriteFontCollection* collToUse = st->fontRenderSupported ? st->customFontCollection.Get() : nullptr;
    const wchar_t* famToUse = (st->fontRenderSupported && !st->fontFamilyName.empty())
        ? st->fontFamilyName.c_str()
        : L"Segoe UI";

    // Build shared DirectWrite Typography for active OpenType features
    ComPtr<IDWriteTypography> pTypography;
    if (!st->fontOtFeatures.empty() && theme.dwriteFactory) {
        if (SUCCEEDED(theme.dwriteFactory->CreateTypography(&pTypography)) && pTypography) {
            for (const auto& ft : st->fontOtFeatures) {
                DWRITE_FONT_FEATURE f{};
                f.nameTag = static_cast<DWRITE_FONT_FEATURE_TAG>(ft.tagFourCC);
                f.parameter = ft.enabled ? 1u : 0u;
                pTypography->AddFontFeature(f);
            }
        }
    }

    const float pad = config.ScaleF(20.0f);
    const float btnH = config.ScaleF(30.0f);
    const float maxR = canvasLeft + canvasWidth - pad;
    float curY = topY + config.ScaleF(14.0f);

    // 1. Top Hero Header (Specimen Title + Action Buttons)
    const float linkW = CalcToolbarButtonWidth(theme, config, L"Copy <link>", 96.0f);
    const float cssW  = CalcToolbarButtonWidth(theme, config, L"Copy @font-face", 132.0f);
    const float btnReserve = linkW + cssW + config.ScaleF(24.0f);

    float rx = maxR;
    st->rcBtnCopyLinkTag = {};
    st->rcBtnCopyFontFace = {};
    if (rx - linkW > canvasLeft + pad + config.ScaleF(140.0f)) {
        st->rcBtnCopyLinkTag = D2D1::RectF(rx - linkW, curY, rx, curY + btnH);
        DrawToolbarButton(pRT, theme, config, st->rcBtnCopyLinkTag, L"Copy <link>", false,
                          st->hoveredButtonId == 9, brBtnBg.Get(), brBtnHover.Get(), brBorder.Get());
        rx -= linkW + config.ScaleF(8.0f);
    }
    if (rx - cssW > canvasLeft + pad + config.ScaleF(140.0f)) {
        st->rcBtnCopyFontFace = D2D1::RectF(rx - cssW, curY, rx, curY + btnH);
        DrawToolbarButton(pRT, theme, config, st->rcBtnCopyFontFace, L"Copy @font-face", false,
                          st->hoveredButtonId == 8, brBtnBg.Get(), brBtnHover.Get(), brBorder.Get());
    }

    // Hero specimen headline rendered IN THE LOADED FONT ITSELF
    const float titleMaxR = (std::max)(canvasLeft + pad + config.ScaleF(80.0f), maxR - btnReserve);
    if (theme.dwriteFactory && txtPri) {
        if (pSubFmt && txtMuted) {
            pRT->DrawText(L"You see", 7, pSubFmt,
                          D2D1::RectF(canvasLeft + pad, curY + config.ScaleF(10.0f),
                                      canvasLeft + pad + config.ScaleF(64.0f), curY + config.ScaleF(34.0f)),
                          txtMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        ComPtr<IDWriteTextFormat> pHeroFmt;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextFormat(
                famToUse, collToUse, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(28.0f), L"en-us", &pHeroFmt)) && pHeroFmt) {
            pHeroFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING tr{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            ComPtr<IDWriteInlineObject> ell;
            if (SUCCEEDED(theme.dwriteFactory->CreateEllipsisTrimmingSign(pHeroFmt.Get(), &ell))) {
                pHeroFmt->SetTrimming(&tr, ell.Get());
            }
            pRT->DrawText(st->fontFamilyName.c_str(), static_cast<UINT32>(st->fontFamilyName.size()), pHeroFmt.Get(),
                          D2D1::RectF(canvasLeft + pad + config.ScaleF(64.0f), curY - config.ScaleF(2.0f),
                                      titleMaxR, curY + config.ScaleF(38.0f)),
                          txtPri, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    curY += config.ScaleF(40.0f);

    // Metadata summary strip
    {
        std::wstring meta = L"Style: " + st->fontSubfamilyName +
                            L"   \u2022   " + st->fontFormatLabel +
                            L" (" + FormatByteSize(st->fileSizeBytes) + L")" +
                            (st->fontIsFixedPitch ? L"   \u2022   Monospaced" : L"");
        if (st->fontNumGlyphs > 0) meta += L"   \u2022   " + std::to_wstring(st->fontNumGlyphs) + L" glyphs";
        if (!st->fontVersionStr.empty()) meta += L"   \u2022   " + st->fontVersionStr;
        if (!st->toastMessage.empty() && GetTickCount64() < st->toastExpireTick) {
            meta = st->toastMessage;
        } else if (!st->fontStatusNotice.empty()) {
            meta = st->fontStatusNotice;
        }
        if (pSubFmt && txtSec) {
            pRT->DrawText(meta.c_str(), static_cast<UINT32>(meta.size()), pSubFmt,
                          D2D1::RectF(canvasLeft + pad, curY, maxR, curY + config.ScaleF(22.0f)),
                          (!st->toastMessage.empty() && GetTickCount64() < st->toastExpireTick) ? theme.brushBrandAccent.Get() : txtSec,
                          D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        curY += config.ScaleF(26.0f);
    }

    // 2. OpenType Feature Toggles Row (if SFNT GSUB/GPOS features detected)
    for (auto& ft : st->fontOtFeatures) ft.bounds = {};
    if (!st->fontOtFeatures.empty()) {
        float fx = canvasLeft + pad;
        const float lblW = config.ScaleF(132.0f);
        if (pSubFmt && txtMuted) {
            pRT->DrawText(L"OpenType Features:", 18, pSubFmt,
                          D2D1::RectF(fx, curY, fx + lblW, curY + config.ScaleF(26.0f)),
                          txtMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        fx += lblW + config.ScaleF(4.0f);
        const float otH = config.ScaleF(26.0f);
        for (size_t i = 0; i < st->fontOtFeatures.size(); ++i) {
            auto& ft = st->fontOtFeatures[i];
            const float w = config.ScaleF(64.0f);
            if (fx + w > maxR) break;
            ft.bounds = D2D1::RectF(fx, curY, fx + w, curY + otH);
            const bool hov = (st->hoveredOtFeatureIdx == static_cast<int>(i));
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(ft.bounds, otH * 0.5f, otH * 0.5f);
            pRT->FillRoundedRectangle(rrc, ft.enabled ? theme.brushBrandAccent.Get() : (hov ? brBtnHover.Get() : brBtnBg.Get()));
            pRT->DrawRoundedRectangle(rrc, ft.enabled ? theme.brushBrandAccent.Get() : brBorder.Get(), 1.0f);

            // Switch knob dot + tag name
            const float knobR = config.ScaleF(4.2f);
            const float knobX = ft.enabled ? (ft.bounds.right - config.ScaleF(10.0f)) : (ft.bounds.left + config.ScaleF(10.0f));
            const float knobY = (ft.bounds.top + ft.bounds.bottom) * 0.5f;
            ID2D1Brush* knobBr = ft.enabled ? theme.brushTextWhite.Get() : txtMuted;
            if (knobBr) pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, knobY), knobR, knobR), knobBr);

            if (pSubFmt) {
                D2D1_RECT_F rcTag = ft.enabled
                    ? D2D1::RectF(ft.bounds.left + config.ScaleF(7.0f), ft.bounds.top, knobX - knobR - 2.0f, ft.bounds.bottom)
                    : D2D1::RectF(knobX + knobR + 3.0f, ft.bounds.top, ft.bounds.right - config.ScaleF(4.0f), ft.bounds.bottom);
                DWRITE_PARAGRAPH_ALIGNMENT oldPa = pSubFmt->GetParagraphAlignment();
                pSubFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                pRT->DrawText(ft.tag.c_str(), static_cast<UINT32>(ft.tag.size()), pSubFmt, rcTag,
                              ft.enabled ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : txtPri) : txtPri,
                              D2D1_DRAW_TEXT_OPTIONS_CLIP);
                pSubFmt->SetParagraphAlignment(oldPa);
            }
            fx += w + config.ScaleF(6.0f);
        }
        curY += otH + config.ScaleF(10.0f);
    }

    // 3. View Mode Tabs: [Waterfall] [Glyphs (N)] [Type Yourself] [Specs & CSS]
    {
        float tx = canvasLeft + pad;
        auto drawTabBtn = [&](D2D1_RECT_F& outRc, const std::wstring& lbl, AssetViewerState::FontViewTab t, int btnId) {
            const float w = CalcToolbarButtonWidth(theme, config, lbl.c_str(), 78.0f);
            if (tx + w > maxR) { outRc = {}; return; }
            outRc = D2D1::RectF(tx, curY, tx + w, curY + btnH);
            DrawToolbarButton(pRT, theme, config, outRc, lbl.c_str(), st->fontTab == t,
                              st->hoveredButtonId == btnId, brBtnBg.Get(), brBtnHover.Get(), brBorder.Get());
            tx += w + config.ScaleF(8.0f);
        };
        std::wstring glyphsTabLbl = L"Glyphs (" + std::to_wstring(st->fontNumGlyphs > 0 ? st->fontNumGlyphs : static_cast<uint32_t>(st->fontGlyphCells.size())) + L")";
        drawTabBtn(st->rcFontTabWaterfall, L"Waterfall",     AssetViewerState::FontViewTab::Waterfall, 10);
        drawTabBtn(st->rcFontTabGlyphs,    glyphsTabLbl,     AssetViewerState::FontViewTab::Glyphs,    11);
        drawTabBtn(st->rcFontTabSpecimen,  L"Type Yourself", AssetViewerState::FontViewTab::Specimen,  12);
        drawTabBtn(st->rcFontTabSpecs,     L"Specs & CSS",   AssetViewerState::FontViewTab::Specs,     13);
        curY += btnH + config.ScaleF(10.0f);
    }

    // Interactive Type-Yourself Sample Box on Waterfall & Specimen tabs
    if (st->fontTab == AssetViewerState::FontViewTab::Waterfall ||
        st->fontTab == AssetViewerState::FontViewTab::Specimen) {
        const float boxH = config.ScaleF(34.0f);
        st->rcFontSampleBox = D2D1::RectF(canvasLeft + pad, curY, maxR, curY + boxH);
        D2D1_ROUNDED_RECT rrcBox = D2D1::RoundedRect(st->rcFontSampleBox, config.ScaleF(5.0f), config.ScaleF(5.0f));
        if (brCard) pRT->FillRoundedRectangle(rrcBox, brCard.Get());
        pRT->DrawRoundedRectangle(rrcBox,
                                  st->fontSampleFocused ? theme.brushBrandAccent.Get() : brBorder.Get(),
                                  st->fontSampleFocused ? 1.5f : 1.0f);
        if (pUiFmt && txtPri) {
            D2D1_RECT_F rcTxt = D2D1::RectF(st->rcFontSampleBox.left + config.ScaleF(10.0f), st->rcFontSampleBox.top,
                                            st->rcFontSampleBox.right - config.ScaleF(10.0f), st->rcFontSampleBox.bottom);
            DWRITE_PARAGRAPH_ALIGNMENT oldPa = pUiFmt->GetParagraphAlignment();
            pUiFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            const std::wstring disp = st->fontSampleText.empty() ? L"Click here and type custom specimen text..." : st->fontSampleText;
            pRT->DrawText(disp.c_str(), static_cast<UINT32>(disp.size()), pUiFmt, rcTxt,
                          st->fontSampleText.empty() ? txtMuted : txtPri, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            pUiFmt->SetParagraphAlignment(oldPa);
        }
        curY += boxH + config.ScaleF(10.0f);
    } else {
        st->rcFontSampleBox = {};
    }

    pRT->DrawLine(D2D1::Point2F(canvasLeft, curY), D2D1::Point2F(canvasLeft + canvasWidth, curY), brBorder.Get(), 1.0f);

    // 4. Scrollable Content Body Viewport
    st->rcFontBodyViewport = D2D1::RectF(canvasLeft, curY, canvasLeft + canvasWidth, clientHeight);
    for (auto& gc : st->fontGlyphCells) gc.bounds = {};

    pRT->PushAxisAlignedClip(st->rcFontBodyViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float bodyW = (std::max)(40.0f, canvasWidth - pad * 2.0f);
    const float vpH = (std::max)(1.0f, clientHeight - curY);
    float y = curY + config.ScaleF(16.0f) - st->fontScrollY;
    float totalH = config.ScaleF(24.0f);

    auto drawLoadedFontText = [&](const std::wstring& txt, float ptSize, const D2D1_RECT_F& rc,
                                  ID2D1Brush* br, DWRITE_WORD_WRAPPING wrap, DWRITE_TEXT_ALIGNMENT align) {
        if (!theme.dwriteFactory || !br || txt.empty() || rc.right <= rc.left + 2.0f) return;
        ComPtr<IDWriteTextFormat> pF;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextFormat(
                famToUse, collToUse, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, config.ScaleF(ptSize), L"en-us", &pF)) && pF) {
            pF->SetWordWrapping(wrap);
            pF->SetTextAlignment(align);
            pF->SetParagraphAlignment(wrap == DWRITE_WORD_WRAPPING_NO_WRAP ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            if (wrap == DWRITE_WORD_WRAPPING_NO_WRAP) {
                DWRITE_TRIMMING tr{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                ComPtr<IDWriteInlineObject> ell;
                if (SUCCEEDED(theme.dwriteFactory->CreateEllipsisTrimmingSign(pF.Get(), &ell))) {
                    pF->SetTrimming(&tr, ell.Get());
                }
            }
            ComPtr<IDWriteTextLayout> pLayout;
            const float lw = (std::max)(4.0f, rc.right - rc.left);
            const float lh = (std::max)(4.0f, rc.bottom - rc.top);
            if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                    txt.c_str(), static_cast<UINT32>(txt.size()), pF.Get(), lw, lh, &pLayout)) && pLayout) {
                if (pTypography) {
                    pLayout->SetTypography(pTypography.Get(), { 0u, static_cast<UINT32>(txt.size()) });
                }
                pRT->DrawTextLayout(D2D1::Point2F(rc.left, rc.top), pLayout.Get(), br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
    };

    if (st->fontTab == AssetViewerState::FontViewTab::Waterfall) {
        static constexpr float kSizes[] = { 12.0f, 16.0f, 20.0f, 28.0f, 36.0f, 48.0f };
        for (float sz : kSizes) {
            const float rowH = config.ScaleF(sz) * 1.48f + config.ScaleF(16.0f);
            totalH += rowH + config.ScaleF(8.0f);
            if (y + rowH >= curY && y <= clientHeight) {
                D2D1_RECT_F rcRow = D2D1::RectF(canvasLeft + pad, y, maxR, y + rowH);
                if (brCard) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcRow, 6.0f, 6.0f), brCard.Get());
                if (brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcRow, 6.0f, 6.0f), brBorder.Get(), 0.9f);

                wchar_t szLbl[24]{};
                swprintf_s(szLbl, L"%dpx", static_cast<int>(sz));
                if (pSubFmt && txtMuted) {
                    DWRITE_PARAGRAPH_ALIGNMENT oldPa = pSubFmt->GetParagraphAlignment();
                    pSubFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    pRT->DrawText(szLbl, static_cast<UINT32>(wcslen(szLbl)), pSubFmt,
                                  D2D1::RectF(rcRow.left + config.ScaleF(12.0f), rcRow.top,
                                              rcRow.left + config.ScaleF(62.0f), rcRow.bottom),
                                  txtMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    pSubFmt->SetParagraphAlignment(oldPa);
                }
                drawLoadedFontText(st->fontSampleText, sz,
                                   D2D1::RectF(rcRow.left + config.ScaleF(66.0f), rcRow.top + config.ScaleF(4.0f),
                                               rcRow.right - config.ScaleF(12.0f), rcRow.bottom - config.ScaleF(4.0f)),
                                   txtPri, DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_TEXT_ALIGNMENT_LEADING);
            }
            y += rowH + config.ScaleF(8.0f);
        }
    } else if (st->fontTab == AssetViewerState::FontViewTab::Glyphs) {
        if (st->fontGlyphCells.empty()) {
            if (pUiFmt && txtMuted) {
                pRT->DrawText(L"Glyph inspection is supported directly for uncompressed .ttf / .otf fonts.", 72,
                              pUiFmt, D2D1::RectF(canvasLeft + pad, y, maxR, y + config.ScaleF(40.0f)),
                              txtMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        } else {
            const float tileW = config.ScaleF(84.0f);
            const float tileH = config.ScaleF(78.0f);
            const float gap = config.ScaleF(8.0f);
            const size_t cols = (std::max)(size_t(1), static_cast<size_t>((bodyW + gap) / (tileW + gap)));
            const size_t rows = (st->fontGlyphCells.size() + cols - 1) / cols;
            totalH += static_cast<float>(rows) * (tileH + gap) + config.ScaleF(16.0f);

            for (size_t i = 0; i < st->fontGlyphCells.size(); ++i) {
                const size_t r = i / cols;
                const size_t c = i % cols;
                const float gx = canvasLeft + pad + static_cast<float>(c) * (tileW + gap);
                const float gy = y + static_cast<float>(r) * (tileH + gap);
                if (gy + tileH < curY || gy > clientHeight) continue;

                D2D1_RECT_F rcTile = D2D1::RectF(gx, gy, gx + tileW, gy + tileH);
                st->fontGlyphCells[i].bounds = rcTile;
                const bool hov = (st->hoveredGlyphIdx == static_cast<int>(i));
                D2D1_ROUNDED_RECT rrcT = D2D1::RoundedRect(rcTile, config.ScaleF(6.0f), config.ScaleF(6.0f));
                if (brCard) pRT->FillRoundedRectangle(rrcT, hov ? brCardAlt.Get() : brCard.Get());
                pRT->DrawRoundedRectangle(rrcT, hov ? theme.brushBrandAccent.Get() : brBorder.Get(), hov ? 1.4f : 0.8f);

                drawLoadedFontText(st->fontGlyphCells[i].glyphChar, 28.0f,
                                   D2D1::RectF(rcTile.left + 4.0f, rcTile.top + config.ScaleF(6.0f),
                                               rcTile.right - 4.0f, rcTile.bottom - config.ScaleF(22.0f)),
                                   txtPri, DWRITE_WORD_WRAPPING_NO_WRAP, DWRITE_TEXT_ALIGNMENT_CENTER);
                if (theme.fontBadge && txtMuted) {
                    pRT->DrawText(st->fontGlyphCells[i].hexCode.c_str(),
                                  static_cast<UINT32>(st->fontGlyphCells[i].hexCode.size()),
                                  theme.fontBadge.Get(),
                                  D2D1::RectF(rcTile.left + 4.0f, rcTile.bottom - config.ScaleF(18.0f),
                                              rcTile.right - 4.0f, rcTile.bottom - config.ScaleF(3.0f)),
                                  txtMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
            }
        }
    } else if (st->fontTab == AssetViewerState::FontViewTab::Specimen) {
        const float box1H = config.ScaleF(140.0f);
        const float box2H = config.ScaleF(160.0f);
        const float box3H = config.ScaleF(130.0f);
        totalH += box1H + box2H + box3H + config.ScaleF(48.0f);

        auto drawSpecimenCard = [&](const wchar_t* heading, float ptSz, const std::wstring& body, float h) {
            if (y + h >= curY && y <= clientHeight) {
                D2D1_RECT_F rcC = D2D1::RectF(canvasLeft + pad, y, maxR, y + h);
                if (brCard) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcC, 6.0f, 6.0f), brCard.Get());
                if (brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcC, 6.0f, 6.0f), brBorder.Get(), 1.0f);
                if (pSubFmt && txtMuted) {
                    pRT->DrawText(heading, static_cast<UINT32>(wcslen(heading)), pSubFmt,
                                  D2D1::RectF(rcC.left + config.ScaleF(14.0f), rcC.top + config.ScaleF(10.0f),
                                              rcC.right - config.ScaleF(14.0f), rcC.top + config.ScaleF(30.0f)),
                                  txtMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                drawLoadedFontText(body, ptSz,
                                   D2D1::RectF(rcC.left + config.ScaleF(14.0f), rcC.top + config.ScaleF(32.0f),
                                               rcC.right - config.ScaleF(14.0f), rcC.bottom - config.ScaleF(12.0f)),
                                   txtPri, DWRITE_WORD_WRAPPING_WRAP, DWRITE_TEXT_ALIGNMENT_LEADING);
            }
            y += h + config.ScaleF(12.0f);
        };

        drawSpecimenCard(L"DISPLAY HEADLINE (32px)", 32.0f, st->fontSampleText, box1H);
        drawSpecimenCard(L"CODE & LIGATURES SPECIMEN (16px)", 16.0f,
                         L"const sum = (a, b) => a !== b && a >= 0 ? (a -> b) : null;\nSELECT id, email FROM users WHERE active != false;",
                         box2H);
        drawSpecimenCard(L"CHARACTER SET OVERVIEW (15px)", 15.0f,
                         L"ABCDEFGHIJKLMNOPQRSTUVWXYZ  abcdefghijklmnopqrstuvwxyz\n0123456789  !@#$%^&*()-_=+[]{}|;:'\",.<>/?~`",
                         box3H);
    } else {
        // FontViewTab::Specs
        struct SpecRow { std::wstring key; std::wstring val; };
        std::wstring otSummary;
        for (size_t i = 0; i < st->fontOtFeatures.size(); ++i) {
            if (i > 0) otSummary += L", ";
            otSummary += st->fontOtFeatures[i].tag;
        }
        std::vector<SpecRow> rows = {
            { L"Family Name",      st->fontFamilyName },
            { L"Subfamily / Style", st->fontSubfamilyName },
            { L"Container Format", st->fontFormatLabel + L" (" + FormatByteSize(st->fileSizeBytes) + L")" },
            { L"Total Glyphs",     st->fontNumGlyphs > 0 ? std::to_wstring(st->fontNumGlyphs) : L"Unknown" },
            { L"Units Per Em",     std::to_wstring(st->fontUnitsPerEm) },
            { L"Spacing",          st->fontIsFixedPitch ? L"Monospaced (fixed pitch)" : L"Proportional" },
            { L"Version",          st->fontVersionStr.empty() ? L"-" : st->fontVersionStr },
            { L"Manufacturer",     st->fontManufacturerStr.empty() ? L"-" : st->fontManufacturerStr },
            { L"License",          st->fontLicenseStr.empty() ? L"-" : st->fontLicenseStr },
            { L"OpenType Features", otSummary.empty() ? L"None detected" : otSummary }
        };
        const float rowH = config.ScaleF(34.0f);
        totalH += static_cast<float>(rows.size()) * (rowH + config.ScaleF(4.0f)) + config.ScaleF(160.0f);
        for (const auto& sr : rows) {
            if (y + rowH >= curY && y <= clientHeight) {
                D2D1_RECT_F rcR = D2D1::RectF(canvasLeft + pad, y, maxR, y + rowH);
                if (brCard) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcR, 5.0f, 5.0f), brCard.Get());
                if (brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcR, 5.0f, 5.0f), brBorder.Get(), 0.8f);
                if (pSubFmt && txtMuted) {
                    DWRITE_PARAGRAPH_ALIGNMENT oldPa = pSubFmt->GetParagraphAlignment();
                    pSubFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    pRT->DrawText(sr.key.c_str(), static_cast<UINT32>(sr.key.size()), pSubFmt,
                                  D2D1::RectF(rcR.left + config.ScaleF(12.0f), rcR.top,
                                              rcR.left + config.ScaleF(180.0f), rcR.bottom),
                                  txtMuted, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    pSubFmt->SetParagraphAlignment(oldPa);
                }
                if (pUiFmt && txtPri) {
                    DWRITE_PARAGRAPH_ALIGNMENT oldPa = pUiFmt->GetParagraphAlignment();
                    pUiFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    pRT->DrawText(sr.val.c_str(), static_cast<UINT32>(sr.val.size()), pUiFmt,
                                  D2D1::RectF(rcR.left + config.ScaleF(186.0f), rcR.top,
                                              rcR.right - config.ScaleF(12.0f), rcR.bottom),
                                  txtPri, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    pUiFmt->SetParagraphAlignment(oldPa);
                }
            }
            y += rowH + config.ScaleF(4.0f);
        }
        std::wstring relName = std::filesystem::path(doc.filePath).filename().wstring();
        std::wstring fmtHint = (st->fontFormatLabel == L"WOFF2") ? L"woff2"
                             : (st->fontFormatLabel == L"WOFF") ? L"woff"
                             : (st->fontFormatLabel == L"OTF") ? L"opentype" : L"truetype";
        std::wstring cssPreview = L"@font-face {\n  font-family: '" + st->fontFamilyName + L"';\n  src: url('./" +
                                  relName + L"') format('" + fmtHint + L"');\n  font-display: swap;\n}";
        const float cssBoxH = config.ScaleF(116.0f);
        if (y + cssBoxH >= curY && y <= clientHeight && pCodeFmt && txtSec) {
            D2D1_RECT_F rcCss = D2D1::RectF(canvasLeft + pad, y + config.ScaleF(8.0f), maxR, y + config.ScaleF(8.0f) + cssBoxH);
            if (brCardAlt) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcCss, 6.0f, 6.0f), brCardAlt.Get());
            if (brBorder) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcCss, 6.0f, 6.0f), brBorder.Get(), 1.0f);
            pRT->DrawText(cssPreview.c_str(), static_cast<UINT32>(cssPreview.size()), pCodeFmt,
                          D2D1::RectF(rcCss.left + config.ScaleF(12.0f), rcCss.top + config.ScaleF(10.0f),
                                      rcCss.right - config.ScaleF(12.0f), rcCss.bottom - config.ScaleF(10.0f)),
                          txtSec, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    st->fontMaxScrollY = (std::max)(0.0f, totalH - vpH);
    if (st->fontScrollY > st->fontMaxScrollY) st->fontScrollY = st->fontMaxScrollY;

    pRT->PopAxisAlignedClip();
}

bool AssetViewerRenderer::HandleLButtonDown(POINT clientPt, HWND hwnd, phide::editor::OpenDocument& doc,
                                            const app::AppConfig& /*config*/) {
    if (!doc.assetView) return false;
    auto& st = *doc.assetView;

    if (doc.isFontView) {
        st.fontSampleFocused = PtInRectF(clientPt, st.rcFontSampleBox);
        if (PtInRectF(clientPt, st.rcBtnCopyFontFace)) {
            std::wstring relName = std::filesystem::path(doc.filePath).filename().wstring();
            std::wstring fam = st.fontFamilyName.empty() ? L"CustomFont" : st.fontFamilyName;
            std::wstring fmtHint = (st.fontFormatLabel == L"WOFF2") ? L"woff2"
                                 : (st.fontFormatLabel == L"WOFF") ? L"woff"
                                 : (st.fontFormatLabel == L"OTF") ? L"opentype" : L"truetype";
            std::wstring css = L"@font-face {\n  font-family: '" + fam + L"';\n  src: url('./" + relName +
                               L"') format('" + fmtHint + L"');\n  font-weight: normal;\n  font-style: normal;\n  font-display: swap;\n}\n";
            if (CopyWideTextToClipboard(hwnd, css)) SetToast(st, L"Copied @font-face CSS to clipboard");
            return true;
        }
        if (PtInRectF(clientPt, st.rcBtnCopyLinkTag)) {
            std::wstring relName = std::filesystem::path(doc.filePath).filename().wstring();
            std::wstring link = L"<link rel=\"preload\" href=\"./" + relName + L"\" as=\"font\" type=\"" +
                                MimeTypeForAssetPath(doc.filePath) + L"\" crossorigin />";
            if (CopyWideTextToClipboard(hwnd, link)) SetToast(st, L"Copied font preload <link> tag");
            return true;
        }
        if (PtInRectF(clientPt, st.rcFontTabWaterfall)) { st.fontTab = AssetViewerState::FontViewTab::Waterfall; st.fontScrollY = 0.0f; return true; }
        if (PtInRectF(clientPt, st.rcFontTabGlyphs))    { st.fontTab = AssetViewerState::FontViewTab::Glyphs;    st.fontScrollY = 0.0f; return true; }
        if (PtInRectF(clientPt, st.rcFontTabSpecimen))  { st.fontTab = AssetViewerState::FontViewTab::Specimen;  st.fontScrollY = 0.0f; return true; }
        if (PtInRectF(clientPt, st.rcFontTabSpecs))     { st.fontTab = AssetViewerState::FontViewTab::Specs;     st.fontScrollY = 0.0f; return true; }

        for (auto& ft : st.fontOtFeatures) {
            if (PtInRectF(clientPt, ft.bounds)) {
                ft.enabled = !ft.enabled;
                SetToast(st, L"OpenType '" + ft.tag + L"' " + (ft.enabled ? L"enabled" : L"disabled"));
                return true;
            }
        }
        if (st.fontTab == AssetViewerState::FontViewTab::Glyphs) {
            for (const auto& gc : st.fontGlyphCells) {
                if (PtInRectF(clientPt, gc.bounds)) {
                    if (CopyWideTextToClipboard(hwnd, gc.glyphChar)) {
                        SetToast(st, L"Copied glyph '" + gc.glyphChar + L"' (" + gc.hexCode + L")");
                    }
                    return true;
                }
            }
        }
        return true;
    }

    if (doc.svgSplitPreview && !doc.isImageView) {
        if (PtInRectF(clientPt, st.rcBtnChecker)) {
            st.showCheckerboard = !st.showCheckerboard;
            return true;
        }
        if (PtInRectF(clientPt, st.rcBtnCopyImgTag)) {
            if (CopyWideTextToClipboard(hwnd, doc.buffer.GetText())) {
                SetToast(st, L"Copied SVG markup");
            }
            return true;
        }
        return false;
    }

    if (doc.isImageView) {
        if (PtInRectF(clientPt, st.rcBtnZoomOut)) {
            float cur = (st.imageZoom <= 0.0f) ? 1.0f : st.imageZoom;
            st.imageZoom = (std::max)(0.1f, cur * 0.8f);
            return true;
        }
        if (PtInRectF(clientPt, st.rcBtnZoomFit)) {
            st.imageZoom = 0.0f;
            return true;
        }
        if (PtInRectF(clientPt, st.rcBtnZoom100)) {
            st.imageZoom = 1.0f;
            return true;
        }
        if (PtInRectF(clientPt, st.rcBtnZoomIn)) {
            float cur = (st.imageZoom <= 0.0f) ? 1.0f : st.imageZoom;
            st.imageZoom = (std::min)(8.0f, cur * 1.25f);
            return true;
        }
        if (PtInRectF(clientPt, st.rcBtnChecker)) {
            st.showCheckerboard = !st.showCheckerboard;
            return true;
        }
        if (PtInRectF(clientPt, st.rcBtnCopyImgTag)) {
            std::wstring fn = std::filesystem::path(doc.filePath).filename().wstring();
            std::wstring tag = L"<img src=\"./" + fn + L"\" width=\"" + std::to_wstring(doc.imageWidth) +
                               L"\" height=\"" + std::to_wstring(doc.imageHeight) + L"\" alt=\"\" />";
            if (CopyWideTextToClipboard(hwnd, tag)) SetToast(st, L"Copied <img> HTML tag");
            return true;
        }
        if (PtInRectF(clientPt, st.rcBtnCopyBase64)) {
            std::error_code ec;
            uint64_t fsz = std::filesystem::file_size(doc.filePath, ec);
            if (!ec && fsz > 0 && fsz <= AssetViewerState::kMaxBase64FileBytes) {
                std::ifstream ifs(doc.filePath, std::ios::binary);
                if (ifs.is_open()) {
                    std::vector<uint8_t> raw(static_cast<size_t>(fsz));
                    ifs.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(fsz));
                    std::wstring uri = L"data:" + MimeTypeForAssetPath(doc.filePath) + L";base64," +
                                       Utf8ToWide(EncodeBase64(raw));
                    if (CopyWideTextToClipboard(hwnd, uri)) SetToast(st, L"Copied Base64 Data URI");
                }
            } else {
                SetToast(st, L"File exceeds 4 MB Base64 copy limit");
            }
            return true;
        }
        for (size_t i = 0; i < st.dominantColors.size(); ++i) {
            if (PtInRectF(clientPt, st.dominantColors[i].bounds)) {
                if (CopyWideTextToClipboard(hwnd, st.dominantColors[i].hexString)) {
                    SetToast(st, L"Copied " + st.dominantColors[i].hexString);
                }
                return true;
            }
        }
    }
    return false;
}

bool AssetViewerRenderer::HandleMouseMove(POINT clientPt, phide::editor::OpenDocument& doc) {
    if (!doc.assetView) return false;
    auto& st = *doc.assetView;
    int newBtn = -1;
    if (PtInRectF(clientPt, st.rcBtnZoomOut)) newBtn = 1;
    else if (PtInRectF(clientPt, st.rcBtnZoomFit)) newBtn = 2;
    else if (PtInRectF(clientPt, st.rcBtnZoom100)) newBtn = 3;
    else if (PtInRectF(clientPt, st.rcBtnZoomIn)) newBtn = 4;
    else if (PtInRectF(clientPt, st.rcBtnChecker)) newBtn = 5;
    else if (PtInRectF(clientPt, st.rcBtnCopyImgTag)) newBtn = 6;
    else if (PtInRectF(clientPt, st.rcBtnCopyBase64)) newBtn = 7;
    else if (PtInRectF(clientPt, st.rcBtnCopyFontFace)) newBtn = 8;
    else if (PtInRectF(clientPt, st.rcBtnCopyLinkTag)) newBtn = 9;
    else if (PtInRectF(clientPt, st.rcFontTabWaterfall)) newBtn = 10;
    else if (PtInRectF(clientPt, st.rcFontTabGlyphs)) newBtn = 11;
    else if (PtInRectF(clientPt, st.rcFontTabSpecimen)) newBtn = 12;
    else if (PtInRectF(clientPt, st.rcFontTabSpecs)) newBtn = 13;

    int newSw = -1;
    for (size_t i = 0; i < st.dominantColors.size(); ++i) {
        if (PtInRectF(clientPt, st.dominantColors[i].bounds)) {
            newSw = static_cast<int>(i);
            break;
        }
    }
    int newOt = -1;
    for (size_t i = 0; i < st.fontOtFeatures.size(); ++i) {
        if (PtInRectF(clientPt, st.fontOtFeatures[i].bounds)) {
            newOt = static_cast<int>(i);
            break;
        }
    }
    int newGl = -1;
    if (doc.isFontView && st.fontTab == AssetViewerState::FontViewTab::Glyphs) {
        for (size_t i = 0; i < st.fontGlyphCells.size(); ++i) {
            if (PtInRectF(clientPt, st.fontGlyphCells[i].bounds)) {
                newGl = static_cast<int>(i);
                break;
            }
        }
    }

    bool changed = (newBtn != st.hoveredButtonId || newSw != st.hoveredSwatchIdx ||
                    newOt != st.hoveredOtFeatureIdx || newGl != st.hoveredGlyphIdx);
    st.hoveredButtonId = newBtn;
    st.hoveredSwatchIdx = newSw;
    st.hoveredOtFeatureIdx = newOt;
    st.hoveredGlyphIdx = newGl;
    return changed;
}

LPCWSTR AssetViewerRenderer::GetCursorForPoint(POINT clientPt, const phide::editor::OpenDocument& doc) noexcept {
    if (!doc.assetView) return nullptr;
    const auto& st = *doc.assetView;
    if (PtInRectF(clientPt, st.rcBtnZoomOut) || PtInRectF(clientPt, st.rcBtnZoomFit) ||
        PtInRectF(clientPt, st.rcBtnZoom100) || PtInRectF(clientPt, st.rcBtnZoomIn) ||
        PtInRectF(clientPt, st.rcBtnChecker) || PtInRectF(clientPt, st.rcBtnCopyImgTag) ||
        PtInRectF(clientPt, st.rcBtnCopyBase64) || PtInRectF(clientPt, st.rcBtnCopyFontFace) ||
        PtInRectF(clientPt, st.rcBtnCopyLinkTag) || PtInRectF(clientPt, st.rcFontTabWaterfall) ||
        PtInRectF(clientPt, st.rcFontTabGlyphs) || PtInRectF(clientPt, st.rcFontTabSpecimen) ||
        PtInRectF(clientPt, st.rcFontTabSpecs)) {
        return IDC_HAND;
    }
    for (const auto& sw : st.dominantColors) {
        if (PtInRectF(clientPt, sw.bounds)) return IDC_HAND;
    }
    for (const auto& ft : st.fontOtFeatures) {
        if (PtInRectF(clientPt, ft.bounds)) return IDC_HAND;
    }
    for (const auto& gc : st.fontGlyphCells) {
        if (PtInRectF(clientPt, gc.bounds)) return IDC_HAND;
    }
    if (doc.isFontView && PtInRectF(clientPt, st.rcFontSampleBox)) {
        return IDC_IBEAM;
    }
    return nullptr;
}

bool AssetViewerRenderer::HandleMouseWheel(short delta, bool isCtrlDown, phide::editor::OpenDocument& doc,
                                           const app::AppConfig& config) {
    if (!doc.assetView) return false;
    auto& st = *doc.assetView;
    if (doc.isFontView) {
        float step = config.ScaleF(42.0f) * (delta > 0 ? -1.0f : 1.0f);
        st.fontScrollY = std::clamp(st.fontScrollY + step, 0.0f, st.fontMaxScrollY);
        return true;
    }
    if (doc.isImageView && isCtrlDown) {
        float cur = (st.imageZoom <= 0.0f) ? 1.0f : st.imageZoom;
        st.imageZoom = std::clamp(cur * (delta > 0 ? 1.15f : 0.87f), 0.1f, 8.0f);
        return true;
    }
    return false;
}

bool AssetViewerRenderer::HandleChar(wchar_t ch, phide::editor::OpenDocument& doc) {
    if (!doc.isFontView || !doc.assetView || !doc.assetView->fontSampleFocused) return false;
    if (ch >= 32 && ch != 127 && doc.assetView->fontSampleText.size() < 160) {
        doc.assetView->fontSampleText.push_back(ch);
        return true;
    }
    return false;
}

bool AssetViewerRenderer::HandleKeyDown(WPARAM vk, phide::editor::OpenDocument& doc) {
    if (!doc.isFontView || !doc.assetView || !doc.assetView->fontSampleFocused) return false;
    if (vk == VK_BACK && !doc.assetView->fontSampleText.empty()) {
        doc.assetView->fontSampleText.pop_back();
        return true;
    }
    if (vk == VK_ESCAPE) {
        doc.assetView->fontSampleFocused = false;
        return true;
    }
    return false;
}

} // namespace phide::ui
