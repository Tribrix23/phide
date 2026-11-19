/**
 * @file WorkspaceAuditEngine.cpp
 * @brief Implementation of WorkspaceAuditEngine (#7) and EnvAuditEngine (#8).
 */

#include "phide/editor/WorkspaceAuditEngine.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace phide::editor {

namespace fs = std::filesystem;

namespace {

static std::wstring ToLowerW(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) {
        std::wstring fallback;
        fallback.reserve(s.size());
        for (unsigned char ch : s) fallback.push_back(static_cast<wchar_t>(ch));
        return fallback;
    }
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr);
    return out;
}

static bool ReadFileBoundedUtf8(const std::wstring& path, uint64_t maxBytes, std::wstring& outText) {
    outText.clear();
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_regular_file(path, ec)) return false;
    uint64_t sz = fs::file_size(path, ec);
    if (ec || sz > maxBytes) return false;

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;
    std::string raw(static_cast<size_t>(sz), '\0');
    if (sz > 0) {
        ifs.read(raw.data(), static_cast<std::streamsize>(sz));
    }
    // Strip UTF-8 BOM if present
    if (raw.size() >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }
    outText = Utf8ToWide(raw);
    return true;
}

static std::wstring MakeDisplayRelPath(const std::wstring& fullPath, const std::wstring& root) {
    if (root.empty()) return fs::path(fullPath).filename().wstring();
    std::error_code ec;
    fs::path rel = fs::relative(fs::path(fullPath), fs::path(root), ec);
    if (!ec && !rel.empty()) {
        std::wstring s = rel.wstring();
        if (s.rfind(L"..", 0) != 0) return s;
    }
    return fs::path(fullPath).filename().wstring();
}

static bool IsSkippedDirName(const std::wstring& lowerName) {
    return lowerName == L"node_modules" || lowerName == L".git" || lowerName == L"vendor" ||
           lowerName == L".venv" || lowerName == L"venv" || lowerName == L"dist" ||
           lowerName == L"build" || lowerName == L"out" || lowerName == L".next" ||
           lowerName == L".phide" || lowerName == L"bin" || lowerName == L"obj" ||
           lowerName == L"__pycache__" || lowerName == L".vs" || lowerName == L"coverage";
}

static bool IsEnvIdentChar(wchar_t c) noexcept {
    return iswalnum(c) || c == L'_';
}

static bool IsWellKnownRuntimeEnvKey(const std::wstring& key) {
    static const std::unordered_set<std::wstring> kBuiltins = {
        L"NODE_ENV", L"PATH", L"HOME", L"USER", L"USERNAME", L"PORT", L"PWD", L"SHELL",
        L"COMSPEC", L"TEMP", L"TMP", L"APPDATA", L"LOCALAPPDATA", L"USERPROFILE", L"OS",
        L"REQUEST_METHOD", L"REQUEST_URI", L"HTTP_HOST", L"HTTP_USER_AGENT", L"HTTP_ACCEPT",
        L"HTTP_AUTHORIZATION", L"REMOTE_ADDR", L"SCRIPT_NAME", L"SCRIPT_FILENAME",
        L"SERVER_NAME", L"SERVER_PORT", L"SERVER_PROTOCOL", L"CONTENT_TYPE", L"CONTENT_LENGTH",
        L"DOCUMENT_ROOT", L"QUERY_STRING", L"HTTPS", L"PHP_SELF", L"BASE_URL", L"MODE",
        L"DEV", L"PROD", L"SSR", L"CI", L"TZ", L"LANG", L"LC_ALL", L"TERM", L"DEBUG",
        L"PYTHONPATH", L"VIRTUAL_ENV", L"PHIDE_MYSQL_PORT"
    };
    return kBuiltins.count(key) > 0;
}

static bool IsIdentChar(wchar_t c) noexcept {
    return iswalnum(c) || c == L'_' || c == L'$';
}

static bool IsCssIdentChar(wchar_t c) noexcept {
    return iswalnum(c) || c == L'-' || c == L'_';
}

