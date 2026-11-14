/**
 * @file WorkspaceNavigation.cpp
 * @brief Implementation of search helpers, fuzzy ranking, document outline,
 *        workspace symbol index, references, call hierarchy, metrics, rename, and tasks.
 */

#include "phide/editor/WorkspaceNavigation.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace phide::editor {

namespace {

std::wstring ToLowerW(std::wstring s) {
    for (auto& ch : s) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return s;
}

bool IsWordChar(wchar_t c) noexcept {
    return std::iswalnum(c) != 0 || c == L'_';
}

bool IsTextCodeExtension(const std::wstring& extLower) {
    static const std::unordered_set<std::wstring> kExts = {
        L".c", L".cc", L".cpp", L".cxx", L".h", L".hh", L".hpp", L".hxx",
        L".js", L".jsx", L".mjs", L".cjs", L".ts", L".tsx",
        L".php", L".phtml", L".py", L".pyw",
        L".css", L".scss", L".less", L".html", L".htm",
        L".java", L".cs", L".go", L".rs", L".rb", L".swift", L".kt",
        L".sql", L".json", L".md", L".yaml", L".yml", L".toml", L".sh", L".bat", L".ps1"
    };
    return kExts.count(extLower) > 0;
}

bool ShouldSkipDirectoryName(const std::wstring& dirNameLower) {
    return dirNameLower == L".git" || dirNameLower == L".vs" || dirNameLower == L".phide" ||
           dirNameLower == L".vscode" || dirNameLower == L"node_modules" || dirNameLower == L"vendor" ||
           dirNameLower == L".venv" || dirNameLower == L"venv" || dirNameLower == L"__pycache__" ||
           dirNameLower == L"bin" || dirNameLower == L"obj" || dirNameLower == L"build" ||
           dirNameLower == L"dist" || dirNameLower == L"out" || dirNameLower == L"target" ||
           dirNameLower == L".next" || dirNameLower == L".nuxt" || dirNameLower == L".cache" ||
           dirNameLower == L"coverage";
}

bool ReadUtf8TextFileBounded(const std::filesystem::path& p, std::wstring& outText, size_t maxBytes = 2 * 1024 * 1024) {
    outText.clear();
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || !std::filesystem::is_regular_file(p, ec)) return false;
    auto fsize = std::filesystem::file_size(p, ec);
    if (ec || fsize == 0 || fsize > maxBytes) return false;

    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) return false;

    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (raw.empty() || raw.find('\0') != std::string::npos) return false;

    if (raw.size() >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }
    if (raw.empty()) return true;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
    UINT cp = CP_UTF8;
    if (wlen <= 0) {
        cp = CP_ACP;
        wlen = MultiByteToWideChar(cp, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
    }
    if (wlen <= 0) return false;

    outText.resize(static_cast<size_t>(wlen));
    MultiByteToWideChar(cp, 0, raw.data(), static_cast<int>(raw.size()), &outText[0], wlen);
    return true;
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find(L'\n', pos);
        if (nl == std::wstring::npos) {
            std::wstring line = text.substr(pos);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            lines.push_back(std::move(line));
            break;
        }
        std::wstring line = text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(std::move(line));
        pos = nl + 1;
    }
    return lines;
}

std::wstring TrimW(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::wstring NormalizePathKey(std::wstring p) {
    for (auto& ch : p) {
        if (ch == L'/') ch = L'\\';
        else ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return p;
}

std::vector<std::filesystem::path> CollectWorkspaceSourceFiles(
    const std::wstring& workspaceRoot,
    const phide::workspace::IgnoreEngine& ignoreEngine,
    size_t maxFiles)
{
    std::vector<std::filesystem::path> files;
    if (workspaceRoot.empty()) return files;

    std::error_code ec;
    std::filesystem::path root(workspaceRoot);
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return files;
    }

    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end && files.size() < maxFiles; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code entryEc;
        std::wstring fname = it->path().filename().wstring();
        if (it->is_directory(entryEc)) {
            std::wstring lowerDir = ToLowerW(fname);
            if (ShouldSkipDirectoryName(lowerDir) ||
                (!fname.empty() && fname[0] == L'.') ||
                ignoreEngine.IsIgnored(it->path().wstring(), fname, true)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file(entryEc)) continue;
        std::wstring extLower = ToLowerW(it->path().extension().wstring());
        if (!IsTextCodeExtension(extLower)) continue;
        if (ignoreEngine.IsIgnored(it->path().wstring(), fname, false)) continue;
        files.push_back(it->path());
    }
    return files;
}

bool IsValidIdentifier(const std::wstring& name) {
    if (name.empty()) return false;
    if (!std::iswalpha(name[0]) && name[0] != L'_' && name[0] != L'$') return false;
    for (size_t i = 1; i < name.size(); ++i) {
        if (!std::iswalnum(name[i]) && name[i] != L'_' && name[i] != L'$') return false;
    }
    return true;
}

} // namespace

std::wstring ApplyPreserveCase(const std::wstring& matchedText, const std::wstring& replacement) {
    if (matchedText.empty() || replacement.empty()) return replacement;

    bool hasAlpha = false;
    bool allUpper = true;
    bool allLower = true;
    size_t alphaCount = 0;
    size_t firstAlphaIdx = std::wstring::npos;
    for (size_t i = 0; i < matchedText.size(); ++i) {
        if (std::iswalpha(matchedText[i])) {
            hasAlpha = true;
            if (firstAlphaIdx == std::wstring::npos) firstAlphaIdx = i;
            if (!std::iswupper(matchedText[i])) allUpper = false;
            if (!std::iswlower(matchedText[i])) allLower = false;
            alphaCount++;
        }
    }
    if (!hasAlpha) return replacement;

    std::wstring out = replacement;
    if (allUpper && alphaCount > 1) {
        for (auto& ch : out) ch = static_cast<wchar_t>(std::towupper(ch));
        return out;
    }
    if (allLower) {
        for (auto& ch : out) ch = static_cast<wchar_t>(std::towlower(ch));
        return out;
    }

    // First alpha is uppercase & rest of word is lowercase => TitleCase
    bool restAllLower = true;
    for (size_t i = firstAlphaIdx + 1; i < matchedText.size(); ++i) {
        if (std::iswalpha(matchedText[i]) && std::iswupper(matchedText[i])) {
            restAllLower = false;
            break;
        }
    }
    if (std::iswupper(matchedText[firstAlphaIdx]) && restAllLower) {
        bool capped = false;
        for (auto& ch : out) {
            if (std::iswalpha(ch)) {
                if (!capped) {
                    ch = static_cast<wchar_t>(std::towupper(ch));
                    capped = true;
                } else {
                    ch = static_cast<wchar_t>(std::towlower(ch));
                }
            }
        }
        return out;
    }

    // Preserve first character's case (camelCase vs PascalCase) when mixed
    if (std::iswlower(matchedText[firstAlphaIdx])) {
        for (auto& ch : out) {
            if (std::iswalpha(ch)) {
                ch = static_cast<wchar_t>(std::towlower(ch));
                break;
            }
        }
    } else if (std::iswupper(matchedText[firstAlphaIdx])) {
        for (auto& ch : out) {
            if (std::iswalpha(ch)) {
                ch = static_cast<wchar_t>(std::towupper(ch));
                break;
            }
        }
    }
    return out;
}

