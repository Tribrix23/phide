/**
 * @file StructuredViewers.cpp
 * @brief Implementation of modular structured viewers (Hex/ASCII, CSV Table, JSON Tree, Log Tail).
 */

#include "phide/ui/StructuredViewers.hpp"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdio>
#include <regex>

namespace phide::ui {

namespace {

std::wstring ToLowerCopy(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& c : out) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return out;
}

bool ContainsCaseInsensitive(const std::wstring& haystack, const std::wstring& needleLower) {
    if (needleLower.empty()) return true;
    if (needleLower.size() > haystack.size()) return false;
    for (size_t i = 0; i + needleLower.size() <= haystack.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needleLower.size(); ++j) {
            if (static_cast<wchar_t>(towlower(haystack[i + j])) != needleLower[j]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool TryParseNumber(const std::wstring& s, double& outVal) {
    if (s.empty()) return false;
    wchar_t* endPtr = nullptr;
    outVal = wcstod(s.c_str(), &endPtr);
    if (endPtr == s.c_str()) return false;
    while (*endPtr == L' ' || *endPtr == L'\t') ++endPtr;
    return *endPtr == L'\0';
}

bool PtInRectF(POINT pt, const D2D1_RECT_F& r) noexcept {
    return r.right > r.left && r.bottom > r.top &&
           pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
}

std::wstring BytesToWideLossy(const char* data, size_t len) {
    if (!data || len == 0) return {};
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(len), nullptr, 0);
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (needed <= 0) {
        cp = CP_ACP;
        flags = 0;
        needed = MultiByteToWideChar(cp, flags, data, static_cast<int>(len), nullptr, 0);
    }
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(cp, flags, data, static_cast<int>(len), out.data(), needed);
    return out;
}

void DrawButtonPill(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                    const D2D1_RECT_F& rc, const std::wstring& label,
                    ID2D1Brush* fillBrush, ID2D1Brush* textBrush, ID2D1Brush* borderBrush) {
    if (rc.right <= rc.left) return;
    const float rad = config.ScaleF(4.0f);
    D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rc, rad, rad);
    if (fillBrush) pRT->FillRoundedRectangle(rrc, fillBrush);
    if (borderBrush) pRT->DrawRoundedRectangle(rrc, borderBrush, 0.8f);
    IDWriteTextFormat* pFmt = theme.fontMenu ? theme.fontMenu.Get() : theme.fontBody.Get();
    if (pFmt && textBrush) {
        DWRITE_TEXT_ALIGNMENT oldAlign = pFmt->GetTextAlignment();
        pFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRT->DrawText(label.c_str(), static_cast<UINT32>(label.length()), pFmt, rc, textBrush);
        pFmt->SetTextAlignment(oldAlign);
    }
}

bool TryParseHexBytesQuery(const std::wstring& query, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    std::wstring hexOnly;
    hexOnly.reserve(query.size());
    for (size_t i = 0; i < query.size(); ++i) {
        wchar_t c = query[i];
        if (c == L' ' || c == L'\t') continue;
        if ((c == L'0') && i + 1 < query.size() && (query[i + 1] == L'x' || query[i + 1] == L'X') && hexOnly.empty()) {
            ++i;
            continue;
        }
        if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F')) {
            hexOnly.push_back(c);
        } else {
            outBytes.clear();
            return false;
        }
    }
    if (hexOnly.size() < 2 || (hexOnly.size() % 2) != 0) {
        outBytes.clear();
        return false;
    }
    for (size_t i = 0; i < hexOnly.size(); i += 2) {
        auto nibble = [](wchar_t ch) -> uint8_t {
            if (ch >= L'0' && ch <= L'9') return static_cast<uint8_t>(ch - L'0');
            if (ch >= L'a' && ch <= L'f') return static_cast<uint8_t>(10 + (ch - L'a'));
            return static_cast<uint8_t>(10 + (ch - L'A'));
        };
        outBytes.push_back(static_cast<uint8_t>((nibble(hexOnly[i]) << 4) | nibble(hexOnly[i + 1])));
    }
    return !outBytes.empty();
}

} // namespace

// ============================================================================
// StructuredViewerState Methods
// ============================================================================

void StructuredViewerState::ClearCachedBuffers() {
    std::vector<uint8_t>().swap(hexWindowBytes);
    std::vector<HexRowHit>().swap(hexRowHits);
    std::vector<std::wstring>().swap(csvHeaders);
    std::vector<std::vector<std::wstring>>().swap(csvRows);
    std::vector<uint32_t>().swap(csvViewIndices);
    std::vector<float>().swap(csvColWidths);
    std::vector<CsvHeaderHit>().swap(csvHeaderHits);
    std::vector<JsonNode>().swap(jsonNodes);
    std::vector<uint32_t>().swap(jsonVisibleIndices);
    std::vector<JsonRowHit>().swap(jsonRowHits);
    std::vector<std::wstring>().swap(logTailLines);
    std::vector<uint32_t>().swap(logFilteredIndices);
    statusMessage.clear();
    logFilterError.clear();
}

bool StructuredViewerState::LoadHexPage(const std::wstring& path, uint64_t startOffset) {
    if (path.empty()) return false;
    filePath = path;
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        statusMessage = L"Unable to read binary file.";
        return false;
    }

    LARGE_INTEGER liSize{};
    if (!GetFileSizeEx(hFile, &liSize) || liSize.QuadPart < 0) {
        CloseHandle(hFile);
        return false;
    }
    hexTotalSize = static_cast<uint64_t>(liSize.QuadPart);
    uint64_t aligned = (startOffset / kHexBytesPerRow) * kHexBytesPerRow;
    if (hexTotalSize > 0 && aligned >= hexTotalSize) {
        aligned = ((hexTotalSize - 1) / kHexBytesPerRow) * kHexBytesPerRow;
    }
    hexWindowOffset = aligned;
    uint64_t remaining = (hexTotalSize > aligned) ? (hexTotalSize - aligned) : 0;
    size_t toRead = static_cast<size_t>((std::min<uint64_t>)(remaining, kHexPageBytes));
    hexWindowBytes.resize(toRead);

    if (toRead > 0) {
        LARGE_INTEGER liPos{};
        liPos.QuadPart = static_cast<LONGLONG>(aligned);
        if (!SetFilePointerEx(hFile, liPos, nullptr, FILE_BEGIN)) {
            CloseHandle(hFile);
            return false;
        }
        size_t totalRead = 0;
        while (totalRead < toRead) {
            DWORD chunk = 0;
            DWORD ask = static_cast<DWORD>((std::min<size_t>)(toRead - totalRead, 65536u));
            if (!ReadFile(hFile, hexWindowBytes.data() + totalRead, ask, &chunk, nullptr) || chunk == 0) {
                break;
            }
            totalRead += chunk;
        }
        hexWindowBytes.resize(totalRead);
    }
    CloseHandle(hFile);
    return true;
}

bool StructuredViewerState::EnsureHexRowLoaded(uint64_t targetRow) {
    if (hexTotalSize == 0) return true;
    uint64_t byteOff = targetRow * kHexBytesPerRow;
    if (byteOff >= hexWindowOffset && byteOff < hexWindowOffset + hexWindowBytes.size()) {
        // Check if visible page end (~64 rows) might cross past loaded window
        uint64_t tailNeeded = byteOff + 64u * kHexBytesPerRow;
        if (tailNeeded <= hexWindowOffset + hexWindowBytes.size() ||
            hexWindowOffset + hexWindowBytes.size() >= hexTotalSize) {
            return true;
        }
    }
    // Center the 32 KB window around the requested row cleanly
    uint64_t quarterPage = kHexPageBytes / 4;
    uint64_t desiredStart = (byteOff > quarterPage) ? (byteOff - quarterPage) : 0;
    return LoadHexPage(filePath, desiredStart);
}

bool StructuredViewerState::JumpToHexOffsetInput(const std::wstring& input) {
    if (input.empty()) return false;
    std::wstring trimmed = input;
    while (!trimmed.empty() && iswspace(trimmed.front())) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && iswspace(trimmed.back())) trimmed.pop_back();
    if (trimmed.empty()) return false;

    int base = 10;
    const wchar_t* ptr = trimmed.c_str();
    if (trimmed.size() >= 2 && trimmed[0] == L'0' && (trimmed[1] == L'x' || trimmed[1] == L'X')) {
        base = 16;
    } else {
        for (wchar_t c : trimmed) {
            if ((c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F')) {
                base = 16;
                break;
            }
        }
    }
    wchar_t* endP = nullptr;
    uint64_t off = _wcstoui64(ptr, &endP, base);
    if (endP == ptr) return false;
    if (hexTotalSize > 0 && off >= hexTotalSize) {
        off = hexTotalSize - 1;
    }
    hexSelectedOffset = static_cast<int64_t>(off);
    uint64_t row = off / kHexBytesPerRow;
    scrollRow = static_cast<size_t>(row);
    EnsureHexRowLoaded(row);
    wchar_t buf[64]{};
    swprintf_s(buf, L"Offset 0x%llX (%llu)", static_cast<unsigned long long>(off), static_cast<unsigned long long>(off));
    statusMessage = buf;
    return true;
}