static std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find(L'\n', start);
        if (nl == std::wstring::npos) {
            std::wstring ln = text.substr(start);
            if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
            lines.push_back(std::move(ln));
            break;
        }
        std::wstring ln = text.substr(start, nl - start);
        if (!ln.empty() && ln.back() == L'\r') ln.pop_back();
        lines.push_back(std::move(ln));
        start = nl + 1;
        if (start == text.size()) break;
    }
    return lines;
}

static bool IsEntrypointOrTestFile(const std::wstring& lowerPath) {
    std::wstring fname = ToLowerW(fs::path(lowerPath).filename().wstring());
    if (fname.rfind(L"index.", 0) == 0 || fname.rfind(L"main.", 0) == 0 ||
        fname.rfind(L"app.", 0) == 0 || fname.rfind(L"page.", 0) == 0 ||
        fname.rfind(L"layout.", 0) == 0 || fname.rfind(L"route.", 0) == 0 ||
        fname.rfind(L"server.", 0) == 0) {
        return true;
    }
    if (fname.find(L".config.") != std::wstring::npos ||
        fname.find(L".test.") != std::wstring::npos ||
        fname.find(L".spec.") != std::wstring::npos ||
        fname.ends_with(L".d.ts")) {
        return true;
    }
    return false;
}

/**
 * @brief Extracts exported symbols from a JS/TS/JSX/TSX or PHP file.
 */
static void ExtractExportedSymbols(const std::wstring& lowerExt, const std::wstring& lowerPath,
                                   const std::vector<std::wstring>& lines,
                                   std::vector<std::pair<std::wstring, size_t>>& outSymbols) {
    outSymbols.clear();
    if (IsEntrypointOrTestFile(lowerPath)) return;

    const bool isJsTs = (lowerExt == L".js" || lowerExt == L".mjs" || lowerExt == L".cjs" ||
                         lowerExt == L".ts" || lowerExt == L".jsx" || lowerExt == L".tsx");
    const bool isPhp  = (lowerExt == L".php");
    if (!isJsTs && !isPhp) return;

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::wstring& ln = lines[li];
        size_t first = ln.find_first_not_of(L" \t");
        if (first == std::wstring::npos) continue;
        std::wstring tr = ln.substr(first);
        if (tr.rfind(L"//", 0) == 0 || tr.rfind(L"/*", 0) == 0 || tr.rfind(L"*", 0) == 0) continue;

        if (isJsTs && tr.rfind(L"export ", 0) == 0) {
            std::wstring rest = tr.substr(7);
            size_t p = rest.find_first_not_of(L" \t");
            if (p == std::wstring::npos) continue;
            rest = rest.substr(p);
            if (rest.rfind(L"default ", 0) == 0) continue; // Default exports are often imported under any alias
            if (rest.rfind(L"async ", 0) == 0) {
                rest = rest.substr(6);
                p = rest.find_first_not_of(L" \t");
                if (p == std::wstring::npos) continue;
                rest = rest.substr(p);
            }
            const wchar_t* kws[] = {
                L"function*", L"function ", L"const ", L"let ", L"var ",
                L"class ", L"interface ", L"type ", L"enum "
            };
            for (const wchar_t* kw : kws) {
                size_t klen = wcslen(kw);
                if (rest.rfind(kw, 0) == 0) {
                    size_t s = klen;
                    while (s < rest.size() && (iswspace(rest[s]) || rest[s] == L'*')) ++s;
                    size_t e = s;
                    while (e < rest.size() && IsIdentChar(rest[e])) ++e;
                    if (e > s) {
                        std::wstring sym = rest.substr(s, e - s);
                        if (sym.size() >= 2) outSymbols.emplace_back(std::move(sym), li);
                    }
                    break;
                }
            }
        } else if (isPhp && first == 0) {
            // Top-level PHP function or class/interface/trait definitions
            const wchar_t* phpKws[] = { L"function ", L"class ", L"interface ", L"trait " };
            for (const wchar_t* kw : phpKws) {
                size_t klen = wcslen(kw);
                if (tr.rfind(kw, 0) == 0) {
                    size_t s = klen;
                    while (s < tr.size() && (iswspace(tr[s]) || tr[s] == L'&')) ++s;
                    size_t e = s;
                    while (e < tr.size() && IsIdentChar(tr[e])) ++e;
                    if (e > s) {
                        std::wstring sym = tr.substr(s, e - s);
                        if (sym.size() >= 2 && sym.rfind(L"__", 0) != 0) {
                            outSymbols.emplace_back(std::move(sym), li);
                        }
                    }
                    break;
                }
            }
        }
    }
}

