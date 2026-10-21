/**
 * @file Buffer.cpp
 * @brief Implementation of the text buffer and syntax lexer.
 */

#include "phide/editor/Buffer.hpp"
#include <windows.h>
#include <cwctype>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace phide::editor {

// --- SyntaxLexer Implementation ---

SyntaxLexer::SyntaxLexer() {
    // PHP & C++ Keywords
    m_keywords = {
        L"class", L"function", L"public", L"private", L"protected",
        L"return", L"namespace", L"use", L"echo", L"new", L"if", L"else",
        L"while", L"for", L"foreach", L"switch", L"case", L"break",
        L"try", L"catch", L"throw", L"static", L"const", L"auto",
        L"template", L"typename", L"struct", L"enum", L"virtual",
        L"override", L"final", L"constexpr", L"explicit", L"inline"
    };

    // Common Primitive & Standard Types
    m_types = {
        L"string", L"int", L"bool", L"float", L"void", L"array",
        L"object", L"mixed", L"iterable", L"std::string", L"std::vector",
        L"uint32_t", L"size_t", L"int32_t", L"double", L"char", L"wchar_t"
    };
}

std::vector<SyntaxToken> SyntaxLexer::TokenizeLine(const std::wstring& line) const {
    std::vector<SyntaxToken> tokens;
    if (line.empty()) return tokens;

    size_t i = 0;
    const size_t len = line.length();

    while (i < len) {
        // Skip whitespace
        if (iswspace(line[i])) {
            ++i;
            continue;
        }

        // Single-line Comments (// or #)
        if ((i + 1 < len && line[i] == L'/' && line[i + 1] == L'/') || (line[i] == L'#')) {
            tokens.push_back({ TokenType::Comment, i, len - i });
            break;
        }

        // Block Comment opening or inside (/* ...)
        if (i + 1 < len && line[i] == L'/' && line[i + 1] == L'*') {
            tokens.push_back({ TokenType::Comment, i, len - i });
            break;
        }

        // String literals ("..." or '...')
        if (line[i] == L'"' || line[i] == L'\'') {
            wchar_t quote = line[i];
            size_t start = i++;
            while (i < len && line[i] != quote) {
                if (line[i] == L'\\' && i + 1 < len) i += 2;
                else ++i;
            }
            if (i < len) ++i; // Consume closing quote
            tokens.push_back({ TokenType::String, start, i - start });
            continue;
        }

        // Numeric Literals
        if (iswdigit(line[i])) {
            size_t start = i++;
            while (i < len && (iswalnum(line[i]) || line[i] == L'.')) ++i;
            tokens.push_back({ TokenType::Number, start, i - start });
            continue;
        }

        // Identifiers, Keywords, and Variables (e.g., $workspace, class, echo)
        if (iswalpha(line[i]) || line[i] == L'_' || line[i] == L'$') {
            size_t start = i++;
            while (i < len && (iswalnum(line[i]) || line[i] == L'_' || line[i] == L':')) ++i;

            std::wstring word = line.substr(start, i - start);
            // Strip leading '$' for keyword test if PHP variable
            std::wstring lookup = (word[0] == L'$') ? word.substr(1) : word;

            if (m_keywords.find(lookup) != m_keywords.end()) {
                tokens.push_back({ TokenType::Keyword, start, i - start });
            } else if (m_types.find(lookup) != m_types.end()) {
                tokens.push_back({ TokenType::Type, start, i - start });
            } else {
                tokens.push_back({ TokenType::Default, start, i - start });
            }
            continue;
        }

        // Operators & Punctuation
        tokens.push_back({ TokenType::Operator, i, 1 });
        ++i;
    }

    return tokens;
}

// --- Buffer Implementation ---

Buffer::Buffer() {
    m_lines = { L"" };
    m_cursor = { 0, 0 };
    m_isModified = false;
}

Buffer::Buffer(const std::wstring& initialContent) {
    if (initialContent.empty()) {
        m_lines = { L"" };
        m_cursor = { 0, 0 };
        m_isModified = false;
    } else {
        SetText(initialContent);
    }
}