bool StructuredViewerState::SearchHexPattern(const std::wstring& queryText) {
    if (queryText.empty() || filePath.empty()) return false;
    std::vector<uint8_t> needle;
    if (!TryParseHexBytesQuery(queryText, needle)) {
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, queryText.c_str(), static_cast<int>(queryText.size()), nullptr, 0, nullptr, nullptr);
        if (utf8Len <= 0) return false;
        needle.resize(static_cast<size_t>(utf8Len));
        WideCharToMultiByte(CP_UTF8, 0, queryText.c_str(), static_cast<int>(queryText.size()),
                            reinterpret_cast<char*>(needle.data()), utf8Len, nullptr, nullptr);
    }
    if (needle.empty()) return false;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    uint64_t startPos = (hexSelectedOffset >= 0) ? (static_cast<uint64_t>(hexSelectedOffset) + 1u) : 0u;
    constexpr size_t kScanChunk = 65536u;
    constexpr uint64_t kMaxScanSpan = 64ull * 1024ull * 1024ull;
    std::vector<uint8_t> chunkBuf(kScanChunk + needle.size());
    size_t carry = 0;

    LARGE_INTEGER liPos{};
    liPos.QuadPart = static_cast<LONGLONG>(startPos);
    SetFilePointerEx(hFile, liPos, nullptr, FILE_BEGIN);

    uint64_t curFileOffset = startPos;
    uint64_t scanned = 0;
    int64_t foundOffset = -1;
    while (scanned < kMaxScanSpan) {
        DWORD bytesRead = 0;
        if (!ReadFile(hFile, chunkBuf.data() + carry, static_cast<DWORD>(kScanChunk), &bytesRead, nullptr) || bytesRead == 0) {
            break;
        }
        size_t total = carry + bytesRead;
        if (total >= needle.size()) {
            auto it = std::search(chunkBuf.begin(), chunkBuf.begin() + total, needle.begin(), needle.end());
            if (it != chunkBuf.begin() + total) {
                size_t dist = static_cast<size_t>(it - chunkBuf.begin());
                foundOffset = static_cast<int64_t>(curFileOffset - carry + dist);
                break;
            }
        }
        curFileOffset += bytesRead;
        scanned += bytesRead;
        carry = (std::min)(needle.size() - 1, total);
        if (carry > 0) {
            memmove(chunkBuf.data(), chunkBuf.data() + total - carry, carry);
        }
    }
    CloseHandle(hFile);

    if (foundOffset >= 0) {
        hexSelectedOffset = foundOffset;
        uint64_t row = static_cast<uint64_t>(foundOffset) / kHexBytesPerRow;
        scrollRow = static_cast<size_t>(row);
        EnsureHexRowLoaded(row);
        wchar_t msg[64]{};
        swprintf_s(msg, L"Match at 0x%llX", static_cast<unsigned long long>(foundOffset));
        statusMessage = msg;
        return true;
    }
    statusMessage = L"Pattern not found (from current cursor)";
    return false;
}