std::vector<SearchLineMatch> FindMatchesInLine(
    const std::wstring& lineText,
    const std::wstring& query,
    bool matchCase,
    bool matchWord,
    bool useRegex)
{
    std::vector<SearchLineMatch> results;
    if (query.empty() || lineText.empty()) return results;

    if (useRegex) {
        try {
            auto flags = std::regex_constants::ECMAScript;
            if (!matchCase) flags |= std::regex_constants::icase;
            std::wregex re(query, flags);
            auto begin = std::wsregex_iterator(lineText.begin(), lineText.end(), re);
            auto end = std::wsregex_iterator();
            for (auto it = begin; it != end && results.size() < 256; ++it) {
                size_t pos = static_cast<size_t>(it->position());
                size_t len = static_cast<size_t>(it->length());
                if (len == 0) continue;
                if (matchWord) {
                    bool leftOk = (pos == 0) || !IsWordChar(lineText[pos - 1]);
                    bool rightOk = (pos + len >= lineText.size()) || !IsWordChar(lineText[pos + len]);
                    if (!leftOk || !rightOk) continue;
                }
                results.push_back({0, pos, len});
            }
        } catch (...) {}
        return results;
    }

    std::wstring haystack = matchCase ? lineText : ToLowerW(lineText);
    std::wstring needle = matchCase ? query : ToLowerW(query);
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::wstring::npos && results.size() < 256) {
        size_t len = needle.size();
        bool keep = true;
        if (matchWord) {
            bool leftOk = (pos == 0) || !IsWordChar(lineText[pos - 1]);
            bool rightOk = (pos + len >= lineText.size()) || !IsWordChar(lineText[pos + len]);
            keep = leftOk && rightOk;
        }
        if (keep) {
            results.push_back({0, pos, len});
        }
        pos += (std::max)(size_t(1), len);
    }
    return results;
}

std::vector<SearchLineMatch> FindMatchesInLines(
    const std::vector<std::wstring>& lines,
    const std::wstring& query,
    bool matchCase,
    bool matchWord,
    bool useRegex,
    const SearchRangeFilter& filter)
{
    std::vector<SearchLineMatch> all;
    if (query.empty() || lines.empty()) return all;

    size_t firstL = filter.restrictToRange ? (std::min)(filter.startLine, lines.size() - 1) : 0;
    size_t lastL = filter.restrictToRange ? (std::min)(filter.endLine, lines.size() - 1) : (lines.size() - 1);
    if (firstL > lastL) std::swap(firstL, lastL);

    for (size_t l = firstL; l <= lastL && all.size() < 5000; ++l) {
        auto lineMatches = FindMatchesInLine(lines[l], query, matchCase, matchWord, useRegex);
        for (auto& m : lineMatches) {
            m.line = l;
            if (filter.restrictToRange) {
                if (l == filter.startLine && m.col < filter.startCol) continue;
                if (l == filter.endLine && m.col + m.length > filter.endCol) continue;
            }
            all.push_back(m);
            if (all.size() >= 5000) break;
        }
    }
    return all;
}

bool ReplaceInLine(
    std::wstring& lineText,
    const std::wstring& query,
    const std::wstring& replacement,
    bool matchCase,
    bool matchWord,
    bool useRegex,
    bool preserveCase)
{
    if (query.empty() || lineText.empty()) return false;
    auto matches = FindMatchesInLine(lineText, query, matchCase, matchWord, useRegex);
    if (matches.empty()) return false;

    std::wstring out;
    out.reserve(lineText.size());
    size_t cursor = 0;
    for (const auto& m : matches) {
        out.append(lineText, cursor, m.col - cursor);
        std::wstring matchedSub = lineText.substr(m.col, m.length);
        std::wstring rep = preserveCase ? ApplyPreserveCase(matchedSub, replacement) : replacement;
        out.append(rep);
        cursor = m.col + m.length;
    }
    out.append(lineText, cursor, lineText.size() - cursor);
    if (out == lineText) return false;
    lineText = std::move(out);
    return true;
}