/**
 * @brief Extracts top-level CSS .class and #id selectors from .css / .scss rule headers.
 */
static void ExtractCssSelectors(const std::wstring& text,
                                std::vector<std::tuple<std::wstring, std::wstring, size_t>>& outSelectors) {
    // Tuple: (displaySelector e.g. ".card-btn", tokenForMatch e.g. "card-btn", line0)
    outSelectors.clear();
    bool inComment = false;
    int braceDepth = 0;
    size_t line0 = 0;
    std::unordered_set<std::wstring> seen;

    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c == L'\n') { ++line0; continue; }
        if (inComment) {
            if (c == L'*' && i + 1 < text.size() && text[i + 1] == L'/') {
                inComment = false;
                ++i;
            }
            continue;
        }
        if (c == L'/' && i + 1 < text.size() && text[i + 1] == L'*') {
            inComment = true;
            ++i;
            continue;
        }
        if (c == L'{') { ++braceDepth; continue; }
        if (c == L'}') { braceDepth = (std::max)(0, braceDepth - 1); continue; }
        if (braceDepth > 0) continue; // Inside declaration block: skip values/hex colors

        // Skip @media / @keyframes / @import lines
        if (c == L'@') {
            while (i < text.size() && text[i] != L'{' && text[i] != L';' && text[i] != L'\n') ++i;
            if (i < text.size() && text[i] == L'{') ++braceDepth;
            if (i < text.size() && text[i] == L'\n') ++line0;
            continue;
        }

        if (c == L'.' || c == L'#') {
            size_t s = i + 1;
            if (s < text.size() && (iswalpha(text[s]) || text[s] == L'_' || text[s] == L'-')) {
                size_t e = s;
                while (e < text.size() && IsCssIdentChar(text[e])) ++e;
                std::wstring tok = text.substr(s, e - s);
                if (tok.size() >= 2 && tok.rfind(L"--", 0) != 0) {
                    std::wstring disp = std::wstring(1, c) + tok;
                    if (seen.insert(disp).second) {
                        outSelectors.emplace_back(std::move(disp), std::move(tok), line0);
                    }
                }
                i = e - 1;
            }
        }
    }
}

/**
 * @brief Extracts JSON string keys inside a top-level object section (e.g. "dependencies" or "require").
 */
static void ExtractJsonObjectSectionKeys(const std::wstring& jsonText, const std::wstring& sectionKey,
                                         std::vector<std::pair<std::wstring, size_t>>& outKeys) {
    outKeys.clear();
    std::wstring needle = L"\"" + sectionKey + L"\"";
    size_t pos = jsonText.find(needle);
    if (pos == std::wstring::npos) return;
    size_t brace = jsonText.find(L'{', pos + needle.size());
    if (brace == std::wstring::npos) return;

    size_t line0 = 0;
    for (size_t k = 0; k < brace; ++k) {
        if (jsonText[k] == L'\n') ++line0;
    }

    int depth = 1;
    bool inStr = false;
    std::wstring curStr;
    bool expectColon = false;
    for (size_t i = brace + 1; i < jsonText.size() && depth > 0; ++i) {
        wchar_t c = jsonText[i];
        if (c == L'\n') ++line0;
        if (inStr) {
            if (c == L'\\' && i + 1 < jsonText.size()) { ++i; continue; }
            if (c == L'"') {
                inStr = false;
                if (depth == 1 && expectColon) {
                    size_t peek = i + 1;
                    while (peek < jsonText.size() && iswspace(jsonText[peek])) ++peek;
                    if (peek < jsonText.size() && jsonText[peek] == L':') {
                        outKeys.emplace_back(curStr, line0);
                    }
                }
                curStr.clear();
            } else {
                curStr.push_back(c);
            }
            continue;
        }
        if (c == L'"') {
            inStr = true;
            curStr.clear();
            expectColon = (depth == 1);
        } else if (c == L'{') {
            ++depth;
        } else if (c == L'}') {
            --depth;
        }
    }
}

} // namespace

