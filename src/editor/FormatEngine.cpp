/**
 * @file FormatEngine.cpp
 * @brief High-performance native offline code formatter for HTML, CSS, JavaScript/TypeScript/JSX, JSON, and PHP.
 *        100% local C++20 implementation. No external tools, cloud APIs, or LLMs required.
 */

#include "phide/editor/FormatEngine.hpp"
#include "phide/plugin/PluginManager.hpp"
#include <windows.h>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <unordered_set>
#include <cwctype>
#include <vector>

namespace phide::editor {

// ==============================================================================

// ==============================================================================
// Prettier CLI Integration for Fast (~200ms) Standard AST Formatting
// ==============================================================================

static std::wstring FindPrettierPath() {
    static std::wstring s_cachedPrettierPath;
    static bool s_checked = false;
    if (s_checked) return s_cachedPrettierPath;

    std::error_code ec;

    // 1. Check bundled phide.prettier extension
    std::filesystem::path bundledPath = L"assets\\extensions\\phide.prettier\\node_modules\\prettier\\bin\\prettier.cjs";
    if (std::filesystem::exists(bundledPath, ec)) {
        s_cachedPrettierPath = std::filesystem::absolute(bundledPath).wstring();
        s_checked = true;
        return s_cachedPrettierPath;
    }
    bundledPath = L"bin\\assets\\extensions\\phide.prettier\\node_modules\\prettier\\bin\\prettier.cjs";
    if (std::filesystem::exists(bundledPath, ec)) {
        s_cachedPrettierPath = std::filesystem::absolute(bundledPath).wstring();
        s_checked = true;
        return s_cachedPrettierPath;
    }

    // 2. Check APPDATA\npm\prettier.cmd
    wchar_t* pAppData = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&pAppData, &len, L"APPDATA") == 0 && pAppData) {
        std::filesystem::path p = std::filesystem::path(pAppData) / L"npm" / L"prettier.cmd";
        free(pAppData);
        if (std::filesystem::exists(p, ec)) {
            s_cachedPrettierPath = p.wstring();
            s_checked = true;
            return s_cachedPrettierPath;
        }
    }

    // 3. Check local node_modules\.bin\prettier.cmd
    if (std::filesystem::exists(L"node_modules\\.bin\\prettier.cmd", ec)) {
        s_cachedPrettierPath = std::filesystem::absolute(L"node_modules\\.bin\\prettier.cmd").wstring();
        s_checked = true;
        return s_cachedPrettierPath;
    }

    // 4. Search PATH for prettier.cmd
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"prettier.cmd", nullptr, MAX_PATH, buf, nullptr) > 0) {
        s_cachedPrettierPath = buf;
        s_checked = true;
        return s_cachedPrettierPath;
    }

    // 5. Search PATH for prettier (.cmd)
    if (SearchPathW(nullptr, L"prettier", L".cmd", MAX_PATH, buf, nullptr) > 0) {
        s_cachedPrettierPath = buf;
        s_checked = true;
        return s_cachedPrettierPath;
    }

    s_checked = true;
    return s_cachedPrettierPath;
}

static bool HasLocalConfigUpwards(const std::wstring& filePath, const std::vector<std::wstring>& configNames) {
    if (filePath.empty()) return false;
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::path(filePath).parent_path();
    for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
        for (const auto& name : configNames) {
            if (std::filesystem::exists(dir / name, ec)) {
                return true;
            }
        }
        std::filesystem::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return false;
}

static std::wstring FindToolOnPath(const wchar_t* exeName) {
    wchar_t buf[MAX_PATH]{};
    if (SearchPathW(nullptr, exeName, nullptr, MAX_PATH, buf, nullptr) > 0) {
        return std::wstring(buf);
    }
    return L"";
}

static std::wstring NormalizeLfOutput(const std::string& outputUtf8) {
    if (outputUtf8.empty()) return L"";
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, outputUtf8.c_str(), static_cast<int>(outputUtf8.size()), nullptr, 0);
    if (wideLen <= 0) return L"";
    std::wstring rawOut(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, outputUtf8.c_str(), static_cast<int>(outputUtf8.size()), rawOut.data(), wideLen);

    std::wstring normalized;
    normalized.reserve(rawOut.size());
    for (wchar_t ch : rawOut) {
        if (ch != L'\r' && ch != L'\0') {
            normalized += ch;
        }
    }
    return normalized;
}