int ScoreFuzzyMatch(
    const std::wstring& query,
    const std::wstring& title,
    const std::wstring& subtitle,
    bool isOpen,
    bool isRecent)
{
    if (query.empty()) {
        int base = 100;
        if (isOpen) base += 500;
        if (isRecent) base += 200;
        return base;
    }

    std::wstring qLower = ToLowerW(query);
    std::wstring tLower = ToLowerW(title);
    std::wstring sLower = ToLowerW(subtitle);

    auto scoreTarget = [&](const std::wstring& targetLower, const std::wstring& targetOrig, int weight) -> int {
        if (targetLower.empty()) return -1;
        size_t qi = 0;
        int score = 0;
        int consecutive = 0;
        size_t firstMatch = std::wstring::npos;
        size_t lastMatch = 0;

        for (size_t ti = 0; ti < targetLower.size() && qi < qLower.size(); ++ti) {
            if (targetLower[ti] == qLower[qi]) {
                if (firstMatch == std::wstring::npos) firstMatch = ti;
                int charBonus = 10;
                if (ti == 0) {
                    charBonus += 25;
                } else {
                    wchar_t prev = targetOrig[ti - 1];
                    wchar_t cur = targetOrig[ti];
                    if (prev == L'\\' || prev == L'/' || prev == L'_' || prev == L'-' || prev == L'.' || prev == L' ') {
                        charBonus += 18;
                    } else if (std::iswlower(prev) && std::iswupper(cur)) {
                        charBonus += 16; // CamelCase boundary
                    }
                }
                if (consecutive > 0) {
                    charBonus += (std::min)(consecutive * 12, 60);
                }
                if (ti < query.size() && targetOrig[ti] == query[qi]) {
                    charBonus += 4; // exact case
                }
                score += charBonus;
                consecutive++;
                lastMatch = ti;
                qi++;
            } else {
                consecutive = 0;
            }
        }

        if (qi < qLower.size()) return -1;

        if (targetLower == qLower) score += 200;
        else if (targetLower.rfind(qLower, 0) == 0) score += 120;
        else if (targetLower.find(qLower) != std::wstring::npos) score += 70;

        if (firstMatch != std::wstring::npos) {
            score -= static_cast<int>((std::min)(firstMatch * 2, size_t(40)));
            size_t spread = lastMatch - firstMatch;
            score -= static_cast<int>((std::min)(spread, size_t(40)));
        }
        return (score * weight) / 100;
    };

    int titleScore = scoreTarget(tLower, title, 120);
    int subScore = scoreTarget(sLower, subtitle, 70);
    int combinedScore = -1;
    if (titleScore >= 0 || subScore >= 0) {
        combinedScore = (std::max)(titleScore, subScore);
    } else if (!subtitle.empty()) {
        std::wstring full = subtitle + L"/" + title;
        combinedScore = scoreTarget(ToLowerW(full), full, 90);
    }
    if (combinedScore < 0) return -1;

    if (isOpen) combinedScore += 45;
    if (isRecent) combinedScore += 20;
    return combinedScore;
}

bool ParseLineColSpec(const std::wstring& spec, int& outLine1Based, int& outCol1Based) {
    outLine1Based = 0;
    outCol1Based = 0;
    std::wstring s = TrimW(spec);
    if (!s.empty() && s[0] == L':') s.erase(0, 1);
    s = TrimW(s);
    if (s.empty()) return false;

    size_t sep = s.find(L':');
    if (sep == std::wstring::npos) sep = s.find(L',');
    std::wstring linePart = (sep == std::wstring::npos) ? s : TrimW(s.substr(0, sep));
    std::wstring colPart = (sep == std::wstring::npos) ? L"" : TrimW(s.substr(sep + 1));

    try {
        if (!linePart.empty()) outLine1Based = std::stoi(linePart);
        if (!colPart.empty()) outCol1Based = std::stoi(colPart);
    } catch (...) {
        return false;
    }
    return outLine1Based > 0;
}

const wchar_t* GetSymbolKindTag(SymbolKind kind) noexcept {
    switch (kind) {
        case SymbolKind::Class: return L"cls";
        case SymbolKind::Struct: return L"str";
        case SymbolKind::Interface: return L"int";
        case SymbolKind::Enum: return L"enm";
        case SymbolKind::Namespace: return L"ns";
        case SymbolKind::Function: return L"fn";
        case SymbolKind::Method: return L"mth";
        case SymbolKind::Constructor: return L"new";
        case SymbolKind::Constant: return L"cst";
        case SymbolKind::Variable: return L"var";
        case SymbolKind::Property: return L"prp";
        case SymbolKind::Section: return L"sec";
        default: return L"sym";
    }
}

