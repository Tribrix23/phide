/**
 * @file ActivityHeatmapView.cpp
 * @brief Interactive GitHub-style Commit, Code Churn & IDE Activity Heatmap implementation.
 */

#include "phide/ui/ActivityHeatmapView.hpp"
#include "phide/ui/DrawerPanel.hpp"
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <sstream>
#include <thread>

namespace phide::ui {

namespace {

bool PtInRectF(POINT pt, const D2D1_RECT_F& rc) noexcept {
    return static_cast<float>(pt.x) >= rc.left && static_cast<float>(pt.x) <= rc.right &&
           static_cast<float>(pt.y) >= rc.top && static_cast<float>(pt.y) <= rc.bottom;
}

std::wstring TrimWide(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}

std::wstring ToLowerWide(std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(towlower(ch));
    return s;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

std::wstring FormatIntWithCommas(int val) {
    bool neg = val < 0;
    uint64_t u = neg ? static_cast<uint64_t>(-static_cast<int64_t>(val)) : static_cast<uint64_t>(val);
    std::wstring raw = std::to_wstring(u);
    std::wstring out;
    int count = 0;
    for (int i = static_cast<int>(raw.size()) - 1; i >= 0; --i) {
        if (count > 0 && (count % 3) == 0) out.push_back(L',');
        out.push_back(raw[static_cast<size_t>(i)]);
        ++count;
    }
    if (neg) out.push_back(L'-');
    std::reverse(out.begin(), out.end());
    return out;
}

float MeasureStringWidth(IDWriteFactory* pFactory, IDWriteTextFormat* pFmt, const std::wstring& text) {
    if (!pFactory || !pFmt || text.empty()) return 0.0f;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(pFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), pFmt, 2000.0f, 100.0f, &layout)) || !layout) {
        return static_cast<float>(text.size()) * 7.5f;
    }
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(layout->GetMetrics(&metrics))) {
        return metrics.widthIncludingTrailingWhitespace;
    }
    return static_cast<float>(text.size()) * 7.5f;
}

void AttachEllipsisTrimming(IDWriteFactory* pFactory, IDWriteTextFormat* pFmt) {
    if (!pFactory || !pFmt) return;
    Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(pFactory->CreateEllipsisTrimmingSign(pFmt, &ellipsis)) && ellipsis) {
        DWRITE_TRIMMING trim{};
        trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        pFmt->SetTrimming(&trim, ellipsis.Get());
    }
}

bool MatchesLocalAuthor(const std::wstring& commitAuthor, const std::wstring& localAuthor) {
    if (localAuthor.empty()) return false;
    std::wstring ca = ToLowerWide(TrimWide(commitAuthor));
    std::wstring la = ToLowerWide(TrimWide(localAuthor));
    if (ca.empty() || la.empty()) return false;
    return ca == la || ca.find(la) != std::wstring::npos || la.find(ca) != std::wstring::npos;
}

// Reads integer value following `"key":` inside a JSON object slice `[start, end)`.
int ParseJsonIntField(const std::string& json, size_t start, size_t end, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    size_t pos = json.find(pat, start);
    if (pos == std::string::npos || pos >= end) return 0;
    size_t colon = json.find(':', pos + pat.size());
    if (colon == std::string::npos || colon >= end) return 0;
    size_t i = colon + 1;
    while (i < end && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
    int val = 0;
    while (i < end && json[i] >= '0' && json[i] <= '9') {
        val = val * 10 + (json[i] - '0');
        ++i;
    }
    return val;
}

constexpr const wchar_t* s_monthNames[12] = {
    L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
    L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"
};

constexpr const wchar_t* s_weekdayNames[7] = {
    L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"
};

enum PillActionId {
    PillMetricCommits = 1,
    PillMetricChurn = 2,
    PillMetricIde = 3,
    PillAuthorAll = 4,
    PillAuthorMine = 5,
    PillRangeAuto = 10,
    PillRange16w = 11,
    PillRange26w = 12,
    PillRange52w = 13,
    PillRefresh = 20,
    PillSwitchIdeCta = 21,
    PillClearDay = 22
};

} // namespace

ActivityHeatmapView::ActivityHeatmapView() = default;

ActivityHeatmapView::~ActivityHeatmapView() {
    if (m_isAlive) m_isAlive->store(false);
    SaveIdeHistoryIfDirty(true);
}

bool ActivityHeatmapView::ParseDateYmd(const std::wstring& ymd, int& outYear, int& outMonth, int& outDay) noexcept {
    if (ymd.size() != 10 || ymd[4] != L'-' || ymd[7] != L'-') return false;
    auto parseDigits = [&](size_t start, size_t count, int& outVal) noexcept -> bool {
        int acc = 0;
        for (size_t i = 0; i < count; ++i) {
            wchar_t ch = ymd[start + i];
            if (ch < L'0' || ch > L'9') return false;
            acc = acc * 10 + (ch - L'0');
        }
        outVal = acc;
        return true;
    };
    int y = 0, m = 0, d = 0;
    if (!parseDigits(0, 4, y) || !parseDigits(5, 2, m) || !parseDigits(8, 2, d)) return false;
    if (y < 1970 || y > 2999 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    outYear = y;
    outMonth = m;
    outDay = d;
    return true;
}

std::wstring ActivityHeatmapView::FormatDateYmd(int year, int month, int day) {
    wchar_t buf[32]{};
    swprintf_s(buf, L"%04d-%02d-%02d", year, month, day);
    return buf;
}

int64_t ActivityHeatmapView::DaysFromCivil(int year, int month, int day) noexcept {
    int64_t y = static_cast<int64_t>(year) - (month <= 2 ? 1 : 0);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t m = month;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void ActivityHeatmapView::CivilFromDays(int64_t daysSinceEpoch, int& outYear, int& outMonth, int& outDay) noexcept {
    int64_t z = daysSinceEpoch + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const int64_t m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2 ? 1 : 0);
    outYear = static_cast<int>(y);
    outMonth = static_cast<int>(m);
    outDay = static_cast<int>(d);
}

int ActivityHeatmapView::DayOfWeekYmd(const std::wstring& ymd) noexcept {
    int y = 0, m = 0, d = 0;
    if (!ParseDateYmd(ymd, y, m, d)) return 0;
    int64_t days = DaysFromCivil(y, m, d);
    int dow = static_cast<int>((days + 4) % 7);
    if (dow < 0) dow += 7;
    return dow; // 0 = Sun .. 6 = Sat
}

std::wstring ActivityHeatmapView::AddDaysYmd(const std::wstring& ymd, int deltaDays) {
    int y = 0, m = 0, d = 0;
    if (!ParseDateYmd(ymd, y, m, d)) return ymd;
    int64_t days = DaysFromCivil(y, m, d) + deltaDays;
    CivilFromDays(days, y, m, d);
    return FormatDateYmd(y, m, d);
}

std::wstring ActivityHeatmapView::TodayLocalYmd() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return FormatDateYmd(st.wYear, st.wMonth, st.wDay);
}

std::wstring ActivityHeatmapView::EffectiveTodayYmd() const {
    return !m_overrideTodayYmd.empty() ? m_overrideTodayYmd : TodayLocalYmd();
}

std::wstring ActivityHeatmapView::ResolveDefaultStoragePath() const {
    if (!m_customStoragePath.empty()) return m_customStoragePath;
    wchar_t appData[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        std::filesystem::path dir = std::filesystem::path(appData) / L"phide";
        return (dir / L"activity_heatmap.json").wstring();
    }
    return L"activity_heatmap.json";
}

void ActivityHeatmapView::SetStoragePathForTesting(const std::wstring& filePath) {
    m_customStoragePath = filePath;
    m_ideLoaded = false;
    m_ideDirty = false;
}

void ActivityHeatmapView::EnsureLoaded() {
    if (!m_ideLoaded) {
        LoadIdeHistory();
    }
}

void ActivityHeatmapView::LoadIdeHistory() {
    m_ideLoaded = true;
    std::wstring path = ResolveDefaultStoragePath();
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    LARGE_INTEGER sizeLi{};
    if (!GetFileSizeEx(hFile, &sizeLi) || sizeLi.QuadPart <= 0 || sizeLi.QuadPart > (4LL << 20)) {
        CloseHandle(hFile);
        return;
    }

    std::string raw(static_cast<size_t>(sizeLi.QuadPart), '\0');
    DWORD readBytes = 0;
    BOOL ok = ReadFile(hFile, raw.data(), static_cast<DWORD>(raw.size()), &readBytes, nullptr);
    CloseHandle(hFile);
    if (!ok || readBytes == 0) return;
    raw.resize(readBytes);

    size_t daysKey = raw.find("\"days\"");
    if (daysKey == std::string::npos) return;
    size_t braceStart = raw.find('{', daysKey);
    if (braceStart == std::string::npos) return;

    size_t cursor = braceStart + 1;
    while (cursor < raw.size()) {
        while (cursor < raw.size() && (raw[cursor] == ' ' || raw[cursor] == '\t' || raw[cursor] == '\r' || raw[cursor] == '\n' || raw[cursor] == ',')) {
            ++cursor;
        }
        if (cursor >= raw.size() || raw[cursor] == '}') break;
        if (raw[cursor] != '"') {
            ++cursor;
            continue;
        }
        size_t dateEnd = raw.find('"', cursor + 1);
        if (dateEnd == std::string::npos) break;
        std::wstring dateW = Utf8ToWide(raw.substr(cursor + 1, dateEnd - cursor - 1));
        size_t objStart = raw.find('{', dateEnd);
        if (objStart == std::string::npos) break;
        size_t objEnd = raw.find('}', objStart + 1);
        if (objEnd == std::string::npos) break;

        int y = 0, m = 0, d = 0;
        if (ParseDateYmd(dateW, y, m, d)) {
            auto& rec = m_days[dateW];
            rec.date = dateW;
            rec.saves += ParseJsonIntField(raw, objStart, objEnd, "s");
            rec.commitsTriggered += ParseJsonIntField(raw, objStart, objEnd, "c");
            rec.builds += ParseJsonIntField(raw, objStart, objEnd, "b");
            rec.testsPassed += ParseJsonIntField(raw, objStart, objEnd, "tp");
            rec.testsFailed += ParseJsonIntField(raw, objStart, objEnd, "tf");
            rec.sqlQueries += ParseJsonIntField(raw, objStart, objEnd, "q");
        }
        cursor = objEnd + 1;
    }

    PruneAgedIdeDays(EffectiveTodayYmd());
}