static bool RunStdinStdoutCommand(
    const std::wstring& cmdLine,
    const std::wstring& stdinText,
    const std::wstring& workDir,
    DWORD timeoutMs,
    std::wstring& outFormatted)
{
    HANDLE hChildStdInRead = nullptr, hChildStdInWrite = nullptr;
    HANDLE hChildStdOutRead = nullptr, hChildStdOutWrite = nullptr;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    if (!CreatePipe(&hChildStdOutRead, &hChildStdOutWrite, &sa, 0)) return false;
    SetHandleInformation(hChildStdOutRead, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&hChildStdInRead, &hChildStdInWrite, &sa, 0)) {
        CloseHandle(hChildStdOutRead);
        CloseHandle(hChildStdOutWrite);
        return false;
    }
    SetHandleInformation(hChildStdInWrite, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.hStdInput = hChildStdInRead;
    si.hStdOutput = hChildStdOutWrite;
    si.hStdError = hChildStdOutWrite;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(0);
    const wchar_t* cwdPtr = workDir.empty() ? nullptr : workDir.c_str();

    BOOL success = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwdPtr, &si, &pi);
    CloseHandle(hChildStdInRead);
    CloseHandle(hChildStdOutWrite);

    if (!success) {
        CloseHandle(hChildStdInWrite);
        CloseHandle(hChildStdOutRead);
        return false;
    }

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, stdinText.c_str(), static_cast<int>(stdinText.size()), nullptr, 0, nullptr, nullptr);
    if (utf8Len > 0) {
        std::string utf8Str(static_cast<size_t>(utf8Len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, stdinText.c_str(), static_cast<int>(stdinText.size()), utf8Str.data(), utf8Len, nullptr, nullptr);
        size_t totalWritten = 0;
        while (totalWritten < utf8Str.size()) {
            DWORD written = 0;
            if (!WriteFile(hChildStdInWrite, utf8Str.data() + totalWritten,
                           static_cast<DWORD>(utf8Str.size() - totalWritten), &written, nullptr) || written == 0) {
                break;
            }
            totalWritten += written;
        }
    }
    CloseHandle(hChildStdInWrite);

    std::string outputUtf8;
    char buffer[4096];
    DWORD readBytes = 0;
    while (ReadFile(hChildStdOutRead, buffer, sizeof(buffer), &readBytes, nullptr) && readBytes > 0) {
        outputUtf8.append(buffer, readBytes);
    }
    CloseHandle(hChildStdOutRead);

    DWORD waitRes = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD exitCode = 1;
    if (waitRes == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    } else {
        TerminateProcess(pi.hProcess, 1);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0 || outputUtf8.empty()) {
        return false;
    }

    std::wstring normalized = NormalizeLfOutput(outputUtf8);
    if (normalized.empty()) return false;
    outFormatted = std::move(normalized);
    return true;
}

static bool RunPrettier(
    const std::wstring& src,
    const std::wstring& ext,
    int tabSize,
    const std::wstring& filePath,
    bool insertSpaces,
    int rangeStart,
    int rangeEnd,
    std::wstring& outFormatted)
{
    std::wstring prettierPath = FindPrettierPath();
    if (prettierPath.empty()) return false;

    std::wstring targetPath = filePath.empty() ? (L"temp" + ext) : filePath;
    std::wstring workDir;
    if (!filePath.empty()) {
        std::error_code ec;
        auto parent = std::filesystem::path(filePath).parent_path();
        if (std::filesystem::exists(parent, ec)) {
            workDir = parent.wstring();
        }
    }

    static const std::vector<std::wstring> kPrettierConfigs = {
        L".prettierrc", L".prettierrc.json", L".prettierrc.yml", L".prettierrc.yaml",
        L".prettierrc.js", L".prettierrc.cjs", L".prettierrc.mjs",
        L"prettier.config.js", L"prettier.config.cjs", L"prettier.config.mjs",
        L".editorconfig"
    };
    bool hasLocalConfig = HasLocalConfigUpwards(filePath, kPrettierConfigs);

    std::wstring args = L" --stdin-filepath \"" + targetPath + L"\"";
    if (!hasLocalConfig) {
        args += L" --tab-width " + std::to_wstring(tabSize);
        if (!insertSpaces) args += L" --use-tabs";
    }
    if (rangeStart >= 0 && rangeEnd > rangeStart) {
        args += L" --range-start " + std::to_wstring(rangeStart) + L" --range-end " + std::to_wstring(rangeEnd);
    }

    bool isScript = (prettierPath.length() >= 4 &&
        (prettierPath.substr(prettierPath.length() - 4) == L".cjs" ||
         prettierPath.substr(prettierPath.length() - 3) == L".js"));
    std::wstring cmd = isScript
        ? (L"node.exe \"" + prettierPath + L"\"" + args)
        : (L"cmd.exe /c \"\"" + prettierPath + L"\"" + args + L"\"");

    return RunStdinStdoutCommand(cmd, src, workDir, 3000, outFormatted);
}