void Buffer::PushUndo() {
    m_undoStack.push_back({ m_lines, m_cursor });
    if (m_undoStack.size() > 100) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
}

bool Buffer::Undo() {
    if (m_undoStack.empty()) return false;
    m_redoStack.push_back({ m_lines, m_cursor });
    auto last = m_undoStack.back();
    m_undoStack.pop_back();
    m_lines = last.lines;
    m_cursor = last.cursor;
    m_isModified = true;
    return true;
}

bool Buffer::Redo() {
    if (m_redoStack.empty()) return false;
    m_undoStack.push_back({ m_lines, m_cursor });
    auto next = m_redoStack.back();
    m_redoStack.pop_back();
    m_lines = next.lines;
    m_cursor = next.cursor;
    m_isModified = true;
    return true;
}

void Buffer::InsertText(const std::wstring& text) {
    if (text.empty()) return;
    PushUndo();
    for (wchar_t ch : text) {
        if (ch == L'\r') continue;
        if (m_lines.empty()) m_lines.push_back(L"");
        if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;

        auto& line = m_lines[m_cursor.line];
        if (m_cursor.col > line.length()) m_cursor.col = line.length();

        if (ch == L'\n') {
            std::wstring remainder = line.substr(m_cursor.col);
            line = line.substr(0, m_cursor.col);
            m_lines.insert(m_lines.begin() + m_cursor.line + 1, remainder);
            m_cursor.line++;
            m_cursor.col = 0;
        } else {
            line.insert(line.begin() + m_cursor.col, ch);
            m_cursor.col++;
        }
    }
    m_isModified = true;
}

void Buffer::LoadDefaultSample() {
    m_lines = {
        L"<?php",
        L"namespace Phide\\Core;",
        L"",
        L"/**",
        L" * EchoForge Engine - Next Generation PHP & C++ IDE",
        L" */",
        L"class EchoIDE {",
        L"    private string $workspace;",
        L"    private bool $isLspActive = true;",
        L"",
        L"    public function __construct(string $workspace) {",
        L"        $this->workspace = $workspace;",
        L"        echo \"Echo IDE initialized: \" . $this->workspace . \"\\n\";",
        L"    }",
        L"",
        L"    public function startLanguageServer(): void {",
        L"        // Launch Intelephense / Clangd via asynchronous JSON-RPC pipes",
        L"        $this->isLspActive = true;",
        L"    }",
        L"}"
    };
    m_cursor = { 12, 10 };
}

const std::wstring& Buffer::GetLine(size_t lineIdx) const {
    static const std::wstring emptyLine;
    if (lineIdx < m_lines.size()) {
        return m_lines[lineIdx];
    }
    return emptyLine;
}

std::vector<SyntaxToken> Buffer::GetTokensForLine(size_t lineIdx) const {
    if (lineIdx < m_lines.size()) {
        return m_lexer.TokenizeLine(m_lines[lineIdx]);
    }
    return {};
}

void Buffer::SetCursor(size_t line, size_t col) {
    if (m_lines.empty()) {
        m_cursor = { 0, 0 };
        return;
    }
    m_cursor.line = (std::min)(line, m_lines.size() - 1);
    m_cursor.col  = (std::min)(col, m_lines[m_cursor.line].length());
}

void Buffer::InsertChar(wchar_t ch) {
    PushUndo();
    if (m_lines.empty()) m_lines.push_back(L"");
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;

    auto& line = m_lines[m_cursor.line];
    if (m_cursor.col > line.length()) m_cursor.col = line.length();

    if (ch == L'\r' || ch == L'\n') {
        // Split current line at cursor
        std::wstring remainder = line.substr(m_cursor.col);
        line = line.substr(0, m_cursor.col);
        m_lines.insert(m_lines.begin() + m_cursor.line + 1, remainder);
        m_cursor.line++;
        m_cursor.col = 0;
    } else {
        line.insert(line.begin() + m_cursor.col, ch);
        m_cursor.col++;
    }
    m_isModified = true;
}