std::vector<DocumentSymbol> ExtractDocumentSymbols(const std::wstring& text, const std::wstring& ext) {
    std::vector<DocumentSymbol> out;
    if (text.empty()) return out;

    std::wstring extL = ToLowerW(ext);
    const bool isMarkdown = (extL == L".md" || extL == L".markdown");
    const bool isCss = (extL == L".css" || extL == L".scss" || extL == L".less");
    const bool isHtml = (extL == L".html" || extL == L".htm" || extL == L".xml");
    const bool isPython = (extL == L".py" || extL == L".pyw");

    auto lines = SplitLines(text);
    const size_t maxLines = (std::min)(lines.size(), size_t(20000));

    static const std::wregex kReClass(LR"(^\s*(?:export\s+)?(?:default\s+)?(?:abstract\s+|final\s+|public\s+|private\s+|internal\s+)?(class|struct|interface|enum|trait|namespace)\s+([A-Za-z_][A-Za-z0-9_:]*))");
    static const std::wregex kReFuncKw(LR"(^\s*(?:export\s+)?(?:default\s+)?(?:async\s+)?(?:public\s+|private\s+|protected\s+|static\s+|final\s+)*(?:function|fn|def|func|sub)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\()");
    static const std::wregex kReJsArrow(LR"(^\s*(?:export\s+)?(?:const|let|var)\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*=\s*(?:async\s+)?(?:\([^)]*\)|[A-Za-z_$][A-Za-z0-9_$]*)\s*=>)");
    static const std::wregex kReCppFunc(LR"(^\s*(?:[\w:<>&\*\[\]]+\s+)+([A-Za-z_~][A-Za-z0-9_:~]*)\s*\([^;]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:\{|$))");
    static const std::wregex kReMethod(LR"(^\s+(?:async\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{)");
    static const std::wregex kReConstVar(LR"(^\s*(?:export\s+)?(?:const|constexpr|static\s+const)\s+([A-Za-z_][A-Za-z0-9_]*)\s*=)");
    static const std::wregex kReCssSelector(LR"(^\s*([.#@]?[A-Za-z_][A-Za-z0-9_\-: ]*)\s*\{)");
    static const std::wregex kReHtmlId(LR"(<([A-Za-z][A-Za-z0-9\-]*)\b[^>]*\bid\s*=\s*["']([^"']+)["'])");

    int braceDepth = 0;
    std::vector<std::pair<int, std::wstring>> scopeStack;

    for (size_t i = 0; i < maxLines; ++i) {
        const std::wstring& rawLine = lines[i];
        std::wstring trimmed = TrimW(rawLine);
        if (trimmed.empty()) continue;

        if (!isMarkdown && (trimmed.rfind(L"//", 0) == 0 || trimmed.rfind(L"#!", 0) == 0 ||
            trimmed.rfind(L"/*", 0) == 0 || trimmed.rfind(L"*", 0) == 0)) {
            if (!(isPython && trimmed.rfind(L"# region", 0) == 0)) {
                for (wchar_t ch : rawLine) {
                    if (ch == L'{') braceDepth++;
                    else if (ch == L'}' && braceDepth > 0) braceDepth--;
                }
                continue;
            }
        }

        while (!scopeStack.empty() && braceDepth <= scopeStack.back().first) {
            scopeStack.pop_back();
        }

        auto pushSym = [&](const std::wstring& name, SymbolKind kind, size_t col, const std::wstring& detail, bool opensScope) {
            if (name.empty()) return;
            if (name == L"if" || name == L"for" || name == L"while" || name == L"switch" ||
                name == L"catch" || name == L"return" || name == L"sizeof" || name == L"else") {
                return;
            }
            DocumentSymbol ds;
            ds.name = name;
            ds.kind = kind;
            ds.line = i;
            ds.col = col;
            ds.endLine = i;
            ds.detail = detail;
            ds.depth = static_cast<int>(scopeStack.size());
            if (!scopeStack.empty()) {
                ds.containerName = scopeStack.back().second;
            }
            out.push_back(std::move(ds));
            if (opensScope) {
                scopeStack.push_back({braceDepth, name});
            }
        };

        std::wsmatch m;
        if (isMarkdown) {
            if (trimmed[0] == L'#') {
                size_t hashes = 0;
                while (hashes < trimmed.size() && trimmed[hashes] == L'#') hashes++;
                std::wstring heading = TrimW(trimmed.substr(hashes));
                if (!heading.empty()) {
                    DocumentSymbol ds;
                    ds.name = heading;
                    ds.kind = SymbolKind::Section;
                    ds.line = i;
                    ds.col = 0;
                    ds.endLine = i;
                    ds.depth = static_cast<int>(hashes > 0 ? hashes - 1 : 0);
                    out.push_back(std::move(ds));
                }
            }
            continue;
        }

        if (isHtml) {
            if (std::regex_search(rawLine, m, kReHtmlId) && m.size() >= 3) {
                std::wstring nm = m[1].str() + L"#" + m[2].str();
                pushSym(nm, SymbolKind::Section, static_cast<size_t>(m.position(2)), L"element", false);
            }
            continue;
        }

        if (isCss) {
            if (std::regex_search(rawLine, m, kReCssSelector) && m.size() >= 2) {
                std::wstring sel = TrimW(m[1].str());
                if (!sel.empty()) {
                    pushSym(sel, SymbolKind::Class, static_cast<size_t>(m.position(1)), L"selector", false);
                }
            }
            continue;
        }

        if (std::regex_search(rawLine, m, kReClass) && m.size() >= 3) {
            std::wstring kw = m[1].str();
            SymbolKind k = SymbolKind::Class;
            if (kw == L"struct") k = SymbolKind::Struct;
            else if (kw == L"interface") k = SymbolKind::Interface;
            else if (kw == L"enum") k = SymbolKind::Enum;
            else if (kw == L"namespace") k = SymbolKind::Namespace;
            pushSym(m[2].str(), k, static_cast<size_t>(m.position(2)), kw, true);
        } else if (std::regex_search(rawLine, m, kReFuncKw) && m.size() >= 2) {
            SymbolKind k = scopeStack.empty() ? SymbolKind::Function : SymbolKind::Method;
            pushSym(m[1].str(), k, static_cast<size_t>(m.position(1)), isPython ? L"def" : L"function", false);
        } else if (std::regex_search(rawLine, m, kReJsArrow) && m.size() >= 2) {
            pushSym(m[1].str(), SymbolKind::Function, static_cast<size_t>(m.position(1)), L"arrow fn", false);
        } else if (!isPython && std::regex_search(rawLine, m, kReCppFunc) && m.size() >= 2) {
            std::wstring fn = m[1].str();
            SymbolKind k = (fn.find(L"::") != std::wstring::npos || !scopeStack.empty()) ? SymbolKind::Method : SymbolKind::Function;
            pushSym(fn, k, static_cast<size_t>(m.position(1)), L"method/fn", false);
        } else if (!isPython && !scopeStack.empty() && std::regex_search(rawLine, m, kReMethod) && m.size() >= 2) {
            pushSym(m[1].str(), SymbolKind::Method, static_cast<size_t>(m.position(1)), L"method", false);
        } else if (std::regex_search(rawLine, m, kReConstVar) && m.size() >= 2) {
            pushSym(m[1].str(), SymbolKind::Constant, static_cast<size_t>(m.position(1)), L"const", false);
        }

        if (!isPython) {
            for (wchar_t ch : rawLine) {
                if (ch == L'{') braceDepth++;
                else if (ch == L'}' && braceDepth > 0) braceDepth--;
            }
        }
        if (out.size() >= 800) break;
    }

    return out;
}

FileCodeMetrics ComputeFileMetrics(const std::wstring& filePath, const std::wstring& text, const std::wstring& ext) {
    FileCodeMetrics metrics;
    metrics.filePath = filePath;
    if (text.empty()) return metrics;

    auto lines = SplitLines(text);
    metrics.totalLines = lines.size();

    bool inBlockComment = false;
    size_t currentDepth = 0;

    static const std::wregex kDecisionKw(LR"(\b(if|else\s+if|for|while|case|catch|elif|except)\b|(&&|\|\||\?))");

    for (const auto& line : lines) {
        std::wstring t = TrimW(line);
        if (t.empty()) {
            metrics.blankLines++;
            continue;
        }
        if (inBlockComment) {
            metrics.commentLines++;
            if (t.find(L"*/") != std::wstring::npos) {
                inBlockComment = false;
            }
            continue;
        }
        if (t.rfind(L"//", 0) == 0 || t.rfind(L"#", 0) == 0 || t.rfind(L"-- ", 0) == 0) {
            metrics.commentLines++;
            continue;
        }
        if (t.rfind(L"/*", 0) == 0) {
            metrics.commentLines++;
            if (t.find(L"*/") == std::wstring::npos) {
                inBlockComment = true;
            }
            continue;
        }

        metrics.codeLines++;

        for (wchar_t ch : line) {
            if (ch == L'{') {
                currentDepth++;
                if (currentDepth > metrics.maxNestingDepth) {
                    metrics.maxNestingDepth = currentDepth;
                }
            } else if (ch == L'}' && currentDepth > 0) {
                currentDepth--;
            }
        }

        auto begin = std::wsregex_iterator(line.begin(), line.end(), kDecisionKw);
        auto end = std::wsregex_iterator();
        for (auto it = begin; it != end; ++it) {
            metrics.cyclomaticComplexity++;
        }
    }

    auto syms = ExtractDocumentSymbols(text, ext);
    metrics.symbolCount = syms.size();
    for (const auto& s : syms) {
        if (s.kind == SymbolKind::Function || s.kind == SymbolKind::Method) {
            metrics.functionCount++;
        } else if (s.kind == SymbolKind::Class || s.kind == SymbolKind::Struct || s.kind == SymbolKind::Interface) {
            metrics.classCount++;
        }
    }

    return metrics;
}

