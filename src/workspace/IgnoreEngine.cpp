#include "phide/workspace/IgnoreEngine.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fstream>
#include <sstream>
#include <cwctype>
#include <algorithm>

namespace phide::workspace {

namespace {

std::wstring ToLower(std::wstring_view s) {
    std::wstring res;
    res.reserve(s.size());
    for (wchar_t c : s) {
        res.push_back(static_cast<wchar_t>(towlower(c)));
    }
    return res;
}

std::wstring ReadUtf8OrAnsiFile(const std::filesystem::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) return {};

    std::string bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return {};

    // Check UTF-8 BOM
    size_t offset = 0;
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        offset = 3;
    }

    int needed = MultiByteToWideChar(CP_UTF8, 0, bytes.data() + offset, static_cast<int>(bytes.size() - offset), nullptr, 0);
    if (needed > 0) {
        std::wstring result(needed, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, bytes.data() + offset, static_cast<int>(bytes.size() - offset), result.data(), needed);
        return result;
    }

    // Fallback to ANSI
    needed = MultiByteToWideChar(CP_ACP, 0, bytes.data() + offset, static_cast<int>(bytes.size() - offset), nullptr, 0);
    if (needed > 0) {
        std::wstring result(needed, L'\0');
        MultiByteToWideChar(CP_ACP, 0, bytes.data() + offset, static_cast<int>(bytes.size() - offset), result.data(), needed);
        return result;
    }
    return {};
}

} // namespace

IgnoreEngine::IgnoreEngine() = default;

void IgnoreEngine::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rules.clear();
    m_gitIgnoredSet.clear();
    m_loadedSourceFiles.clear();
    m_workspaceRoot.clear();
}

std::wstring IgnoreEngine::NormalizePath(const std::wstring& path) {
    std::wstring norm = path;
    for (auto& c : norm) {
        if (c == L'/') c = L'\\';
        c = static_cast<wchar_t>(towlower(c));
    }
    while (!norm.empty() && norm.back() == L'\\') {
        norm.pop_back();
    }
    return norm;
}

std::vector<std::wstring> IgnoreEngine::SplitSegments(std::wstring_view path) {
    std::vector<std::wstring> segs;
    size_t start = 0;
    while (start < path.size()) {
        while (start < path.size() && (path[start] == L'/' || path[start] == L'\\')) {
            start++;
        }
        if (start >= path.size()) break;
        size_t end = start;
        while (end < path.size() && path[end] != L'/' && path[end] != L'\\') {
            end++;
        }
        segs.push_back(ToLower(path.substr(start, end - start)));
        start = end;
    }
    return segs;
}

bool IgnoreEngine::GlobMatch(std::wstring_view text, std::wstring_view pattern) {
    size_t t = 0, p = 0;
    size_t starIdx = std::wstring_view::npos;
    size_t matchIdx = 0;

    while (t < text.length()) {
        if (p < pattern.length() && (pattern[p] == L'?' || towlower(pattern[p]) == towlower(text[t]))) {
            ++t;
            ++p;
        } else if (p < pattern.length() && pattern[p] == L'*') {
            starIdx = p;
            matchIdx = t;
            ++p;
        } else if (starIdx != std::wstring_view::npos) {
            p = starIdx + 1;
            ++matchIdx;
            t = matchIdx;
        } else {
            return false;
        }
    }
    while (p < pattern.length() && pattern[p] == L'*') {
        ++p;
    }
    return p == pattern.length();
}

bool IgnoreEngine::MatchSegments(
    const std::vector<std::wstring>& pathSegs,
    size_t pathIdx,
    const std::vector<std::wstring>& patSegs,
    size_t patIdx
) {
    if (patIdx == patSegs.size()) {
        return pathIdx == pathSegs.size();
    }

    if (patSegs[patIdx] == L"**") {
        // ** can match zero or more segments
        if (patIdx + 1 == patSegs.size()) {
            return true; // trailing ** matches everything
        }
        for (size_t nextPath = pathIdx; nextPath <= pathSegs.size(); ++nextPath) {
            if (MatchSegments(pathSegs, nextPath, patSegs, patIdx + 1)) {
                return true;
            }
        }
        return false;
    }

    if (pathIdx >= pathSegs.size()) {
        return false;
    }

    if (GlobMatch(pathSegs[pathIdx], patSegs[patIdx])) {
        return MatchSegments(pathSegs, pathIdx + 1, patSegs, patIdx + 1);
    }

    return false;
}