bool StructuredViewerState::ParseCsvText(const std::wstring& text, wchar_t forcedDelimiter) {
    std::vector<std::wstring>().swap(csvHeaders);
    std::vector<std::vector<std::wstring>>().swap(csvRows);
    std::vector<uint32_t>().swap(csvViewIndices);
    csvSortCol = -1;
    csvSortAsc = true;
    csvTooLarge = false;
    csvTruncated = false;
    scrollRow = 0;
    scrollX = 0.0f;

    if (text.size() > kCsvMaxChars) {
        csvTooLarge = true;
        statusMessage = L"CSV exceeds 8 MB limit (use Raw Text)";
        return false;
    }

    if (forcedDelimiter != L'\0') {
        csvDelimiter = forcedDelimiter;
    } else {
        size_t commas = 0, tabs = 0, semis = 0;
        size_t sampleLimit = (std::min<size_t>)(text.size(), 4096u);
        bool inQ = false;
        for (size_t i = 0; i < sampleLimit; ++i) {
            wchar_t c = text[i];
            if (c == L'"') inQ = !inQ;
            else if (!inQ) {
                if (c == L'\n') break;
                if (c == L',') ++commas;
                else if (c == L'\t') ++tabs;
                else if (c == L';') ++semis;
            }
        }
        if (tabs > commas && tabs >= semis) csvDelimiter = L'\t';
        else if (semis > commas && semis > tabs) csvDelimiter = L';';
        else csvDelimiter = L',';
    }

    std::vector<std::wstring> currentRow;
    std::wstring currentCell;
    bool inQuotes = false;
    bool cellStarted = false;

    auto commitCell = [&]() {
        currentRow.push_back(std::move(currentCell));
        currentCell.clear();
        cellStarted = false;
    };
    auto commitRow = [&]() -> bool {
        if (csvHeaders.empty()) {
            csvHeaders = std::move(currentRow);
        } else {
            if (csvRows.size() >= kCsvMaxRows) {
                csvTruncated = true;
                currentRow.clear();
                return false;
            }
            csvRows.push_back(std::move(currentRow));
        }
        currentRow.clear();
        return true;
    };

    const size_t n = text.size();
    for (size_t i = 0; i < n; ++i) {
        wchar_t c = text[i];
        if (inQuotes) {
            if (c == L'"') {
                if (i + 1 < n && text[i + 1] == L'"') {
                    currentCell.push_back(L'"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                currentCell.push_back(c);
            }
        } else {
            if (c == L'"' && !cellStarted && currentCell.empty()) {
                inQuotes = true;
                cellStarted = true;
            } else if (c == csvDelimiter) {
                commitCell();
            } else if (c == L'\r') {
                if (i + 1 < n && text[i + 1] == L'\n') ++i;
                commitCell();
                if (!commitRow()) break;
            } else if (c == L'\n') {
                commitCell();
                if (!commitRow()) break;
            } else {
                cellStarted = true;
                currentCell.push_back(c);
            }
        }
    }
    if (!csvTruncated && (cellStarted || !currentCell.empty() || !currentRow.empty())) {
        commitCell();
        commitRow();
    }

    // Normalize column count & compute reasonable column widths
    size_t colCount = csvHeaders.size();
    for (const auto& r : csvRows) {
        if (r.size() > colCount) colCount = r.size();
    }
    while (csvHeaders.size() < colCount) {
        csvHeaders.push_back(L"Col " + std::to_wstring(csvHeaders.size() + 1));
    }
    csvColWidths.assign(colCount, 120.0f);
    for (size_t c = 0; c < colCount; ++c) {
        size_t maxLen = csvHeaders[c].size() + 2;
        size_t scanRows = (std::min<size_t>)(csvRows.size(), 256u);
        for (size_t r = 0; r < scanRows; ++r) {
            if (c < csvRows[r].size() && csvRows[r][c].size() > maxLen) {
                maxLen = csvRows[r][c].size();
            }
        }
        float w = static_cast<float>((std::clamp<size_t>)(maxLen * 8u + 24u, 76u, 320u));
        csvColWidths[c] = w;
    }

    RebuildCsvFilter();
    return true;
}

void StructuredViewerState::RebuildCsvFilter() {
    csvViewIndices.clear();
    const std::wstring needleLower = ToLowerCopy(filterText);
    const size_t n = csvRows.size();
    csvViewIndices.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        if (needleLower.empty()) {
            csvViewIndices.push_back(i);
            continue;
        }
        bool matched = false;
        for (const auto& cell : csvRows[i]) {
            if (ContainsCaseInsensitive(cell, needleLower)) {
                matched = true;
                break;
            }
        }
        if (matched) csvViewIndices.push_back(i);
    }

    if (csvSortCol >= 0 && static_cast<size_t>(csvSortCol) < csvHeaders.size()) {
        const size_t col = static_cast<size_t>(csvSortCol);
        const bool asc = csvSortAsc;
        std::stable_sort(csvViewIndices.begin(), csvViewIndices.end(), [&](uint32_t aIdx, uint32_t bIdx) {
            const auto& rowA = csvRows[aIdx];
            const auto& rowB = csvRows[bIdx];
            const std::wstring& va = col < rowA.size() ? rowA[col] : std::wstring();
            const std::wstring& vb = col < rowB.size() ? rowB[col] : std::wstring();
            double na = 0.0, nb = 0.0;
            bool isNumA = TryParseNumber(va, na);
            bool isNumB = TryParseNumber(vb, nb);
            int cmp = 0;
            if (isNumA && isNumB) {
                cmp = (na < nb) ? -1 : ((na > nb) ? 1 : 0);
            } else {
                cmp = _wcsicmp(va.c_str(), vb.c_str());
            }
            return asc ? (cmp < 0) : (cmp > 0);
        });
    }
    if (scrollRow >= csvViewIndices.size()) scrollRow = 0;
}

void StructuredViewerState::SortCsvColumn(int colIdx) {
    if (colIdx < 0 || static_cast<size_t>(colIdx) >= csvHeaders.size()) return;
    if (csvSortCol == colIdx) {
        csvSortAsc = !csvSortAsc;
    } else {
        csvSortCol = colIdx;
        csvSortAsc = true;
    }
    RebuildCsvFilter();
}

bool StructuredViewerState::ParseJsonText(const std::wstring& text) {
    std::vector<JsonNode>().swap(jsonNodes);
    std::vector<uint32_t>().swap(jsonVisibleIndices);
    jsonParseError = false;
    jsonTooLarge = false;
    jsonTruncated = false;
    jsonSelectedVisIdx = -1;
    scrollRow = 0;
    scrollX = 0.0f;

    if (text.size() > kJsonMaxChars) {
        jsonTooLarge = true;
        statusMessage = L"JSON exceeds 8 MB limit (use Raw Text)";
        return false;
    }

    struct StackFrame {
        int nodeIdx = -1;
        uint8_t kind = 0; // 0=Obj, 1=Arr
        uint32_t arrayIdx = 0;
        bool expectingKey = false;
        bool expectingColon = false;
        bool expectingCommaOrEnd = false;
        std::wstring pendingKey;
        bool hasPendingKey = false;
    };

    std::vector<StackFrame> stack;
    const size_t n = text.size();
    size_t i = 0;

    auto skipWs = [&]() {
        while (i < n && (text[i] == L' ' || text[i] == L'\t' || text[i] == L'\r' || text[i] == L'\n')) ++i;
    };

    auto parseString = [&](std::wstring& out) -> bool {
        if (i >= n || text[i] != L'"') return false;
        ++i;
        out.clear();
        while (i < n) {
            wchar_t c = text[i++];
            if (c == L'"') return true;
            if (c == L'\\') {
                if (i >= n) return false;
                wchar_t esc = text[i++];
                switch (esc) {
                    case L'"': out.push_back(L'"'); break;
                    case L'\\': out.push_back(L'\\'); break;
                    case L'/': out.push_back(L'/'); break;
                    case L'b': out.push_back(L'\b'); break;
                    case L'f': out.push_back(L'\f'); break;
                    case L'n': out.push_back(L'\n'); break;
                    case L'r': out.push_back(L'\r'); break;
                    case L't': out.push_back(L'\t'); break;
                    case L'u': {
                        if (i + 4 <= n) {
                            unsigned int code = 0;
                            for (int k = 0; k < 4; ++k) {
                                wchar_t hc = text[i + k];
                                code <<= 4;
                                if (hc >= L'0' && hc <= L'9') code |= static_cast<unsigned int>(hc - L'0');
                                else if (hc >= L'a' && hc <= L'f') code |= static_cast<unsigned int>(10 + hc - L'a');
                                else if (hc >= L'A' && hc <= L'F') code |= static_cast<unsigned int>(10 + hc - L'A');
                            }
                            out.push_back(static_cast<wchar_t>(code));
                            i += 4;
                        }
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    };

    auto addNode = [&](int parentIdx, uint8_t kind, const std::wstring& key, const std::wstring& val) -> int {
        if (jsonNodes.size() >= kJsonMaxNodes) {
            jsonTruncated = true;
            return -1;
        }
        JsonNode node;
        node.parentIdx = parentIdx;
        node.depth = parentIdx >= 0 ? static_cast<uint16_t>(jsonNodes[parentIdx].depth + 1) : 0;
        node.kind = kind;
        node.expanded = (node.depth <= 1) && (kind == 0 || kind == 1);
        node.hasChildren = false;
        node.key = key;
        node.valuePreview = val;
        int idx = static_cast<int>(jsonNodes.size());
        jsonNodes.push_back(std::move(node));
        if (parentIdx >= 0) {
            jsonNodes[parentIdx].hasChildren = true;
            jsonNodes[parentIdx].childCount++;
        }
        return idx;
    };

    auto nextKeyForParent = [&](std::wstring& outKey) -> bool {
        if (stack.empty()) {
            outKey = L"root";
            return true;
        }
        auto& top = stack.back();
        if (top.kind == 0) {
            if (!top.hasPendingKey) return false;
            outKey = std::move(top.pendingKey);
            top.pendingKey.clear();
            top.hasPendingKey = false;
            return true;
        }
        outKey = L"[" + std::to_wstring(top.arrayIdx++) + L"]";
        return true;
    };

    while (i < n && !jsonTruncated) {
        skipWs();
        if (i >= n) break;

        if (!stack.empty()) {
            auto& top = stack.back();
            if (top.kind == 0 && top.expectingColon) {
                if (text[i] != L':') { jsonParseError = true; break; }
                ++i;
                top.expectingColon = false;
                continue;
            }
            if (top.expectingCommaOrEnd) {
                wchar_t closer = (top.kind == 0) ? L'}' : L']';
                if (text[i] == L',') {
                    ++i;
                    top.expectingCommaOrEnd = false;
                    if (top.kind == 0) top.expectingKey = true;
                    continue;
                }
                if (text[i] == closer) {
                    ++i;
                    stack.pop_back();
                    if (!stack.empty()) stack.back().expectingCommaOrEnd = true;
                    continue;
                }
                jsonParseError = true;
                break;
            }
            if (top.kind == 0 && top.expectingKey) {
                if (text[i] == L'}') {
                    ++i;
                    stack.pop_back();
                    if (!stack.empty()) stack.back().expectingCommaOrEnd = true;
                    continue;
                }
                if (text[i] != L'"') { jsonParseError = true; break; }
                if (!parseString(top.pendingKey)) { jsonParseError = true; break; }
                top.hasPendingKey = true;
                top.expectingKey = false;
                top.expectingColon = true;
                continue;
            }
            if (top.kind == 1 && text[i] == L']') {
                ++i;
                stack.pop_back();
                if (!stack.empty()) stack.back().expectingCommaOrEnd = true;
                continue;
            }
        }

        int parentIdx = stack.empty() ? -1 : stack.back().nodeIdx;
        std::wstring kName;
        if (!nextKeyForParent(kName)) { jsonParseError = true; break; }

        wchar_t ch = text[i];
        if (ch == L'{' || ch == L'[') {
            uint8_t cKind = (ch == L'{') ? 0 : 1;
            ++i;
            int idx = addNode(parentIdx, cKind, kName, cKind == 0 ? L"{...}" : L"[...]");
            if (idx < 0) break;
            if (!stack.empty()) stack.back().expectingCommaOrEnd = true;
            StackFrame f;
            f.nodeIdx = idx;
            f.kind = cKind;
            f.expectingKey = (cKind == 0);
            stack.push_back(std::move(f));
        } else if (ch == L'"') {
            std::wstring val;
            if (!parseString(val)) { jsonParseError = true; break; }
            if (val.size() > 240) {
                val.resize(240);
                val += L"...";
            }
            addNode(parentIdx, 2, kName, L"\"" + val + L"\"");
            if (!stack.empty()) stack.back().expectingCommaOrEnd = true;
            else break;
        } else {
            size_t start = i;
            while (i < n && text[i] != L',' && text[i] != L'}' && text[i] != L']' &&
                   text[i] != L' ' && text[i] != L'\t' && text[i] != L'\r' && text[i] != L'\n') {
                ++i;
            }
            std::wstring tok = text.substr(start, i - start);
            if (tok.empty()) { jsonParseError = true; break; }
            uint8_t vKind = 3;
            if (tok == L"true" || tok == L"false") vKind = 4;
            else if (tok == L"null") vKind = 5;
            addNode(parentIdx, vKind, kName, tok);
            if (!stack.empty()) stack.back().expectingCommaOrEnd = true;
            else break;
        }
    }

    if (jsonParseError) {
        statusMessage = L"JSON syntax error in document";
    } else if (jsonTruncated) {
        statusMessage = L"JSON tree truncated at 50,000 nodes";
    } else {
        statusMessage = std::to_wstring(jsonNodes.size()) + L" nodes";
    }
    RebuildJsonVisible();
    return !jsonNodes.empty();
}

void StructuredViewerState::RebuildJsonVisible() {
    jsonVisibleIndices.clear();
    if (jsonNodes.empty()) return;

    const std::wstring needleLower = ToLowerCopy(filterText);
    const size_t n = jsonNodes.size();

    if (!needleLower.empty()) {
        std::vector<uint8_t> keep(n, 0);
        for (size_t idx = 0; idx < n; ++idx) {
            const auto& node = jsonNodes[idx];
            if (ContainsCaseInsensitive(node.key, needleLower) ||
                ContainsCaseInsensitive(node.valuePreview, needleLower)) {
                int cur = static_cast<int>(idx);
                while (cur >= 0 && !keep[cur]) {
                    keep[cur] = 1;
                    cur = jsonNodes[cur].parentIdx;
                }
            }
        }
        for (uint32_t idx = 0; idx < n; ++idx) {
            if (keep[idx]) jsonVisibleIndices.push_back(idx);
        }
        if (scrollRow >= jsonVisibleIndices.size()) scrollRow = 0;
        return;
    }

    std::vector<uint8_t> vis(n, 0);
    for (uint32_t idx = 0; idx < n; ++idx) {
        int p = jsonNodes[idx].parentIdx;
        if (p < 0) {
            vis[idx] = 1;
        } else if (vis[p] && jsonNodes[p].expanded) {
            vis[idx] = 1;
        }
        if (vis[idx]) jsonVisibleIndices.push_back(idx);
    }
    if (scrollRow >= jsonVisibleIndices.size()) scrollRow = 0;
}

void StructuredViewerState::ToggleJsonNode(uint32_t nodeIdx) {
    if (nodeIdx >= jsonNodes.size() || !jsonNodes[nodeIdx].hasChildren) return;
    jsonNodes[nodeIdx].expanded = !jsonNodes[nodeIdx].expanded;
    RebuildJsonVisible();
}

void StructuredViewerState::SetJsonExpandAll(bool expand) {
    for (auto& node : jsonNodes) {
        if (node.hasChildren) node.expanded = expand;
    }
    RebuildJsonVisible();
}

bool StructuredViewerState::ReloadLogTailFromFile(const std::wstring& path, bool forceFullTail) {
    if (path.empty()) return false;
    filePath = path;

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        statusMessage = L"Log file unavailable.";
        return false;
    }

    LARGE_INTEGER liSize{};
    if (!GetFileSizeEx(hFile, &liSize) || liSize.QuadPart < 0) {
        CloseHandle(hFile);
        return false;
    }
    const uint64_t diskSize = static_cast<uint64_t>(liSize.QuadPart);

    // Incremental append path when log grows by < 256 KB
    bool canIncremental = (!forceFullTail && !logTailLines.empty() &&
                           diskSize > logFileSize && (diskSize - logFileSize) < kLogTailMaxBytes);

    uint64_t readStart = 0;
    size_t readBytes = 0;
    bool skippedFirstPartial = false;
    if (canIncremental) {
        readStart = logFileSize;
        readBytes = static_cast<size_t>(diskSize - logFileSize);
    } else {
        if (!forceFullTail && !logTailLines.empty() && diskSize == logFileSize) {
            CloseHandle(hFile);
            return false; // No file change
        }
        readStart = (diskSize > kLogTailMaxBytes) ? (diskSize - kLogTailMaxBytes) : 0;
        readBytes = static_cast<size_t>(diskSize - readStart);
        skippedFirstPartial = (readStart > 0);
        std::vector<std::wstring>().swap(logTailLines);
        logTailStartOffset = readStart;
    }

    std::string rawBytes;
    if (readBytes > 0) {
        rawBytes.resize(readBytes);
        LARGE_INTEGER liPos{};
        liPos.QuadPart = static_cast<LONGLONG>(readStart);
        SetFilePointerEx(hFile, liPos, nullptr, FILE_BEGIN);
        size_t got = 0;
        while (got < readBytes) {
            DWORD chunk = 0;
            DWORD ask = static_cast<DWORD>((std::min<size_t>)(readBytes - got, 65536u));
            if (!ReadFile(hFile, rawBytes.data() + got, ask, &chunk, nullptr) || chunk == 0) break;
            got += chunk;
        }
        rawBytes.resize(got);
    }
    CloseHandle(hFile);

    logFileSize = diskSize;
    std::wstring wchunk = BytesToWideLossy(rawBytes.data(), rawBytes.size());

    size_t pos = 0;
    if (skippedFirstPartial) {
        size_t firstNl = wchunk.find(L'\n');
        pos = (firstNl != std::wstring::npos) ? (firstNl + 1) : wchunk.size();
    }

    while (pos < wchunk.size()) {
        size_t nl = wchunk.find(L'\n', pos);
        size_t end = (nl == std::wstring::npos) ? wchunk.size() : nl;
        if (end > pos && wchunk[end - 1] == L'\r') --end;
        logTailLines.push_back(wchunk.substr(pos, end - pos));
        if (nl == std::wstring::npos) break;
        pos = nl + 1;
    }

    // Keep total retained tail strictly bounded under kLogTailMaxBytes chars
    size_t approxChars = 0;
    for (const auto& l : logTailLines) approxChars += l.size() + 2;
    if (approxChars > kLogTailMaxBytes) {
        size_t trimIdx = 0;
        while (trimIdx + 1 < logTailLines.size() && approxChars > kLogTailMaxBytes) {
            approxChars -= (logTailLines[trimIdx].size() + 2);
            ++trimIdx;
        }
        if (trimIdx > 0) {
            logTailLines.erase(logTailLines.begin(), logTailLines.begin() + trimIdx);
        }
    }

    RebuildLogFilter();
    if (logFollow && logFilteredIndices.size() > 1) {
        scrollRow = (logFilteredIndices.size() > 24) ? (logFilteredIndices.size() - 24) : 0;
    }
    return true;
}

bool StructuredViewerState::PollLogTail(const std::wstring& path, uint64_t nowTick) {
    if (!logFollow || path.empty()) return false;
    if (nowTick - logLastPollTick < 400u) return false;
    logLastPollTick = nowTick;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    ULARGE_INTEGER sz{};
    sz.LowPart = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    if (sz.QuadPart == logFileSize) return false;

    return ReloadLogTailFromFile(path, false);
}

void StructuredViewerState::RebuildLogFilter() {
    logFilteredIndices.clear();
    logFilterError.clear();
    const size_t n = logTailLines.size();
    logFilteredIndices.reserve(n);

    std::unique_ptr<std::wregex> re;
    if (logRegex && !filterText.empty()) {
        try {
            re = std::make_unique<std::wregex>(filterText, std::regex_constants::icase);
        } catch (const std::regex_error&) {
            logFilterError = L"Invalid regex";
        }
    }
    const std::wstring needleLower = ToLowerCopy(filterText);

    for (uint32_t i = 0; i < n; ++i) {
        const auto& line = logTailLines[i];
        if (logLevelFilter == 1) {
            if (!ContainsCaseInsensitive(line, L"error") && !ContainsCaseInsensitive(line, L"fatal") &&
                !ContainsCaseInsensitive(line, L"[err]") && !ContainsCaseInsensitive(line, L"exception")) {
                continue;
            }
        } else if (logLevelFilter == 2) {
            if (!ContainsCaseInsensitive(line, L"warn")) continue;
        } else if (logLevelFilter == 3) {
            if (!ContainsCaseInsensitive(line, L"info")) continue;
        }
        if (!filterText.empty()) {
            if (logRegex && re) {
                if (!std::regex_search(line, *re)) continue;
            } else if (!ContainsCaseInsensitive(line, needleLower)) {
                continue;
            }
        }
        logFilteredIndices.push_back(i);
    }
    if (scrollRow >= logFilteredIndices.size()) scrollRow = 0;
}

// ============================================================================
// StructuredViewRenderer
// ============================================================================

StructuredViewMode StructuredViewRenderer::DetectModeForExtension(const std::wstring& extLower) noexcept {
    if (extLower == L".csv" || extLower == L".tsv") return StructuredViewMode::CsvTable;
    if (extLower == L".json" || extLower == L".jsonc") return StructuredViewMode::JsonTree;
    if (extLower == L".log") return StructuredViewMode::LogTail;
    return StructuredViewMode::None;
}

void StructuredViewRenderer::DiscardDeviceResources() noexcept {
    m_pCachedRT = nullptr;
    m_pHeaderBgBrush.Reset();
    m_pRowAltBrush.Reset();
    m_pGridLineBrush.Reset();
    m_pSelBgBrush.Reset();
    m_pPillBgBrush.Reset();
    m_pErrBrush.Reset();
    m_pWarnBrush.Reset();
    m_pInfoBrush.Reset();
}

void StructuredViewRenderer::EnsureBrushes(ID2D1RenderTarget* pRT, const Theme& theme) {
    if (!pRT) return;
    if (m_pCachedRT == pRT && m_pHeaderBgBrush) return;
    DiscardDeviceResources();
    m_pCachedRT = pRT;
    const bool dark = theme.IsDarkMode();
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.145f, 0.155f, 0.180f, 1.0f) : D2D1::ColorF(0.945f, 0.955f, 0.970f, 1.0f), &m_pHeaderBgBrush);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.028f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.030f), &m_pRowAltBrush);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.09f), &m_pGridLineBrush);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.22f, 0.45f, 0.85f, 0.28f) : D2D1::ColorF(0.20f, 0.48f, 0.90f, 0.20f), &m_pSelBgBrush);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.20f, 0.215f, 0.25f, 1.0f) : D2D1::ColorF(0.89f, 0.91f, 0.95f, 1.0f), &m_pPillBgBrush);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.95f, 0.42f, 0.42f, 1.0f) : D2D1::ColorF(0.78f, 0.15f, 0.15f, 1.0f), &m_pErrBrush);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.95f, 0.76f, 0.30f, 1.0f) : D2D1::ColorF(0.72f, 0.48f, 0.05f, 1.0f), &m_pWarnBrush);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.45f, 0.78f, 0.98f, 1.0f) : D2D1::ColorF(0.10f, 0.45f, 0.78f, 1.0f), &m_pInfoBrush);
}