// =============================================================================
// EnvAuditEngine (#8)
// =============================================================================

std::vector<EnvKeyEntry> EnvAuditEngine::ParseEnvTextKeys(const std::wstring& text) {
    std::vector<EnvKeyEntry> keys;
    std::unordered_set<std::wstring> seen;
    auto lines = SplitLines(text);
    for (size_t li = 0; li < lines.size(); ++li) {
        std::wstring ln = lines[li];
        size_t s = ln.find_first_not_of(L" \t");
        if (s == std::wstring::npos) continue;
        ln = ln.substr(s);
        if (ln[0] == L'#') continue;
        if (ln.rfind(L"export ", 0) == 0) {
            ln = ln.substr(7);
            size_t s2 = ln.find_first_not_of(L" \t");
            if (s2 == std::wstring::npos) continue;
            ln = ln.substr(s2);
        }
        size_t eq = ln.find(L'=');
        if (eq == std::wstring::npos || eq == 0) continue;
        std::wstring key = ln.substr(0, eq);
        while (!key.empty() && iswspace(key.back())) key.pop_back();
        if (key.empty()) continue;
        bool valid = true;
        for (wchar_t ch : key) {
            if (!IsEnvIdentChar(ch)) { valid = false; break; }
        }
        if (valid && seen.insert(key).second) {
            keys.push_back({ std::move(key), li });
        }
    }
    return keys;
}

std::vector<EnvKeyEntry> EnvAuditEngine::ParseEnvFileKeys(const std::wstring& filePath) {
    std::wstring text;
    if (!ReadFileBoundedUtf8(filePath, 512u * 1024u, text)) return {};
    return ParseEnvTextKeys(text);
}

std::unordered_set<std::wstring> EnvAuditEngine::CollectKnownWorkspaceEnvKeys(const std::wstring& workspaceRoot) {
    std::unordered_set<std::wstring> known;
    if (workspaceRoot.empty()) return known;
    fs::path root(workspaceRoot);
    for (const auto& entry : ParseEnvFileKeys((root / L".env").wstring())) {
        known.insert(entry.key);
    }
    for (const auto& entry : ParseEnvFileKeys((root / L".env.example").wstring())) {
        known.insert(entry.key);
    }
    return known;
}

std::vector<AuditFinding> EnvAuditEngine::CheckEnvFileDrift(const std::wstring& workspaceRoot) {
    std::vector<AuditFinding> findings;
    if (workspaceRoot.empty()) return findings;

    fs::path root(workspaceRoot);
    fs::path envPath = root / L".env";
    fs::path examplePath = root / L".env.example";
    std::error_code ec;
    const bool hasEnv = fs::exists(envPath, ec);
    const bool hasExample = fs::exists(examplePath, ec);
    if (!hasEnv && !hasExample) return findings;

    auto envKeys = hasEnv ? ParseEnvFileKeys(envPath.wstring()) : std::vector<EnvKeyEntry>{};
    auto exKeys  = hasExample ? ParseEnvFileKeys(examplePath.wstring()) : std::vector<EnvKeyEntry>{};

    std::unordered_set<std::wstring> envSet, exSet;
    for (const auto& e : envKeys) envSet.insert(e.key);
    for (const auto& e : exKeys) exSet.insert(e.key);

    if (hasEnv) {
        for (const auto& e : envKeys) {
            if (exSet.count(e.key) == 0) {
                AuditFinding f;
                f.category = AuditCategory::EnvDrift;
                f.severity = 2;
                f.symbol = e.key;
                f.filePath = envPath.wstring();
                f.displayPath = L".env";
                f.line0 = e.line0;
                f.col0 = 0;
                f.canSyncEnvExample = true;
                f.message = hasExample
                    ? (L"Env key '" + e.key + L"' is in .env but missing from .env.example")
                    : (L"Env key '" + e.key + L"' is in .env (no .env.example file found)");
                findings.push_back(std::move(f));
            }
        }
    }

    if (hasExample && hasEnv) {
        for (const auto& e : exKeys) {
            if (envSet.count(e.key) == 0) {
                AuditFinding f;
                f.category = AuditCategory::EnvDrift;
                f.severity = 2;
                f.symbol = e.key;
                f.filePath = examplePath.wstring();
                f.displayPath = L".env.example";
                f.line0 = e.line0;
                f.col0 = 0;
                f.canSyncEnvExample = false;
                f.message = L"Env key '" + e.key + L"' is required by .env.example but missing from .env";
                findings.push_back(std::move(f));
            }
        }
    }
    return findings;
}