void ActivityHeatmapView::PruneAgedIdeDays(const std::wstring& todayYmd) {
    std::wstring cutoff = AddDaysYmd(todayYmd, -400);
    for (auto it = m_days.begin(); it != m_days.end();) {
        if (it->first < cutoff) {
            it = m_days.erase(it);
        } else {
            ++it;
        }
    }
}

void ActivityHeatmapView::SaveIdeHistoryIfDirty(bool force) {
    if (!m_ideDirty) return;
    ULONGLONG now = GetTickCount64();
    if (!force && (now - m_lastIdeSaveTick) < 15000ULL) return;

    std::wstring path = ResolveDefaultStoragePath();
    std::error_code ec;
    std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }

    std::ostringstream oss;
    oss << "{\n  \"version\": 1,\n  \"days\": {\n";
    bool first = true;
    for (const auto& [date, rec] : m_days) {
        if (rec.TotalIdeActions() <= 0) continue;
        if (!first) oss << ",\n";
        first = false;
        oss << "    \"" << WideToUtf8(date) << "\": {"
            << "\"s\":" << rec.saves
            << ",\"c\":" << rec.commitsTriggered
            << ",\"b\":" << rec.builds
            << ",\"tp\":" << rec.testsPassed
            << ",\"tf\":" << rec.testsFailed
            << ",\"q\":" << rec.sqlQueries << "}";
    }
    oss << "\n  }\n}\n";

    std::string payload = oss.str();
    std::wstring tmpPath = path + L".tmp";
    HANDLE hFile = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
    FlushFileBuffers(hFile);
    CloseHandle(hFile);

    if (ok && written == payload.size()) {
        if (MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            m_ideDirty = false;
            m_lastIdeSaveTick = now;
            return;
        }
    }
    DeleteFileW(tmpPath.c_str());
}

void ActivityHeatmapView::RecordSave() {
    EnsureLoaded();
    std::wstring today = EffectiveTodayYmd();
    auto& rec = m_days[today];
    rec.date = today;
    rec.saves += 1;
    m_ideDirty = true;
    SaveIdeHistoryIfDirty(false);
}

void ActivityHeatmapView::RecordCommitMilestone() {
    EnsureLoaded();
    std::wstring today = EffectiveTodayYmd();
    auto& rec = m_days[today];
    rec.date = today;
    rec.commitsTriggered += 1;
    m_ideDirty = true;
    SaveIdeHistoryIfDirty(true);
}

void ActivityHeatmapView::RecordBuild() {
    EnsureLoaded();
    std::wstring today = EffectiveTodayYmd();
    auto& rec = m_days[today];
    rec.date = today;
    rec.builds += 1;
    m_ideDirty = true;
    SaveIdeHistoryIfDirty(false);
}

void ActivityHeatmapView::RecordTestRun(bool allPassed) {
    EnsureLoaded();
    std::wstring today = EffectiveTodayYmd();
    auto& rec = m_days[today];
    rec.date = today;
    if (allPassed) rec.testsPassed += 1;
    else rec.testsFailed += 1;
    m_ideDirty = true;
    SaveIdeHistoryIfDirty(false);
}

void ActivityHeatmapView::RecordSqlQuery() {
    EnsureLoaded();
    std::wstring today = EffectiveTodayYmd();
    auto& rec = m_days[today];
    rec.date = today;
    rec.sqlQueries += 1;
    m_ideDirty = true;
    SaveIdeHistoryIfDirty(false);
}

void ActivityHeatmapView::ParseGitLogOutput(
    const std::wstring& output,
    const std::wstring& localAuthor,
    std::map<std::wstring, HeatmapDayRecord>& inOutDays,
    std::unordered_map<std::wstring, std::vector<HeatmapCommitEntry>>& inOutCommits) {
    std::wstringstream ss(output);
    std::wstring line;
    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.pop_back();
        if (line.empty()) continue;

        std::vector<std::wstring> parts;
        size_t start = 0;
        while (parts.size() < 4) {
            size_t sep = line.find(L'\x001F', start);
            if (sep == std::wstring::npos) break;
            parts.push_back(line.substr(start, sep - start));
            start = sep + 1;
        }
        parts.push_back(line.substr(start));
        if (parts.size() < 5) continue;

        std::wstring hash = TrimWide(parts[0]);
        std::wstring shortHash = TrimWide(parts[1]);
        std::wstring date = TrimWide(parts[2]);
        std::wstring author = TrimWide(parts[3]);
        std::wstring message = TrimWide(parts[4]);

        int y = 0, m = 0, d = 0;
        if (hash.empty() || !ParseDateYmd(date, y, m, d)) continue;

        bool mine = MatchesLocalAuthor(author, localAuthor);
        auto& day = inOutDays[date];
        day.date = date;
        day.commitCount += 1;
        if (mine) day.myCommitCount += 1;

        auto& vec = inOutCommits[date];
        if (vec.size() < 200) {
            HeatmapCommitEntry ce;
            ce.hash = std::move(hash);
            ce.shortHash = shortHash.empty() ? ce.hash.substr(0, (std::min<size_t>)(7, ce.hash.size())) : std::move(shortHash);
            ce.date = date;
            ce.author = std::move(author);
            ce.message = std::move(message);
            ce.isMine = mine;
            vec.push_back(std::move(ce));
        }
    }
}

void ActivityHeatmapView::ParseGitNumstatOutput(
    const std::wstring& output,
    const std::wstring& localAuthor,
    std::map<std::wstring, HeatmapDayRecord>& inOutDays,
    std::unordered_map<std::wstring, std::vector<HeatmapCommitEntry>>& inOutCommits) {
    std::wstringstream ss(output);
    std::wstring line;
    std::wstring curHash;
    std::wstring curDate;
    bool curMine = false;

    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.pop_back();
        if (line.empty()) continue;

        if (line.rfind(L"@HM@", 0) == 0) {
            curHash.clear();
            curDate.clear();
            curMine = false;
            std::wstring body = line.substr(4);
            size_t s1 = body.find(L'\x001F');
            if (s1 == std::wstring::npos) continue;
            size_t s2 = body.find(L'\x001F', s1 + 1);
            if (s2 == std::wstring::npos) continue;
            curHash = TrimWide(body.substr(0, s1));
            curDate = TrimWide(body.substr(s1 + 1, s2 - s1 - 1));
            std::wstring author = TrimWide(body.substr(s2 + 1));
            curMine = MatchesLocalAuthor(author, localAuthor);
            int y = 0, m = 0, d = 0;
            if (!ParseDateYmd(curDate, y, m, d)) {
                curDate.clear();
                curHash.clear();
            }
            continue;
        }

        if (curDate.empty()) continue;
        size_t t1 = line.find(L'\t');
        if (t1 == std::wstring::npos) continue;
        size_t t2 = line.find(L'\t', t1 + 1);
        if (t2 == std::wstring::npos) continue;

        auto parseNum = [](const std::wstring& tok) noexcept -> int {
            if (tok.empty() || tok == L"-") return 0;
            int v = 0;
            for (wchar_t ch : tok) {
                if (ch < L'0' || ch > L'9') return 0;
                if (v > 10000000) break;
                v = v * 10 + (ch - L'0');
            }
            return v;
        };

        int ins = parseNum(line.substr(0, t1));
        int del = parseNum(line.substr(t1 + 1, t2 - t1 - 1));
        if (ins == 0 && del == 0) continue;

        auto& day = inOutDays[curDate];
        day.date = curDate;
        day.linesAdded += ins;
        day.linesDeleted += del;
        if (curMine) {
            day.myLinesAdded += ins;
            day.myLinesDeleted += del;
        }

        if (!curHash.empty()) {
            auto it = inOutCommits.find(curDate);
            if (it != inOutCommits.end()) {
                for (auto& ce : it->second) {
                    if (ce.hash == curHash) {
                        ce.additions += ins;
                        ce.deletions += del;
                        break;
                    }
                }
            }
        }
    }
}

void ActivityHeatmapView::StartRefreshAsync(const std::wstring& repoRoot, HWND notifyHwnd, UINT notifyMsg) {
    EnsureLoaded();
    m_repoPath = repoRoot;
    uint64_t gen = m_refreshGen.fetch_add(1) + 1;

    if (repoRoot.empty()) {
        m_isGitRepo = false;
        m_isLoading = false;
        m_hasLoadedGitOnce = true;
        m_commitsByDate.clear();
        for (auto& [d, rec] : m_days) {
            rec.commitCount = 0;
            rec.myCommitCount = 0;
            rec.linesAdded = 0;
            rec.linesDeleted = 0;
            rec.myLinesAdded = 0;
            rec.myLinesDeleted = 0;
        }
        return;
    }

    m_isLoading = true;
    auto inbox = m_asyncInbox;
    auto alive = m_isAlive;
    std::wstring dir = repoRoot;

    std::thread([dir, gen, inbox, alive, notifyHwnd, notifyMsg]() {
        if (!alive || !alive->load()) return;

        bool isRepo = false;
        std::wstring localAuthor;
        std::map<std::wstring, HeatmapDayRecord> gitDays;
        std::unordered_map<std::wstring, std::vector<HeatmapCommitEntry>> commitsByDate;

        auto revRes = RunGitCmd(L"rev-parse --is-inside-work-tree", dir, false);
        if (revRes.exitCode == 0 && revRes.output.find(L"true") != std::wstring::npos) {
            isRepo = true;
            auto authRes = RunGitCmd(L"config user.name", dir, false);
            if (authRes.exitCode == 0) {
                localAuthor = TrimWide(authRes.output);
            }

            auto logRes = RunGitCmd(
                L"log -n 4000 --since=\"380 days ago\" --date=short --pretty=format:\"%H%x1f%h%x1f%ad%x1f%an%x1f%s\"",
                dir, false);
            if (logRes.exitCode == 0 && !logRes.output.empty()) {
                ParseGitLogOutput(logRes.output, localAuthor, gitDays, commitsByDate);
            }

            auto statRes = RunGitCmd(
                L"log -n 2000 --since=\"380 days ago\" --date=short --numstat --pretty=format:\"@HM@%H%x1f%ad%x1f%an\"",
                dir, false);
            if (statRes.exitCode == 0 && !statRes.output.empty()) {
                ParseGitNumstatOutput(statRes.output, localAuthor, gitDays, commitsByDate);
            }
        }

        if (!alive || !alive->load()) return;
        {
            std::lock_guard<std::mutex> lk(inbox->mtx);
            inbox->hasUpdate = true;
            inbox->reqGen = gen;
            inbox->repoPath = dir;
            inbox->isGitRepo = isRepo;
            inbox->gitAuthor = std::move(localAuthor);
            inbox->gitDays = std::move(gitDays);
            inbox->commitsByDate = std::move(commitsByDate);
        }
        if (notifyHwnd && IsWindow(notifyHwnd)) {
            PostMessageW(notifyHwnd, notifyMsg, 0, 0);
        }
    }).detach();
}