void StructuredViewRenderer::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                    StructuredViewerState& state, float canvasLeft, float canvasWidth,
                                    float topY, float clientHeight) {
    if (!pRT || canvasWidth <= 40.0f || clientHeight <= topY + 40.0f) return;
    EnsureBrushes(pRT, theme);

    const float toolbarH = config.ScaleF(36.0f);
    const float padX = config.ScaleF(12.0f);
    state.rcToolbar = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, topY + toolbarH);
    if (m_pHeaderBgBrush) pRT->FillRectangle(state.rcToolbar, m_pHeaderBgBrush.Get());
    if (m_pGridLineBrush) {
        pRT->DrawLine(D2D1::Point2F(canvasLeft, state.rcToolbar.bottom),
                      D2D1::Point2F(canvasLeft + canvasWidth, state.rcToolbar.bottom),
                      m_pGridLineBrush.Get(), 1.0f);
    }

    // Reset dynamic hit rects cleanly
    state.rcBtnRawText = {};
    state.rcBtnAction1 = {};
    state.rcBtnAction2 = {};
    state.rcBtnAction3 = {};
    state.rcBtnAction4 = {};
    state.rcFilterBox = {};
    state.hexRowHits.clear();
    state.csvHeaderHits.clear();
    state.jsonRowHits.clear();

    const float btnH = config.ScaleF(24.0f);
    const float btnY = topY + (toolbarH - btnH) * 0.5f;
    float rightX = canvasLeft + canvasWidth - padX;

    // Rightmost button: Raw Text / Back to Unsupported Banner
    const std::wstring rawLabel = (state.mode == StructuredViewMode::HexBinary) ? L"Close Hex" : L"Raw Text";
    const float rawW = config.ScaleF(78.0f);
    rightX -= rawW;
    state.rcBtnRawText = D2D1::RectF(rightX, btnY, rightX + rawW, btnY + btnH);
    DrawButtonPill(pRT, theme, config, state.rcBtnRawText, rawLabel, m_pPillBgBrush.Get(),
                   theme.brushTextPrimary.Get(), theme.brushBorderSubtle.Get());
    rightX -= config.ScaleF(8.0f);

    float leftX = canvasLeft + padX;
    std::wstring badgeTitle;
    if (state.mode == StructuredViewMode::HexBinary) badgeTitle = L"HEX VIEWER (32KB PAGE)";
    else if (state.mode == StructuredViewMode::CsvTable) badgeTitle = L"CSV TABLE";
    else if (state.mode == StructuredViewMode::JsonTree) badgeTitle = L"JSON TREE";
    else if (state.mode == StructuredViewMode::LogTail) badgeTitle = L"LOG TAIL (256KB)";

    const float badgeW = config.ScaleF(static_cast<float>(badgeTitle.size()) * 7.2f + 16.0f);
    if (theme.fontMenu) {
        pRT->DrawText(badgeTitle.c_str(), static_cast<UINT32>(badgeTitle.size()), theme.fontMenu.Get(),
                      D2D1::RectF(leftX, btnY, leftX + badgeW, btnY + btnH), theme.brushBrandAccent.Get());
    }
    leftX += badgeW + config.ScaleF(8.0f);

    // Filter / Search input box
    const float filterW = (std::clamp)(canvasWidth * 0.25f, config.ScaleF(120.0f), config.ScaleF(260.0f));
    if (leftX + filterW < rightX - config.ScaleF(160.0f)) {
        state.rcFilterBox = D2D1::RectF(leftX, btnY, leftX + filterW, btnY + btnH);
        D2D1_ROUNDED_RECT rrcF = D2D1::RoundedRect(state.rcFilterBox, config.ScaleF(4.0f), config.ScaleF(4.0f));
        pRT->FillRoundedRectangle(rrcF, theme.brushItemHover.Get());
        pRT->DrawRoundedRectangle(rrcF,
            state.filterFocused ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get(),
            state.filterFocused ? 1.4f : 0.8f);
        std::wstring ph = (state.mode == StructuredViewMode::HexBinary) ? L"Hex / Text / 0xOff..." : L"Filter...";
        const std::wstring& txt = state.filterText.empty() ? ph : state.filterText;
        ID2D1Brush* fBrush = state.filterText.empty() ? theme.brushTextMuted.Get() : theme.brushTextPrimary.Get();
        IDWriteTextFormat* pFmt = theme.fontMenu ? theme.fontMenu.Get() : theme.fontCode.Get();
        if (pFmt && fBrush) {
            pRT->DrawText(txt.c_str(), static_cast<UINT32>(txt.size()), pFmt,
                          D2D1::RectF(state.rcFilterBox.left + config.ScaleF(8.0f), btnY + config.ScaleF(3.0f),
                                      state.rcFilterBox.right - config.ScaleF(6.0f), btnY + btnH),
                          fBrush);
        }
        leftX += filterW + config.ScaleF(8.0f);
    }

    // Mode-specific toolbar action buttons
    auto pushActionBtn = [&](D2D1_RECT_F& outRc, const std::wstring& lbl, float w, bool active) {
        if (leftX + w >= rightX - config.ScaleF(8.0f)) return;
        outRc = D2D1::RectF(leftX, btnY, leftX + w, btnY + btnH);
        DrawButtonPill(pRT, theme, config, outRc, lbl,
                       active ? theme.brushBrandAccent.Get() : m_pPillBgBrush.Get(),
                       active ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get(),
                       theme.brushBorderSubtle.Get());
        leftX += w + config.ScaleF(6.0f);
    };

    if (state.mode == StructuredViewMode::HexBinary) {
        pushActionBtn(state.rcBtnAction1, L"Find Next", config.ScaleF(74.0f), false);
        pushActionBtn(state.rcBtnAction2, L"Go Offset", config.ScaleF(74.0f), false);
        pushActionBtn(state.rcBtnAction3, L"< Page", config.ScaleF(58.0f), false);
        pushActionBtn(state.rcBtnAction4, L"Page >", config.ScaleF(58.0f), false);
    } else if (state.mode == StructuredViewMode::CsvTable) {
        pushActionBtn(state.rcBtnAction1, L"Reset Sort", config.ScaleF(78.0f), state.csvSortCol >= 0);
    } else if (state.mode == StructuredViewMode::JsonTree) {
        pushActionBtn(state.rcBtnAction1, L"Expand All", config.ScaleF(78.0f), false);
        pushActionBtn(state.rcBtnAction2, L"Collapse", config.ScaleF(68.0f), false);
    } else if (state.mode == StructuredViewMode::LogTail) {
        pushActionBtn(state.rcBtnAction1, state.logFollow ? L"Follow: ON" : L"Follow: OFF", config.ScaleF(86.0f), state.logFollow);
        pushActionBtn(state.rcBtnAction2, state.logRegex ? L"Regex: ON" : L"Regex", config.ScaleF(68.0f), state.logRegex);
        const wchar_t* lvlTxt = (state.logLevelFilter == 1) ? L"LVL: ERR"
                              : (state.logLevelFilter == 2) ? L"LVL: WARN"
                              : (state.logLevelFilter == 3) ? L"LVL: INFO" : L"LVL: ALL";
        pushActionBtn(state.rcBtnAction3, lvlTxt, config.ScaleF(72.0f), state.logLevelFilter > 0);
    }

    // Status message on remaining toolbar space
    if (!state.statusMessage.empty() && leftX + config.ScaleF(40.0f) < rightX) {
        IDWriteTextFormat* pFmt = theme.fontMenu ? theme.fontMenu.Get() : theme.fontBody.Get();
        if (pFmt && theme.brushTextMuted) {
            pRT->DrawText(state.statusMessage.c_str(), static_cast<UINT32>(state.statusMessage.size()),
                          pFmt, D2D1::RectF(leftX, btnY + config.ScaleF(3.0f), rightX, btnY + btnH),
                          theme.brushTextMuted.Get());
        }
    }

    const D2D1_RECT_F vp = D2D1::RectF(canvasLeft, state.rcToolbar.bottom, canvasLeft + canvasWidth, clientHeight);
    state.rcViewport = vp;

    pRT->PushAxisAlignedClip(vp, D2D1_ANTIALIAS_MODE_ALIASED);
    if (state.mode == StructuredViewMode::HexBinary) RenderHexBinary(pRT, theme, config, state, vp);
    else if (state.mode == StructuredViewMode::CsvTable) RenderCsvTable(pRT, theme, config, state, vp);
    else if (state.mode == StructuredViewMode::JsonTree) RenderJsonTree(pRT, theme, config, state, vp);
    else if (state.mode == StructuredViewMode::LogTail) RenderLogTail(pRT, theme, config, state, vp);
    pRT->PopAxisAlignedClip();
}