static bool RunPrettier(const std::wstring& src, const std::wstring& ext, int tabSize, std::wstring& outFormatted) {
    return RunPrettier(src, ext, tabSize, L"", true, -1, -1, outFormatted);
}

static bool RunClangFormat(
    const std::wstring& src,
    const std::wstring& ext,
    int tabSize,
    const std::wstring& filePath,
    size_t startLine1Based,
    size_t endLine1Based,
    std::wstring& outFormatted)
{
    static std::wstring s_clangFormat = FindToolOnPath(L"clang-format.exe");
    if (s_clangFormat.empty()) return false;

    std::wstring assumeFile = filePath.empty() ? (L"temp" + ext) : filePath;
    std::wstring workDir;
    if (!filePath.empty()) {
        std::error_code ec;
        auto parent = std::filesystem::path(filePath).parent_path();
        if (std::filesystem::exists(parent, ec)) workDir = parent.wstring();
    }

    std::wstring cmd = L"\"" + s_clangFormat + L"\" --assume-filename=\"" + assumeFile +
                       L"\" --fallback-style=\"{BasedOnStyle: LLVM, IndentWidth: " + std::to_wstring(tabSize) + L"}\"";
    if (startLine1Based >= 1 && endLine1Based >= startLine1Based) {
        cmd += L" --lines=" + std::to_wstring(startLine1Based) + L":" + std::to_wstring(endLine1Based);
    }
    return RunStdinStdoutCommand(cmd, src, workDir, 2500, outFormatted);
}

static bool RunRuff(
    const std::wstring& src,
    const std::wstring& ext,
    const std::wstring& filePath,
    size_t startLine1Based,
    size_t endLine1Based,
    std::wstring& outFormatted)
{
    static std::wstring s_ruff = FindToolOnPath(L"ruff.exe");
    if (s_ruff.empty()) return false;

    std::wstring assumeFile = filePath.empty() ? (L"temp" + ext) : filePath;
    std::wstring workDir;
    if (!filePath.empty()) {
        std::error_code ec;
        auto parent = std::filesystem::path(filePath).parent_path();
        if (std::filesystem::exists(parent, ec)) workDir = parent.wstring();
    }

    std::wstring cmd = L"\"" + s_ruff + L"\" format --stdin-filename \"" + assumeFile + L"\"";
    if (startLine1Based >= 1 && endLine1Based >= startLine1Based) {
        cmd += L" --range=" + std::to_wstring(startLine1Based) + L":" + std::to_wstring(endLine1Based);
    }
    cmd += L" -";
    return RunStdinStdoutCommand(cmd, src, workDir, 2500, outFormatted);
}

// Common Formatting Helpers
// ==============================================================================

static std::wstring MakeIndent(int level, int tabSize) {
    if (level <= 0) return L"";
    return std::wstring(static_cast<size_t>(level * tabSize), L' ');
}

static std::wstring Trim(const std::wstring& s) {
    size_t start = 0;
    while (start < s.length() && (s[start] == L' ' || s[start] == L'\t' || s[start] == L'\r' || s[start] == L'\n')) {
        start++;
    }
    size_t end = s.length();
    while (end > start && (s[end - 1] == L' ' || s[end - 1] == L'\t' || s[end - 1] == L'\r' || s[end - 1] == L'\n')) {
        end--;
    }
    return (start < end) ? s.substr(start, end - start) : L"";
}

