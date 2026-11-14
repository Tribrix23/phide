/**
 * @file WorkspaceConfigEngine.cpp
 * @brief Implements hierarchical .editorconfig resolution, multi-root .phide-workspace
 *        persistence, and per-workspace .phide/settings.json with automatic ignore-file hygiene.
 */

#include "phide/workspace/WorkspaceConfigEngine.hpp"
#include "phide/workspace/IgnoreEngine.hpp"
#include "phide/editor/Buffer.hpp"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace phide::workspace {

namespace {

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) {
        n = ::MultiByteToWideChar(CP_ACP, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (n <= 0) return L"";
        std::wstring out(static_cast<size_t>(n), L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], n);
        return out;
    }
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], n);
    return out;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring TrimWide(const std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

std::wstring ToLowerWide(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(::towlower(c));
    return s;
}

std::wstring ToPosixSlashes(std::wstring p) {
    std::replace(p.begin(), p.end(), L'\\', L'/');
    return p;
}

// Recursive glob matcher supporting *, **, ?, [abc], [!abc], and backslash escapes
bool MatchGlobSimple(const std::wstring& pat, size_t pi, const std::wstring& text, size_t ti) {
    while (pi < pat.size()) {
        wchar_t pc = pat[pi];
        if (pc == L'\\' && pi + 1 < pat.size()) {
            if (ti >= text.size() || pat[pi + 1] != text[ti]) return false;
            pi += 2;
            ++ti;
            continue;
        }
        if (pc == L'*') {
            bool doubleStar = (pi + 1 < pat.size() && pat[pi + 1] == L'*');
            while (pi < pat.size() && pat[pi] == L'*') ++pi;
            // Collapse "**/" optional zero-directory match
            if (doubleStar && pi < pat.size() && pat[pi] == L'/') {
                if (MatchGlobSimple(pat, pi + 1, text, ti)) return true;
            }
            for (size_t k = ti; k <= text.size(); ++k) {
                if (!doubleStar && k > ti && text[k - 1] == L'/') break;
                if (MatchGlobSimple(pat, pi, text, k)) return true;
            }
            return false;
        }
        if (pc == L'?') {
            if (ti >= text.size() || text[ti] == L'/') return false;
            ++pi;
            ++ti;
            continue;
        }
        if (pc == L'[') {
            if (ti >= text.size() || text[ti] == L'/') return false;
            size_t close = pi + 1;
            bool negate = false;
            if (close < pat.size() && (pat[close] == L'!' || pat[close] == L'^')) {
                negate = true;
                ++close;
            }
            bool matchedChar = false;
            while (close < pat.size() && pat[close] != L']') {
                if (close + 2 < pat.size() && pat[close + 1] == L'-' && pat[close + 2] != L']') {
                    wchar_t lo = pat[close];
                    wchar_t hi = pat[close + 2];
                    if (text[ti] >= lo && text[ti] <= hi) matchedChar = true;
                    close += 3;
                } else {
                    if (pat[close] == text[ti]) matchedChar = true;
                    ++close;
                }
            }
            if (close >= pat.size() || pat[close] != L']') {
                // Unclosed '[', treat literally
                if (ti >= text.size() || text[ti] != L'[') return false;
                ++pi;
                ++ti;
                continue;
            }
            if (negate ? matchedChar : !matchedChar) return false;
            pi = close + 1;
            ++ti;
            continue;
        }
        if (ti >= text.size() || pc != text[ti]) return false;
        ++pi;
        ++ti;
    }
    return ti == text.size();
}

// Expand top-level {a,b,{c,d}} and {1..5} brace groups into alternatives
void ExpandBraces(const std::wstring& pat, std::vector<std::wstring>& out) {
    int depth = 0;
    size_t openPos = std::wstring::npos;
    size_t closePos = std::wstring::npos;
    for (size_t i = 0; i < pat.size(); ++i) {
        if (pat[i] == L'\\' && i + 1 < pat.size()) {
            ++i;
            continue;
        }
        if (pat[i] == L'{') {
            if (depth == 0) openPos = i;
            ++depth;
        } else if (pat[i] == L'}') {
            if (depth > 0) {
                --depth;
                if (depth == 0 && openPos != std::wstring::npos) {
                    closePos = i;
                    break;
                }
            }
        }
    }
    if (openPos == std::wstring::npos || closePos == std::wstring::npos) {
        out.push_back(pat);
        return;
    }

    std::wstring prefix = pat.substr(0, openPos);
    std::wstring inner = pat.substr(openPos + 1, closePos - openPos - 1);
    std::wstring suffix = pat.substr(closePos + 1);

    // Numeric range {a..b}
    size_t dots = inner.find(L"..");
    if (dots != std::wstring::npos && inner.find(L',') == std::wstring::npos && inner.find(L'{') == std::wstring::npos) {
        try {
            long long lo = std::stoll(inner.substr(0, dots));
            long long hi = std::stoll(inner.substr(dots + 2));
            long long step = (lo <= hi) ? 1 : -1;
            int count = 0;
            for (long long v = lo; (step > 0 ? v <= hi : v >= hi) && count < 64; v += step, ++count) {
                ExpandBraces(prefix + std::to_wstring(v) + suffix, out);
            }
            return;
        } catch (...) {
        }
    }

    // Comma-separated list inside {...}
    std::vector<std::wstring> parts;
    int subDepth = 0;
    size_t start = 0;
    for (size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == L'\\' && i + 1 < inner.size()) {
            ++i;
            continue;
        }
        if (inner[i] == L'{') ++subDepth;
        else if (inner[i] == L'}' && subDepth > 0) --subDepth;
        else if (inner[i] == L',' && subDepth == 0) {
            parts.push_back(inner.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.push_back(inner.substr(start));

    if (parts.size() <= 1) {
        // Not a brace alternation; treat literal braces
        std::vector<std::wstring> tailList;
        ExpandBraces(suffix, tailList);
        for (const auto& t : tailList) {
            out.push_back(prefix + L"{" + inner + L"}" + t);
        }
        return;
    }

    for (const auto& part : parts) {
        ExpandBraces(prefix + part + suffix, out);
        if (out.size() > 256) break;
    }
}

std::wstring EscapeJsonWide(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 8);
    for (wchar_t ch : s) {
        switch (ch) {
            case L'\\': out += L"\\\\"; break;
            case L'"':  out += L"\\\""; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:    out += ch; break;
        }
    }
    return out;
}

std::wstring StripJsonComments(const std::wstring& src) {
    std::wstring out;
    out.reserve(src.size());
    bool inString = false;
    bool inLineComment = false;
    bool inBlockComment = false;
    for (size_t i = 0; i < src.size(); ++i) {
        wchar_t c = src[i];
        if (inLineComment) {
            if (c == L'\n') {
                inLineComment = false;
                out += c;
            }
            continue;
        }
        if (inBlockComment) {
            if (c == L'*' && i + 1 < src.size() && src[i + 1] == L'/') {
                inBlockComment = false;
                ++i;
            }
            continue;
        }
        if (inString) {
            out += c;
            if (c == L'\\' && i + 1 < src.size()) {
                out += src[++i];
            } else if (c == L'"') {
                inString = false;
            }
            continue;
        }
        if (c == L'"') {
            inString = true;
            out += c;
        } else if (c == L'/' && i + 1 < src.size() && src[i + 1] == L'/') {
            inLineComment = true;
            ++i;
        } else if (c == L'/' && i + 1 < src.size() && src[i + 1] == L'*') {
            inBlockComment = true;
            ++i;
        } else {
            out += c;
        }
    }
    return out;
}

void SkipWs(const std::wstring& s, size_t& i) {
    while (i < s.size() && iswspace(s[i])) ++i;
}

bool ParseJsonString(const std::wstring& s, size_t& i, std::wstring& out) {
    SkipWs(s, i);
    if (i >= s.size() || s[i] != L'"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        wchar_t c = s[i++];
        if (c == L'"') return true;
        if (c == L'\\' && i < s.size()) {
            wchar_t esc = s[i++];
            switch (esc) {
                case L'"':  out += L'"'; break;
                case L'\\': out += L'\\'; break;
                case L'/':  out += L'/'; break;
                case L'n':  out += L'\n'; break;
                case L'r':  out += L'\r'; break;
                case L't':  out += L'\t'; break;
                default:    out += esc; break;
            }
        } else {
            out += c;
        }
    }
    return false;
}

bool ParseJsonScalarAsString(const std::wstring& s, size_t& i, std::wstring& out) {
    SkipWs(s, i);
    if (i >= s.size()) return false;
    if (s[i] == L'"') {
        return ParseJsonString(s, i, out);
    }
    size_t start = i;
    while (i < s.size() && s[i] != L',' && s[i] != L'}' && s[i] != L']' && !iswspace(s[i])) {
        ++i;
    }
    if (i == start) return false;
    out = s.substr(start, i - start);
    return true;
}

void SkipJsonValue(const std::wstring& s, size_t& i) {
    SkipWs(s, i);
    if (i >= s.size()) return;
    if (s[i] == L'"') {
        std::wstring dummy;
        ParseJsonString(s, i, dummy);
        return;
    }
    if (s[i] == L'{' || s[i] == L'[') {
        wchar_t openCh = s[i];
        wchar_t closeCh = (openCh == L'{') ? L'}' : L']';
        int depth = 0;
        bool inStr = false;
        while (i < s.size()) {
            wchar_t c = s[i++];
            if (inStr) {
                if (c == L'\\' && i < s.size()) ++i;
                else if (c == L'"') inStr = false;
            } else {
                if (c == L'"') inStr = true;
                else if (c == openCh) ++depth;
                else if (c == closeCh) {
                    --depth;
                    if (depth == 0) break;
                }
            }
        }
        return;
    }
    std::wstring dummy;
    ParseJsonScalarAsString(s, i, dummy);
}

bool ReadUtf8TextFile(const fs::path& p, std::wstring& outContent) {
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string raw = ss.str();
    if (raw.size() >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw = raw.substr(3);
    }
    outContent = Utf8ToWide(raw);
    return true;
}

bool WriteUtf8TextFile(const fs::path& p, const std::wstring& content) {
    std::error_code ec;
    if (!p.parent_path().empty()) {
        fs::create_directories(p.parent_path(), ec);
    }
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    std::string u8 = WideToUtf8(content);
    out.write(u8.data(), static_cast<std::streamsize>(u8.size()));
    return out.good();
}

} // namespace

bool WorkspaceConfigEngine::MatchEditorConfigGlob(const std::wstring& pattern, const std::wstring& relPosixPath) {
    if (pattern.empty()) return false;
    std::wstring cleanPat = ToPosixSlashes(pattern);
    if (cleanPat.front() == L'/') {
        cleanPat = cleanPat.substr(1);
    }
    std::wstring target = ToPosixSlashes(relPosixPath);
    if (!target.empty() && target.front() == L'/') {
        target = target.substr(1);
    }

    // Per EditorConfig spec: if pattern has no '/', match against filename only
    if (cleanPat.find(L'/') == std::wstring::npos) {
        size_t slash = target.find_last_of(L'/');
        if (slash != std::wstring::npos) {
            target = target.substr(slash + 1);
        }
    }

    std::vector<std::wstring> expanded;
    ExpandBraces(cleanPat, expanded);
    for (const auto& alt : expanded) {
        if (MatchGlobSimple(alt, 0, target, 0)) {
            return true;
        }
    }
    return false;
}

EditorConfigRules WorkspaceConfigEngine::ResolveEditorConfigForFile(const std::wstring& filePath) {
    EditorConfigRules rules;
    if (filePath.empty()) return rules;

    std::error_code ec;
    fs::path absTarget = fs::absolute(fs::path(filePath), ec);
    if (ec) absTarget = fs::path(filePath);
    absTarget = absTarget.lexically_normal();

    struct LayerFile {
        fs::path baseDir;
        fs::path ecPath;
    };
    std::vector<LayerFile> layers;

    fs::path dir = absTarget.parent_path();
    for (int guard = 0; guard < 64 && !dir.empty(); ++guard) {
        fs::path candidate = dir / L".editorconfig";
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            layers.push_back({ dir, candidate });
            std::wstring text;
            if (ReadUtf8TextFile(candidate, text)) {
                std::wistringstream wss(text);
                std::wstring line;
                bool stoppedForRoot = false;
                while (std::getline(wss, line)) {
                    std::wstring t = TrimWide(line);
                    if (t.empty() || t[0] == L'#' || t[0] == L';') continue;
                    if (t[0] == L'[') break;
                    size_t eq = t.find(L'=');
                    if (eq != std::wstring::npos) {
                        std::wstring k = ToLowerWide(TrimWide(t.substr(0, eq)));
                        std::wstring v = ToLowerWide(TrimWide(t.substr(eq + 1)));
                        if (k == L"root" && v == L"true") {
                            stoppedForRoot = true;
                            break;
                        }
                    }
                }
                if (stoppedForRoot) break;
            }
        }
        fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }

    if (layers.empty()) return rules;

    // Apply from root-most ancestor down to file-nearest directory
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        std::wstring text;
        if (!ReadUtf8TextFile(it->ecPath, text)) continue;

        fs::path relPath = absTarget.lexically_relative(it->baseDir);
        std::wstring relPosix = ToPosixSlashes(relPath.wstring());

        std::wistringstream wss(text);
        std::wstring rawLine;
        bool inSection = false;
        bool sectionMatches = false;
        std::wstring pendingIndentSize;

        auto flushSectionDerived = [&]() {
            if (!pendingIndentSize.empty()) {
                if (pendingIndentSize == L"tab") {
                    rules.hasIndentSize = true;
                    rules.indentSize = rules.hasTabWidth ? rules.tabWidth : 4;
                }
                pendingIndentSize.clear();
            }
        };

        while (std::getline(wss, rawLine)) {
            std::wstring t = TrimWide(rawLine);
            if (t.empty() || t[0] == L'#' || t[0] == L';') continue;

            if (t.front() == L'[' && t.back() == L']' && t.size() >= 2) {
                flushSectionDerived();
                inSection = true;
                std::wstring secGlob = TrimWide(t.substr(1, t.size() - 2));
                sectionMatches = MatchEditorConfigGlob(secGlob, relPosix);
                if (sectionMatches) rules.foundAny = true;
                continue;
            }

            if (!inSection || !sectionMatches) continue;

            size_t eq = t.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring key = ToLowerWide(TrimWide(t.substr(0, eq)));
            std::wstring val = ToLowerWide(TrimWide(t.substr(eq + 1)));
            // Strip inline comments only if preceded by whitespace (safe simple check)
            size_t hashPos = val.find(L" #");
            if (hashPos != std::wstring::npos) val = TrimWide(val.substr(0, hashPos));

            if (key == L"indent_style") {
                if (val == L"tab" || val == L"space") {
                    rules.hasIndentStyle = true;
                    rules.indentStyleTabs = (val == L"tab");
                }
            } else if (key == L"indent_size") {
                if (val == L"tab") {
                    pendingIndentSize = L"tab";
                } else {
                    try {
                        int sz = std::stoi(val);
                        if (sz > 0 && sz <= 16) {
                            rules.hasIndentSize = true;
                            rules.indentSize = sz;
                            if (!rules.hasTabWidth) rules.tabWidth = sz;
                        }
                    } catch (...) {}
                }
            } else if (key == L"tab_width") {
                try {
                    int tw = std::stoi(val);
                    if (tw > 0 && tw <= 16) {
                        rules.hasTabWidth = true;
                        rules.tabWidth = tw;
                    }
                } catch (...) {}
            } else if (key == L"end_of_line") {
                if (val == L"lf") {
                    rules.hasEndOfLine = true;
                    rules.endOfLine = L"\n";
                } else if (val == L"crlf") {
                    rules.hasEndOfLine = true;
                    rules.endOfLine = L"\r\n";
                } else if (val == L"cr") {
                    rules.hasEndOfLine = true;
                    rules.endOfLine = L"\r";
                }
            } else if (key == L"trim_trailing_whitespace") {
                if (val == L"true" || val == L"false") {
                    rules.hasTrimTrailingWhitespace = true;
                    rules.trimTrailingWhitespace = (val == L"true");
                }
            } else if (key == L"insert_final_newline") {
                if (val == L"true" || val == L"false") {
                    rules.hasInsertFinalNewline = true;
                    rules.insertFinalNewline = (val == L"true");
                }
            } else if (key == L"charset") {
                if (!val.empty()) {
                    rules.hasCharset = true;
                    rules.charset = val;
                }
            }
        }
        flushSectionDerived();
    }

    return rules;
}

