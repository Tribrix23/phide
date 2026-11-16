#pragma once

/**
 * @file LocalTimeline.hpp
 * @brief Out-of-workspace bounded local file timeline snapshots (%LOCALAPPDATA%\phide\timeline).
 *
 * Security & Storage Guarantees:
 * - Never writes inside any project workspace (.phide/history is strictly prohibited & cleaned).
 * - Filters sensitive credential/secret paths (.env*, keys, PEM/PFX/P12, credentials).
 * - Skips binary/null-byte content and files larger than 256 KB.
 * - Deduplicates consecutive identical content via FNV-1a 64-bit hash.
 * - Caps per-file history to <= 8 snapshots and global cache across all workspaces to <= 20 MB LRU.
 * - Uses atomic write (.tmp + MoveFileExW) for crash-safe persistence.
 */

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace phide::app {

struct TimelineSnapshotEntry {
    std::wstring timestampId;   ///< UTC epoch-ms + sequence identifier
    std::wstring displayTime;   ///< Human readable timestamp
    uint64_t contentHash = 0;
    uint64_t byteSize = 0;
    std::wstring snapshotPath;  ///< Out-of-workspace snapshot file path
};

class LocalTimeline {
public:
    static constexpr uint64_t kMaxFileBytes = 256ULL * 1024ULL;
    static constexpr size_t   kMaxSnapshotsPerFile = 8;
    static constexpr uint64_t kMaxGlobalBytes = 20ULL * 1024ULL * 1024ULL;

    [[nodiscard]] static uint64_t Fnv1a64(const void* data, size_t len) noexcept {
        uint64_t h = 14695981039346656037ULL;
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < len; ++i) {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
        return h;
    }

    [[nodiscard]] static std::wstring HashHex16(const std::wstring& s) {
        std::wstring low = s;
        for (wchar_t& c : low) {
            if (c == L'\\') c = L'/';
            c = static_cast<wchar_t>(::towlower(c));
        }
        uint64_t h = Fnv1a64(low.data(), low.size() * sizeof(wchar_t));
        wchar_t buf[20]{};
        swprintf_s(buf, L"%016llx", static_cast<unsigned long long>(h));
        return buf;
    }

    [[nodiscard]] static bool IsSensitiveOrExcludedPath(const std::wstring& path) {
        if (path.empty()) return true;
        std::wstring low = path;
        for (wchar_t& c : low) {
            if (c == L'\\') c = L'/';
            c = static_cast<wchar_t>(::towlower(c));
        }
        if (low.find(L"/.git/") != std::wstring::npos ||
            low.find(L"/node_modules/") != std::wstring::npos ||
            low.find(L"/.phide/") != std::wstring::npos) {
            return true;
        }
        std::filesystem::path p(low);
        std::wstring fname = p.filename().wstring();
        std::wstring ext = p.extension().wstring();
        if (fname == L".env" || fname.rfind(L".env.", 0) == 0) return true;
        if (fname.find(L"secret") != std::wstring::npos ||
            fname.find(L"credential") != std::wstring::npos ||
            fname.find(L"id_rsa") != std::wstring::npos ||
            fname.find(L"id_ed25519") != std::wstring::npos ||
            fname.find(L"token") != std::wstring::npos) {
            return true;
        }
        if (ext == L".pem" || ext == L".key" || ext == L".pfx" || ext == L".p12" || ext == L".kdbx") {
            return true;
        }
        return false;
    }

    [[nodiscard]] static bool IsBinaryBytes(const std::string& bytes) noexcept {
        size_t probe = (std::min<size_t>)(bytes.size(), 4096);
        for (size_t i = 0; i < probe; ++i) {
            if (bytes[i] == '\0') return true;
        }
        return false;
    }

    [[nodiscard]] static std::filesystem::path GetTimelineBaseDir(const std::wstring& customRootOverride = L"") {
        if (!customRootOverride.empty()) {
            return std::filesystem::path(customRootOverride);
        }
        wchar_t buf[MAX_PATH]{};
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) {
            n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        }
        if (n == 0 || n >= MAX_PATH) return {};
        return std::filesystem::path(buf) / L"phide" / L"timeline";
    }

    [[nodiscard]] static std::filesystem::path GetFileBucketDir(
        const std::wstring& wsRoot,
        const std::wstring& filePath,
        const std::wstring& customRootOverride = L""
    ) {
        auto base = GetTimelineBaseDir(customRootOverride);
        if (base.empty() || filePath.empty()) return {};
        std::wstring wsKey = wsRoot.empty() ? L"_single" : HashHex16(wsRoot);
        std::wstring fileKey = HashHex16(filePath);
        return base / wsKey / fileKey;
    }

    static bool WriteAtomicFile(const std::filesystem::path& target, const std::string& content) {
        std::error_code ec;
        if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);
        std::filesystem::path tmp = target;
        tmp += L".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return false;
            if (!content.empty()) out.write(content.data(), static_cast<std::streamsize>(content.size()));
            out.flush();
            if (!out.good()) {
                out.close();
                std::filesystem::remove(tmp, ec);
                return false;
            }
        }
        if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
        return true;
    }

    static bool CaptureSnapshot(
        const std::wstring& wsRoot,
        const std::wstring& filePath,
        const std::string& utf8Content,
        const std::wstring& customRootOverride = L""
    ) {
        if (IsSensitiveOrExcludedPath(filePath)) return false;
        if (utf8Content.empty() || utf8Content.size() > kMaxFileBytes) return false;
        if (IsBinaryBytes(utf8Content)) return false;

        std::filesystem::path bucket = GetFileBucketDir(wsRoot, filePath, customRootOverride);
        if (bucket.empty()) return false;

        const uint64_t newHash = Fnv1a64(utf8Content.data(), utf8Content.size());
        auto existing = ListSnapshots(wsRoot, filePath, customRootOverride);
        if (!existing.empty() && existing.front().contentHash == newHash) {
            return false; // Deduplicated: identical to latest snapshot
        }

        const auto msNow = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        wchar_t idBuf[48]{};
        swprintf_s(idBuf, L"%016lld_%016llx",
                   static_cast<long long>(msNow),
                   static_cast<unsigned long long>(newHash));
        std::filesystem::path snapPath = bucket / (std::wstring(idBuf) + L".snap");
        if (!WriteAtomicFile(snapPath, utf8Content)) return false;

        // Enforce per-file cap (<= 8 snapshots)
        existing = ListSnapshots(wsRoot, filePath, customRootOverride);
        for (size_t i = kMaxSnapshotsPerFile; i < existing.size(); ++i) {
            std::error_code ec;
            std::filesystem::remove(existing[i].snapshotPath, ec);
        }

        // Enforce global LRU disk budget (<= 20 MB)
        EnforceGlobalBudget(GetTimelineBaseDir(customRootOverride), kMaxGlobalBytes);
        return true;
    }

    [[nodiscard]] static std::vector<TimelineSnapshotEntry> ListSnapshots(
        const std::wstring& wsRoot,
        const std::wstring& filePath,
        const std::wstring& customRootOverride = L""
    ) {
        std::vector<TimelineSnapshotEntry> out;
        std::filesystem::path bucket = GetFileBucketDir(wsRoot, filePath, customRootOverride);
        if (bucket.empty()) return out;
        std::error_code ec;
        if (!std::filesystem::exists(bucket, ec)) return out;

        for (const auto& entry : std::filesystem::directory_iterator(
                 bucket, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != L".snap") continue;

            std::wstring stem = entry.path().stem().wstring();
            size_t us = stem.rfind(L'_');
            uint64_t hash = 0;
            int64_t epochMs = 0;
            if (us != std::wstring::npos) {
                try { epochMs = std::stoll(stem.substr(0, us)); } catch (...) {}
                try { hash = std::stoull(stem.substr(us + 1), nullptr, 16); } catch (...) {}
            }
            TimelineSnapshotEntry e;
            e.timestampId = stem;
            e.contentHash = hash;
            e.byteSize = entry.file_size(ec);
            e.snapshotPath = entry.path().wstring();
            if (epochMs > 0) {
                std::time_t tt = static_cast<std::time_t>(epochMs / 1000);
                struct tm tmBuf{};
                if (localtime_s(&tmBuf, &tt) == 0) {
                    wchar_t tbuf[32]{};
                    wcsftime(tbuf, 32, L"%Y-%m-%d %H:%M:%S", &tmBuf);
                    e.displayTime = tbuf;
                }
            }
            out.push_back(std::move(e));
        }

        std::sort(out.begin(), out.end(), [](const TimelineSnapshotEntry& a, const TimelineSnapshotEntry& b) {
            return a.timestampId > b.timestampId; // Newest first
        });
        return out;
    }

    [[nodiscard]] static bool LoadSnapshotContent(const std::wstring& snapshotPath, std::string& outUtf8) {
        outUtf8.clear();
        std::error_code ec;
        if (snapshotPath.empty() || !std::filesystem::exists(snapshotPath, ec)) return false;
        if (std::filesystem::file_size(snapshotPath, ec) > kMaxFileBytes) return false;
        std::ifstream ifs(std::filesystem::path(snapshotPath), std::ios::binary);
        if (!ifs.is_open()) return false;
        outUtf8.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        return true;
    }

    static void EnforceGlobalBudget(const std::filesystem::path& baseDir, uint64_t maxBytes = kMaxGlobalBytes) {
        if (baseDir.empty()) return;
        std::error_code ec;
        if (!std::filesystem::exists(baseDir, ec)) return;

        struct DiskItem {
            std::filesystem::path path;
            std::filesystem::file_time_type mtime;
            uint64_t bytes = 0;
        };
        std::vector<DiskItem> allFiles;
        uint64_t totalBytes = 0;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 baseDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            uint64_t sz = entry.file_size(ec);
            auto mt = entry.last_write_time(ec);
            totalBytes += sz;
            allFiles.push_back({ entry.path(), mt, sz });
        }
        if (totalBytes <= maxBytes) return;

        std::sort(allFiles.begin(), allFiles.end(), [](const DiskItem& a, const DiskItem& b) {
            return a.mtime < b.mtime; // Oldest first
        });
        for (const auto& item : allFiles) {
            if (totalBytes <= maxBytes) break;
            if (std::filesystem::remove(item.path, ec)) {
                totalBytes = (totalBytes > item.bytes) ? (totalBytes - item.bytes) : 0;
            }
        }
    }
};

} // namespace phide::app