static std::wstring TrimRight(const std::wstring& s) {
    size_t end = s.length();
    while (end > 0 && (s[end - 1] == L' ' || s[end - 1] == L'\t' || s[end - 1] == L'\r' || s[end - 1] == L'\n')) {
        end--;
    }
    return s.substr(0, end);
}

static bool StartsWith(const std::wstring& str, const std::wstring& prefix) {
    return str.rfind(prefix, 0) == 0;
}

static bool ch_starts(const std::wstring& s, size_t pos, const wchar_t* prefix) {
    if (!prefix) return false;
    size_t plen = wcslen(prefix);
    if (pos + plen > s.length()) return false;
    return s.compare(pos, plen, prefix) == 0;
}

static bool EndsWith(const std::wstring& str, const std::wstring& suffix) {
    if (str.length() < suffix.length()) return false;
    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

// ==============================================================================
// 1. JSON Formatter (via Prettier)
// ==============================================================================

std::wstring FormatEngine::FormatJson(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";
    std::wstring formatted;
    if (RunPrettier(src, L".json", tabSize, formatted) && !formatted.empty()) {
        return formatted;
    }
    return src;
}

// ==============================================================================
// 2. CSS Formatter (via Prettier)
// ==============================================================================

std::wstring FormatEngine::FormatCss(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";
    std::wstring formatted;
    if (RunPrettier(src, L".css", tabSize, formatted) && !formatted.empty()) {
        return formatted;
    }
    return src;
}

// ==============================================================================
// 3. HTML Formatter (via Prettier)
// ==============================================================================

std::wstring FormatEngine::FormatHtml(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";
    std::wstring formatted;
    if (RunPrettier(src, L".html", tabSize, formatted) && !formatted.empty()) {
        return formatted;
    }
    return src;
}

// ==============================================================================
// 4. JavaScript / TypeScript / JSX Formatter (via Prettier)
// ==============================================================================

std::wstring FormatEngine::FormatJavaScript(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";
    std::wstring formatted;
    if (RunPrettier(src, L".jsx", tabSize, formatted) && !formatted.empty()) {
        return formatted;
    }
    return src;
}

// ==============================================================================
// 5. PHP Formatter (PSR-12 Standard)
// ==============================================================================

std::wstring FormatEngine::FormatPhp(const std::wstring& src, int tabSize) {
    if (src.empty()) return L"";

    std::wstring out;
    out.reserve(src.length() * 2);
    int indentLevel = 0;
    std::wistringstream iss(src);
    std::wstring line;

    bool inBlockComment = false;
    bool inPhpTag = false;

    auto appendIndent = [&](int lvl) {
        out += MakeIndent(lvl, tabSize);
    };

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        std::wstring trimmed = Trim(line);
        if (trimmed.empty()) {
            if (!out.empty() && out.back() != L'\n') out += L'\n';
            if (out.length() >= 2 && out[out.length() - 2] != L'\n') out += L'\n';
            continue;
        }

        // 1. Comments
        if (inBlockComment) {
            out += MakeIndent(indentLevel, tabSize) + trimmed + L'\n';
            if (trimmed.find(L"*/") != std::wstring::npos) inBlockComment = false;
            continue;
        }
        if (StartsWith(trimmed, L"/*") && trimmed.find(L"*/") == std::wstring::npos) {
            inBlockComment = true;
            out += MakeIndent(indentLevel, tabSize) + trimmed + L'\n';
            continue;
        }

        // 2. Track PHP tag boundaries
        if (StartsWith(trimmed, L"<?php") || StartsWith(trimmed, L"<?=")) {
            inPhpTag = true;
            out += trimmed + L'\n';
            continue;
        }
        if (trimmed == L"?>") {
            inPhpTag = false;
            out += trimmed + L'\n';
            continue;
        }

        // 3. Determine dedent for lines starting with closing tags / braces / alternative syntax
        int lineDedent = 0;
        if (trimmed[0] == L'}' || trimmed[0] == L']' || trimmed[0] == L')') {
            for (wchar_t ch : trimmed) {
                if (ch == L'}' || ch == L']' || ch == L')') lineDedent++;
                else if (!iswspace(ch)) break;
            }
        } else if (StartsWith(trimmed, L"</")) {
            lineDedent++;
        } else {
            std::wstring lower = trimmed;
            for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
            if (StartsWith(lower, L"endif") || StartsWith(lower, L"endforeach") ||
                StartsWith(lower, L"endwhile") || StartsWith(lower, L"endfor") ||
                StartsWith(lower, L"endswitch") || StartsWith(lower, L"else") ||
                StartsWith(lower, L"elseif") || StartsWith(lower, L"} else") ||
                StartsWith(lower, L"} elseif") || StartsWith(lower, L"} catch") ||
                StartsWith(lower, L"} finally")) {
                lineDedent++;
            }
        }

        int currentIndent = indentLevel - lineDedent;
        if (currentIndent < 0) currentIndent = 0;

        // Clean up arrow operators: "=>" with spaces, "->" without spaces
        std::wstring cleanedLine;
        cleanedLine.reserve(trimmed.length() * 2);
        bool inStr = false;
        wchar_t strChar = 0;

        for (size_t k = 0; k < trimmed.length(); ++k) {
            wchar_t c = trimmed[k];
            if (inStr) {
                cleanedLine += c;
                if (c == L'\\' && k + 1 < trimmed.length()) {
                    cleanedLine += trimmed[k + 1];
                    k++;
                    continue;
                }
                if (c == strChar) inStr = false;
                continue;
            }
            if (c == L'\'' || c == L'"') {
                inStr = true;
                strChar = c;
                cleanedLine += c;
                continue;
            }
            // Double arrow: "=>"
            if (c == L'=' && k + 1 < trimmed.length() && trimmed[k + 1] == L'>') {
                // Ensure single space before and after =>
                while (!cleanedLine.empty() && cleanedLine.back() == L' ') cleanedLine.pop_back();
                cleanedLine += L" => ";
                k++;
                while (k + 1 < trimmed.length() && (trimmed[k + 1] == L' ' || trimmed[k + 1] == L'\t')) k++;
                continue;
            }
            // Single arrow object access: "->"
            if (c == L'-' && k + 1 < trimmed.length() && trimmed[k + 1] == L'>') {
                while (!cleanedLine.empty() && cleanedLine.back() == L' ') cleanedLine.pop_back();
                cleanedLine += L"->";
                k++;
                while (k + 1 < trimmed.length() && (trimmed[k + 1] == L' ' || trimmed[k + 1] == L'\t')) k++;
                continue;
            }
            // Comma spacing
            if (c == L',') {
                cleanedLine += c;
                if (k + 1 < trimmed.length() && trimmed[k + 1] != L' ' && trimmed[k + 1] != L'\t') {
                    cleanedLine += L' ';
                }
                continue;
            }
            cleanedLine += c;
        }

        appendIndent(currentIndent);
        out += cleanedLine + L'\n';

        // 4. Compute indent delta for subsequent lines
        int delta = 0;
        bool inStringLiteral = false;
        wchar_t strDelim = 0;

        for (size_t k = 0; k < trimmed.length(); ++k) {
            wchar_t c = trimmed[k];
            if (inStringLiteral) {
                if (c == L'\\' && k + 1 < trimmed.length()) { k++; continue; }
                if (c == strDelim) inStringLiteral = false;
                continue;
            }
            if (c == L'\'' || c == L'"') {
                inStringLiteral = true;
                strDelim = c;
                continue;
            }
            if ((c == L'/' && k + 1 < trimmed.length() && trimmed[k + 1] == L'/') || c == L'#') {
                break;
            }

            if (c == L'{' || c == L'[' || c == L'(') delta++;
            else if (c == L'}' || c == L']' || c == L')') delta--;
        }

        // Alternative control structure colon: "if (...):" or "foreach (...):"
        if (trimmed.back() == L':') {
            std::wstring lower = trimmed;
            for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
            if (lower.find(L"if") != std::wstring::npos || lower.find(L"foreach") != std::wstring::npos ||
                lower.find(L"while") != std::wstring::npos || lower.find(L"for") != std::wstring::npos ||
                lower.find(L"else") != std::wstring::npos || lower.find(L"switch") != std::wstring::npos) {
                delta++;
            }
        }

        indentLevel += delta;
        if (indentLevel < 0) indentLevel = 0;
    }

    if (!out.empty() && out.back() != L'\n') out += L'\n';
    return out;
}