void StructuredViewRenderer::RenderHexBinary(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                             StructuredViewerState& state, const D2D1_RECT_F& vp) {
    const float rowH = config.ScaleF(20.0f);
    const float padX = config.ScaleF(14.0f);
    const uint64_t totalRows = (state.hexTotalSize + StructuredViewerState::kHexBytesPerRow - 1u) / StructuredViewerState::kHexBytesPerRow;
    if (totalRows > 0 && state.scrollRow >= totalRows) {
        state.scrollRow = static_cast<size_t>(totalRows - 1);
    }
    state.EnsureHexRowLoaded(state.scrollRow);

    // Header strip
    const D2D1_RECT_F rcHdr = D2D1::RectF(vp.left, vp.top, vp.right, vp.top + rowH + config.ScaleF(4.0f));
    if (m_pHeaderBgBrush) pRT->FillRectangle(rcHdr, m_pHeaderBgBrush.Get());
    IDWriteTextFormat* pCode = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();
    if (pCode && theme.brushTextSecondary) {
        const wchar_t* hdr = L"Offset      00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F   Decoded ASCII";
        pRT->DrawText(hdr, static_cast<UINT32>(wcslen(hdr)), pCode,
                      D2D1::RectF(vp.left + padX, vp.top + config.ScaleF(2.0f), vp.right - padX, rcHdr.bottom),
                      theme.brushTextSecondary.Get());
    }

    float curY = rcHdr.bottom + config.ScaleF(2.0f);
    size_t visibleCap = static_cast<size_t>((std::max)(1.0f, (vp.bottom - curY) / rowH)) + 1;
    wchar_t lineBuf[128]{};
    const wchar_t hexDigits[] = L"0123456789ABCDEF";

    for (size_t r = 0; r < visibleCap && state.scrollRow + r < totalRows; ++r) {
        uint64_t rowIdx = static_cast<uint64_t>(state.scrollRow) + r;
        uint64_t rowByteStart = rowIdx * StructuredViewerState::kHexBytesPerRow;
        D2D1_RECT_F rcRow = D2D1::RectF(vp.left, curY, vp.right, curY + rowH);
        bool isSel = (state.hexSelectedOffset >= 0 &&
                      static_cast<uint64_t>(state.hexSelectedOffset) >= rowByteStart &&
                      static_cast<uint64_t>(state.hexSelectedOffset) < rowByteStart + 16u);
        if (isSel && m_pSelBgBrush) {
            pRT->FillRectangle(rcRow, m_pSelBgBrush.Get());
        } else if ((rowIdx % 2u) == 1u && m_pRowAltBrush) {
            pRT->FillRectangle(rcRow, m_pRowAltBrush.Get());
        }
        state.hexRowHits.push_back({ rowIdx, rcRow });

        int pos = swprintf_s(lineBuf, L"%08llX    ", static_cast<unsigned long long>(rowByteStart));
        wchar_t asciiPart[20]{};
        size_t asciiLen = 0;
        for (size_t b = 0; b < 16u; ++b) {
            if (b == 8u) lineBuf[pos++] = L' ';
            uint64_t absOff = rowByteStart + b;
            if (absOff >= state.hexWindowOffset && absOff < state.hexWindowOffset + state.hexWindowBytes.size()) {
                uint8_t val = state.hexWindowBytes[static_cast<size_t>(absOff - state.hexWindowOffset)];
                lineBuf[pos++] = hexDigits[(val >> 4) & 0xF];
                lineBuf[pos++] = hexDigits[val & 0xF];
                lineBuf[pos++] = L' ';
                asciiPart[asciiLen++] = (val >= 32 && val <= 126) ? static_cast<wchar_t>(val) : L'.';
            } else {
                lineBuf[pos++] = L' ';
                lineBuf[pos++] = L' ';
                lineBuf[pos++] = L' ';
            }
        }
        lineBuf[pos++] = L' ';
        lineBuf[pos++] = L' ';
        for (size_t a = 0; a < asciiLen; ++a) lineBuf[pos++] = asciiPart[a];
        lineBuf[pos] = L'\0';

        if (pCode && theme.brushTextPrimary) {
            pRT->DrawText(lineBuf, static_cast<UINT32>(pos), pCode,
                          D2D1::RectF(vp.left + padX, curY, vp.right - padX, curY + rowH),
                          theme.brushTextPrimary.Get());
        }
        curY += rowH;
    }
}