WorkspaceCodeMetrics ComputeWorkspaceMetrics(
    const std::wstring& workspaceRoot,
    const phide::workspace::IgnoreEngine& ignoreEngine,
    const std::vector<OpenDocContentSnapshot>& openSnapshots,
    size_t maxFiles)
{
    WorkspaceCodeMetrics total;
    std::unordered_map<std::wstring, OpenDocContentSnapshot> openMap;
    for (const auto& snap : openSnapshots) {
        if (!snap.filePath.empty()) {
            openMap[NormalizePathKey(snap.filePath)] = snap;
        }
    }

    auto diskFiles = CollectWorkspaceSourceFiles(workspaceRoot, ignoreEngine, maxFiles);
    std::unordered_set<std::wstring> seen;
    std::vector<FileCodeMetrics> allFileMetrics;

    auto processOne = [&](const std::wstring& path, const std::wstring& text) {
        std::wstring ext = std::filesystem::path(path).extension().wstring();
        auto fm = ComputeFileMetrics(path, text, ext);
        total.totalFiles++;
        total.totalLines += fm.totalLines;
        total.codeLines += fm.codeLines;
        total.blankLines += fm.blankLines;
        total.commentLines += fm.commentLines;
        total.totalFunctions += fm.functionCount;
        total.totalClasses += fm.classCount;
        allFileMetrics.push_back(std::move(fm));
    };

    for (const auto& p : diskFiles) {
        std::wstring pStr = p.wstring();
        std::wstring key = NormalizePathKey(pStr);
        seen.insert(key);
        auto it = openMap.find(key);
        if (it != openMap.end()) {
            processOne(pStr, it->second.content);
        } else {
            std::wstring text;
            if (ReadUtf8TextFileBounded(p, text)) {
                processOne(pStr, text);
            }
        }
    }

    for (const auto& snap : openSnapshots) {
        std::wstring key = NormalizePathKey(snap.filePath);
        if (seen.count(key) == 0 && !snap.filePath.empty()) {
            processOne(snap.filePath, snap.content);
        }
    }

    if (!allFileMetrics.empty()) {
        size_t sumComp = 0;
        for (const auto& f : allFileMetrics) sumComp += f.cyclomaticComplexity;
        total.avgComplexity = (std::max)(size_t(1), sumComp / allFileMetrics.size());

        std::sort(allFileMetrics.begin(), allFileMetrics.end(),
                  [](const FileCodeMetrics& a, const FileCodeMetrics& b) {
                      if (a.cyclomaticComplexity != b.cyclomaticComplexity) {
                          return a.cyclomaticComplexity > b.cyclomaticComplexity;
                      }
                      return a.codeLines > b.codeLines;
                  });
        if (allFileMetrics.size() > 8) allFileMetrics.resize(8);
        total.topComplexFiles = std::move(allFileMetrics);
    }

    return total;
}

std::vector<ReferenceLocation> FindWorkspaceReferences(
    const std::wstring& symbolName,
    const std::wstring& workspaceRoot,
    const phide::workspace::IgnoreEngine& ignoreEngine,
    const std::vector<OpenDocContentSnapshot>& openSnapshots,
    size_t maxFiles,
    size_t maxResults)
{
    std::vector<ReferenceLocation> refs;
    if (symbolName.empty()) return refs;

    std::unordered_map<std::wstring, OpenDocContentSnapshot> openMap;
    for (const auto& snap : openSnapshots) {
        if (!snap.filePath.empty()) {
            openMap[NormalizePathKey(snap.filePath)] = snap;
        }
    }

    auto scanContent = [&](const std::wstring& fpath, const std::wstring& text) {
        if (refs.size() >= maxResults || text.find(symbolName) == std::wstring::npos) return;
        auto lines = SplitLines(text);
        for (size_t l = 0; l < lines.size() && refs.size() < maxResults; ++l) {
            const auto& line = lines[l];
            if (line.find(symbolName) == std::wstring::npos) continue;
            auto matches = FindMatchesInLine(line, symbolName, true, true, false);
            for (const auto& m : matches) {
                ReferenceLocation loc;
                loc.filePath = fpath;
                loc.line = l;
                loc.col = m.col;
                loc.length = m.length;
                loc.previewLine = TrimW(line);
                if (loc.previewLine.size() > 140) {
                    loc.previewLine = loc.previewLine.substr(0, 137) + L"...";
                }
                refs.push_back(std::move(loc));
                if (refs.size() >= maxResults) break;
            }
        }
    };

    std::unordered_set<std::wstring> seen;
    for (const auto& snap : openSnapshots) {
        if (!snap.filePath.empty()) {
            seen.insert(NormalizePathKey(snap.filePath));
            scanContent(snap.filePath, snap.content);
        }
    }

    auto diskFiles = CollectWorkspaceSourceFiles(workspaceRoot, ignoreEngine, maxFiles);
    for (const auto& p : diskFiles) {
        if (refs.size() >= maxResults) break;
        std::wstring pStr = p.wstring();
        std::wstring key = NormalizePathKey(pStr);
        if (seen.count(key) > 0) continue;
        std::wstring text;
        if (ReadUtf8TextFileBounded(p, text)) {
            scanContent(pStr, text);
        }
    }

    return refs;
}