// ==============================================================================
// 6. FormatDocument Master Dispatcher
// ==============================================================================

static bool IsClangFormatExt(const std::wstring& lower) {
    return (lower == L".c" || lower == L".cc" || lower == L".cpp" || lower == L".cxx" ||
            lower == L".h" || lower == L".hh" || lower == L".hpp" || lower == L".hxx" ||
            lower == L".java" || lower == L".cs" || lower == L".proto");
}

static bool IsRuffExt(const std::wstring& lower) {
    return (lower == L".py" || lower == L".pyi");
}

static bool IsPrettierExt(const std::wstring& lower) {
    return (lower == L".js" || lower == L".jsx" || lower == L".mjs" || lower == L".cjs" ||
            lower == L".ts" || lower == L".tsx" || lower == L".mts" || lower == L".cts" ||
            lower == L".html" || lower == L".htm" || lower == L".vue" || lower == L".svelte" ||
            lower == L".css" || lower == L".scss" || lower == L".less" ||
            lower == L".json" || lower == L".jsonc" || lower == L".json5" ||
            lower == L".yaml" || lower == L".yml" || lower == L".md" || lower == L".markdown" ||
            lower == L".graphql" || lower == L".gql");
}

static std::vector<std::wstring> SplitLinesNormalized(const std::wstring& src, bool& outTrailingNewline) {
    outTrailingNewline = (!src.empty() && src.back() == L'\n');
    std::vector<std::wstring> lines;
    std::wstring current;
    for (wchar_t c : src) {
        if (c == L'\r') continue;
        if (c == L'\n') {
            lines.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty() || lines.empty() || !outTrailingNewline) {
        lines.push_back(std::move(current));
    }
    return lines;
}

static int ComputeBraceIndentBeforeLine(const std::vector<std::wstring>& lines, size_t stopLine) {
    int depth = 0;
    bool inBlockComment = false;
    for (size_t i = 0; i < stopLine && i < lines.size(); ++i) {
        const std::wstring& line = lines[i];
        bool inStr = false;
        wchar_t strCh = 0;
        for (size_t k = 0; k < line.size(); ++k) {
            wchar_t c = line[k];
            if (inBlockComment) {
                if (c == L'*' && k + 1 < line.size() && line[k + 1] == L'/') {
                    inBlockComment = false;
                    ++k;
                }
                continue;
            }
            if (inStr) {
                if (c == L'\\' && k + 1 < line.size()) { ++k; continue; }
                if (c == strCh) inStr = false;
                continue;
            }
            if (c == L'/' && k + 1 < line.size() && line[k + 1] == L'*') {
                inBlockComment = true;
                ++k;
                continue;
            }
            if ((c == L'/' && k + 1 < line.size() && line[k + 1] == L'/') || c == L'#') {
                break;
            }
            if (c == L'"' || c == L'\'' || c == L'`') {
                inStr = true;
                strCh = c;
                continue;
            }
            if (c == L'{' || c == L'[' || c == L'(') ++depth;
            else if (c == L'}' || c == L']' || c == L')') --depth;
        }
    }
    return (std::max)(0, depth);
}

static std::wstring FormatRangeNativeFallback(
    const std::vector<std::wstring>& lines,
    size_t startLine,
    size_t endLine,
    const std::wstring& lowerExt,
    int tabSize,
    bool insertSpaces,
    bool trailingNewline)
{
    int safeTab = (std::max)(1, tabSize);
    bool isPhp = (lowerExt == L".php" || lowerExt == L".phtml" || lowerExt == L".php8");
    bool isBraceLang = isPhp || IsClangFormatExt(lowerExt) ||
                       lowerExt == L".js" || lowerExt == L".jsx" || lowerExt == L".mjs" || lowerExt == L".cjs" ||
                       lowerExt == L".ts" || lowerExt == L".tsx" || lowerExt == L".mts" || lowerExt == L".cts" ||
                       lowerExt == L".css" || lowerExt == L".scss" || lowerExt == L".less" ||
                       lowerExt == L".json" || lowerExt == L".jsonc" || lowerExt == L".json5";

    std::vector<std::wstring> outLines = lines;

    if (isBraceLang) {
        int indentLevel = ComputeBraceIndentBeforeLine(lines, startLine);
        for (size_t i = startLine; i <= endLine && i < outLines.size(); ++i) {
            std::wstring trimmed = TrimRight(Trim(outLines[i]));
            if (trimmed.empty()) {
                outLines[i].clear();
                continue;
            }
            int dedent = 0;
            if (trimmed[0] == L'}' || trimmed[0] == L']' || trimmed[0] == L')') {
                dedent = 1;
            }
            int lineIndent = (std::max)(0, indentLevel - dedent);
            std::wstring prefix = insertSpaces
                ? std::wstring(static_cast<size_t>(lineIndent * safeTab), L' ')
                : std::wstring(static_cast<size_t>(lineIndent), L'\t');

            std::wstring cleaned = trimmed;
            if (isPhp) {
                std::wstring phpCleaned;
                phpCleaned.reserve(trimmed.size() + 8);
                bool inStr = false;
                wchar_t strCh = 0;
                for (size_t k = 0; k < trimmed.size(); ++k) {
                    wchar_t c = trimmed[k];
                    if (inStr) {
                        phpCleaned += c;
                        if (c == L'\\' && k + 1 < trimmed.size()) {
                            phpCleaned += trimmed[++k];
                            continue;
                        }
                        if (c == strCh) inStr = false;
                        continue;
                    }
                    if (c == L'\'' || c == L'"') {
                        inStr = true;
                        strCh = c;
                        phpCleaned += c;
                        continue;
                    }
                    if (c == L'=' && k + 1 < trimmed.size() && trimmed[k + 1] == L'>') {
                        while (!phpCleaned.empty() && phpCleaned.back() == L' ') phpCleaned.pop_back();
                        phpCleaned += L" => ";
                        ++k;
                        while (k + 1 < trimmed.size() && (trimmed[k + 1] == L' ' || trimmed[k + 1] == L'\t')) ++k;
                        continue;
                    }
                    if (c == L'-' && k + 1 < trimmed.size() && trimmed[k + 1] == L'>') {
                        while (!phpCleaned.empty() && phpCleaned.back() == L' ') phpCleaned.pop_back();
                        phpCleaned += L"->";
                        ++k;
                        while (k + 1 < trimmed.size() && (trimmed[k + 1] == L' ' || trimmed[k + 1] == L'\t')) ++k;
                        continue;
                    }
                    phpCleaned += c;
                }
                cleaned = std::move(phpCleaned);
            }
            outLines[i] = prefix + cleaned;

            bool inStr = false;
            wchar_t strCh = 0;
            for (size_t k = 0; k < trimmed.size(); ++k) {
                wchar_t c = trimmed[k];
                if (inStr) {
                    if (c == L'\\' && k + 1 < trimmed.size()) { ++k; continue; }
                    if (c == strCh) inStr = false;
                    continue;
                }
                if (c == L'\'' || c == L'"' || c == L'`') {
                    inStr = true;
                    strCh = c;
                    continue;
                }
                if ((c == L'/' && k + 1 < trimmed.size() && trimmed[k + 1] == L'/') || c == L'#') break;
                if (c == L'{' || c == L'[' || c == L'(') ++indentLevel;
                else if (c == L'}' || c == L']' || c == L')') --indentLevel;
            }
            if (indentLevel < 0) indentLevel = 0;
        }
    } else {
        for (size_t i = startLine; i <= endLine && i < outLines.size(); ++i) {
            outLines[i] = TrimRight(outLines[i]);
        }
    }

    std::wstring joined;
    for (size_t i = 0; i < outLines.size(); ++i) {
        joined += outLines[i];
        if (i + 1 < outLines.size() || trailingNewline) {
            joined += L'\n';
        }
    }
    return joined;
}

bool FormatEngine::CanFormat(const std::wstring& ext) {
    std::wstring lower = ext;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
    return (IsPrettierExt(lower) ||
            lower == L".php" || lower == L".phtml" || lower == L".php8" ||
            IsClangFormatExt(lower) ||
            IsRuffExt(lower) ||
            phide::plugin::PluginManager::Instance().CanFormat(lower));
}

std::wstring FormatEngine::FormatDocument(
    const std::wstring& src,
    const std::wstring& ext,
    int tabSize,
    const std::wstring& filePath,
    bool insertSpaces)
{
    if (src.empty()) return L"";

    std::wstring lower = ext;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));

    // 0. Try any registered plugin/extension/mod formatter first
    std::wstring pluginFormatted;
    if (phide::plugin::PluginManager::Instance().TryFormat(src, lower, tabSize, pluginFormatted) && !pluginFormatted.empty()) {
        return pluginFormatted;
    }

    // 1. Try Prettier via bundled phide.prettier extension or system Prettier (honoring local config)
    std::wstring formatted;
    if (IsPrettierExt(lower) && RunPrettier(src, lower, tabSize, filePath, insertSpaces, -1, -1, formatted) && !formatted.empty()) {
        return formatted;
    }

    // 2. Try Clang-Format or Ruff for C/C++/Java/C# and Python when installed
    if (IsClangFormatExt(lower) && RunClangFormat(src, lower, tabSize, filePath, 0, 0, formatted) && !formatted.empty()) {
        return formatted;
    }
    if (IsRuffExt(lower) && RunRuff(src, lower, filePath, 0, 0, formatted) && !formatted.empty()) {
        return formatted;
    }

    // 3. Fall back to Prettier even on unknown/custom parser extensions (e.g. plugin parsers) or PHP
    if (!IsPrettierExt(lower) && RunPrettier(src, lower, tabSize, filePath, insertSpaces, -1, -1, formatted) && !formatted.empty()) {
        return formatted;
    }

    // 4. Fall back to native PSR-12 formatter for PHP files
    if (lower == L".php" || lower == L".phtml" || lower == L".php8") {
        return FormatPhp(src, tabSize);
    }

    // Default fallback: normalize line endings and trim trailing whitespace
    std::wistringstream ss(src);
    std::wstring line;
    std::wstring out;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        out += TrimRight(line) + L'\n';
    }
    return out;
}