std::wstring WorkspaceConfigEngine::ApplyEditorConfigToSaveText(
    const std::wstring& rawText,
    const EditorConfigRules& rules,
    std::wstring& inOutLineEnding,
    bool& inOutHasTrailingNewline) {
    if (rules.hasEndOfLine) {
        inOutLineEnding = rules.endOfLine;
    }
    if (rules.hasInsertFinalNewline) {
        inOutHasTrailingNewline = rules.insertFinalNewline;
    }

    std::vector<std::wstring> lines;
    std::wstring cur;
    for (size_t i = 0; i < rawText.size(); ++i) {
        wchar_t c = rawText[i];
        if (c == L'\r') {
            lines.push_back(cur);
            cur.clear();
            if (i + 1 < rawText.size() && rawText[i + 1] == L'\n') ++i;
        } else if (c == L'\n') {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    bool inputEndedWithNewline = (!rawText.empty() && (rawText.back() == L'\n' || rawText.back() == L'\r'));
    if (!cur.empty() || !inputEndedWithNewline) {
        lines.push_back(cur);
    }

    if (rules.hasTrimTrailingWhitespace && rules.trimTrailingWhitespace) {
        for (auto& ln : lines) {
            size_t endPos = ln.find_last_not_of(L" \t");
            ln.resize((endPos == std::wstring::npos) ? 0 : (endPos + 1));
        }
    }

    std::wstring out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size()) {
            out += inOutLineEnding;
        }
    }
    if (inOutHasTrailingNewline && !out.empty()) {
        out += inOutLineEnding;
    } else if (inOutHasTrailingNewline && lines.size() == 1 && lines[0].empty() && inputEndedWithNewline) {
        out += inOutLineEnding;
    }
    return out;
}