bool IgnoreEngine::IsSupportedIgnoreFileName(const std::wstring& fileName) {
    std::wstring lower = ToLower(fileName);
    return lower == L".gitignore" ||
           lower == L".dockerignore" ||
           lower == L".npmignore" ||
           lower == L".eslintignore" ||
           lower == L".prettierignore" ||
           lower == L".ignore" ||
           lower == L".pnpmignore" ||
           lower == L".helmignore" ||
           lower == L".hgignore" ||
           lower == L".bzrignore";
}

void IgnoreEngine::ParseRules(const std::wstring& content, const std::wstring& baseDir, const std::wstring& sourceName) {
    std::wstringstream ss(content);
    std::wstring line;
    std::wstring normBase = NormalizePath(baseDir);

    while (std::getline(ss, line)) {
        // Strip carriage return
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ' || line.back() == L'\t')) {
            line.pop_back();
        }
        // Leading whitespace trim
        size_t firstNonSpace = 0;
        while (firstNonSpace < line.length() && (line[firstNonSpace] == L' ' || line[firstNonSpace] == L'\t')) {
            firstNonSpace++;
        }
        if (firstNonSpace > 0) {
            line = line.substr(firstNonSpace);
        }

        if (line.empty()) continue;
        if (line[0] == L'#') continue; // comment

        IgnoreRule rule;
        rule.baseDir = normBase;
        rule.sourceFileName = sourceName;
        rule.rawPattern = line;

        // Check for negation '!'
        if (line[0] == L'!') {
            rule.isNegative = true;
            line = line.substr(1);
            if (line.empty()) continue;
        }

        // Unescape leading \# or \!
        if (line.size() >= 2 && line[0] == L'\\' && (line[1] == L'#' || line[1] == L'!')) {
            line = line.substr(1);
        }

        // Check for directory-only trailing slash
        if (!line.empty() && (line.back() == L'/' || line.back() == L'\\')) {
            rule.directoryOnly = true;
            while (!line.empty() && (line.back() == L'/' || line.back() == L'\\')) {
                line.pop_back();
            }
        }

        // Check for root-only leading slash
        if (!line.empty() && (line.front() == L'/' || line.front() == L'\\')) {
            rule.rootOnly = true;
            while (!line.empty() && (line.front() == L'/' || line.front() == L'\\')) {
                line.erase(0, 1);
            }
        }

        if (line.empty()) continue;

        // Check if there is an internal slash
        rule.hasSlash = (line.find(L'/') != std::wstring::npos || line.find(L'\\') != std::wstring::npos);

        rule.patternSegments = SplitSegments(line);
        if (rule.patternSegments.empty()) continue;

        m_rules.push_back(std::move(rule));
    }
}

size_t IgnoreEngine::LoadIgnoreFile(const std::filesystem::path& filePath) {
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec) || !std::filesystem::is_regular_file(filePath, ec)) {
        return 0;
    }

    std::wstring content = ReadUtf8OrAnsiFile(filePath);
    if (content.empty()) return 0;

    std::wstring baseDir = filePath.parent_path().wstring();
    std::wstring sourceName = filePath.filename().wstring();

    size_t beforeCount = m_rules.size();
    ParseRules(content, baseDir, sourceName);
    size_t added = m_rules.size() - beforeCount;

    if (added > 0) {
        m_loadedSourceFiles.push_back(filePath.wstring());
    }
    return added;
}

void IgnoreEngine::LoadWorkspaceIgnoreFiles(const std::wstring& workspaceRoot) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rules.clear();
    m_loadedSourceFiles.clear();
    m_workspaceRoot = NormalizePath(workspaceRoot);

    if (m_workspaceRoot.empty()) return;

    std::filesystem::path root(m_workspaceRoot);
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return;
    }

    // 1. Load priority root ignore files (.gitignore, .dockerignore, .npmignore, etc.)
    static const std::vector<std::wstring> rootIgnoreCandidates = {
        L".gitignore",
        L".dockerignore",
        L".npmignore",
        L".eslintignore",
        L".prettierignore",
        L".ignore",
        L".pnpmignore",
        L".helmignore",
        L".hgignore",
        L".bzrignore"
    };

    for (const auto& fname : rootIgnoreCandidates) {
        auto candPath = root / fname;
        if (std::filesystem::exists(candPath, ec)) {
            LoadIgnoreFile(candPath);
        }
    }

    // 2. Discover nested ignore files (e.g. client/.dockerignore, packages/*/.gitignore, etc.)
    // Traverse with limit to avoid recursion into massive deep trees
    size_t dirsScanned = 0;
    const size_t MAX_DIRS = 300;

    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end && dirsScanned < MAX_DIRS; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }

        if (it->is_directory(ec)) {
            std::wstring dname = it->path().filename().wstring();
            std::wstring lowerDname = ToLower(dname);
            // Prune heavy internal dirs
            if (lowerDname == L".git" || lowerDname == L"node_modules" || lowerDname == L"vendor" ||
                lowerDname == L".phide" || lowerDname == L".venv" || lowerDname == L"bin" || lowerDname == L"build" || lowerDname == L"dist") {
                it.disable_recursion_pending();
            }
            dirsScanned++;
            continue;
        }

        if (it->is_regular_file(ec)) {
            std::wstring fname = it->path().filename().wstring();
            if (IsSupportedIgnoreFileName(fname)) {
                // Root ones already loaded above, load nested ones
                if (it->path().parent_path() != root) {
                    LoadIgnoreFile(it->path());
                }
            }
        }
    }
}