void Buffer::Backspace() {
    if (m_lines.empty()) return;
    PushUndo();
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;

    auto& line = m_lines[m_cursor.line];
    if (m_cursor.col > 0) {
        line.erase(m_cursor.col - 1, 1);
        m_cursor.col--;
        m_isModified = true;
    } else if (m_cursor.line > 0) {
        // Join with previous line
        size_t prevLen = m_lines[m_cursor.line - 1].length();
        m_lines[m_cursor.line - 1] += line;
        m_lines.erase(m_lines.begin() + m_cursor.line);
        m_cursor.line--;
        m_cursor.col = prevLen;
        m_isModified = true;
    }
}

void Buffer::MoveCursorLeft() {
    if (m_cursor.col > 0) {
        m_cursor.col--;
    } else if (m_cursor.line > 0) {
        m_cursor.line--;
        m_cursor.col = m_lines[m_cursor.line].length();
    }
}

void Buffer::MoveCursorRight() {
    if (m_cursor.line < m_lines.size()) {
        if (m_cursor.col < m_lines[m_cursor.line].length()) {
            m_cursor.col++;
        } else if (m_cursor.line + 1 < m_lines.size()) {
            m_cursor.line++;
            m_cursor.col = 0;
        }
    }
}

void Buffer::MoveCursorUp() {
    if (m_cursor.line > 0) {
        m_cursor.line--;
        if (m_cursor.col > m_lines[m_cursor.line].length()) {
            m_cursor.col = m_lines[m_cursor.line].length();
        }
    }
}

void Buffer::MoveCursorDown() {
    if (m_cursor.line + 1 < m_lines.size()) {
        m_cursor.line++;
        if (m_cursor.col > m_lines[m_cursor.line].length()) {
            m_cursor.col = m_lines[m_cursor.line].length();
        }
    }
}

void Buffer::Clear() {
    m_lines.clear();
    m_lines.push_back(L"");
    m_cursor = { 0, 0 };
    m_isModified = false;
}

void Buffer::SetLines(const std::vector<std::wstring>& lines) {
    m_lines = lines;
    if (m_lines.empty()) m_lines.push_back(L"");
    m_cursor = { 0, 0 };
    m_isModified = false;
}

void Buffer::SetText(const std::wstring& text) {
    m_lines.clear();
    std::wistringstream ss(text);
    std::wstring line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        m_lines.push_back(line);
    }
    if (m_lines.empty()) m_lines.push_back(L"");
    m_cursor = { 0, 0 };
    m_isModified = false;
}

bool Buffer::LoadFromFile(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string bytes = buffer.str();

    // Convert UTF-8 to wstring
    std::wstring wide;
    if (!bytes.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (wlen > 0) {
            wide.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), &wide[0], wlen);
        }
    }

    SetText(wide);
    m_filePath = path;

    // Extract filename from path
    auto pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        m_fileName = path.substr(pos + 1);
    } else {
        m_fileName = path;
    }

    m_isModified = false;
    return true;
}

bool Buffer::SaveToFile(const std::wstring& path) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    // Concatenate lines with \r\n
    std::wstring combined;
    for (size_t i = 0; i < m_lines.size(); ++i) {
        combined += m_lines[i];
        if (i + 1 < m_lines.size()) combined += L"\r\n";
    }

    // Convert wstring to UTF-8
    if (!combined.empty()) {
        int ulen = WideCharToMultiByte(CP_UTF8, 0, combined.data(), static_cast<int>(combined.size()), nullptr, 0, nullptr, nullptr);
        if (ulen > 0) {
            std::string u8(ulen, 0);
            WideCharToMultiByte(CP_UTF8, 0, combined.data(), static_cast<int>(combined.size()), &u8[0], ulen, nullptr, nullptr);
            file.write(u8.data(), u8.size());
        }
    }

    m_filePath = path;
    auto pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        m_fileName = path.substr(pos + 1);
    } else {
        m_fileName = path;
    }

    m_isModified = false;
    return true;
}

} // namespace phide::editor