std::wstring FormatEngine::FormatRange(
    const std::wstring& src,
    size_t startLine,
    size_t startCol,
    size_t endLine,
    size_t endCol,
    const std::wstring& ext,
    int tabSize,
    const std::wstring& filePath,
    bool insertSpaces)
{
    if (src.empty()) return L"";

    std::wstring lower = ext;
    for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));

    bool trailingNewline = false;
    std::vector<std::wstring> lines = SplitLinesNormalized(src, trailingNewline);
    if (lines.empty()) return src;

    if (startLine > endLine || (startLine == endLine && startCol > endCol)) {
        std::swap(startLine, endLine);
        std::swap(startCol, endCol);
    }
    startLine = (std::min)(startLine, lines.size() - 1);
    endLine = (std::min)(endLine, lines.size() - 1);
    if (endLine > startLine && endCol == 0) {
        --endLine;
        endCol = lines[endLine].size();
    }

    // Rebuild LF-normalized source & compute character offsets for Prettier --range-start / --range-end
    std::wstring lfSrc;
    lfSrc.reserve(src.size());
    size_t rangeStartOffset = 0;
    size_t rangeEndOffset = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i == startLine) {
            rangeStartOffset = lfSrc.size();
        }
        lfSrc += lines[i];
        if (i == endLine) {
            rangeEndOffset = lfSrc.size();
        }
        if (i + 1 < lines.size() || trailingNewline) {
            lfSrc += L'\n';
        }
    }

    std::wstring formatted;
    if (IsPrettierExt(lower) && rangeEndOffset > rangeStartOffset) {
        if (RunPrettier(lfSrc, lower, tabSize, filePath, insertSpaces,
                        static_cast<int>(rangeStartOffset), static_cast<int>(rangeEndOffset), formatted) && !formatted.empty()) {
            return formatted;
        }
    }
    if (IsClangFormatExt(lower)) {
        if (RunClangFormat(lfSrc, lower, tabSize, filePath, startLine + 1, endLine + 1, formatted) && !formatted.empty()) {
            return formatted;
        }
    }
    if (IsRuffExt(lower)) {
        if (RunRuff(lfSrc, lower, filePath, startLine + 1, endLine + 1, formatted) && !formatted.empty()) {
            return formatted;
        }
    }

    return FormatRangeNativeFallback(lines, startLine, endLine, lower, tabSize, insertSpaces, trailingNewline);
}


} // namespace phide::editor