std::vector<AuditFinding> EnvAuditEngine::ScanCodeEnvReferences(
    const std::wstring& filePath,
    const std::wstring& workspaceRoot,
    const std::wstring& content,
    const std::unordered_set<std::wstring>& knownKeys) {

    std::vector<AuditFinding> findings;
    if (knownKeys.empty() || content.empty()) return findings;

    std::wstring ext = ToLowerW(fs::path(filePath).extension().wstring());
    if (ext != L".js" && ext != L".mjs" && ext != L".cjs" && ext != L".ts" &&
        ext != L".jsx" && ext != L".tsx" && ext != L".php" && ext != L".py") {
        return findings;
    }

    std::wstring relDisp = MakeDisplayRelPath(filePath, workspaceRoot);
    auto lines = SplitLines(content);
    std::unordered_set<std::wstring> reported;

    auto recordMissing = [&](const std::wstring& key, size_t line0, size_t col0) {
        if (key.size() < 2 || IsWellKnownRuntimeEnvKey(key)) return;
        if (knownKeys.count(key) > 0) return;
        std::wstring dedup = key + L"@" + std::to_wstring(line0);
        if (!reported.insert(dedup).second) return;

        AuditFinding f;
        f.category = AuditCategory::EnvDrift;
        f.severity = 2;
        f.symbol = key;
        f.filePath = filePath;
        f.displayPath = relDisp;
        f.line0 = line0;
        f.col0 = col0;
        f.message = L"Undefined environment variable '" + key + L"' (not found in .env or .env.example)";
        findings.push_back(std::move(f));
    };

    auto scanQuotedCall = [&](const std::wstring& ln, size_t line0, const wchar_t* prefix) {
        size_t plen = wcslen(prefix);
        size_t pos = 0;
        while ((pos = ln.find(prefix, pos)) != std::wstring::npos) {
            size_t qPos = pos + plen;
            while (qPos < ln.size() && iswspace(ln[qPos])) ++qPos;
            if (qPos < ln.size() && (ln[qPos] == L'\'' || ln[qPos] == L'"')) {
                wchar_t q = ln[qPos];
                size_t kStart = qPos + 1;
                size_t kEnd = kStart;
                while (kEnd < ln.size() && IsEnvIdentChar(ln[kEnd])) ++kEnd;
                if (kEnd > kStart && kEnd < ln.size() && ln[kEnd] == q) {
                    recordMissing(ln.substr(kStart, kEnd - kStart), line0, kStart);
                }
            }
            pos += plen;
        }
    };

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::wstring& ln = lines[li];
        size_t first = ln.find_first_not_of(L" \t");
        if (first == std::wstring::npos) continue;
        if (ln.compare(first, 2, L"//") == 0 || ln[first] == L'#') continue;

        // JS/TS: process.env.KEY / import.meta.env.KEY
        for (const wchar_t* dotPrefix : { L"process.env.", L"import.meta.env." }) {
            size_t plen = wcslen(dotPrefix);
            size_t pos = 0;
            while ((pos = ln.find(dotPrefix, pos)) != std::wstring::npos) {
                size_t kStart = pos + plen;
                size_t kEnd = kStart;
                while (kEnd < ln.size() && IsEnvIdentChar(ln[kEnd])) ++kEnd;
                if (kEnd > kStart) {
                    std::wstring key = ln.substr(kStart, kEnd - kStart);
                    // Exclude property accesses like process.env.length
                    bool allUpper = true;
                    for (wchar_t ch : key) {
                        if (iswalpha(ch) && !iswupper(ch)) { allUpper = false; break; }
                    }
                    if (allUpper) recordMissing(key, li, kStart);
                }
                pos = kEnd;
            }
        }

        // PHP / Python / JS bracket or call access
        scanQuotedCall(ln, li, L"process.env[");
        scanQuotedCall(ln, li, L"$_ENV[");
        scanQuotedCall(ln, li, L"$_SERVER[");
        scanQuotedCall(ln, li, L"getenv(");
        scanQuotedCall(ln, li, L"os.getenv(");
        scanQuotedCall(ln, li, L"os.environ[");
        scanQuotedCall(ln, li, L"os.environ.get(");
    }
    return findings;
}