std::vector<CallHierarchyNode> BuildStaticCallHierarchy(
    const std::wstring& symbolName,
    const std::wstring& activeFilePath,
    const std::wstring& workspaceRoot,
    const phide::workspace::IgnoreEngine& ignoreEngine,
    const std::vector<OpenDocContentSnapshot>& openSnapshots)
{
    std::vector<CallHierarchyNode> nodes;
    if (symbolName.empty()) return nodes;

    // 1. Outgoing callees from the body of `symbolName`
    std::wstring defText;
    for (const auto& snap : openSnapshots) {
        if (NormalizePathKey(snap.filePath) == NormalizePathKey(activeFilePath)) {
            defText = snap.content;
            break;
        }
    }
    if (defText.empty() && !activeFilePath.empty()) {
        ReadUtf8TextFileBounded(activeFilePath, defText);
    }

    if (!defText.empty()) {
        auto lines = SplitLines(defText);
        auto syms = ExtractDocumentSymbols(defText, std::filesystem::path(activeFilePath).extension().wstring());
        size_t fnStart = std::wstring::npos;
        size_t fnEnd = lines.size();
        for (size_t i = 0; i < syms.size(); ++i) {
            if (syms[i].name == symbolName || syms[i].name.ends_with(L"::" + symbolName)) {
                fnStart = syms[i].line;
                if (i + 1 < syms.size()) {
                    fnEnd = (std::min)(lines.size(), syms[i + 1].line);
                }
                break;
            }
        }
        if (fnStart != std::wstring::npos) {
            static const std::wregex kCallRe(LR"(\b([A-Za-z_][A-Za-z0-9_]*)\s*\()");
            std::unordered_set<std::wstring> seenCallees;
            static const std::unordered_set<std::wstring> kKeywords = {
                L"if", L"for", L"while", L"switch", L"catch", L"return", L"sizeof",
                L"decltype", L"static_cast", L"dynamic_cast", L"reinterpret_cast", L"const_cast"
            };
            for (size_t l = fnStart + 1; l < fnEnd && l < fnStart + 160; ++l) {
                auto begin = std::wsregex_iterator(lines[l].begin(), lines[l].end(), kCallRe);
                auto end = std::wsregex_iterator();
                for (auto it = begin; it != end; ++it) {
                    std::wstring callee = (*it)[1].str();
                    if (callee == symbolName || kKeywords.count(callee) > 0) continue;
                    if (seenCallees.insert(callee).second) {
                        nodes.push_back({ callee, L"Calls from " + symbolName, activeFilePath, l, static_cast<size_t>(it->position(1)), false });
                        if (seenCallees.size() >= 25) break;
                    }
                }
                if (seenCallees.size() >= 25) break;
            }
        }
    }

    // 2. Incoming callers across workspace references
    auto refs = FindWorkspaceReferences(symbolName, workspaceRoot, ignoreEngine, openSnapshots, 400, 120);
    std::unordered_map<std::wstring, std::vector<DocumentSymbol>> symCache;
    std::unordered_set<std::wstring> seenCallers;

    for (const auto& r : refs) {
        if (r.previewLine.find(symbolName + L"(") == std::wstring::npos &&
            r.previewLine.find(symbolName + L" (") == std::wstring::npos) {
            continue;
        }
        std::wstring key = NormalizePathKey(r.filePath);
        if (symCache.find(key) == symCache.end()) {
            std::wstring content;
            for (const auto& s : openSnapshots) {
                if (NormalizePathKey(s.filePath) == key) {
                    content = s.content;
                    break;
                }
            }
            if (content.empty()) ReadUtf8TextFileBounded(r.filePath, content);
            symCache[key] = ExtractDocumentSymbols(content, std::filesystem::path(r.filePath).extension().wstring());
        }
        std::wstring callerName = std::filesystem::path(r.filePath).filename().wstring();
        const auto& fSyms = symCache[key];
        for (auto it = fSyms.rbegin(); it != fSyms.rend(); ++it) {
            if (it->line <= r.line &&
                (it->kind == SymbolKind::Function || it->kind == SymbolKind::Method)) {
                callerName = it->name;
                break;
            }
        }
        if (callerName == symbolName && NormalizePathKey(r.filePath) == NormalizePathKey(activeFilePath)) {
            if (r.previewLine.rfind(L"void " + symbolName, 0) == 0 ||
                r.previewLine.rfind(L"def " + symbolName, 0) == 0 ||
                r.previewLine.rfind(L"function " + symbolName, 0) == 0) {
                continue;
            }
        }
        std::wstring dedup = callerName + L"|" + std::to_wstring(r.line);
        if (seenCallers.insert(dedup).second) {
            nodes.push_back({ callerName, L"Calls " + symbolName, r.filePath, r.line, r.col, true });
        }
        if (nodes.size() >= 60) break;
    }

    return nodes;
}

RenamePlan BuildStaticRenamePlan(
    const std::wstring& oldName,
    const std::wstring& newName,
    const std::wstring& workspaceRoot,
    const phide::workspace::IgnoreEngine& ignoreEngine,
    const std::vector<OpenDocContentSnapshot>& openSnapshots,
    size_t maxFiles)
{
    RenamePlan plan;
    plan.oldName = oldName;
    plan.newName = newName;
    if (oldName.empty() || !IsValidIdentifier(oldName)) {
        plan.errorMessage = L"Place cursor on a valid identifier to rename.";
        return plan;
    }
    if (newName.empty() || !IsValidIdentifier(newName)) {
        plan.errorMessage = L"Invalid identifier name: '" + newName + L"'.";
        return plan;
    }
    if (oldName == newName) {
        plan.errorMessage = L"New name matches the existing symbol name.";
        return plan;
    }

    std::unordered_set<std::wstring> dirtyKeys;
    for (const auto& s : openSnapshots) {
        if (s.isDirty && !s.filePath.empty()) {
            dirtyKeys.insert(NormalizePathKey(s.filePath));
        }
    }

    auto refs = FindWorkspaceReferences(oldName, workspaceRoot, ignoreEngine, openSnapshots, maxFiles, 2000);
    std::unordered_map<std::wstring, size_t> groupIdxByKey;

    for (auto& ref : refs) {
        std::wstring key = NormalizePathKey(ref.filePath);
        auto it = groupIdxByKey.find(key);
        if (it == groupIdxByKey.end()) {
            RenameFileEditGroup grp;
            grp.filePath = ref.filePath;
            grp.isDirtyBuffer = (dirtyKeys.count(key) > 0);
            groupIdxByKey[key] = plan.fileGroups.size();
            plan.fileGroups.push_back(std::move(grp));
            it = groupIdxByKey.find(key);
        }
        plan.fileGroups[it->second].edits.push_back(std::move(ref));
        plan.totalEdits++;
    }

    plan.valid = true;
    return plan;
}

