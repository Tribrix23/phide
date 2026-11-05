import os

with open(r'src/editor/Buffer.cpp', 'r', encoding='utf-8') as f:
    lines = f.readlines()

new_lines = []
in_emmet = False
include_added = False

target_impl = """bool Buffer::ExpandEmmet(const std::wstring& fileExt) {
    if (m_lines.empty()) return false;
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;
    auto& line = m_lines[m_cursor.line];
    if (m_cursor.col == 0 || line.empty()) return false;

    std::wstring ext = fileExt;
    if (ext.empty() && !m_filePath.empty()) {
        std::filesystem::path p(m_filePath);
        ext = p.extension().wstring();
        if (!ext.empty() && ext[0] == L'.') ext = ext.substr(1);
    }

    bool isCss = (ext == L"css" || ext == L"scss" || ext == L"less");

    size_t startCol = 0;
    std::wstring abbr = EmmetEngine::ExtractAbbreviation(line, m_cursor.col, isCss, &startCol);
    if (abbr.empty() || !EmmetEngine::IsEmmetAbbreviation(abbr, isCss)) {
        return false;
    }

    // Determine current line indentation
    size_t indentLen = 0;
    while (indentLen < startCol && (line[indentLen] == L' ' || line[indentLen] == L'\\t')) {
        indentLen++;
    }
    std::wstring lineIndent = line.substr(0, indentLen);
    std::wstring indentUnit = L"    "; // 4 spaces default

    size_t cursorOffset = std::wstring::npos;
    std::wstring expansion = EmmetEngine::Expand(abbr, indentUnit, lineIndent, isCss, &cursorOffset);
    if (expansion.empty()) {
        return false;
    }

    PushUndo();

    // Split expansion into lines
    std::vector<std::wstring> expLines;
    {
        std::wstringstream ss(expansion);
        std::wstring l;
        while (std::getline(ss, l)) {
            if (!l.empty() && l.back() == L'\\r') l.pop_back();
            expLines.push_back(l);
        }
    }
    if (expLines.empty()) expLines.push_back(expansion);

    std::wstring suffix = line.substr(m_cursor.col);

    if (expLines.size() == 1) {
        std::wstring rep = expLines[0];
        if (!lineIndent.empty() && rep.rfind(lineIndent, 0) == 0) {
            rep = rep.substr(lineIndent.length());
        }
        line = line.substr(0, startCol) + rep + suffix;
        if (cursorOffset != std::wstring::npos && cursorOffset <= expLines[0].length()) {
            size_t relOffset = cursorOffset;
            if (!lineIndent.empty() && relOffset >= lineIndent.length()) {
                relOffset -= lineIndent.length();
            }
            m_cursor.col = startCol + relOffset;
        } else {
            m_cursor.col = startCol + rep.length();
        }
    } else {
        std::wstring repFirst = expLines[0];
        if (!lineIndent.empty() && repFirst.rfind(lineIndent, 0) == 0) {
            repFirst = repFirst.substr(lineIndent.length());
        }
        line = line.substr(0, startCol) + repFirst;

        size_t baseLine = m_cursor.line;
        for (size_t i = 1; i < expLines.size(); ++i) {
            std::wstring nextLine = expLines[i];
            if (i == expLines.size() - 1) {
                nextLine += suffix;
            }
            m_lines.insert(m_lines.begin() + baseLine + i, nextLine);
        }

        if (cursorOffset != std::wstring::npos) {
            size_t running = 0;
            size_t targetLine = 0;
            size_t targetCol = 0;
            bool found = false;
            for (size_t i = 0; i < expLines.size(); ++i) {
                size_t lLen = expLines[i].length() + 1;
                if (cursorOffset <= running + lLen) {
                    targetLine = i;
                    targetCol = cursorOffset - running;
                    found = true;
                    break;
                }
                running += lLen;
            }
            if (found) {
                m_cursor.line = baseLine + targetLine;
                if (targetLine == 0) {
                    size_t relCol = targetCol;
                    if (!lineIndent.empty() && relCol >= lineIndent.length()) relCol -= lineIndent.length();
                    m_cursor.col = startCol + relCol;
                } else {
                    m_cursor.col = targetCol;
                }
            } else {
                m_cursor.line = baseLine + expLines.size() - 1;
                m_cursor.col = expLines.back().length();
            }
        } else {
            m_cursor.line = baseLine + expLines.size() - 1;
            m_cursor.col = expLines.back().length();
        }
    }

    m_isModified = true;
    return true;
}
"""

for line in lines:
    if '#include "phide/editor/Buffer.hpp"' in line and not include_added:
        new_lines.append(line)
        new_lines.append('#include "phide/editor/EmmetEngine.hpp"\n')
        include_added = True
        continue

    if line.startswith('bool Buffer::ExpandEmmet()'):
        in_emmet = True
        for tl in target_impl.split('\n'):
            new_lines.append(tl + '\n')
        continue

    if in_emmet:
        if line.startswith('bool Buffer::ReplacePrefixWithSnippet'):
            in_emmet = False
            new_lines.append(line)
        continue

    new_lines.append(line)

with open(r'src/editor/Buffer.cpp', 'w', encoding='utf-8') as f:
    f.writelines(new_lines)

print("Buffer.cpp successfully replaced!")