void StructuredViewRenderer::RenderCsvTable(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                            StructuredViewerState& state, const D2D1_RECT_F& vp) {
    if (state.csvTooLarge) {
        if (theme.fontBody && theme.brushTextSecondary) {
            const wchar_t* msg = L"CSV file exceeds 8 MB table limit. Click 'Raw Text' to inspect via memory-mapped reader.";
            pRT->DrawText(msg, static_cast<UINT32>(wcslen(msg)), theme.fontBody.Get(),
                          D2D1::RectF(vp.left + config.ScaleF(20.0f), vp.top + config.ScaleF(20.0f), vp.right, vp.bottom),
                          theme.brushTextSecondary.Get());
        }
        return;
    }

    const float rowH = config.ScaleF(22.0f);
    const float hdrH = config.ScaleF(26.0f);
    const float rowNumW = config.ScaleF(52.0f);
    float curX = vp.left + rowNumW - state.scrollX;

    // Column Header strip
    const D2D1_RECT_F rcHdr = D2D1::RectF(vp.left, vp.top, vp.right, vp.top + hdrH);
    if (m_pHeaderBgBrush) pRT->FillRectangle(rcHdr, m_pHeaderBgBrush.Get());
    IDWriteTextFormat* pFmt = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();
    if (pFmt) {
        D2D1_RECT_F rcCorner = D2D1::RectF(vp.left, vp.top, vp.left + rowNumW, rcHdr.bottom);
        pRT->DrawText(L"#", 1, pFmt, rcCorner, theme.brushTextMuted.Get());
    }

    for (size_t c = 0; c < state.csvHeaders.size(); ++c) {
        float w = (c < state.csvColWidths.size()) ? config.ScaleF(state.csvColWidths[c]) : config.ScaleF(120.0f);
        D2D1_RECT_F rcCol = D2D1::RectF(curX, vp.top, curX + w, rcHdr.bottom);
        if (rcCol.right > vp.left + rowNumW && rcCol.left < vp.right) {
            state.csvHeaderHits.push_back({ static_cast<int>(c), rcCol });
            std::wstring label = state.csvHeaders[c];
            if (state.csvSortCol == static_cast<int>(c)) {
                label += state.csvSortAsc ? L" ▲" : L" ▼";
            }
            if (pFmt && theme.brushTextPrimary) {
                pRT->DrawText(label.c_str(), static_cast<UINT32>(label.size()), pFmt,
                              D2D1::RectF(rcCol.left + config.ScaleF(6.0f), rcCol.top + config.ScaleF(3.0f),
                                          rcCol.right - config.ScaleF(6.0f), rcCol.bottom),
                              theme.brushTextPrimary.Get());
            }
            if (m_pGridLineBrush) {
                pRT->DrawLine(D2D1::Point2F(rcCol.right, rcHdr.top), D2D1::Point2F(rcCol.right, vp.bottom), m_pGridLineBrush.Get(), 0.8f);
            }
        }
        curX += w;
    }
    if (m_pGridLineBrush) {
        pRT->DrawLine(D2D1::Point2F(vp.left, rcHdr.bottom), D2D1::Point2F(vp.right, rcHdr.bottom), m_pGridLineBrush.Get(), 1.0f);
    }

    float curY = rcHdr.bottom;
    const size_t total = state.csvViewIndices.size();
    const size_t visCount = static_cast<size_t>((std::max)(1.0f, (vp.bottom - curY) / rowH)) + 1;
    for (size_t i = 0; i < visCount && state.scrollRow + i < total; ++i) {
        size_t rIdx = state.csvViewIndices[state.scrollRow + i];
        D2D1_RECT_F rcRow = D2D1::RectF(vp.left, curY, vp.right, curY + rowH);
        if ((i % 2u) == 1u && m_pRowAltBrush) pRT->FillRectangle(rcRow, m_pRowAltBrush.Get());
        if (pFmt && theme.brushTextMuted) {
            std::wstring rn = std::to_wstring(rIdx + 1);
            pRT->DrawText(rn.c_str(), static_cast<UINT32>(rn.size()), pFmt,
                          D2D1::RectF(vp.left + config.ScaleF(6.0f), curY + config.ScaleF(2.0f), vp.left + rowNumW - config.ScaleF(4.0f), curY + rowH),
                          theme.brushTextMuted.Get());
        }
        float cx = vp.left + rowNumW - state.scrollX;
        const auto& row = state.csvRows[rIdx];
        for (size_t c = 0; c < state.csvHeaders.size(); ++c) {
            float w = (c < state.csvColWidths.size()) ? config.ScaleF(state.csvColWidths[c]) : config.ScaleF(120.0f);
            if (cx + w > vp.left + rowNumW && cx < vp.right && c < row.size()) {
                if (pFmt && theme.brushTextPrimary) {
                    pRT->DrawText(row[c].c_str(), static_cast<UINT32>(row[c].size()), pFmt,
                                  D2D1::RectF(cx + config.ScaleF(6.0f), curY + config.ScaleF(2.0f), cx + w - config.ScaleF(6.0f), curY + rowH),
                                  theme.brushTextPrimary.Get());
                }
            }
            cx += w;
        }
        curY += rowH;
    }
}