std::vector<DiscoveredTask> DiscoverWorkspaceTasks(const std::wstring& workspaceRoot) {
    std::vector<DiscoveredTask> tasks;
    if (workspaceRoot.empty()) return tasks;
    std::error_code ec;
    std::filesystem::path root(workspaceRoot);
    if (!std::filesystem::exists(root, ec)) return tasks;

    // 1. package.json scripts
    {
        std::wstring pkgText;
        if (ReadUtf8TextFileBounded(root / L"package.json", pkgText, 512 * 1024)) {
            size_t scriptsPos = pkgText.find(L"\"scripts\"");
            if (scriptsPos != std::wstring::npos) {
                size_t braceStart = pkgText.find(L'{', scriptsPos);
                if (braceStart != std::wstring::npos) {
                    int depth = 0;
                    size_t braceEnd = braceStart;
                    for (; braceEnd < pkgText.size(); ++braceEnd) {
                        if (pkgText[braceEnd] == L'{') depth++;
                        else if (pkgText[braceEnd] == L'}') {
                            depth--;
                            if (depth == 0) break;
                        }
                    }
                    std::wstring block = pkgText.substr(braceStart, braceEnd - braceStart + 1);
                    static const std::wregex kScriptRe(LR"(\"([A-Za-z0-9_:\-\.]+)\"\s*:\s*\"([^\"]+)\")");
                    auto begin = std::wsregex_iterator(block.begin(), block.end(), kScriptRe);
                    auto end = std::wsregex_iterator();
                    for (auto it = begin; it != end; ++it) {
                        std::wstring name = (*it)[1].str();
                        std::wstring cmd = (*it)[2].str();
                        tasks.push_back({ L"npm: " + name, L"npm run " + name, cmd, false });
                    }
                }
            }
        }
    }

    // 2. composer.json scripts
    {
        std::wstring compText;
        if (ReadUtf8TextFileBounded(root / L"composer.json", compText, 512 * 1024)) {
            size_t scriptsPos = compText.find(L"\"scripts\"");
            if (scriptsPos != std::wstring::npos) {
                size_t braceStart = compText.find(L'{', scriptsPos);
                if (braceStart != std::wstring::npos) {
                    size_t braceEnd = compText.find(L'}', braceStart);
                    if (braceEnd != std::wstring::npos) {
                        std::wstring block = compText.substr(braceStart, braceEnd - braceStart + 1);
                        static const std::wregex kCompRe(LR"(\"([A-Za-z0-9_:\-\.]+)\"\s*:)");
                        auto begin = std::wsregex_iterator(block.begin(), block.end(), kCompRe);
                        auto end = std::wsregex_iterator();
                        for (auto it = begin; it != end; ++it) {
                            std::wstring name = (*it)[1].str();
                            tasks.push_back({ L"composer: " + name, L"composer run-script " + name, L"composer.json script", false });
                        }
                    }
                }
            }
        }
    }

    // 3. Makefile targets
    {
        std::wstring mkText;
        if (ReadUtf8TextFileBounded(root / L"Makefile", mkText, 256 * 1024)) {
            static const std::wregex kTargetRe(LR"(^([A-Za-z0-9_\-\.]+)\s*:)");
            for (const auto& line : SplitLines(mkText)) {
                std::wsmatch m;
                if (std::regex_search(line, m, kTargetRe) && m.size() >= 2) {
                    std::wstring tgt = m[1].str();
                    if (tgt[0] != L'.') {
                        tasks.push_back({ L"make: " + tgt, L"make " + tgt, L"Makefile target", false });
                    }
                }
            }
        }
    }

    // 4. scripts\*.bat / scripts\*.ps1
    {
        std::filesystem::path scriptsDir = root / L"scripts";
        if (std::filesystem::exists(scriptsDir, ec) && std::filesystem::is_directory(scriptsDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(scriptsDir, ec)) {
                if (entry.is_regular_file(ec)) {
                    std::wstring fn = entry.path().filename().wstring();
                    std::wstring ext = ToLowerW(entry.path().extension().wstring());
                    if (ext == L".bat" || ext == L".cmd") {
                        tasks.push_back({ L"script: " + fn, L"cmd /c scripts\\" + fn, L"Workspace batch script", false });
                    } else if (ext == L".ps1") {
                        tasks.push_back({ L"script: " + fn, L"powershell -File scripts\\" + fn, L"Workspace PowerShell script", false });
                    }
                }
            }
        }
    }

    // 5. .vscode/tasks.json labels
    {
        std::wstring vscTasks;
        if (ReadUtf8TextFileBounded(root / L".vscode" / L"tasks.json", vscTasks, 256 * 1024)) {
            static const std::wregex kTaskLabelRe(LR"(\"label\"\s*:\s*\"([^\"]+)\")");
            auto begin = std::wsregex_iterator(vscTasks.begin(), vscTasks.end(), kTaskLabelRe);
            auto end = std::wsregex_iterator();
            for (auto it = begin; it != end; ++it) {
                std::wstring lbl = (*it)[1].str();
                tasks.push_back({ L"task: " + lbl, lbl, L".vscode/tasks.json", false });
            }
        }
    }

    return tasks;
}

std::vector<DiscoveredTask> DiscoverLaunchConfigurations(const std::wstring& workspaceRoot) {
    std::vector<DiscoveredTask> cfgs;
    if (workspaceRoot.empty()) return cfgs;
    std::filesystem::path launchPath = std::filesystem::path(workspaceRoot) / L".vscode" / L"launch.json";
    std::wstring text;
    if (ReadUtf8TextFileBounded(launchPath, text, 256 * 1024)) {
        static const std::wregex kNameRe(LR"(\"name\"\s*:\s*\"([^\"]+)\")");
        auto begin = std::wsregex_iterator(text.begin(), text.end(), kNameRe);
        auto end = std::wsregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::wstring name = (*it)[1].str();
            cfgs.push_back({ L"Debug: " + name, L"cmd_start_debugging", L".vscode/launch.json", true });
        }
    }
    return cfgs;
}

WorkspaceSymbolIndex::~WorkspaceSymbolIndex() {
    if (m_alive) m_alive->store(false);
    m_generation.fetch_add(1);
}

void WorkspaceSymbolIndex::Clear() {
    m_generation.fetch_add(1);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_indexedRoot.clear();
    m_symbolsByFile.clear();
}

void WorkspaceSymbolIndex::RebuildSync(
    const std::wstring& workspaceRoot,
    const phide::workspace::IgnoreEngine* pIgnore,
    size_t maxFiles)
{
    uint64_t gen = m_generation.fetch_add(1) + 1;
    auto ignoreCopy = std::make_shared<phide::workspace::IgnoreEngine>();
    if (pIgnore) {
        ignoreCopy->CopyFrom(*pIgnore);
    } else if (!workspaceRoot.empty()) {
        ignoreCopy->LoadWorkspaceIgnoreFiles(workspaceRoot);
    }
    IndexRootInternal(gen, workspaceRoot, std::move(ignoreCopy), maxFiles, nullptr);
}

