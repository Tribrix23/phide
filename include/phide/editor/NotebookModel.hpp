/**
 * @file NotebookModel.hpp
 * @brief Data structures and JSON serialization for Jupyter Notebook (.ipynb) compatible documents.
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <windows.h>
#include <d2d1.h>
#include "phide/editor/Buffer.hpp"

namespace phide::editor {

enum class NotebookCellType {
    Code,
    Markdown
};

struct CellOutput {
    std::wstring streamName = L"stdout"; // stdout, stderr
    std::wstring text;
    bool isError = false;
    ULONGLONG elapsedMs = 0;
};

struct NotebookCell {
    std::string id;
    NotebookCellType type = NotebookCellType::Code;
    std::wstring language = L"python";
    int executionCount = -1; // -1: unrun [ ], -2: running [*], >= 1: [ 1 ]
    Buffer inputBuffer;
    std::vector<CellOutput> outputs;
    bool isExecuting = false;
    bool isQueued = false;
    bool isOutputCollapsed = false;
    bool hasError = false;
    int errorLine = -1; // 0-based error line in code cell (-1 if none)
    ULONGLONG lastElapsedMs = 0;
    float outputScrollY = 0.0f;
    float outputScrollX = 0.0f;

    // Text Selection State
    bool hasSelection = false;
    CursorPosition selAnchor{ 0, 0 };
    CursorPosition selEnd{ 0, 0 };
    bool isSelecting = false;
    std::wstring selectedWord;

    [[nodiscard]] bool HasSelection() const noexcept {
        return hasSelection && (selAnchor.line != selEnd.line || selAnchor.col != selEnd.col);
    }

    void ClearSelection() noexcept {
        hasSelection = false;
        isSelecting = false;
        selectedWord.clear();
    }

    void GetSelectionRange(size_t& startLine, size_t& startCol, size_t& endLine, size_t& endCol) const noexcept {
        if (selAnchor.line < selEnd.line || (selAnchor.line == selEnd.line && selAnchor.col <= selEnd.col)) {
            startLine = selAnchor.line; startCol = selAnchor.col;
            endLine = selEnd.line; endCol = selEnd.col;
        } else {
            startLine = selEnd.line; startCol = selEnd.col;
            endLine = selAnchor.line; endCol = selAnchor.col;
        }
    }

    [[nodiscard]] std::wstring GetSelectedText() const {
        if (!HasSelection()) return L"";
        size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
        GetSelectionRange(sLine, sCol, eLine, eCol);
        if (sLine == eLine) {
            const auto& line = inputBuffer.GetLine(sLine);
            if (sCol < line.length()) {
                return line.substr(sCol, (std::min)(eCol - sCol, line.length() - sCol));
            }
            return L"";
        }
        std::wstring result;
        for (size_t l = sLine; l <= eLine; ++l) {
            const auto& line = inputBuffer.GetLine(l);
            if (l == sLine) {
                if (sCol < line.length()) result += line.substr(sCol);
            } else if (l == eLine) {
                if (eCol <= line.length()) result += line.substr(0, eCol);
                else result += line;
            } else {
                result += line;
            }
            if (l < eLine) result += L"\n";
        }
        return result;
    }

    void SelectWordAt(size_t lineIdx, size_t colIdx) {
        if (lineIdx >= inputBuffer.GetLineCount()) return;
        const auto& line = inputBuffer.GetLine(lineIdx);
        if (line.empty()) return;
        if (colIdx >= line.length()) colIdx = line.length() > 0 ? line.length() - 1 : 0;

        auto isWordChar = [](wchar_t ch) {
            return iswalnum(ch) || ch == L'_' || ch == L'$';
        };

        if (!isWordChar(line[colIdx])) {
            return;
        }

        size_t startCol = colIdx;
        while (startCol > 0 && isWordChar(line[startCol - 1])) {
            startCol--;
        }
        size_t endCol = colIdx;
        while (endCol < line.length() && isWordChar(line[endCol])) {
            endCol++;
        }

        selAnchor = { lineIdx, startCol };
        selEnd = { lineIdx, endCol };
        hasSelection = true;
        inputBuffer.SetCursor(lineIdx, endCol);
        selectedWord = line.substr(startCol, endCol - startCol);
    }

    bool DeleteSelection() {
        if (!HasSelection()) return false;
        inputBuffer.PushUndo();
        size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
        GetSelectionRange(sLine, sCol, eLine, eCol);
        if (sLine == eLine) {
            const auto& line = inputBuffer.GetLine(sLine);
            std::wstring newLine = line.substr(0, sCol);
            if (eCol < line.length()) {
                newLine += line.substr(eCol);
            }
            inputBuffer.SetLine(sLine, newLine);
            inputBuffer.SetCursor(sLine, sCol);
        } else {
            const auto& startLineText = inputBuffer.GetLine(sLine);
            const auto& endLineText = inputBuffer.GetLine(eLine);
            std::wstring merged = startLineText.substr(0, sCol);
            if (eCol < endLineText.length()) {
                merged += endLineText.substr(eCol);
            }
            std::vector<std::wstring> newLines;
            for (size_t l = 0; l < inputBuffer.GetLineCount(); ++l) {
                if (l < sLine) {
                    newLines.push_back(inputBuffer.GetLine(l));
                } else if (l == sLine) {
                    newLines.push_back(merged);
                } else if (l > eLine) {
                    newLines.push_back(inputBuffer.GetLine(l));
                }
            }
            inputBuffer.SetLines(newLines);
            inputBuffer.SetCursor(sLine, sCol);
        }
        ClearSelection();
        return true;
    }

    // Direct2D bounding rectangles updated during layout/render
    D2D1_RECT_F bounds{};
    D2D1_RECT_F runBtnBounds{};
    D2D1_RECT_F deleteBtnBounds{};
    D2D1_RECT_F moveUpBtnBounds{};
    D2D1_RECT_F moveDownBtnBounds{};
    D2D1_RECT_F dragHandleBounds{};
    D2D1_RECT_F codeAreaBounds{};
    D2D1_RECT_F outputAreaBounds{};

    // Cell Output Scrollbars (Vertical & Horizontal)
    D2D1_RECT_F outputVScrollTrackBounds{};
    D2D1_RECT_F outputVScrollThumbBounds{};
    D2D1_RECT_F outputHScrollTrackBounds{};
    D2D1_RECT_F outputHScrollThumbBounds{};
    float outputMaxScrollY = 0.0f;
    float outputMaxScrollX = 0.0f;

    // Bottom border hover (+ Code, + Text) pill buttons (Colab style)
    D2D1_RECT_F bottomDividerBounds{};
    D2D1_RECT_F bottomAddCodeBounds{};
    D2D1_RECT_F bottomAddTextBounds{};
};

class NotebookDocument {
public:
    NotebookDocument() {
        InitDefaultScratchpad();
    }
    ~NotebookDocument() = default;

    std::wstring filePath;
    std::wstring title = L"Notebook 1.ipynb";
    std::wstring kernelLanguage = L"python";
    std::vector<NotebookCell> cells;
    int activeCellIdx = 0;
    bool isRunningAll = false;
    bool isDirty = false;
    int nextExecutionCount = 1;
    float scrollY = 0.0f;

    void InitDefaultScratchpad() {
        cells.clear();
        nextExecutionCount = 1;
        scrollY = 0.0f;

        // Exactly one cell with "#Welcome to Phide notebook" as requested by the user
        // Initialized as a Text (Markdown) cell, not a Code cell
        NotebookCell cell;
        cell.id = "text-cell-1";
        cell.type = NotebookCellType::Markdown;
        cell.language = L"markdown";
        cell.inputBuffer = Buffer(L"#Welcome to Phide notebook\n");
        cells.push_back(std::move(cell));

        activeCellIdx = 0;
    }

    NotebookCell* AddCell(NotebookCellType type, int afterIdx = -1) {
        NotebookCell newCell;
        newCell.id = "cell-" + std::to_string(GetTickCount64()) + "-" + std::to_string(cells.size() + 1);
        newCell.type = type;
        newCell.language = kernelLanguage;
        newCell.inputBuffer = Buffer(L"");

        if (afterIdx < 0 || afterIdx >= static_cast<int>(cells.size())) {
            cells.push_back(std::move(newCell));
            activeCellIdx = static_cast<int>(cells.size() - 1);
            isDirty = true;
            return &cells.back();
        } else {
            auto it = cells.insert(cells.begin() + afterIdx + 1, std::move(newCell));
            activeCellIdx = afterIdx + 1;
            isDirty = true;
            return &(*it);
        }
    }

    void DeleteCell(int idx) {
        if (idx < 0 || idx >= static_cast<int>(cells.size())) return;
        if (cells.size() <= 1) {
            // Keep at least one cell
            cells[0].inputBuffer = Buffer(L"");
            cells[0].outputs.clear();
            cells[0].executionCount = -1;
            activeCellIdx = 0;
            isDirty = true;
            return;
        }
        cells.erase(cells.begin() + idx);
        if (activeCellIdx >= static_cast<int>(cells.size())) {
            activeCellIdx = static_cast<int>(cells.size() - 1);
        }
        isDirty = true;
    }

    void MoveCell(int fromIdx, int toIdx) {
        if (fromIdx < 0 || fromIdx >= static_cast<int>(cells.size())) return;
        if (toIdx < 0 || toIdx >= static_cast<int>(cells.size()) || fromIdx == toIdx) return;
        auto cell = std::move(cells[fromIdx]);
        cells.erase(cells.begin() + fromIdx);
        cells.insert(cells.begin() + toIdx, std::move(cell));
        activeCellIdx = toIdx;
        isDirty = true;
    }

    void ClearAllOutputs() {
        for (auto& c : cells) {
            c.outputs.clear();
            c.executionCount = -1;
            c.isExecuting = false;
        }
        nextExecutionCount = 1;
        isDirty = true;
    }

    static std::string UnescapeJsonString(const std::string& raw) {
        std::string unescaped;
        unescaped.reserve(raw.size());
        for (size_t k = 0; k < raw.size(); ++k) {
            if (raw[k] == '\\' && k + 1 < raw.size()) {
                if (raw[k + 1] == 'n') { unescaped += '\n'; k++; }
                else if (raw[k + 1] == '"') { unescaped += '"'; k++; }
                else if (raw[k + 1] == '\\') { unescaped += '\\'; k++; }
                else if (raw[k + 1] == 't') { unescaped += '\t'; k++; }
                else if (raw[k + 1] == 'r') { unescaped += '\r'; k++; }
                else if (raw[k + 1] == 'u' && k + 5 < raw.size()) {
                    std::string hexStr = raw.substr(k + 2, 4);
                    try {
                        unsigned int cp = std::stoul(hexStr, nullptr, 16);
                        if (cp == 0x001b || cp == 0x1b) {
                            unescaped += '\x1b';
                        } else if (cp < 128) {
                            unescaped += static_cast<char>(cp);
                        } else if (cp <= 0x7FF) {
                            unescaped += static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
                            unescaped += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            unescaped += static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
                            unescaped += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            unescaped += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        k += 5;
                    } catch (...) {
                        unescaped += raw[k];
                    }
                } else {
                    unescaped += raw[k];
                }
            } else {
                unescaped += raw[k];
            }
        }
        return unescaped;
    }

    static std::wstring StripAnsi(const std::wstring& s) {
        std::wstring out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == L'\x1b' && i + 1 < s.size() && s[i + 1] == L'[') {
                i += 2;
                while (i < s.size() && s[i] != L'm' && s[i] != L'K' && s[i] != L'H' && s[i] != L'J') {
                    i++;
                }
                continue;
            }
            if (i + 6 <= s.size() && s.compare(i, 6, L"\\u001b") == 0) {
                i += 5;
                if (i + 1 < s.size() && s[i + 1] == L'[') {
                    i += 2;
                    while (i < s.size() && s[i] != L'm' && s[i] != L'K' && s[i] != L'H' && s[i] != L'J') {
                        i++;
                    }
                }
                continue;
            }
            out.push_back(s[i]);
        }
        return out;
    }

    static std::wstring ExtractStringOrArray(const std::string& parentStr, size_t keyPos) {
        size_t colon = parentStr.find(':', keyPos);
        if (colon == std::string::npos) return L"";
        size_t firstChar = parentStr.find_first_not_of(" \t\r\n", colon + 1);
        if (firstChar == std::string::npos) return L"";

        std::string accumulated;
        if (parentStr[firstChar] == '"') {
            size_t endQ = firstChar + 1;
            while (endQ < parentStr.size()) {
                if (parentStr[endQ] == '"' && parentStr[endQ - 1] != '\\') break;
                endQ++;
            }
            if (endQ < parentStr.size()) {
                accumulated = UnescapeJsonString(parentStr.substr(firstChar + 1, endQ - firstChar - 1));
            }
        } else if (parentStr[firstChar] == '[') {
            size_t arrEnd = std::string::npos;
            int depth = 0;
            bool inStr = false;
            for (size_t i = firstChar; i < parentStr.size(); ++i) {
                if (parentStr[i] == '"' && (i == 0 || parentStr[i - 1] != '\\')) {
                    inStr = !inStr;
                } else if (!inStr) {
                    if (parentStr[i] == '[') depth++;
                    else if (parentStr[i] == ']') {
                        depth--;
                        if (depth == 0) { arrEnd = i; break; }
                    }
                }
            }
            if (arrEnd != std::string::npos) {
                size_t p = firstChar + 1;
                while (p < arrEnd) {
                    size_t s1 = parentStr.find('"', p);
                    if (s1 == std::string::npos || s1 >= arrEnd) break;
                    size_t s2 = s1 + 1;
                    while (s2 < arrEnd) {
                        if (parentStr[s2] == '"' && parentStr[s2 - 1] != '\\') break;
                        s2++;
                    }
                    if (s2 <= arrEnd) {
                        accumulated += UnescapeJsonString(parentStr.substr(s1 + 1, s2 - s1 - 1));
                    }
                    p = s2 + 1;
                }
            }
        }

        if (accumulated.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, accumulated.c_str(), -1, nullptr, 0);
        if (len <= 0) return L"";
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, accumulated.c_str(), -1, w.data(), len);
        if (!w.empty() && w.back() == L'\0') w.pop_back();
        return w;
    }

    std::string EscapeJson(const std::string& s) const {
        std::string res;
        for (char c : s) {
            if (c == '"') res += "\\\"";
            else if (c == '\\') res += "\\\\";
            else if (c == '\n') res += "\\n";
            else if (c == '\r') res += "\\r";
            else if (c == '\t') res += "\\t";
            else res += c;
        }
        return res;
    }

    std::string WideToUtf8(const std::wstring& w) const {
        if (w.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return "";
        std::string s(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
        if (!s.empty() && s.back() == '\0') s.pop_back();
        return s;
    }

    std::wstring Utf8ToWide(const std::string& u) const {
        if (u.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
        if (len <= 0) return L"";
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, w.data(), len);
        if (!w.empty() && w.back() == L'\0') w.pop_back();
        return w;
    }

    std::string ToJson() const {
        std::ostringstream ss;
        ss << "{\n";
        ss << " \"cells\": [\n";
        for (size_t i = 0; i < cells.size(); ++i) {
            const auto& c = cells[i];
            ss << "  {\n";
            ss << "   \"cell_type\": \"" << (c.type == NotebookCellType::Code ? "code" : "markdown") << "\",\n";
            if (c.type == NotebookCellType::Code) {
                if (c.executionCount > 0) ss << "   \"execution_count\": " << c.executionCount << ",\n";
                else ss << "   \"execution_count\": null,\n";
            }
            ss << "   \"metadata\": {},\n";
            ss << "   \"outputs\": [\n";
            for (size_t oi = 0; oi < c.outputs.size(); ++oi) {
                const auto& out = c.outputs[oi];
                ss << "    {\n";
                ss << "     \"name\": \"" << WideToUtf8(out.streamName) << "\",\n";
                ss << "     \"output_type\": \"stream\",\n";
                ss << "     \"text\": [\n";
                std::string u8Text = WideToUtf8(out.text);
                std::istringstream textStream(u8Text);
                std::string line;
                bool firstLine = true;
                while (std::getline(textStream, line)) {
                    if (!firstLine) ss << ",\n";
                    firstLine = false;
                    ss << "      \"" << EscapeJson(line + "\n") << "\"";
                }
                ss << "\n     ]\n";
                ss << "    }" << (oi + 1 < c.outputs.size() ? "," : "") << "\n";
            }
            ss << "   ],\n";
            ss << "   \"source\": [\n";
            const auto& lines = c.inputBuffer.GetLines();
            for (size_t li = 0; li < lines.size(); ++li) {
                std::string u8Line = WideToUtf8(lines[li]);
                if (li + 1 < lines.size()) u8Line += "\n";
                ss << "    \"" << EscapeJson(u8Line) << "\"" << (li + 1 < lines.size() ? "," : "") << "\n";
            }
            ss << "   ]\n";
            ss << "  }" << (i + 1 < cells.size() ? "," : "") << "\n";
        }
        ss << " ],\n";
        ss << " \"metadata\": {\n";
        ss << "  \"language_info\": {\n";
        ss << "   \"name\": \"" << WideToUtf8(kernelLanguage) << "\"\n";
        ss << "  }\n";
        ss << " },\n";
        ss << " \"nbformat\": 4,\n";
        ss << " \"nbformat_minor\": 5\n";
        ss << "}\n";
        return ss.str();
    }

    bool SaveToFile(const std::wstring& path) {
        std::ofstream ofs(path, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!ofs.is_open()) return false;
        std::string jsonStr = ToJson();
        ofs.write(jsonStr.data(), jsonStr.size());
        ofs.close();
        filePath = path;
        title = std::filesystem::path(path).filename().wstring();
        isDirty = false;
        return true;
    }

    bool LoadFromFile(const std::wstring& path) {
        std::ifstream ifs(path, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) {
            InitDefaultScratchpad();
            return false;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        ifs.close();
        filePath = path;
        title = std::filesystem::path(path).filename().wstring();
        bool ok = FromJson(ss.str());
        if (!ok || cells.empty()) {
            InitDefaultScratchpad();
        }
        return ok;
    }

    bool FromJson(const std::string& json) {
        if (json.empty() || json.find("\"cells\"") == std::string::npos) return false;
        cells.clear();
        nextExecutionCount = 1;

        // Parse cells array from Jupyter JSON
        size_t cellsPos = json.find("\"cells\"");
        size_t arrStart = json.find('[', cellsPos);
        if (arrStart == std::string::npos) return false;

        // Find closing ']' for cells array
        size_t arrEnd = std::string::npos;
        int depth = 0;
        bool inStr = false;
        for (size_t i = arrStart; i < json.size(); ++i) {
            if (json[i] == '"' && (i == 0 || json[i - 1] != '\\')) {
                inStr = !inStr;
            } else if (!inStr) {
                if (json[i] == '[') depth++;
                else if (json[i] == ']') {
                    depth--;
                    if (depth == 0) {
                        arrEnd = i;
                        break;
                    }
                }
            }
        }
        if (arrEnd == std::string::npos) return false;

        size_t pos = arrStart + 1;
        while (pos < arrEnd) {
            size_t cellStart = json.find('{', pos);
            if (cellStart == std::string::npos || cellStart >= arrEnd) break;

            // Find end of cell object balancing braces while ignoring chars inside strings
            size_t cellEnd = cellStart;
            int cDepth = 0;
            bool cInStr = false;
            for (size_t i = cellStart; i < arrEnd; ++i) {
                if (json[i] == '"' && (i == 0 || json[i - 1] != '\\')) {
                    cInStr = !cInStr;
                } else if (!cInStr) {
                    if (json[i] == '{') cDepth++;
                    else if (json[i] == '}') {
                        cDepth--;
                        if (cDepth == 0) {
                            cellEnd = i;
                            break;
                        }
                    }
                }
            }
            if (cDepth != 0 || cellEnd <= cellStart) break;

            std::string cellStr = json.substr(cellStart, cellEnd - cellStart + 1);
            NotebookCell cell;
            cell.id = "cell-" + std::to_string(cells.size() + 1);

            // Extract cell_type
            if (cellStr.find("\"cell_type\": \"markdown\"") != std::string::npos ||
                cellStr.find("\"cell_type\":\"markdown\"") != std::string::npos) {
                cell.type = NotebookCellType::Markdown;
                cell.language = L"markdown";
            } else {
                cell.type = NotebookCellType::Code;
                cell.language = kernelLanguage;
            }

            // Extract source lines (supports array or single string)
            size_t srcPos = cellStr.find("\"source\"");
            if (srcPos != std::string::npos) {
                std::wstring srcText = ExtractStringOrArray(cellStr, srcPos);
                cell.inputBuffer = Buffer(srcText);
            }

            // Extract outputs for code cell (supports multiple outputs, Colab streams, error tracebacks, text/plain)
            if (cell.type == NotebookCellType::Code) {
                size_t outPos = cellStr.find("\"outputs\"");
                if (outPos != std::string::npos) {
                    size_t outArrStart = cellStr.find('[', outPos);
                    if (outArrStart != std::string::npos) {
                        size_t outArrEnd = std::string::npos;
                        int oDepth = 0;
                        bool oStr = false;
                        for (size_t k = outArrStart; k < cellStr.size(); ++k) {
                            if (cellStr[k] == '"' && (k == 0 || cellStr[k - 1] != '\\')) {
                                oStr = !oStr;
                            } else if (!oStr) {
                                if (cellStr[k] == '[') oDepth++;
                                else if (cellStr[k] == ']') {
                                    oDepth--;
                                    if (oDepth == 0) {
                                        outArrEnd = k;
                                        break;
                                    }
                                }
                            }
                        }

                        if (outArrEnd != std::string::npos) {
                            size_t objPos = outArrStart + 1;
                            while (objPos < outArrEnd) {
                                size_t oStart = cellStr.find('{', objPos);
                                if (oStart == std::string::npos || oStart >= outArrEnd) break;
                                size_t oEnd = oStart;
                                int bDepth = 0;
                                bool bStr = false;
                                for (size_t i = oStart; i < outArrEnd; ++i) {
                                    if (cellStr[i] == '"' && (i == 0 || cellStr[i - 1] != '\\')) {
                                        bStr = !bStr;
                                    } else if (!bStr) {
                                        if (cellStr[i] == '{') bDepth++;
                                        else if (cellStr[i] == '}') {
                                            bDepth--;
                                            if (bDepth == 0) {
                                                oEnd = i;
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (bDepth != 0 || oEnd <= oStart) break;

                                std::string outObjStr = cellStr.substr(oStart, oEnd - oStart + 1);
                                CellOutput co;
                                co.streamName = L"stdout";

                                bool isErr = (outObjStr.find("\"output_type\": \"error\"") != std::string::npos ||
                                              outObjStr.find("\"output_type\":\"error\"") != std::string::npos);
                                co.isError = isErr;
                                if (outObjStr.find("\"name\": \"stderr\"") != std::string::npos ||
                                    outObjStr.find("\"name\":\"stderr\"") != std::string::npos) {
                                    co.streamName = L"stderr";
                                }
                                if (isErr) {
                                    co.streamName = L"stderr";
                                    cell.hasError = true;
                                }

                                std::wstring outText;
                                size_t tPos = outObjStr.find("\"text\"");
                                if (tPos != std::string::npos) {
                                    outText += ExtractStringOrArray(outObjStr, tPos);
                                }
                                size_t tbPos = outObjStr.find("\"traceback\"");
                                if (tbPos != std::string::npos) {
                                    std::wstring tb = ExtractStringOrArray(outObjStr, tbPos);
                                    if (!tb.empty()) {
                                        if (!outText.empty()) outText += L"\n";
                                        outText += tb;
                                    }
                                }
                                if (outText.empty()) {
                                    size_t tpPos = outObjStr.find("\"text/plain\"");
                                    if (tpPos != std::string::npos) {
                                        outText = ExtractStringOrArray(outObjStr, tpPos);
                                    }
                                }

                                // Strip ANSI color escape codes
                                outText = StripAnsi(outText);

                                if (!outText.empty()) {
                                    co.text = outText;
                                    cell.outputs.push_back(std::move(co));
                                    if (cell.executionCount <= 0) {
                                        cell.executionCount = nextExecutionCount++;
                                    }
                                }

                                objPos = oEnd + 1;
                            }
                        }
                    }
                }
            }

            cells.push_back(std::move(cell));
            pos = cellEnd + 1;
        }

        if (cells.empty()) {
            InitDefaultScratchpad();
        }
        activeCellIdx = 0;
        isDirty = false;
        return true;
    }
};

} // namespace phide::editor