void StructuredViewRenderer::RenderJsonTree(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                            StructuredViewerState& state, const D2D1_RECT_F& vp) {
    if (state.jsonTooLarge) {
        if (theme.fontBody && theme.brushTextSecondary) {
            const wchar_t* msg = L"JSON file exceeds 8 MB tree limit. Click 'Raw Text' to view.";
            pRT->DrawText(msg, static_cast<UINT32>(wcslen(msg)), theme.fontBody.Get(),
                          D2D1::RectF(vp.left + config.ScaleF(20.0f), vp.top + config.ScaleF(20.0f), vp.right, vp.bottom),
                          theme.brushTextSecondary.Get());
        }
        return;
    }

    const float rowH = config.ScaleF(21.0f);
    const float padX = config.ScaleF(12.0f);
    const float indentStep = config.ScaleF(18.0f);
    float curY = vp.top + config.ScaleF(4.0f);
    IDWriteTextFormat* pCode = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();
    const size_t total = state.jsonVisibleIndices.size();
    const size_t visCap = static_cast<size_t>((std::max)(1.0f, (vp.bottom - curY) / rowH)) + 1;

    for (size_t i = 0; i < visCap && state.scrollRow + i < total; ++i) {
        uint32_t visIdx = static_cast<uint32_t>(state.scrollRow + i);
        uint32_t nodeIdx = state.jsonVisibleIndices[visIdx];
        const auto& node = state.jsonNodes[nodeIdx];
        D2D1_RECT_F rcRow = D2D1::RectF(vp.left, curY, vp.right, curY + rowH);
        if (static_cast<int>(visIdx) == state.jsonSelectedVisIdx && m_pSelBgBrush) {
            pRT->FillRectangle(rcRow, m_pSelBgBrush.Get());
        } else if ((i % 2u) == 1u && m_pRowAltBrush) {
            pRT->FillRectangle(rcRow, m_pRowAltBrush.Get());
        }
        state.jsonRowHits.push_back({ visIdx, rcRow });

        float x = vp.left + padX + static_cast<float>(node.depth) * indentStep - state.scrollX;
        std::wstring line;
        if (node.hasChildren) {
            line = node.expanded ? L"▾ " : L"▸ ";
        } else {
            line = L"  ";
        }
        line += node.key + L": ";
        if (node.hasChildren) {
            line += (node.kind == 0 ? L"{ " : L"[ ") + std::to_wstring(node.childCount) +
                    (node.kind == 0 ? L" keys }" : L" items ]");
        } else {
            line += node.valuePreview;
        }
        ID2D1Brush* vBrush = theme.brushTextPrimary.Get();
        if (node.kind == 2 && m_pWarnBrush) vBrush = m_pWarnBrush.Get();
        else if (node.kind == 3 && m_pInfoBrush) vBrush = m_pInfoBrush.Get();
        else if ((node.kind == 4 || node.kind == 5) && theme.brushBrandAccent) vBrush = theme.brushBrandAccent.Get();

        if (pCode && vBrush) {
            pRT->DrawText(line.c_str(), static_cast<UINT32>(line.size()), pCode,
                          D2D1::RectF(x, curY + config.ScaleF(2.0f), vp.right - padX, curY + rowH),
                          vBrush);
        }
        curY += rowH;
    }
}

void StructuredViewRenderer::RenderLogTail(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                           StructuredViewerState& state, const D2D1_RECT_F& vp) {
    const float rowH = config.ScaleF(19.0f);
    const float padX = config.ScaleF(12.0f);
    float curY = vp.top + config.ScaleF(4.0f);
    IDWriteTextFormat* pCode = theme.fontCode ? theme.fontCode.Get() : theme.fontBody.Get();
    const size_t total = state.logFilteredIndices.size();
    const size_t visCap = static_cast<size_t>((std::max)(1.0f, (vp.bottom - curY) / rowH)) + 1;

    for (size_t i = 0; i < visCap && state.scrollRow + i < total; ++i) {
        uint32_t lineIdx = state.logFilteredIndices[state.scrollRow + i];
        const auto& line = state.logTailLines[lineIdx];
        D2D1_RECT_F rcRow = D2D1::RectF(vp.left, curY, vp.right, curY + rowH);
        if ((i % 2u) == 1u && m_pRowAltBrush) pRT->FillRectangle(rcRow, m_pRowAltBrush.Get());

        ID2D1Brush* brush = theme.brushTextPrimary.Get();
        if (ContainsCaseInsensitive(line, L"error") || ContainsCaseInsensitive(line, L"fatal") ||
            ContainsCaseInsensitive(line, L"exception")) {
            if (m_pErrBrush) brush = m_pErrBrush.Get();
        } else if (ContainsCaseInsensitive(line, L"warn")) {
            if (m_pWarnBrush) brush = m_pWarnBrush.Get();
        } else if (ContainsCaseInsensitive(line, L"info")) {
            if (m_pInfoBrush) brush = m_pInfoBrush.Get();
        }
        if (pCode && brush) {
            pRT->DrawText(line.c_str(), static_cast<UINT32>(line.size()), pCode,
                          D2D1::RectF(vp.left + padX - state.scrollX, curY + config.ScaleF(1.0f), vp.right - padX, curY + rowH),
                          brush);
        }
        curY += rowH;
    }
}