bool EnvAuditEngine::SyncEnvExampleKeys(const std::wstring& workspaceRoot, int& outAddedCount) {
    outAddedCount = 0;
    if (workspaceRoot.empty()) return false;

    fs::path root(workspaceRoot);
    fs::path envPath = root / L".env";
    fs::path exPath  = root / L".env.example";
    std::error_code ec;
    if (!fs::exists(envPath, ec)) return false;

    auto envKeys = ParseEnvFileKeys(envPath.wstring());
    if (envKeys.empty()) return true;

    std::unordered_set<std::wstring> exSet;
    std::string existingUtf8;
    if (fs::exists(exPath, ec)) {
        for (const auto& k : ParseEnvFileKeys(exPath.wstring())) {
            exSet.insert(k.key);
        }
        std::ifstream ifs(exPath, std::ios::binary);
        if (ifs.is_open()) {
            existingUtf8.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        }
    }

    std::string toAppend;
    for (const auto& k : envKeys) {
        if (exSet.count(k.key) == 0) {
            toAppend += WideToUtf8(k.key) + "=\n";
            ++outAddedCount;
        }
    }
    if (outAddedCount == 0) return true;

    if (!existingUtf8.empty() && existingUtf8.back() != '\n') {
        existingUtf8.push_back('\n');
    }
    existingUtf8 += toAppend;

    std::ofstream ofs(exPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs.write(existingUtf8.data(), static_cast<std::streamsize>(existingUtf8.size()));
    return ofs.good();
}

// =============================================================================
// WorkspaceAuditEngine (#7)
// =============================================================================

WorkspaceAuditReport WorkspaceAuditEngine::RunFullAudit(
    const std::wstring& workspaceRoot,
    const std::atomic<bool>* cancelFlag) {

    WorkspaceAuditReport report;
    auto t0 = std::chrono::steady_clock::now();
    if (workspaceRoot.empty()) {
        report.completed = true;
        return report;
    }

    auto isCancelled = [cancelFlag]() -> bool {
        return cancelFlag && cancelFlag->load(std::memory_order_relaxed);
    };

    struct ScannedFile {
        std::wstring fullPath;
        std::wstring relDisplay;
        std::wstring extLower;
        std::wstring text;
    };

    std::vector<ScannedFile> files;
    files.reserve(256);

    try {
        std::error_code ec;
        fs::recursive_directory_iterator it(
            fs::path(workspaceRoot), fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator endIt;
        for (; it != endIt && !ec && files.size() < kMaxAuditFiles; it.increment(ec)) {
            if (isCancelled()) { report.cancelled = true; return report; }
            if (it->is_directory(ec)) {
                if (IsSkippedDirName(ToLowerW(it->path().filename().wstring()))) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            std::wstring ext = ToLowerW(it->path().extension().wstring());
            if (ext != L".js" && ext != L".mjs" && ext != L".cjs" && ext != L".ts" &&
                ext != L".jsx" && ext != L".tsx" && ext != L".php" && ext != L".py" &&
                ext != L".html" && ext != L".htm" && ext != L".vue" && ext != L".svelte" &&
                ext != L".css" && ext != L".scss" && ext != L".json") {
                continue;
            }
            std::wstring txt;
            std::wstring full = it->path().wstring();
            if (!ReadFileBoundedUtf8(full, kMaxAuditFileBytes, txt)) continue;
            files.push_back({ full, MakeDisplayRelPath(full, workspaceRoot), ext, std::move(txt) });
        }
    } catch (...) {}

    report.filesScanned = files.size();

    // Build inverted token index (codeTokens + templateTokens) once in O(total_chars)
    // Maps identifier/class token -> set of file indices where it appears
    std::unordered_map<std::wstring, std::vector<size_t>> codeTokenFiles;
    std::unordered_map<std::wstring, std::vector<size_t>> templateTokenFiles;

    auto indexTokens = [](const std::wstring& txt, size_t fileIdx,
                          std::unordered_map<std::wstring, std::vector<size_t>>& mapOut) {
        size_t i = 0;
        while (i < txt.size()) {
            if (IsCssIdentChar(txt[i]) || txt[i] == L'$') {
                size_t s = i;
                while (i < txt.size() && (IsCssIdentChar(txt[i]) || txt[i] == L'$')) ++i;
                if (i - s >= 2 && i - s <= 96) {
                    std::wstring tok = txt.substr(s, i - s);
                    auto& vec = mapOut[tok];
                    if (vec.empty() || vec.back() != fileIdx) vec.push_back(fileIdx);
                }
            } else {
                ++i;
            }
        }
    };

    for (size_t fi = 0; fi < files.size(); ++fi) {
        if (isCancelled()) { report.cancelled = true; return report; }
        const auto& ext = files[fi].extLower;
        if (ext != L".css" && ext != L".scss") {
            indexTokens(files[fi].text, fi, codeTokenFiles);
            if (ext != L".json") {
                indexTokens(files[fi].text, fi, templateTokenFiles);
            }
        }
    }

    // 1. Unused Exports (JS/TS/PHP)
    for (size_t fi = 0; fi < files.size(); ++fi) {
        if (isCancelled()) { report.cancelled = true; return report; }
        const auto& f = files[fi];
        auto lines = SplitLines(f.text);
        std::vector<std::pair<std::wstring, size_t>> exports;
        ExtractExportedSymbols(f.extLower, ToLowerW(f.fullPath), lines, exports);
        for (const auto& [sym, ln] : exports) {
            auto itTok = codeTokenFiles.find(sym);
            bool usedElsewhere = false;
            if (itTok != codeTokenFiles.end()) {
                for (size_t occFile : itTok->second) {
                    if (occFile != fi) { usedElsewhere = true; break; }
                }
            }
            if (!usedElsewhere) {
                AuditFinding af;
                af.category = AuditCategory::UnusedExport;
                af.severity = 2;
                af.symbol = sym;
                af.filePath = f.fullPath;
                af.displayPath = f.relDisplay;
                af.line0 = ln;
                af.message = L"Exported symbol '" + sym + L"' has no references in other workspace files";
                report.findings.push_back(std::move(af));
                report.unusedExportCount++;
            }
        }
    }

    // 2. Unused CSS Selectors (.css / .scss)
    for (size_t fi = 0; fi < files.size(); ++fi) {
        if (isCancelled()) { report.cancelled = true; return report; }
        const auto& f = files[fi];
        if (f.extLower != L".css" && f.extLower != L".scss") continue;

        std::vector<std::tuple<std::wstring, std::wstring, size_t>> selectors;
        ExtractCssSelectors(f.text, selectors);
        for (const auto& [disp, tok, ln] : selectors) {
            if (templateTokenFiles.find(tok) == templateTokenFiles.end()) {
                AuditFinding af;
                af.category = AuditCategory::UnusedCssSelector;
                af.severity = 3;
                af.symbol = disp;
                af.filePath = f.fullPath;
                af.displayPath = f.relDisplay;
                af.line0 = ln;
                af.message = L"CSS selector '" + disp + L"' is not referenced in any HTML/PHP/JSX/JS template";
                report.findings.push_back(std::move(af));
                report.unusedCssCount++;
            }
        }
    }

    // 3. Unused Dependencies (package.json "dependencies" / composer.json "require")
    for (size_t fi = 0; fi < files.size(); ++fi) {
        if (isCancelled()) { report.cancelled = true; return report; }
        const auto& f = files[fi];
        std::wstring fname = ToLowerW(fs::path(f.fullPath).filename().wstring());
        if (fname == L"package.json") {
            std::vector<std::pair<std::wstring, size_t>> deps;
            ExtractJsonObjectSectionKeys(f.text, L"dependencies", deps);
            for (const auto& [pkg, ln] : deps) {
                if (pkg.empty() || pkg.rfind(L"@types/", 0) == 0) continue;
                bool found = false;
                for (size_t oi = 0; oi < files.size(); ++oi) {
                    if (oi == fi) continue;
                    const auto& oe = files[oi].extLower;
                    if (oe == L".js" || oe == L".mjs" || oe == L".cjs" || oe == L".ts" ||
                        oe == L".jsx" || oe == L".tsx" || oe == L".html") {
                        if (files[oi].text.find(pkg) != std::wstring::npos) {
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    AuditFinding af;
                    af.category = AuditCategory::UnusedDependency;
                    af.severity = 2;
                    af.symbol = pkg;
                    af.filePath = f.fullPath;
                    af.displayPath = f.relDisplay;
                    af.line0 = ln;
                    af.message = L"npm dependency '" + pkg + L"' appears unused across workspace source files";
                    report.findings.push_back(std::move(af));
                    report.unusedDepCount++;
                }
            }
        } else if (fname == L"composer.json") {
            std::vector<std::pair<std::wstring, size_t>> reqs;
            ExtractJsonObjectSectionKeys(f.text, L"require", reqs);
            for (const auto& [pkg, ln] : reqs) {
                if (pkg == L"php" || pkg.rfind(L"ext-", 0) == 0 || pkg.rfind(L"lib-", 0) == 0) continue;
                std::wstring needle = pkg;
                auto slash = pkg.find(L'/');
                std::wstring shortPart = (slash != std::wstring::npos) ? pkg.substr(slash + 1) : pkg;
                bool found = false;
                for (size_t oi = 0; oi < files.size(); ++oi) {
                    if (oi == fi || files[oi].extLower != L".php") continue;
                    std::wstring lowTxt = ToLowerW(files[oi].text);
                    if (lowTxt.find(ToLowerW(shortPart)) != std::wstring::npos ||
                        lowTxt.find(ToLowerW(needle)) != std::wstring::npos) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    AuditFinding af;
                    af.category = AuditCategory::UnusedDependency;
                    af.severity = 2;
                    af.symbol = pkg;
                    af.filePath = f.fullPath;
                    af.displayPath = f.relDisplay;
                    af.line0 = ln;
                    af.message = L"Composer dependency '" + pkg + L"' appears unreferenced in workspace PHP files";
                    report.findings.push_back(std::move(af));
                    report.unusedDepCount++;
                }
            }
        }
    }

    // 4. .env vs .env.example Drift & Code Env References
    for (auto& f : EnvAuditEngine::CheckEnvFileDrift(workspaceRoot)) {
        report.findings.push_back(std::move(f));
        report.envDriftCount++;
    }
    auto knownEnvKeys = EnvAuditEngine::CollectKnownWorkspaceEnvKeys(workspaceRoot);
    if (!knownEnvKeys.empty()) {
        for (const auto& f : files) {
            if (isCancelled()) { report.cancelled = true; return report; }
            for (auto& ef : EnvAuditEngine::ScanCodeEnvReferences(f.fullPath, workspaceRoot, f.text, knownEnvKeys)) {
                report.findings.push_back(std::move(ef));
                report.envDriftCount++;
            }
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    report.elapsedMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    report.completed = true;
    return report;
}

} // namespace phide::editor