bool WorkspaceConfigEngine::ApplyEditorConfigToBufferForSave(editor::Buffer& buffer, const EditorConfigRules& rules) {
    if (!rules.foundAny || buffer.IsMemoryMappedReadOnly()) return false;
    bool changed = false;
    if (rules.hasTrimTrailingWhitespace && rules.trimTrailingWhitespace && buffer.GetLineCount() > 0) {
        if (buffer.TrimTrailingWhitespace(0, buffer.GetLineCount() - 1)) {
            changed = true;
        }
    }
    if (rules.hasEndOfLine && buffer.GetLineEnding() != rules.endOfLine) {
        buffer.SetLineEnding(rules.endOfLine);
        changed = true;
    }
    if (rules.hasInsertFinalNewline && buffer.HasTrailingNewline() != rules.insertFinalNewline) {
        buffer.SetHasTrailingNewline(rules.insertFinalNewline);
        changed = true;
    }
    return changed;
}

bool WorkspaceConfigEngine::LoadMultiRootWorkspace(const std::wstring& workspaceFilePath, MultiRootWorkspace& outWs) {
    outWs = MultiRootWorkspace{};
    outWs.workspaceFilePath = workspaceFilePath;

    std::wstring rawContent;
    if (!ReadUtf8TextFile(fs::path(workspaceFilePath), rawContent)) {
        return false;
    }
    std::wstring json = StripJsonComments(rawContent);
    fs::path baseDir = fs::path(workspaceFilePath).parent_path();

    size_t i = 0;
    SkipWs(json, i);
    if (i >= json.size() || json[i] != L'{') return false;
    ++i;

    while (i < json.size()) {
        SkipWs(json, i);
        if (i < json.size() && json[i] == L'}') break;
        if (i < json.size() && json[i] == L',') { ++i; continue; }

        std::wstring key;
        if (!ParseJsonString(json, i, key)) break;
        SkipWs(json, i);
        if (i >= json.size() || json[i] != L':') break;
        ++i;
        SkipWs(json, i);

        if (key == L"folders" && i < json.size() && json[i] == L'[') {
            ++i;
            while (i < json.size()) {
                SkipWs(json, i);
                if (i < json.size() && json[i] == L']') { ++i; break; }
                if (i < json.size() && json[i] == L',') { ++i; continue; }
                if (json[i] == L'{') {
                    ++i;
                    WorkspaceFolderEntry entry;
                    while (i < json.size()) {
                        SkipWs(json, i);
                        if (i < json.size() && json[i] == L'}') { ++i; break; }
                        if (i < json.size() && json[i] == L',') { ++i; continue; }
                        std::wstring fKey;
                        if (!ParseJsonString(json, i, fKey)) break;
                        SkipWs(json, i);
                        if (i >= json.size() || json[i] != L':') break;
                        ++i;
                        std::wstring fVal;
                        if (!ParseJsonScalarAsString(json, i, fVal)) {
                            SkipJsonValue(json, i);
                            continue;
                        }
                        if (fKey == L"path") entry.path = fVal;
                        else if (fKey == L"name") entry.name = fVal;
                    }
                    if (!entry.path.empty()) {
                        std::error_code ec;
                        fs::path fp(entry.path);
                        if (fp.is_relative() && !baseDir.empty()) {
                            fp = baseDir / fp;
                        }
                        fs::path resolved = fs::weakly_canonical(fp, ec);
                        entry.path = (ec ? fp.lexically_normal() : resolved).wstring();
                        if (entry.name.empty()) {
                            entry.name = fs::path(entry.path).filename().wstring();
                            if (entry.name.empty()) entry.name = entry.path;
                        }
                        outWs.folders.push_back(std::move(entry));
                    }
                } else {
                    SkipJsonValue(json, i);
                }
            }
        } else if (key == L"settings" && i < json.size() && json[i] == L'{') {
            ++i;
            while (i < json.size()) {
                SkipWs(json, i);
                if (i < json.size() && json[i] == L'}') { ++i; break; }
                if (i < json.size() && json[i] == L',') { ++i; continue; }
                std::wstring sKey;
                if (!ParseJsonString(json, i, sKey)) break;
                SkipWs(json, i);
                if (i >= json.size() || json[i] != L':') break;
                ++i;
                std::wstring sVal;
                if (ParseJsonScalarAsString(json, i, sVal)) {
                    outWs.settings[sKey] = sVal;
                } else {
                    SkipJsonValue(json, i);
                }
            }
        } else {
            SkipJsonValue(json, i);
        }
    }

    return true;
}