void WorkspaceSymbolIndex::RebuildAsync(
    const std::wstring& workspaceRoot,
    const phide::workspace::IgnoreEngine* pIgnore,
    std::function<void()> onCompleted,
    size_t maxFiles)
{
    uint64_t gen = m_generation.fetch_add(1) + 1;
    auto ignoreCopy = std::make_shared<phide::workspace::IgnoreEngine>();
    if (pIgnore) {
        ignoreCopy->CopyFrom(*pIgnore);
    } else if (!workspaceRoot.empty()) {
        ignoreCopy->LoadWorkspaceIgnoreFiles(workspaceRoot);
    }

    m_isIndexing.store(true);
    auto alive = m_alive;
    std::thread([this, alive, gen, workspaceRoot, ignoreCopy = std::move(ignoreCopy), maxFiles, onCompleted = std::move(onCompleted)]() mutable {
        if (!alive || !alive->load()) return;
        IndexRootInternal(gen, workspaceRoot, std::move(ignoreCopy), maxFiles, std::move(onCompleted));
    }).detach();
}

void WorkspaceSymbolIndex::IndexRootInternal(
    uint64_t generation,
    std::wstring workspaceRoot,
    std::shared_ptr<phide::workspace::IgnoreEngine> ignoreCopy,
    size_t maxFiles,
    std::function<void()> onCompleted)
{
    std::unordered_map<std::wstring, std::vector<IndexedSymbol>> newIndex;
    phide::workspace::IgnoreEngine fallbackIgnore;
    auto files = CollectWorkspaceSourceFiles(workspaceRoot, ignoreCopy ? *ignoreCopy : fallbackIgnore, maxFiles);

    for (const auto& p : files) {
        if (m_generation.load() != generation || (m_alive && !m_alive->load())) {
            m_isIndexing.store(false);
            return;
        }
        std::wstring text;
        if (!ReadUtf8TextFileBounded(p, text, 512 * 1024)) continue;

        std::wstring pStr = p.wstring();
        auto docSyms = ExtractDocumentSymbols(text, p.extension().wstring());
        if (!docSyms.empty()) {
            std::vector<IndexedSymbol> list;
            list.reserve(docSyms.size());
            for (auto& ds : docSyms) {
                IndexedSymbol is;
                is.name = std::move(ds.name);
                is.detail = std::move(ds.detail);
                is.filePath = pStr;
                is.kind = ds.kind;
                is.line = ds.line;
                is.col = ds.col;
                list.push_back(std::move(is));
            }
            newIndex[NormalizePathKey(pStr)] = std::move(list);
        }
    }

    if (m_generation.load() == generation && (!m_alive || m_alive->load())) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_indexedRoot = std::move(workspaceRoot);
        m_symbolsByFile = std::move(newIndex);
    }
    m_isIndexing.store(false);
    if (onCompleted && (!m_alive || m_alive->load())) {
        onCompleted();
    }
}

void WorkspaceSymbolIndex::UpdateFileSymbols(const std::wstring& filePath, const std::wstring& text) {
    if (filePath.empty()) return;
    std::wstring ext = std::filesystem::path(filePath).extension().wstring();
    auto docSyms = ExtractDocumentSymbols(text, ext);

    std::vector<IndexedSymbol> list;
    list.reserve(docSyms.size());
    for (auto& ds : docSyms) {
        IndexedSymbol is;
        is.name = std::move(ds.name);
        is.detail = std::move(ds.detail);
        is.filePath = filePath;
        is.kind = ds.kind;
        is.line = ds.line;
        is.col = ds.col;
        list.push_back(std::move(is));
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    std::wstring key = NormalizePathKey(filePath);
    if (list.empty()) {
        m_symbolsByFile.erase(key);
    } else {
        m_symbolsByFile[key] = std::move(list);
    }
}

std::vector<IndexedSymbol> WorkspaceSymbolIndex::SearchSymbols(const std::wstring& query, size_t maxResults) const {
    struct Scored {
        IndexedSymbol sym;
        int score = 0;
    };
    std::vector<Scored> candidates;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [key, syms] : m_symbolsByFile) {
            (void)key;
            for (const auto& s : syms) {
                std::wstring fn = std::filesystem::path(s.filePath).filename().wstring();
                int sc = ScoreFuzzyMatch(query, s.name, fn, false, false);
                if (sc >= 0) {
                    candidates.push_back({ s, sc });
                }
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.sym.name.size() < b.sym.name.size();
    });

    std::vector<IndexedSymbol> out;
    size_t limit = (std::min)(maxResults, candidates.size());
    out.reserve(limit);
    for (size_t i = 0; i < limit; ++i) {
        out.push_back(std::move(candidates[i].sym));
    }
    return out;
}

std::vector<IndexedSymbol> WorkspaceSymbolIndex::FindDefinitions(
    const std::wstring& symbolName,
    const std::wstring& preferFilePath) const
{
    std::vector<IndexedSymbol> matches;
    if (symbolName.empty()) return matches;

    std::wstring preferKey = NormalizePathKey(preferFilePath);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& [key, syms] : m_symbolsByFile) {
        for (const auto& s : syms) {
            if (s.name == symbolName || s.name.ends_with(L"::" + symbolName)) {
                matches.push_back(s);
            }
        }
    }

    std::sort(matches.begin(), matches.end(), [&](const IndexedSymbol& a, const IndexedSymbol& b) {
        bool aPref = (!preferKey.empty() && NormalizePathKey(a.filePath) == preferKey);
        bool bPref = (!preferKey.empty() && NormalizePathKey(b.filePath) == preferKey);
        if (aPref != bPref) return aPref;
        bool aExact = (a.name == symbolName);
        bool bExact = (b.name == symbolName);
        if (aExact != bExact) return aExact;
        return a.line < b.line;
    });
    return matches;
}

size_t WorkspaceSymbolIndex::GetSymbolCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& [k, v] : m_symbolsByFile) {
        (void)k;
        count += v.size();
    }
    return count;
}

size_t WorkspaceSymbolIndex::GetIndexedFileCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_symbolsByFile.size();
}

std::wstring WorkspaceSymbolIndex::GetIndexedRoot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_indexedRoot;
}

} // namespace phide::editor