bool StructuredViewRenderer::HandleLButtonDown(POINT clientPt, StructuredViewerState& state,
                                               const app::AppConfig& config, bool& outSwitchToRawText) {
    outSwitchToRawText = false;
    if (!state.isActive) return false;

    if (PtInRectF(clientPt, state.rcBtnRawText)) {
        state.filterFocused = false;
        state.isActive = false;
        outSwitchToRawText = true;
        return true;
    }
    if (PtInRectF(clientPt, state.rcFilterBox)) {
        state.filterFocused = true;
        return true;
    }
    state.filterFocused = false;

    if (PtInRectF(clientPt, state.rcBtnAction1)) {
        if (state.mode == StructuredViewMode::HexBinary) {
            state.SearchHexPattern(state.filterText);
        } else if (state.mode == StructuredViewMode::CsvTable) {
            state.csvSortCol = -1;
            state.RebuildCsvFilter();
        } else if (state.mode == StructuredViewMode::JsonTree) {
            state.SetJsonExpandAll(true);
        } else if (state.mode == StructuredViewMode::LogTail) {
            state.logFollow = !state.logFollow;
            if (state.logFollow && !state.logFilteredIndices.empty()) {
                state.scrollRow = (state.logFilteredIndices.size() > 24) ? (state.logFilteredIndices.size() - 24) : 0;
            }
        }
        return true;
    }
    if (PtInRectF(clientPt, state.rcBtnAction2)) {
        if (state.mode == StructuredViewMode::HexBinary) {
            state.JumpToHexOffsetInput(state.filterText);
        } else if (state.mode == StructuredViewMode::JsonTree) {
            state.SetJsonExpandAll(false);
        } else if (state.mode == StructuredViewMode::LogTail) {
            state.logRegex = !state.logRegex;
            state.RebuildLogFilter();
        }
        return true;
    }
    if (PtInRectF(clientPt, state.rcBtnAction3)) {
        if (state.mode == StructuredViewMode::HexBinary) {
            uint64_t back = (state.hexWindowOffset >= StructuredViewerState::kHexPageBytes)
                          ? (state.hexWindowOffset - StructuredViewerState::kHexPageBytes) : 0;
            state.LoadHexPage(state.filePath, back);
            state.scrollRow = static_cast<size_t>(state.hexWindowOffset / StructuredViewerState::kHexBytesPerRow);
        } else if (state.mode == StructuredViewMode::LogTail) {
            state.logLevelFilter = (state.logLevelFilter + 1) % 4;
            state.RebuildLogFilter();
        }
        return true;
    }
    if (PtInRectF(clientPt, state.rcBtnAction4) && state.mode == StructuredViewMode::HexBinary) {
        uint64_t next = state.hexWindowOffset + StructuredViewerState::kHexPageBytes;
        if (next < state.hexTotalSize) {
            state.LoadHexPage(state.filePath, next);
            state.scrollRow = static_cast<size_t>(state.hexWindowOffset / StructuredViewerState::kHexBytesPerRow);
        }
        return true;
    }

    if (state.mode == StructuredViewMode::CsvTable) {
        for (const auto& hh : state.csvHeaderHits) {
            if (PtInRectF(clientPt, hh.bounds)) {
                state.SortCsvColumn(hh.colIdx);
                return true;
            }
        }
    } else if (state.mode == StructuredViewMode::JsonTree) {
        for (const auto& jh : state.jsonRowHits) {
            if (PtInRectF(clientPt, jh.bounds) && jh.visIdx < state.jsonVisibleIndices.size()) {
                state.jsonSelectedVisIdx = static_cast<int>(jh.visIdx);
                uint32_t nodeIdx = state.jsonVisibleIndices[jh.visIdx];
                state.ToggleJsonNode(nodeIdx);
                return true;
            }
        }
    } else if (state.mode == StructuredViewMode::HexBinary) {
        for (const auto& hr : state.hexRowHits) {
            if (PtInRectF(clientPt, hr.bounds)) {
                state.hexSelectedOffset = static_cast<int64_t>(hr.rowIndex * StructuredViewerState::kHexBytesPerRow);
                return true;
            }
        }
    }
    (void)config;
    return PtInRectF(clientPt, state.rcToolbar) || PtInRectF(clientPt, state.rcViewport);
}

bool StructuredViewRenderer::HandleMouseWheel(short delta, bool isShiftDown, StructuredViewerState& state,
                                              const app::AppConfig& config) {
    if (!state.isActive) return false;
    if (isShiftDown) {
        float hDelta = (delta > 0) ? -config.ScaleF(64.0f) : config.ScaleF(64.0f);
        state.scrollX = (std::max)(0.0f, state.scrollX + hDelta);
        return true;
    }

    int step = (delta > 0) ? -3 : 3;
    if (state.mode == StructuredViewMode::LogTail && step < 0) {
        state.logFollow = false;
    }
    size_t maxRow = 0;
    if (state.mode == StructuredViewMode::HexBinary) {
        maxRow = static_cast<size_t>((state.hexTotalSize + 15u) / 16u);
    } else if (state.mode == StructuredViewMode::CsvTable) {
        maxRow = state.csvViewIndices.size();
    } else if (state.mode == StructuredViewMode::JsonTree) {
        maxRow = state.jsonVisibleIndices.size();
    } else if (state.mode == StructuredViewMode::LogTail) {
        maxRow = state.logFilteredIndices.size();
    }

    if (step < 0) {
        size_t sub = static_cast<size_t>(-step);
        state.scrollRow = (state.scrollRow > sub) ? (state.scrollRow - sub) : 0;
    } else if (maxRow > 1) {
        state.scrollRow = (std::min)(maxRow - 1, state.scrollRow + static_cast<size_t>(step));
    }
    return true;
}

bool StructuredViewRenderer::HandleChar(wchar_t ch, StructuredViewerState& state) {
    if (!state.isActive || !state.filterFocused) return false;
    if (ch == L'\b') {
        if (!state.filterText.empty()) {
            state.filterText.pop_back();
            if (state.mode == StructuredViewMode::CsvTable) state.RebuildCsvFilter();
            else if (state.mode == StructuredViewMode::JsonTree) state.RebuildJsonVisible();
            else if (state.mode == StructuredViewMode::LogTail) state.RebuildLogFilter();
        }
        return true;
    }
    if (ch >= 32 && ch != 127) {
        state.filterText.push_back(ch);
        if (state.mode == StructuredViewMode::CsvTable) state.RebuildCsvFilter();
        else if (state.mode == StructuredViewMode::JsonTree) state.RebuildJsonVisible();
        else if (state.mode == StructuredViewMode::LogTail) state.RebuildLogFilter();
        return true;
    }
    return false;
}

bool StructuredViewRenderer::HandleKeyDown(WPARAM vk, StructuredViewerState& state) {
    if (!state.isActive) return false;
    if (state.filterFocused) {
        if (vk == VK_ESCAPE) {
            state.filterFocused = false;
            return true;
        }
        if (vk == VK_RETURN && state.mode == StructuredViewMode::HexBinary) {
            if (!state.JumpToHexOffsetInput(state.filterText)) {
                state.SearchHexPattern(state.filterText);
            }
            return true;
        }
        if (vk == VK_BACK) {
            HandleChar(L'\b', state);
            return true;
        }
        return true;
    }
    if (vk == VK_UP) return HandleMouseWheel(WHEEL_DELTA, false, state, app::AppConfig{});
    if (vk == VK_DOWN) return HandleMouseWheel(-WHEEL_DELTA, false, state, app::AppConfig{});
    return false;
}

LPCWSTR StructuredViewRenderer::GetCursorForPoint(POINT clientPt, const StructuredViewerState& state) noexcept {
    if (!state.isActive) return nullptr;
    if (PtInRectF(clientPt, state.rcFilterBox)) {
        return IDC_IBEAM;
    }
    if (PtInRectF(clientPt, state.rcBtnRawText) ||
        PtInRectF(clientPt, state.rcBtnAction1) ||
        PtInRectF(clientPt, state.rcBtnAction2) ||
        PtInRectF(clientPt, state.rcBtnAction3) ||
        PtInRectF(clientPt, state.rcBtnAction4)) {
        return IDC_HAND;
    }
    if (state.mode == StructuredViewMode::CsvTable) {
        for (const auto& hh : state.csvHeaderHits) {
            if (PtInRectF(clientPt, hh.bounds)) return IDC_HAND;
        }
    } else if (state.mode == StructuredViewMode::JsonTree) {
        for (const auto& jh : state.jsonRowHits) {
            if (PtInRectF(clientPt, jh.bounds)) return IDC_HAND;
        }
    } else if (state.mode == StructuredViewMode::HexBinary) {
        for (const auto& hr : state.hexRowHits) {
            if (PtInRectF(clientPt, hr.bounds)) return IDC_HAND;
        }
    }
    if (PtInRectF(clientPt, state.rcToolbar) || PtInRectF(clientPt, state.rcViewport)) {
        return IDC_ARROW;
    }
    return nullptr;
}

} // namespace phide::ui