bool WorkspaceConfigEngine::SaveMultiRootWorkspace(const std::wstring& workspaceFilePath, const MultiRootWorkspace& ws) {
    if (workspaceFilePath.empty()) return false;
    fs::path wsPath(workspaceFilePath);
    fs::path baseDir = wsPath.parent_path();

    std::wostringstream out;
    out << L"{\n\t\"folders\": [\n";
    for (size_t idx = 0; idx < ws.folders.size(); ++idx) {
        const auto& f = ws.folders[idx];
        std::wstring savePath = f.path;
        std::error_code ec;
        if (!baseDir.empty()) {
            fs::path rel = fs::path(f.path).lexically_relative(baseDir);
            std::wstring relStr = rel.wstring();
            if (!relStr.empty() && relStr.rfind(L"..\\..\\..", 0) != 0 && relStr.find(L':') == std::wstring::npos) {
                savePath = ToPosixSlashes(relStr);
            }
        }
        out << L"\t\t{\n";
        if (!f.name.empty()) {
            out << L"\t\t\t\"name\": \"" << EscapeJsonWide(f.name) << L"\",\n";
        }
        out << L"\t\t\t\"path\": \"" << EscapeJsonWide(savePath) << L"\"\n";
        out << L"\t\t}" << (idx + 1 < ws.folders.size() ? L"," : L"") << L"\n";
    }
    out << L"\t],\n\t\"settings\": {\n";
    size_t sIdx = 0;
    for (const auto& [k, v] : ws.settings) {
        out << L"\t\t\"" << EscapeJsonWide(k) << L"\": \"" << EscapeJsonWide(v) << L"\"";
        if (++sIdx < ws.settings.size()) out << L",";
        out << L"\n";
    }
    out << L"\t}\n}\n";

    return WriteUtf8TextFile(wsPath, out.str());
}