void IgnoreEngine::SetGitIgnoredSet(std::unordered_set<std::wstring> gitSet) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gitIgnoredSet = std::move(gitSet);
}

size_t IgnoreEngine::GetRuleCount() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rules.size();
}

std::vector<std::wstring> IgnoreEngine::GetLoadedSourceFiles() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_loadedSourceFiles;
}

void IgnoreEngine::CopyFrom(const IgnoreEngine& other) {
    if (this == &other) return;
    std::scoped_lock lock(m_mutex, other.m_mutex);
    m_workspaceRoot = other.m_workspaceRoot;
    m_rules = other.m_rules;
    m_gitIgnoredSet = other.m_gitIgnoredSet;
    m_loadedSourceFiles = other.m_loadedSourceFiles;
}

bool IgnoreEngine::IsIgnored(
    const std::wstring& fullPath,
    const std::wstring& fileName,
    bool isDirectory
) const {
    // 1. Built-in hardcoded rules: VCS metadata & build artifacts
    std::wstring lowerName = ToLower(fileName);
    if (lowerName == L".git" || lowerName == L".vs") {
        return true;
    }
    if (!isDirectory && (lowerName.ends_with(L".obj") || lowerName.ends_with(L".pdb") || lowerName.ends_with(L".exe"))) {
        return true;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // 2. Check external git status ignore set if present
    if (!m_gitIgnoredSet.empty()) {
        std::wstring relItem;
        if (!m_workspaceRoot.empty()) {
            std::wstring normFP = NormalizePath(fullPath);
            if (normFP.rfind(m_workspaceRoot, 0) == 0) {
                relItem = normFP.substr(m_workspaceRoot.length());
                while (!relItem.empty() && relItem.front() == L'\\') relItem.erase(0, 1);
            } else {
                relItem = normFP;
            }
        } else {
            relItem = NormalizePath(fullPath);
        }

        if (m_gitIgnoredSet.count(relItem) > 0) return true;

        size_t sep = relItem.find(L'\\');
        while (sep != std::wstring::npos) {
            std::wstring parent = relItem.substr(0, sep);
            if (m_gitIgnoredSet.count(parent) > 0) return true;
            sep = relItem.find(L'\\', sep + 1);
        }
    }

    if (m_rules.empty()) {
        return false;
    }

    std::wstring normFullPath = NormalizePath(fullPath);
    bool ignored = false;

    // 3. Evaluate loaded rules (.gitignore, .dockerignore, .npmignore, etc.) in sequential order
    for (const auto& rule : m_rules) {
        // If rule is directoryOnly and this is not a directory, skip
        if (rule.directoryOnly && !isDirectory) {
            continue;
        }

        // Target path must be located within rule's baseDir
        if (normFullPath.length() < rule.baseDir.length()) {
            continue;
        }
        if (_wcsnicmp(normFullPath.c_str(), rule.baseDir.c_str(), rule.baseDir.length()) != 0) {
            continue;
        }

        // Relative path from rule's baseDir
        std::wstring relFromBase;
        if (normFullPath.length() > rule.baseDir.length()) {
            size_t startOffset = rule.baseDir.length();
            if (normFullPath[startOffset] == L'\\') startOffset++;
            relFromBase = normFullPath.substr(startOffset);
        }

        std::vector<std::wstring> candSegments = SplitSegments(relFromBase);
        if (candSegments.empty()) {
            candSegments.push_back(lowerName);
        }

        bool matches = false;

        if (!rule.hasSlash && !rule.rootOnly) {
            // Pattern has no slash (e.g. "*.log", "temp", "node_modules"):
            // Matches if either the filename itself matches, OR any segment matches
            const std::wstring& targetPat = rule.patternSegments[0];
            if (GlobMatch(lowerName, targetPat)) {
                matches = true;
            } else {
                for (const auto& seg : candSegments) {
                    if (GlobMatch(seg, targetPat)) {
                        matches = true;
                        break;
                    }
                }
            }
        } else if (rule.rootOnly) {
            // Root-only: must match starting at index 0 of candSegments
            matches = MatchSegments(candSegments, 0, rule.patternSegments, 0);
        } else {
            // Pattern has a slash: can match from root or any sub-prefix
            for (size_t startIdx = 0; startIdx <= candSegments.size(); ++startIdx) {
                if (MatchSegments(candSegments, startIdx, rule.patternSegments, 0)) {
                    matches = true;
                    break;
                }
            }
        }

        if (matches) {
            ignored = !rule.isNegative;
        }
    }

    return ignored;
}

namespace {

bool ContainsPhideIgnoreRule(const std::string& content) {
    bool ignored = false;
    size_t pos = 0;
    bool firstLine = true;
    while (pos <= content.size()) {
        size_t nl = content.find('\n', pos);
        std::string line = (nl == std::string::npos) ? content.substr(pos) : content.substr(pos, nl - pos);
        if (firstLine) {
            firstLine = false;
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos) {
            size_t last = line.find_last_not_of(" \t");
            std::string trimmed = line.substr(first, last - first + 1);
            if (!trimmed.empty() && trimmed[0] != '#') {
                bool isNeg = false;
                if (trimmed[0] == '!') {
                    isNeg = true;
                    trimmed.erase(0, 1);
                }
                for (char& ch : trimmed) {
                    if (ch == '\\') ch = '/';
                    else ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
                if (trimmed == ".phide" || trimmed == ".phide/" ||
                    trimmed == "/.phide" || trimmed == "/.phide/" ||
                    trimmed == "**/.phide" || trimmed == "**/.phide/" ||
                    trimmed == ".phide/*" || trimmed == ".phide/**" ||
                    trimmed == "/.phide/*" || trimmed == "/.phide/**" ||
                    trimmed == "**/.phide/*" || trimmed == "**/.phide/**") {
                    ignored = !isNeg;
                }
            }
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return ignored;
}

bool EnsurePhideEntryInIgnoreFile(const std::filesystem::path& filePath, bool createIfMissing) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(filePath, ec);
    if (!exists) {
        if (!createIfMissing) return false;
        std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        static constexpr char kInitial[] = ".phide/\n";
        out.write(kInitial, sizeof(kInitial) - 1);
        return out.good();
    }

    if (!std::filesystem::is_regular_file(filePath, ec)) {
        return false;
    }

    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) return false;
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    if (ContainsPhideIgnoreRule(raw)) {
        return true;
    }

    const std::string eol = (raw.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
    std::string updated = raw;
    if (!updated.empty() && updated.back() != '\n' && updated.back() != '\r') {
        updated += eol;
    }
    updated += ".phide/";
    updated += eol;

    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(updated.data(), static_cast<std::streamsize>(updated.size()));
    return out.good();
}

bool IsAnyRootIgnoreFilename(const std::wstring& fileName) {
    if (IgnoreEngine::IsSupportedIgnoreFileName(fileName)) return true;
    if (fileName.size() <= 7 || fileName[0] != L'.') return false;
    std::wstring lower = fileName;
    for (auto& ch : lower) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return lower.ends_with(L"ignore");
}

} // namespace

bool IgnoreEngine::EnsurePhideIgnored(const std::wstring& workspaceRoot) {
    if (workspaceRoot.empty()) return false;

    static std::mutex s_ensureMutex;
    std::lock_guard<std::mutex> lock(s_ensureMutex);

    std::error_code ec;
    std::filesystem::path root(workspaceRoot);
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return false;
    }

    // 1. Always ensure root .gitignore exists and contains .phide/
    bool ok = EnsurePhideEntryInIgnoreFile(root / L".gitignore", true);

    // 2. Update any other existing root ignore files (.dockerignore, .npmignore, .eslintignore, etc.)
    std::filesystem::directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec);
    std::filesystem::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc)) continue;
        std::wstring fname = it->path().filename().wstring();
        if (_wcsicmp(fname.c_str(), L".gitignore") == 0) continue;
        if (IsAnyRootIgnoreFilename(fname)) {
            EnsurePhideEntryInIgnoreFile(it->path(), false);
        }
    }

    return ok;
}

bool IgnoreEngine::EnsurePhideIgnoredForPath(const std::filesystem::path& pathInPhide) {
    if (pathInPhide.empty()) return false;
    std::filesystem::path cur = pathInPhide;
    while (!cur.empty()) {
        std::wstring name = cur.filename().wstring();
        if (_wcsicmp(name.c_str(), L".phide") == 0 && cur.has_parent_path()) {
            return EnsurePhideIgnored(cur.parent_path().wstring());
        }
        std::filesystem::path parent = cur.parent_path();
        if (parent == cur) break;
        cur = std::move(parent);
    }
    return false;
}

} // namespace phide::workspace