bool ActivityHeatmapView::ApplyPendingRefresh() {
    if (!m_asyncInbox) return false;

    uint64_t seenGen = 0;
    std::wstring repoPath;
    bool isRepo = false;
    std::wstring gitAuthor;
    std::map<std::wstring, HeatmapDayRecord> gitDays;
    std::unordered_map<std::wstring, std::vector<HeatmapCommitEntry>> commitsByDate;

    {
        std::lock_guard<std::mutex> lk(m_asyncInbox->mtx);
        if (!m_asyncInbox->hasUpdate) return false;
        m_asyncInbox->hasUpdate = false;
        seenGen = m_asyncInbox->reqGen;
        repoPath = std::move(m_asyncInbox->repoPath);
        isRepo = m_asyncInbox->isGitRepo;
        gitAuthor = std::move(m_asyncInbox->gitAuthor);
        gitDays = std::move(m_asyncInbox->gitDays);
        commitsByDate = std::move(m_asyncInbox->commitsByDate);
    }

    if (seenGen != m_refreshGen.load()) {
        return false;
    }

    ApplyGitScanForTesting(repoPath, isRepo, gitAuthor, std::move(gitDays), std::move(commitsByDate));
    return true;
}

void ActivityHeatmapView::ApplyGitScanForTesting(
    const std::wstring& repoRoot,
    bool isGitRepo,
    const std::wstring& localAuthor,
    std::map<std::wstring, HeatmapDayRecord> gitDays,
    std::unordered_map<std::wstring, std::vector<HeatmapCommitEntry>> commitsByDate) {
    EnsureLoaded();
    m_repoPath = repoRoot;
    m_isGitRepo = isGitRepo;
    m_gitAuthor = localAuthor;
    m_isLoading = false;
    m_hasLoadedGitOnce = true;

    // Clear previous Git counters while preserving IDE activity counters cleanly
    for (auto it = m_days.begin(); it != m_days.end();) {
        auto& rec = it->second;
        rec.commitCount = 0;
        rec.myCommitCount = 0;
        rec.linesAdded = 0;
        rec.linesDeleted = 0;
        rec.myLinesAdded = 0;
        rec.myLinesDeleted = 0;
        if (rec.TotalIdeActions() == 0) {
            it = m_days.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& [date, gRec] : gitDays) {
        auto& dst = m_days[date];
        dst.date = date;
        dst.commitCount = gRec.commitCount;
        dst.myCommitCount = gRec.myCommitCount;
        dst.linesAdded = gRec.linesAdded;
        dst.linesDeleted = gRec.linesDeleted;
        dst.myLinesAdded = gRec.myLinesAdded;
        dst.myLinesDeleted = gRec.myLinesDeleted;
    }

    m_commitsByDate = std::move(commitsByDate);
    AutoSelectDefaultDayIfNeeded(EffectiveTodayYmd());
}

int ActivityHeatmapView::GetValueForDate(const std::wstring& ymd) const noexcept {
    auto it = m_days.find(ymd);
    if (it == m_days.end()) return 0;
    const auto& rec = it->second;
    switch (m_metric) {
        case HeatmapMetric::Commits:
            return m_onlyMyCommits ? rec.myCommitCount : rec.commitCount;
        case HeatmapMetric::CodeChurn:
            return rec.TotalChurn(m_onlyMyCommits);
        case HeatmapMetric::IdeActivity:
            return rec.TotalIdeActions();
    }
    return 0;
}

std::vector<HeatmapCommitEntry> ActivityHeatmapView::GetCommitsForDate(const std::wstring& ymd) const {
    auto it = m_commitsByDate.find(ymd);
    if (it == m_commitsByDate.end()) return {};
    if (!m_onlyMyCommits || m_metric == HeatmapMetric::IdeActivity) {
        return it->second;
    }
    std::vector<HeatmapCommitEntry> filtered;
    for (const auto& ce : it->second) {
        if (ce.isMine) filtered.push_back(ce);
    }
    return filtered;
}

HeatmapStats ActivityHeatmapView::ComputeStats(int weeks, const std::wstring& endYmd) const {
    HeatmapStats st;
    if (weeks < 1) weeks = 1;
    st.displayedWeeks = weeks;

    int dowToday = DayOfWeekYmd(endYmd);
    std::wstring endWeekSun = AddDaysYmd(endYmd, -dowToday);
    std::wstring startYmd = AddDaysYmd(endWeekSun, -7 * (weeks - 1));

    int64_t totalWindowDays = (weeks - 1) * 7 + dowToday + 1;
    st.windowDays = static_cast<int>(totalWindowDays);

    int runningStreak = 0;
    for (int i = 0; i < st.windowDays; ++i) {
        std::wstring d = AddDaysYmd(startYmd, i);
        int val = GetValueForDate(d);
        if (val > 0) {
            st.totalValue += val;
            st.activeDays += 1;
            runningStreak += 1;
            st.longestStreak = (std::max)(st.longestStreak, runningStreak);
            if (val > st.peakValue) {
                st.peakValue = val;
                st.peakDate = d;
            }
        } else {
            runningStreak = 0;
        }
    }

    // Current streak: consecutive active days walking backward from today (or yesterday if today has 0 so far)
    int todayVal = GetValueForDate(endYmd);
    int offset = (todayVal > 0) ? 0 : -1;
    for (int step = 0; step < st.windowDays; ++step) {
        std::wstring d = AddDaysYmd(endYmd, offset - step);
        if (d < startYmd) break;
        if (GetValueForDate(d) > 0) {
            st.currentStreak += 1;
        } else {
            break;
        }
    }

    return st;
}

void ActivityHeatmapView::AutoSelectDefaultDayIfNeeded(const std::wstring& endYmd) {
    if (!m_selectedDate.empty()) return;
    for (int i = 0; i < 365; ++i) {
        std::wstring d = AddDaysYmd(endYmd, -i);
        if (GetValueForDate(d) > 0) {
            m_selectedDate = d;
            return;
        }
    }
    m_selectedDate = endYmd;
}

int ActivityHeatmapView::ResolveTargetWeeks(float gridAvailW, float scale, int& outMaxFitWeeks) const noexcept {
    const float minCell = 11.0f * scale;
    const float minGap = 3.0f * scale;
    int fitWeeks = static_cast<int>((gridAvailW + minGap) / (minCell + minGap));
    fitWeeks = (std::clamp)(fitWeeks, 8, 52);
    outMaxFitWeeks = fitWeeks;

    if (m_range != HeatmapRange::Auto) {
        return static_cast<int>(m_range);
    }
    if (fitWeeks >= 52) return 52;
    if (fitWeeks >= 36) return 36;
    if (fitWeeks >= 26) return 26;
    if (fitWeeks >= 16) return 16;
    return fitWeeks;
}

int ActivityHeatmapView::ComputeIntensityLevel(int value, int maxWindowVal) const noexcept {
    if (value <= 0) return 0;
    if (maxWindowVal <= 1) return 4;
    if (maxWindowVal <= 4) {
        if (value == 1) return 1;
        if (value == 2) return 2;
        if (value == 3) return 3;
        return 4;
    }
    double ratio = static_cast<double>(value) / static_cast<double>(maxWindowVal);
    if (ratio <= 0.25) return 1;
    if (ratio <= 0.50) return 2;
    if (ratio <= 0.75) return 3;
    return 4;
}

D2D1_COLOR_F ActivityHeatmapView::GetLevelColor(int level, bool isDark) const noexcept {
    if (level <= 0) {
        return isDark ? D2D1::ColorF(0.086f, 0.106f, 0.133f, 1.0f)   // #161B22
                      : D2D1::ColorF(0.922f, 0.929f, 0.941f, 1.0f);  // #EBEDF0
    }
    if (m_metric == HeatmapMetric::Commits) {
        if (isDark) {
            switch (level) {
                case 1: return D2D1::ColorF(0.055f, 0.267f, 0.161f, 1.0f); // #0E4429
                case 2: return D2D1::ColorF(0.000f, 0.427f, 0.196f, 1.0f); // #006D32
                case 3: return D2D1::ColorF(0.149f, 0.651f, 0.255f, 1.0f); // #26A641
                default: return D2D1::ColorF(0.224f, 0.827f, 0.325f, 1.0f); // #39D353
            }
        }
        switch (level) {
            case 1: return D2D1::ColorF(0.608f, 0.914f, 0.659f, 1.0f); // #9BE9A8
            case 2: return D2D1::ColorF(0.251f, 0.769f, 0.388f, 1.0f); // #40C463
            case 3: return D2D1::ColorF(0.188f, 0.631f, 0.306f, 1.0f); // #30A14E
            default: return D2D1::ColorF(0.129f, 0.431f, 0.224f, 1.0f); // #216E39
        }
    }
    if (m_metric == HeatmapMetric::CodeChurn) {
        if (isDark) {
            switch (level) {
                case 1: return D2D1::ColorF(0.290f, 0.169f, 0.051f, 1.0f);
                case 2: return D2D1::ColorF(0.561f, 0.310f, 0.078f, 1.0f);
                case 3: return D2D1::ColorF(0.784f, 0.463f, 0.098f, 1.0f);
                default: return D2D1::ColorF(1.000f, 0.655f, 0.149f, 1.0f);
            }
        }
        switch (level) {
            case 1: return D2D1::ColorF(1.000f, 0.878f, 0.698f, 1.0f);
            case 2: return D2D1::ColorF(1.000f, 0.718f, 0.302f, 1.0f);
            case 3: return D2D1::ColorF(0.984f, 0.549f, 0.000f, 1.0f);
            default: return D2D1::ColorF(0.902f, 0.318f, 0.000f, 1.0f);
        }
    }
    // IdeActivity
    if (isDark) {
        switch (level) {
            case 1: return D2D1::ColorF(0.071f, 0.208f, 0.357f, 1.0f);
            case 2: return D2D1::ColorF(0.106f, 0.369f, 0.604f, 1.0f);
            case 3: return D2D1::ColorF(0.176f, 0.545f, 0.878f, 1.0f);
            default: return D2D1::ColorF(0.345f, 0.722f, 1.000f, 1.0f);
        }
    }
    switch (level) {
        case 1: return D2D1::ColorF(0.733f, 0.871f, 0.984f, 1.0f);
        case 2: return D2D1::ColorF(0.392f, 0.710f, 0.965f, 1.0f);
        case 3: return D2D1::ColorF(0.118f, 0.533f, 0.898f, 1.0f);
        default: return D2D1::ColorF(0.051f, 0.278f, 0.631f, 1.0f);
    }
}

std::wstring ActivityHeatmapView::FormatDateDisplay(const std::wstring& ymd) const {
    int y = 0, m = 0, d = 0;
    if (!ParseDateYmd(ymd, y, m, d)) return ymd;
    int dow = DayOfWeekYmd(ymd);
    wchar_t buf[64]{};
    swprintf_s(buf, L"%s, %s %02d, %04d", s_weekdayNames[dow], s_monthNames[m - 1], d, y);
    return buf;
}

std::wstring ActivityHeatmapView::FormatMetricTooltipText(const HeatmapDayRecord* pRec, const std::wstring& ymd) const {
    std::wstring hdr = FormatDateDisplay(ymd);
    if (!pRec) {
        if (m_metric == HeatmapMetric::Commits) return hdr + L"\nNo commits";
        if (m_metric == HeatmapMetric::CodeChurn) return hdr + L"\n0 lines changed";
        return hdr + L"\nNo IDE activity";
    }
    if (m_metric == HeatmapMetric::Commits) {
        int c = m_onlyMyCommits ? pRec->myCommitCount : pRec->commitCount;
        if (c == 0) return hdr + L"\nNo commits";
        return hdr + L"\n" + FormatIntWithCommas(c) + (c == 1 ? L" commit" : L" commits") + L" · Click to inspect";
    }
    if (m_metric == HeatmapMetric::CodeChurn) {
        int ins = m_onlyMyCommits ? pRec->myLinesAdded : pRec->linesAdded;
        int del = m_onlyMyCommits ? pRec->myLinesDeleted : pRec->linesDeleted;
        if (ins + del == 0) return hdr + L"\n0 lines changed";
        return hdr + L"\n+" + FormatIntWithCommas(ins) + L" / -" + FormatIntWithCommas(del) + L" lines · Click to inspect";
    }
    int tot = pRec->TotalIdeActions();
    if (tot == 0) return hdr + L"\nNo IDE activity";
    return hdr + L"\n" + FormatIntWithCommas(tot) + (tot == 1 ? L" IDE action" : L" IDE actions") +
           L" (" + std::to_wstring(pRec->saves) + L" saves, " +
           std::to_wstring(pRec->builds) + L" builds, " +
           std::to_wstring(pRec->sqlQueries) + L" SQL)";
}

void ActivityHeatmapView::EnsureTextFormats(Theme& theme, const app::AppConfig& config) {
    if (!theme.dwriteFactory) return;
    float scale = config.ScaleF(1.0f);
    std::wstring fam = theme.GetEditorFontFamily().empty() ? L"Segoe UI" : theme.GetEditorFontFamily();
    if (std::abs(m_fmtScale - scale) < 0.01f && m_fmtFamily == fam && m_fmtTitle && m_fmtBody) {
        return;
    }
    m_fmtScale = scale;
    m_fmtFamily = fam;

    auto makeFmt = [&](float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align, Microsoft::WRL::ComPtr<IDWriteTextFormat>& outFmt) {
        outFmt.Reset();
        if (SUCCEEDED(theme.dwriteFactory->CreateTextFormat(
                fam.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                size * scale, L"en-US", &outFmt)) && outFmt) {
            outFmt->SetTextAlignment(align);
            outFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            outFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            AttachEllipsisTrimming(theme.dwriteFactory.Get(), outFmt.Get());
        }
    };

    makeFmt(19.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, m_fmtTitle);
    makeFmt(13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, m_fmtHeading);
    makeFmt(12.0f, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_LEADING, m_fmtBody);
    makeFmt(10.5f, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_LEADING, m_fmtSmall);
    makeFmt(20.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, m_fmtMetricValue);
    makeFmt(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, m_fmtMono);
}

void ActivityHeatmapView::Render(
    ID2D1RenderTarget* pRT,
    Theme& theme,
    const app::AppConfig& config,
    float canvasLeft,
    float canvasWidth,
    float topY,
    float clientHeight) {
    if (!pRT || canvasWidth <= 20.0f || clientHeight <= topY + 20.0f) return;
    EnsureLoaded();
    EnsureTextFormats(theme, config);

    m_rcViewport = D2D1::RectF(canvasLeft, topY, canvasLeft + canvasWidth, clientHeight);
    m_cellHits.clear();
    m_pillHits.clear();
    m_commitHits.clear();
    m_rcRefreshBtn = {};
    m_rcSwitchIdeBtn = {};
    m_rcScrollTrack = {};
    m_rcScrollThumb = {};

    const float scale = config.ScaleF(1.0f);
    const bool isDark = theme.IsDarkMode();
    const bool isNarrow = canvasWidth < (640.0f * scale);
    const float padX = isNarrow ? (16.0f * scale) : (28.0f * scale);
    const float scrollbarReserve = 10.0f * scale;
    const float contentW = (std::max)(220.0f * scale, canvasWidth - 2.0f * padX - scrollbarReserve);
    const float leftX = canvasLeft + padX;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dynBrush;
    if (FAILED(pRT->CreateSolidColorBrush(theme.colors.canvasBg, &dynBrush)) || !dynBrush) return;

    auto setColor = [&](const D2D1_COLOR_F& col) -> ID2D1Brush* {
        dynBrush->SetColor(col);
        return dynBrush.Get();
    };

    const D2D1_COLOR_F cardBg = isDark
        ? D2D1::ColorF((std::min)(1.0f, theme.colors.canvasBg.r + 0.032f),
                       (std::min)(1.0f, theme.colors.canvasBg.g + 0.035f),
                       (std::min)(1.0f, theme.colors.canvasBg.b + 0.042f), 1.0f)
        : D2D1::ColorF(0.968f, 0.973f, 0.980f, 1.0f);
    const D2D1_COLOR_F pillBg = isDark ? theme.colors.itemHover : D2D1::ColorF(0.91f, 0.92f, 0.94f, 1.0f);
    const D2D1_COLOR_F accentCol = theme.colors.brandAccent;
    const D2D1_COLOR_F accentFill = D2D1::ColorF(accentCol.r, accentCol.g, accentCol.b, isDark ? 0.22f : 0.14f);

    pRT->PushAxisAlignedClip(m_rcViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (theme.brushCanvasBg) {
        pRT->FillRectangle(m_rcViewport, theme.brushCanvasBg.Get());
    }

    float curY = topY + (20.0f * scale) - m_scrollY;

    // 1. Header Band (Title + Repo subtitle + Refresh button)
    {
        float btnW = MeasureStringWidth(theme.dwriteFactory.Get(), m_fmtSmall.Get(), L"Refresh") + 22.0f * scale;
        float btnH = 28.0f * scale;
        D2D1_RECT_F rcBtn = D2D1::RectF(leftX + contentW - btnW, curY + 2.0f * scale, leftX + contentW, curY + 2.0f * scale + btnH);
        m_rcRefreshBtn = rcBtn;
        m_pillHits.push_back({ rcBtn, PillRefresh });

        D2D1_RECT_F rcTitle = D2D1::RectF(leftX, curY, rcBtn.left - 12.0f * scale, curY + 26.0f * scale);
        if (m_fmtTitle && theme.brushTextPrimary) {
            pRT->DrawText(L"Activity Heatmap", 16, m_fmtTitle.Get(), rcTitle, theme.brushTextPrimary.Get());
        }

        std::wstring sub = m_isLoading
            ? L"Scanning commit history..."
            : (m_repoPath.empty() ? L"No workspace folder · Local IDE activity active"
                                  : (m_isGitRepo ? (L"Repository: " + std::filesystem::path(m_repoPath).filename().wstring())
                                                 : (L"Folder (non-Git): " + std::filesystem::path(m_repoPath).filename().wstring())));
        D2D1_RECT_F rcSub = D2D1::RectF(leftX, curY + 27.0f * scale, rcBtn.left - 12.0f * scale, curY + 44.0f * scale);
        if (m_fmtSmall && theme.brushTextSecondary) {
            pRT->DrawText(sub.c_str(), static_cast<UINT32>(sub.size()), m_fmtSmall.Get(), rcSub, theme.brushTextSecondary.Get());
        }

        bool hovRefresh = (m_hoveredPillId == PillRefresh);
        D2D1_ROUNDED_RECT rrcBtn = D2D1::RoundedRect(rcBtn, 5.0f * scale, 5.0f * scale);
        pRT->FillRoundedRectangle(rrcBtn, setColor(hovRefresh ? theme.colors.itemActive : pillBg));
        pRT->DrawRoundedRectangle(rrcBtn, setColor(hovRefresh ? accentCol : theme.colors.borderHover), 1.0f);
        if (m_fmtSmall && theme.brushTextPrimary) {
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pRT->DrawText(m_isLoading ? L"..." : L"Refresh", m_isLoading ? 3 : 7, m_fmtSmall.Get(), rcBtn, theme.brushTextPrimary.Get());
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
        curY += 54.0f * scale;
    }

    // 2. Responsive Control Toolbar (Metrics | Authors | Ranges - wraps cleanly onto 2 lines when narrow)
    {
        struct PillDef { int id; const wchar_t* label; bool active; };
        std::vector<PillDef> metricPills = {
            { PillMetricCommits, L"Commits", m_metric == HeatmapMetric::Commits },
            { PillMetricChurn, L"Code Churn", m_metric == HeatmapMetric::CodeChurn },
            { PillMetricIde, L"IDE Activity", m_metric == HeatmapMetric::IdeActivity }
        };
        std::vector<PillDef> authorPills;
        if (m_metric != HeatmapMetric::IdeActivity) {
            authorPills.push_back({ PillAuthorAll, L"All Authors", !m_onlyMyCommits });
            authorPills.push_back({ PillAuthorMine, L"My Commits", m_onlyMyCommits });
        }
        std::vector<PillDef> rangePills = {
            { PillRangeAuto, L"Auto", m_range == HeatmapRange::Auto },
            { PillRange16w, L"16w", m_range == HeatmapRange::Weeks16 },
            { PillRange26w, L"26w", m_range == HeatmapRange::Weeks26 },
            { PillRange52w, L"52w", m_range == HeatmapRange::Weeks52 }
        };

        const float pillH = 26.0f * scale;
        const float pillPadX = 18.0f * scale;
        const float pillGap = 6.0f * scale;
        const float groupGap = 14.0f * scale;

        auto groupWidth = [&](const std::vector<PillDef>& grp) -> float {
            float w = 0.0f;
            for (size_t i = 0; i < grp.size(); ++i) {
                w += MeasureStringWidth(theme.dwriteFactory.Get(), m_fmtSmall.Get(), grp[i].label) + pillPadX;
                if (i + 1 < grp.size()) w += pillGap;
            }
            return w;
        };

        auto drawPillGroup = [&](const std::vector<PillDef>& grp, float startX, float rowY) -> float {
            float x = startX;
            for (const auto& p : grp) {
                float pw = MeasureStringWidth(theme.dwriteFactory.Get(), m_fmtSmall.Get(), p.label) + pillPadX;
                D2D1_RECT_F rc = D2D1::RectF(x, rowY, x + pw, rowY + pillH);
                m_pillHits.push_back({ rc, p.id });
                D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rc, 5.0f * scale, 5.0f * scale);
                bool hov = (m_hoveredPillId == p.id);
                if (p.active) {
                    pRT->FillRoundedRectangle(rrc, setColor(accentFill));
                    pRT->DrawRoundedRectangle(rrc, setColor(accentCol), 1.2f);
                } else {
                    pRT->FillRoundedRectangle(rrc, setColor(hov ? theme.colors.itemActive : pillBg));
                    pRT->DrawRoundedRectangle(rrc, setColor(hov ? theme.colors.borderHover : theme.colors.borderSubtle), 1.0f);
                }
                if (m_fmtSmall) {
                    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    ID2D1Brush* txtBrush = (p.active && theme.brushTextWhite)
                        ? (isDark ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
                        : (theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushTextPrimary.Get());
                    if (txtBrush) {
                        pRT->DrawText(p.label, static_cast<UINT32>(wcslen(p.label)), m_fmtSmall.Get(), rc, txtBrush);
                    }
                    m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                }
                x += pw + pillGap;
            }
            return x - startX;
        };

        float wMetric = groupWidth(metricPills);
        float wAuthor = authorPills.empty() ? 0.0f : groupWidth(authorPills);
        float wRange = groupWidth(rangePills);
        float totalSingleRow = wMetric + (wAuthor > 0.0f ? (groupGap + wAuthor) : 0.0f) + groupGap + wRange;

        if (totalSingleRow <= contentW) {
            float x = drawPillGroup(metricPills, leftX, curY) + leftX;
            if (!authorPills.empty()) {
                drawPillGroup(authorPills, x + groupGap, curY);
            }
            drawPillGroup(rangePills, leftX + contentW - wRange, curY);
            curY += pillH + 12.0f * scale;
        } else {
            drawPillGroup(metricPills, leftX, curY);
            curY += pillH + 7.0f * scale;
            if (!authorPills.empty() && (wAuthor + groupGap + wRange <= contentW)) {
                drawPillGroup(authorPills, leftX, curY);
                drawPillGroup(rangePills, leftX + contentW - wRange, curY);
                curY += pillH + 12.0f * scale;
            } else {
                if (!authorPills.empty()) {
                    drawPillGroup(authorPills, leftX, curY);
                    curY += pillH + 7.0f * scale;
                }
                drawPillGroup(rangePills, leftX, curY);
                curY += pillH + 12.0f * scale;
            }
        }
    }

    // Compute responsive target week count before KPI & Grid so KPI totals match the visible window
    const float cardPadX = (isNarrow ? 12.0f : 16.0f) * scale;
    const float innerCardW = (std::max)(140.0f * scale, contentW - 2.0f * cardPadX);
    const float dayLabelW = (isNarrow ? 26.0f : 32.0f) * scale;
    const float gridAvailW = (std::max)(100.0f * scale, innerCardW - dayLabelW - 6.0f * scale);

    int maxFitWeeks = 52;
    int weeks = ResolveTargetWeeks(gridAvailW, scale, maxFitWeeks);
    m_lastRenderedWeeks = weeks;

    const std::wstring endYmd = EffectiveTodayYmd();
    AutoSelectDefaultDayIfNeeded(endYmd);
    HeatmapStats stats = ComputeStats(weeks, endYmd);

    // 3. Responsive KPI Summary Strip (4-across when wide, 2x2 when split/narrow)
    {
        int cols = (contentW >= 680.0f * scale) ? 4 : (contentW >= 320.0f * scale ? 2 : 1);
        float colGap = 10.0f * scale;
        float rowGap = 10.0f * scale;
        float kpiW = (contentW - colGap * (cols - 1)) / static_cast<float>(cols);
        float kpiH = 68.0f * scale;

        struct KpiItem { std::wstring label; std::wstring value; std::wstring sub; };
        std::wstring unitSuffix = (m_metric == HeatmapMetric::Commits) ? L" commits"
                                 : (m_metric == HeatmapMetric::CodeChurn ? L" lines" : L" actions");
        std::wstring windowTag = L"In last " + std::to_wstring(weeks) + L" weeks (" + std::to_wstring(stats.activeDays) + L" active days)";
        std::wstring streakTag = (GetValueForDate(endYmd) > 0) ? L"Active today" : L"Next action extends streak";
        std::wstring peakSub = !stats.peakDate.empty() ? stats.peakDate : L"No activity yet";

        KpiItem kpis[4] = {
            { m_metric == HeatmapMetric::Commits ? L"TOTAL COMMITS" : (m_metric == HeatmapMetric::CodeChurn ? L"CODE CHURN" : L"IDE ACTIONS"),
              FormatIntWithCommas(stats.totalValue), windowTag },
            { L"CURRENT STREAK", std::to_wstring(stats.currentStreak) + (stats.currentStreak == 1 ? L" day" : L" days"), streakTag },
            { L"LONGEST STREAK", std::to_wstring(stats.longestStreak) + (stats.longestStreak == 1 ? L" day" : L" days"), L"Consecutive active days" },
            { L"PEAK DAY", stats.peakValue > 0 ? (FormatIntWithCommas(stats.peakValue) + unitSuffix) : L"0", peakSub }
        };

        for (int i = 0; i < 4; ++i) {
            int col = i % cols;
            int row = i / cols;
            float x = leftX + col * (kpiW + colGap);
            float y = curY + row * (kpiH + rowGap);
            D2D1_RECT_F rcCard = D2D1::RectF(x, y, x + kpiW, y + kpiH);
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcCard, 7.0f * scale, 7.0f * scale);
            pRT->FillRoundedRectangle(rrc, setColor(cardBg));
            pRT->DrawRoundedRectangle(rrc, setColor(theme.colors.borderSubtle), 1.0f);

            float tx = x + 12.0f * scale;
            float tr = x + kpiW - 10.0f * scale;
            if (m_fmtSmall && theme.brushTextSecondary) {
                pRT->DrawText(kpis[i].label.c_str(), static_cast<UINT32>(kpis[i].label.size()), m_fmtSmall.Get(),
                              D2D1::RectF(tx, y + 7.0f * scale, tr, y + 21.0f * scale), theme.brushTextSecondary.Get());
            }
            if (m_fmtMetricValue && theme.brushTextPrimary) {
                pRT->DrawText(kpis[i].value.c_str(), static_cast<UINT32>(kpis[i].value.size()), m_fmtMetricValue.Get(),
                              D2D1::RectF(tx, y + 22.0f * scale, tr, y + 46.0f * scale), theme.brushTextPrimary.Get());
            }
            if (m_fmtSmall && theme.brushTextMuted) {
                pRT->DrawText(kpis[i].sub.c_str(), static_cast<UINT32>(kpis[i].sub.size()), m_fmtSmall.Get(),
                              D2D1::RectF(tx, y + 47.0f * scale, tr, y + 62.0f * scale), theme.brushTextMuted.Get());
            }
        }
        int rows = (4 + cols - 1) / cols;
        curY += rows * kpiH + (rows - 1) * rowGap + 14.0f * scale;
    }

    // 4. Optional Non-Git Banner (when viewing Git metrics in non-git/empty folder)
    if (m_metric != HeatmapMetric::IdeActivity && !m_isLoading && !m_isGitRepo) {
        float bannerH = isNarrow ? (58.0f * scale) : (42.0f * scale);
        D2D1_RECT_F rcBanner = D2D1::RectF(leftX, curY, leftX + contentW, curY + bannerH);
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcBanner, 7.0f * scale, 7.0f * scale);
        pRT->FillRoundedRectangle(rrc, setColor(cardBg));
        pRT->DrawRoundedRectangle(rrc, setColor(accentCol), 1.0f);

        float ctaW = 148.0f * scale;
        float ctaH = 26.0f * scale;
        D2D1_RECT_F rcCta = isNarrow
            ? D2D1::RectF(leftX + 12.0f * scale, curY + 26.0f * scale, leftX + 12.0f * scale + ctaW, curY + 26.0f * scale + ctaH)
            : D2D1::RectF(leftX + contentW - ctaW - 12.0f * scale, curY + (bannerH - ctaH) * 0.5f, leftX + contentW - 12.0f * scale, curY + (bannerH + ctaH) * 0.5f);
        m_rcSwitchIdeBtn = rcCta;
        m_pillHits.push_back({ rcCta, PillSwitchIdeCta });

        D2D1_RECT_F rcMsg = isNarrow
            ? D2D1::RectF(leftX + 12.0f * scale, curY + 6.0f * scale, leftX + contentW - 12.0f * scale, curY + 24.0f * scale)
            : D2D1::RectF(leftX + 12.0f * scale, curY, rcCta.left - 10.0f * scale, curY + bannerH);
        std::wstring msg = m_repoPath.empty()
            ? L"Open a Git repository to view commit history, or track workspace IDE activity now."
            : L"Workspace is not a Git repo. Switch to IDE Activity to track saves, builds, tests & SQL.";
        if (m_fmtSmall && theme.brushTextPrimary) {
            pRT->DrawText(msg.c_str(), static_cast<UINT32>(msg.size()), m_fmtSmall.Get(), rcMsg, theme.brushTextPrimary.Get());
        }

        bool hovCta = (m_hoveredPillId == PillSwitchIdeCta);
        D2D1_ROUNDED_RECT rrcCta = D2D1::RoundedRect(rcCta, 5.0f * scale, 5.0f * scale);
        pRT->FillRoundedRectangle(rrcCta, setColor(hovCta ? accentCol : accentFill));
        pRT->DrawRoundedRectangle(rrcCta, setColor(accentCol), 1.0f);
        if (m_fmtSmall) {
            static constexpr wchar_t kSwitchIdeText[] = L"Switch to IDE Activity";
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pRT->DrawText(kSwitchIdeText, static_cast<UINT32>(wcslen(kSwitchIdeText)), m_fmtSmall.Get(), rcCta,
                          theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());
            m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
        curY += bannerH + 14.0f * scale;
    }

    // 5. Contribution Calendar Card (Never squeezed!)
    {
        float gap = 3.0f * scale;
        float rawCell = (gridAvailW - gap * static_cast<float>(weeks - 1)) / static_cast<float>(weeks);
        if (rawCell < 9.0f * scale && m_range != HeatmapRange::Auto) {
            // Even when a user manually forces 52w into a tiny pane, use a compact 2px gap to preserve cell readability
            gap = 2.0f * scale;
            rawCell = (gridAvailW - gap * static_cast<float>(weeks - 1)) / static_cast<float>(weeks);
        }
        float cellSize = (std::clamp)(rawCell, (m_range == HeatmapRange::Auto ? 11.0f : 6.0f) * scale, 16.0f * scale);
        m_lastCellSize = cellSize / scale;

        float gridW = static_cast<float>(weeks) * cellSize + static_cast<float>(weeks - 1) * gap;
        float gridH = 7.0f * cellSize + 6.0f * gap;
        float monthRowH = 18.0f * scale;
        bool stackLegend = innerCardW < (430.0f * scale);
        float legendH = stackLegend ? (36.0f * scale) : (22.0f * scale);
        float cardH = 14.0f * scale + monthRowH + 6.0f * scale + gridH + 12.0f * scale + legendH + 12.0f * scale;

        D2D1_RECT_F rcHeatmapCard = D2D1::RectF(leftX, curY, leftX + contentW, curY + cardH);
        D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(rcHeatmapCard, 8.0f * scale, 8.0f * scale);
        pRT->FillRoundedRectangle(rrcCard, setColor(cardBg));
        pRT->DrawRoundedRectangle(rrcCard, setColor(theme.colors.borderSubtle), 1.0f);

        float gridLeft = leftX + cardPadX + dayLabelW + 6.0f * scale;
        float gridTop = curY + 14.0f * scale + monthRowH + 6.0f * scale;
        float gridRight = gridLeft + gridW;

        int dowToday = DayOfWeekYmd(endYmd);
        std::wstring endWeekSun = AddDaysYmd(endYmd, -dowToday);
        std::wstring startSun = AddDaysYmd(endWeekSun, -7 * (weeks - 1));

        // First scan peak non-zero value inside grid for adaptive quartile colors
        int windowPeak = 0;
        for (int c = 0; c < weeks; ++c) {
            std::wstring colSun = AddDaysYmd(startSun, c * 7);
            for (int r = 0; r < 7; ++r) {
                if (c == weeks - 1 && r > dowToday) continue;
                int v = GetValueForDate(AddDaysYmd(colSun, r));
                if (v > windowPeak) windowPeak = v;
            }
        }

        // Weekday labels (`Mon`, `Wed`, `Fri`)
        if (m_fmtSmall && theme.brushTextMuted) {
            static constexpr int labelRows[3] = { 1, 3, 5 };
            static constexpr const wchar_t* labelText[3] = { L"Mon", L"Wed", L"Fri" };
            for (int i = 0; i < 3; ++i) {
                float ry = gridTop + static_cast<float>(labelRows[i]) * (cellSize + gap);
                D2D1_RECT_F rcDayLbl = D2D1::RectF(leftX + cardPadX, ry - 1.0f * scale, gridLeft - 6.0f * scale, ry + cellSize + 1.0f * scale);
                pRT->DrawText(labelText[i], 3, m_fmtSmall.Get(), rcDayLbl, theme.brushTextMuted.Get());
            }
        }

        // Month headers (skip collisions cleanly using minimum 28px horizontal stride)
        float monthY = curY + 12.0f * scale;
        float lastMonthRight = -10000.0f;
        int prevMonth = -1;
        for (int c = 0; c < weeks; ++c) {
            std::wstring colSun = AddDaysYmd(startSun, c * 7);
            int y = 0, m = 0, d = 0;
            if (!ParseDateYmd(colSun, y, m, d)) continue;
            if (m != prevMonth) {
                prevMonth = m;
                float mx = gridLeft + static_cast<float>(c) * (cellSize + gap);
                if (mx >= lastMonthRight + 28.0f * scale && (gridRight - mx) >= 20.0f * scale) {
                    const wchar_t* mName = s_monthNames[m - 1];
                    if (m_fmtSmall && theme.brushTextSecondary) {
                        D2D1_RECT_F rcM = D2D1::RectF(mx, monthY, mx + 36.0f * scale, monthY + monthRowH);
                        pRT->DrawText(mName, 3, m_fmtSmall.Get(), rcM, theme.brushTextSecondary.Get());
                    }
                    lastMonthRight = mx;
                }
            }
        }

        // Draw squares
        const D2D1_COLOR_F emptyBorder = isDark ? D2D1::ColorF(0.18f, 0.21f, 0.25f, 0.85f)
                                                : D2D1::ColorF(0.82f, 0.84f, 0.86f, 0.90f);
        float cornerR = (std::max)(2.0f, cellSize * 0.20f);
        for (int c = 0; c < weeks; ++c) {
            std::wstring colSun = AddDaysYmd(startSun, c * 7);
            for (int r = 0; r < 7; ++r) {
                bool isFuture = (c == weeks - 1 && r > dowToday);
                if (isFuture) continue;

                std::wstring cellDate = AddDaysYmd(colSun, r);
                int val = GetValueForDate(cellDate);
                int level = ComputeIntensityLevel(val, windowPeak);

                float cx = gridLeft + static_cast<float>(c) * (cellSize + gap);
                float cy = gridTop + static_cast<float>(r) * (cellSize + gap);
                D2D1_RECT_F rcCell = D2D1::RectF(cx, cy, cx + cellSize, cy + cellSize);
                m_cellHits.push_back({ rcCell, cellDate, val, 0, level, false });

                D2D1_ROUNDED_RECT rrcCell = D2D1::RoundedRect(rcCell, cornerR, cornerR);
                pRT->FillRoundedRectangle(rrcCell, setColor(GetLevelColor(level, isDark)));
                if (cellDate == m_selectedDate) {
                    pRT->DrawRoundedRectangle(rrcCell, setColor(isDark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f) : accentCol), 1.8f);
                } else if (cellDate == m_hoveredDate) {
                    pRT->DrawRoundedRectangle(rrcCell, setColor(accentCol), 1.4f);
                } else if (level == 0) {
                    pRT->DrawRoundedRectangle(rrcCell, setColor(emptyBorder), 1.0f);
                }
            }
        }

        // Footer caption + Legend (`Less [][][][][] More`)
        float footerY = gridTop + gridH + 12.0f * scale;
        std::wstring rangeNote = (m_range == HeatmapRange::Auto && weeks < 52)
            ? (L"Auto-fitted " + std::to_wstring(weeks) + L" weeks to pane width · " + startSun + L" to " + endYmd)
            : (L"Showing " + std::to_wstring(weeks) + L" weeks · " + startSun + L" to " + endYmd);
        if (m_fmtSmall && theme.brushTextMuted) {
            float capRight = stackLegend ? (leftX + contentW - cardPadX) : (leftX + contentW - cardPadX - 150.0f * scale);
            pRT->DrawText(rangeNote.c_str(), static_cast<UINT32>(rangeNote.size()), m_fmtSmall.Get(),
                          D2D1::RectF(leftX + cardPadX, footerY, capRight, footerY + 18.0f * scale),
                          theme.brushTextMuted.Get());
        }

        float legY = stackLegend ? (footerY + 20.0f * scale) : footerY;
        float legCell = 11.0f * scale;
        float legGap = 3.0f * scale;
        float moreW = 30.0f * scale;
        float lessW = 28.0f * scale;
        float squaresW = 5.0f * legCell + 4.0f * legGap;
        float legRight = leftX + contentW - cardPadX;
        float moreX = legRight - moreW;
        float squaresX = moreX - 6.0f * scale - squaresW;
        float lessX = squaresX - 6.0f * scale - lessW;
        float sqTop = legY + (18.0f * scale - legCell) * 0.5f;

        if (m_fmtSmall && theme.brushTextMuted) {
            pRT->DrawText(L"Less", 4, m_fmtSmall.Get(), D2D1::RectF(lessX, legY, lessX + lessW, legY + 18.0f * scale), theme.brushTextMuted.Get());
            pRT->DrawText(L"More", 4, m_fmtSmall.Get(), D2D1::RectF(moreX, legY, legRight, legY + 18.0f * scale), theme.brushTextMuted.Get());
        }
        for (int lv = 0; lv < 5; ++lv) {
            float sx = squaresX + static_cast<float>(lv) * (legCell + legGap);
            D2D1_ROUNDED_RECT rrcLeg = D2D1::RoundedRect(D2D1::RectF(sx, sqTop, sx + legCell, sqTop + legCell), 2.0f * scale, 2.0f * scale);
            pRT->FillRoundedRectangle(rrcLeg, setColor(GetLevelColor(lv, isDark)));
            if (lv == 0) pRT->DrawRoundedRectangle(rrcLeg, setColor(emptyBorder), 1.0f);
        }

        curY += cardH + 16.0f * scale;
    }

    // 6. Selected Day Breakdown & Clickable Commit Inspector
    {
        std::wstring inspectDate = m_selectedDate.empty() ? endYmd : m_selectedDate;
        const HeatmapDayRecord* pDay = nullptr;
        if (auto it = m_days.find(inspectDate); it != m_days.end()) pDay = &it->second;
        auto dayCommits = GetCommitsForDate(inspectDate);

        float chipGap = 8.0f * scale;
        float dInnerW = contentW - 2.0f * cardPadX;
        float chipW = (dInnerW - 4.0f * chipGap) / 5.0f;
        int chipCols = 5;
        if (isNarrow && chipW < 58.0f * scale) {
            chipCols = 3;
            chipW = (dInnerW - 2.0f * chipGap) / 3.0f;
        }
        int chipRows = (5 + chipCols - 1) / chipCols;
        float chipH = 36.0f * scale;
        float ideChipsH = static_cast<float>(chipRows) * chipH + static_cast<float>(chipRows - 1) * 6.0f * scale;
        float rowH = 34.0f * scale;
        size_t shownCommits = (std::min<size_t>)(dayCommits.size(), 20);
        float commitsSectionH = (m_metric != HeatmapMetric::IdeActivity)
            ? (shownCommits > 0 ? (static_cast<float>(shownCommits) * (rowH + 4.0f * scale)) : (28.0f * scale))
            : 0.0f;
        float totalDetailH = 40.0f * scale + ideChipsH + 12.0f * scale + commitsSectionH + 12.0f * scale;

        D2D1_RECT_F rcDetail = D2D1::RectF(leftX, curY, leftX + contentW, curY + totalDetailH);
        D2D1_ROUNDED_RECT rrcDetail = D2D1::RoundedRect(rcDetail, 8.0f * scale, 8.0f * scale);
        pRT->FillRoundedRectangle(rrcDetail, setColor(cardBg));
        pRT->DrawRoundedRectangle(rrcDetail, setColor(theme.colors.borderSubtle), 1.0f);

        float dLeft = leftX + cardPadX;
        float dRight = leftX + contentW - cardPadX;
        float dy = curY + 12.0f * scale;

        std::wstring selHdr = FormatDateDisplay(inspectDate);
        if (pDay && (pDay->commitCount > 0 || pDay->TotalChurn(false) > 0)) {
            int cCount = m_onlyMyCommits ? pDay->myCommitCount : pDay->commitCount;
            int ins = m_onlyMyCommits ? pDay->myLinesAdded : pDay->linesAdded;
            int del = m_onlyMyCommits ? pDay->myLinesDeleted : pDay->linesDeleted;
            selHdr += L" · " + std::to_wstring(cCount) + (cCount == 1 ? L" commit" : L" commits") +
                      L" (+" + FormatIntWithCommas(ins) + L" / -" + FormatIntWithCommas(del) + L")";
        }
        if (m_fmtHeading && theme.brushTextPrimary) {
            pRT->DrawText(selHdr.c_str(), static_cast<UINT32>(selHdr.size()), m_fmtHeading.Get(),
                          D2D1::RectF(dLeft, dy, dRight, dy + 22.0f * scale), theme.brushTextPrimary.Get());
        }
        dy += 28.0f * scale;

        // IDE Milestones summary row (compact responsive chips)
        {
            struct Chip { std::wstring label; int count; };
            Chip chips[5] = {
                { L"Saves", pDay ? pDay->saves : 0 },
                { L"Commits", pDay ? pDay->commitsTriggered : 0 },
                { L"Builds", pDay ? pDay->builds : 0 },
                { L"Tests", pDay ? (pDay->testsPassed + pDay->testsFailed) : 0 },
                { L"SQL", pDay ? pDay->sqlQueries : 0 }
            };
            for (int i = 0; i < 5; ++i) {
                int col = i % chipCols;
                int row = i / chipCols;
                float cx = dLeft + static_cast<float>(col) * (chipW + chipGap);
                float cy = dy + static_cast<float>(row) * (chipH + 6.0f * scale);
                D2D1_RECT_F rcChip = D2D1::RectF(cx, cy, cx + chipW, cy + chipH);
                D2D1_ROUNDED_RECT rrcChip = D2D1::RoundedRect(rcChip, 5.0f * scale, 5.0f * scale);
                pRT->FillRoundedRectangle(rrcChip, setColor(pillBg));
                pRT->DrawRoundedRectangle(rrcChip, setColor(theme.colors.borderSubtle), 1.0f);
                std::wstring valStr = std::to_wstring(chips[i].count);
                if (m_fmtSmall && theme.brushTextSecondary) {
                    pRT->DrawText(chips[i].label.c_str(), static_cast<UINT32>(chips[i].label.size()), m_fmtSmall.Get(),
                                  D2D1::RectF(cx + 8.0f * scale, cy + 3.0f * scale, rcChip.right - 4.0f * scale, cy + 16.0f * scale),
                                  theme.brushTextSecondary.Get());
                }
                if (m_fmtBody && theme.brushTextPrimary) {
                    pRT->DrawText(valStr.c_str(), static_cast<UINT32>(valStr.size()), m_fmtBody.Get(),
                                  D2D1::RectF(cx + 8.0f * scale, cy + 16.0f * scale, rcChip.right - 4.0f * scale, cy + 33.0f * scale),
                                  theme.brushTextPrimary.Get());
                }
            }
            dy += ideChipsH + 12.0f * scale;
        }

        // Clickable Commit Rows for selected day
        if (m_metric != HeatmapMetric::IdeActivity) {
            if (dayCommits.empty()) {
                if (m_fmtSmall && theme.brushTextMuted) {
                    static constexpr wchar_t kEmptyDayText[] = L"No commits recorded on this date. Click any colored day square above to inspect its commits.";
                    pRT->DrawText(kEmptyDayText, static_cast<UINT32>(wcslen(kEmptyDayText)), m_fmtSmall.Get(),
                                  D2D1::RectF(dLeft, dy, dRight, dy + 24.0f * scale),
                                  theme.brushTextMuted.Get());
                }
                dy += 28.0f * scale;
            } else {
                for (size_t i = 0; i < shownCommits; ++i) {
                    const auto& ce = dayCommits[i];
                    D2D1_RECT_F rcRow = D2D1::RectF(dLeft, dy, dRight, dy + rowH);
                    m_commitHits.push_back({ rcRow, ce.hash });
                    bool hovRow = (m_hoveredCommitIdx == static_cast<int>(i));

                    D2D1_ROUNDED_RECT rrcRow = D2D1::RoundedRect(rcRow, 5.0f * scale, 5.0f * scale);
                    pRT->FillRoundedRectangle(rrcRow, setColor(hovRow ? theme.colors.itemActive : pillBg));
                    if (hovRow) pRT->DrawRoundedRectangle(rrcRow, setColor(accentCol), 1.0f);

                    float badgeW = 62.0f * scale;
                    D2D1_RECT_F rcBadge = D2D1::RectF(dLeft + 8.0f * scale, dy + 6.0f * scale, dLeft + 8.0f * scale + badgeW, dy + rowH - 6.0f * scale);
                    pRT->FillRoundedRectangle(D2D1::RoundedRect(rcBadge, 4.0f * scale, 4.0f * scale), setColor(accentFill));
                    if (m_fmtMono && theme.brushTextPrimary) {
                        pRT->DrawText(ce.shortHash.c_str(), static_cast<UINT32>(ce.shortHash.size()), m_fmtMono.Get(), rcBadge, theme.brushTextPrimary.Get());
                    }

                    std::wstring statStr = (ce.additions > 0 || ce.deletions > 0)
                        ? (L"+" + std::to_wstring(ce.additions) + L" / -" + std::to_wstring(ce.deletions))
                        : L"Diff ->";
                    float statW = MeasureStringWidth(theme.dwriteFactory.Get(), m_fmtSmall.Get(), statStr) + 12.0f * scale;
                    D2D1_RECT_F rcStat = D2D1::RectF(dRight - statW - 8.0f * scale, dy, dRight - 8.0f * scale, dy + rowH);
                    if (m_fmtSmall && theme.brushTextSecondary) {
                        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                        pRT->DrawText(statStr.c_str(), static_cast<UINT32>(statStr.size()), m_fmtSmall.Get(), rcStat, theme.brushTextSecondary.Get());
                        m_fmtSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    }

                    std::wstring msgLine = ce.message;
                    if (!isNarrow && !ce.author.empty()) {
                        msgLine += L"  —  " + ce.author;
                    }
                    D2D1_RECT_F rcMsg = D2D1::RectF(rcBadge.right + 10.0f * scale, dy, rcStat.left - 8.0f * scale, dy + rowH);
                    if (m_fmtBody && theme.brushTextPrimary) {
                        pRT->DrawText(msgLine.c_str(), static_cast<UINT32>(msgLine.size()), m_fmtBody.Get(), rcMsg, theme.brushTextPrimary.Get());
                    }
                    dy += rowH + 4.0f * scale;
                }
            }
        }
        curY += totalDetailH + 20.0f * scale;
    }

    // Scroll metrics & custom slim scrollbar
    float unScrolledBottom = (curY + m_scrollY) - topY;
    m_contentTotalHeight = unScrolledBottom;
    float viewH = clientHeight - topY;
    m_maxScrollY = (std::max)(0.0f, m_contentTotalHeight - viewH);
    m_scrollY = (std::clamp)(m_scrollY, 0.0f, m_maxScrollY);

    if (m_maxScrollY > 1.0f) {
        float sbW = 6.0f * scale;
        float sbLeft = canvasLeft + canvasWidth - sbW - 3.0f * scale;
        m_rcScrollTrack = D2D1::RectF(sbLeft, topY + 6.0f * scale, sbLeft + sbW, clientHeight - 6.0f * scale);
        float trackH = m_rcScrollTrack.bottom - m_rcScrollTrack.top;
        float thumbH = (std::max)(28.0f * scale, trackH * (viewH / m_contentTotalHeight));
        float thumbTop = m_rcScrollTrack.top + (m_scrollY / m_maxScrollY) * (trackH - thumbH);
        m_rcScrollThumb = D2D1::RectF(sbLeft, thumbTop, sbLeft + sbW, thumbTop + thumbH);
        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(m_rcScrollThumb, 3.0f * scale, 3.0f * scale);
        pRT->FillRoundedRectangle(rrcThumb, setColor(theme.colors.borderHover));
    }

    // 7. Floating Hover Tooltip over hovered day square (always clamped cleanly inside pane)
    if (!m_hoveredDate.empty()) {
        for (const auto& cell : m_cellHits) {
            if (cell.date == m_hoveredDate) {
                const HeatmapDayRecord* pRec = nullptr;
                if (auto it = m_days.find(cell.date); it != m_days.end()) pRec = &it->second;
                std::wstring tipText = FormatMetricTooltipText(pRec, cell.date);

                size_t nl = tipText.find(L'\n');
                std::wstring l1 = (nl == std::wstring::npos) ? tipText : tipText.substr(0, nl);
                std::wstring l2 = (nl == std::wstring::npos) ? L"" : tipText.substr(nl + 1);
                float tipW = (std::max)(
                    MeasureStringWidth(theme.dwriteFactory.Get(), m_fmtSmall.Get(), l1),
                    MeasureStringWidth(theme.dwriteFactory.Get(), m_fmtSmall.Get(), l2)) + 20.0f * scale;
                tipW = (std::clamp)(tipW, 140.0f * scale, (std::max)(150.0f * scale, canvasWidth - 16.0f * scale));
                float tipH = l2.empty() ? (28.0f * scale) : (44.0f * scale);

                float tx = (cell.bounds.left + cell.bounds.right) * 0.5f - tipW * 0.5f;
                tx = (std::clamp)(tx, canvasLeft + 8.0f * scale, canvasLeft + canvasWidth - tipW - 8.0f * scale);
                float ty = cell.bounds.top - tipH - 7.0f * scale;
                if (ty < topY + 6.0f * scale) ty = cell.bounds.bottom + 7.0f * scale;

                D2D1_RECT_F rcTip = D2D1::RectF(tx, ty, tx + tipW, ty + tipH);
                D2D1_ROUNDED_RECT rrcTip = D2D1::RoundedRect(rcTip, 6.0f * scale, 6.0f * scale);
                pRT->FillRoundedRectangle(rrcTip, setColor(theme.colors.tooltipBg));
                pRT->DrawRoundedRectangle(rrcTip, setColor(theme.colors.tooltipBorder), 1.0f);

                if (m_fmtSmall && theme.brushTooltipText) {
                    pRT->DrawText(l1.c_str(), static_cast<UINT32>(l1.size()), m_fmtSmall.Get(),
                                  D2D1::RectF(tx + 9.0f * scale, ty + 4.0f * scale, rcTip.right - 9.0f * scale, ty + 20.0f * scale),
                                  theme.brushTooltipText.Get());
                    if (!l2.empty()) {
                        pRT->DrawText(l2.c_str(), static_cast<UINT32>(l2.size()), m_fmtSmall.Get(),
                                      D2D1::RectF(tx + 9.0f * scale, ty + 21.0f * scale, rcTip.right - 9.0f * scale, ty + 39.0f * scale),
                                      theme.brushTooltipText.Get());
                    }
                }
                break;
            }
        }
    }

    pRT->PopAxisAlignedClip();
}

bool ActivityHeatmapView::HandleMouseMove(POINT pt) {
    m_lastMousePt = pt;
    if (m_isDraggingScrollbar && m_maxScrollY > 0.0f) {
        float trackH = m_rcScrollTrack.bottom - m_rcScrollTrack.top;
        float thumbH = m_rcScrollThumb.bottom - m_rcScrollThumb.top;
        float range = trackH - thumbH;
        if (range > 1.0f) {
            float deltaY = static_cast<float>(pt.y) - m_scrollDragStartY;
            m_scrollY = (std::clamp)(m_scrollDragStartScrollY + (deltaY / range) * m_maxScrollY, 0.0f, m_maxScrollY);
            return true;
        }
    }

    std::wstring prevDate = m_hoveredDate;
    int prevPill = m_hoveredPillId;
    int prevCommit = m_hoveredCommitIdx;

    m_hoveredDate.clear();
    m_hoveredPillId = 0;
    m_hoveredCommitIdx = -1;

    for (const auto& cell : m_cellHits) {
        if (PtInRectF(pt, cell.bounds)) {
            m_hoveredDate = cell.date;
            break;
        }
    }
    if (m_hoveredDate.empty()) {
        for (const auto& pill : m_pillHits) {
            if (PtInRectF(pt, pill.bounds)) {
                m_hoveredPillId = pill.id;
                break;
            }
        }
    }
    if (m_hoveredDate.empty() && m_hoveredPillId == 0) {
        for (size_t i = 0; i < m_commitHits.size(); ++i) {
            if (PtInRectF(pt, m_commitHits[i].bounds)) {
                m_hoveredCommitIdx = static_cast<int>(i);
                break;
            }
        }
    }

    return (prevDate != m_hoveredDate) || (prevPill != m_hoveredPillId) || (prevCommit != m_hoveredCommitIdx);
}

bool ActivityHeatmapView::HandleLButtonDown(POINT pt, const app::AppConfig& config) {
    (void)config;
    if (!PtInRectF(pt, m_rcViewport)) return false;

    if (m_maxScrollY > 0.0f && PtInRectF(pt, m_rcScrollThumb)) {
        m_isDraggingScrollbar = true;
        m_scrollDragStartY = static_cast<float>(pt.y);
        m_scrollDragStartScrollY = m_scrollY;
        return true;
    }

    for (const auto& pill : m_pillHits) {
        if (PtInRectF(pt, pill.bounds)) {
            switch (pill.id) {
                case PillMetricCommits: m_metric = HeatmapMetric::Commits; return true;
                case PillMetricChurn:   m_metric = HeatmapMetric::CodeChurn; return true;
                case PillMetricIde:     m_metric = HeatmapMetric::IdeActivity; return true;
                case PillAuthorAll:     m_onlyMyCommits = false; return true;
                case PillAuthorMine:    m_onlyMyCommits = true; return true;
                case PillRangeAuto:     m_range = HeatmapRange::Auto; return true;
                case PillRange16w:      m_range = HeatmapRange::Weeks16; return true;
                case PillRange26w:      m_range = HeatmapRange::Weeks26; return true;
                case PillRange52w:      m_range = HeatmapRange::Weeks52; return true;
                case PillSwitchIdeCta:  m_metric = HeatmapMetric::IdeActivity; return true;
                case PillRefresh:
                    if (m_onRequestRefresh) m_onRequestRefresh();
                    return true;
                default: break;
            }
        }
    }

    for (const auto& cell : m_cellHits) {
        if (PtInRectF(pt, cell.bounds)) {
            m_selectedDate = cell.date;
            return true;
        }
    }

    for (const auto& ch : m_commitHits) {
        if (PtInRectF(pt, ch.bounds)) {
            if (m_onOpenCommitDiff && !ch.hash.empty()) {
                m_onOpenCommitDiff(ch.hash);
            }
            return true;
        }
    }

    return true;
}

bool ActivityHeatmapView::HandleLButtonUp() {
    if (!m_isDraggingScrollbar) return false;
    m_isDraggingScrollbar = false;
    return true;
}

bool ActivityHeatmapView::HandleMouseWheel(short delta) {
    if (m_maxScrollY <= 0.0f) return false;
    float step = (delta > 0) ? -48.0f : 48.0f;
    float next = (std::clamp)(m_scrollY + step, 0.0f, m_maxScrollY);
    if (std::abs(next - m_scrollY) < 0.01f) return false;
    m_scrollY = next;
    return true;
}

LPCWSTR ActivityHeatmapView::GetCursorForPoint(POINT pt) const noexcept {
    if (!PtInRectF(pt, m_rcViewport)) return nullptr;
    for (const auto& cell : m_cellHits) {
        if (PtInRectF(pt, cell.bounds)) return IDC_HAND;
    }
    for (const auto& pill : m_pillHits) {
        if (PtInRectF(pt, pill.bounds)) return IDC_HAND;
    }
    for (const auto& ch : m_commitHits) {
        if (PtInRectF(pt, ch.bounds)) return IDC_HAND;
    }
    if (m_maxScrollY > 0.0f && PtInRectF(pt, m_rcScrollThumb)) return IDC_HAND;
    return IDC_ARROW;
}

void ActivityHeatmapView::RunGarbageCollection() {
    m_cellHits.shrink_to_fit();
    m_pillHits.shrink_to_fit();
    m_commitHits.shrink_to_fit();
    PruneAgedIdeDays(EffectiveTodayYmd());
    SaveIdeHistoryIfDirty(true);
}

} // namespace phide::ui