std::wstring WorkspaceConfigEngine::GetWorkspaceSettingsPath(const std::wstring& workspaceRoot) {
    if (workspaceRoot.empty()) return L"";
    return (fs::path(workspaceRoot) / L".phide" / L"settings.json").wstring();
}

bool WorkspaceConfigEngine::LoadWorkspaceSettings(const std::wstring& workspaceRoot, WorkspaceSettingsOverride& outSettings) {
    outSettings.values.clear();
    if (workspaceRoot.empty()) return false;
    std::wstring path = GetWorkspaceSettingsPath(workspaceRoot);
    std::wstring rawContent;
    if (!ReadUtf8TextFile(fs::path(path), rawContent)) return false;

    std::wstring json = StripJsonComments(rawContent);
    size_t i = 0;
    SkipWs(json, i);
    if (i >= json.size() || json[i] != L'{') return false;
    ++i;
    while (i < json.size()) {
        SkipWs(json, i);
        if (i < json.size() && json[i] == L'}') break;
        if (i < json.size() && json[i] == L',') { ++i; continue; }
        std::wstring k;
        if (!ParseJsonString(json, i, k)) break;
        SkipWs(json, i);
        if (i >= json.size() || json[i] != L':') break;
        ++i;
        std::wstring v;
        if (ParseJsonScalarAsString(json, i, v)) {
            outSettings.values[k] = v;
        } else {
            SkipJsonValue(json, i);
        }
    }
    return true;
}

bool WorkspaceConfigEngine::SaveWorkspaceSettings(const std::wstring& workspaceRoot, const WorkspaceSettingsOverride& settings) {
    if (workspaceRoot.empty()) return false;
    // Always ensure .phide is added to .gitignore (creating it if absent) and other ignore files
    IgnoreEngine::EnsurePhideIgnored(workspaceRoot);

    std::wstring path = GetWorkspaceSettingsPath(workspaceRoot);
    std::wostringstream out;
    out << L"{\n";
    size_t idx = 0;
    for (const auto& [k, v] : settings.values) {
        out << L"\t\"" << EscapeJsonWide(k) << L"\": \"" << EscapeJsonWide(v) << L"\"";
        if (++idx < settings.values.size()) out << L",";
        out << L"\n";
    }
    out << L"}\n";
    return WriteUtf8TextFile(fs::path(path), out.str());
}

} // namespace phide::workspace
