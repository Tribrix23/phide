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
    bool isOutputCollapsed = false;
    bool hasError = false;
    ULONGLONG lastElapsedMs = 0;

    // Direct2D bounding rectangles updated during layout/render
    D2D1_RECT_F bounds{};
    D2D1_RECT_F runBtnBounds{};
    D2D1_RECT_F deleteBtnBounds{};
    D2D1_RECT_F moveUpBtnBounds{};
    D2D1_RECT_F moveDownBtnBounds{};
    D2D1_RECT_F dragHandleBounds{};
    D2D1_RECT_F codeAreaBounds{};
    D2D1_RECT_F outputAreaBounds{};

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

    void InitDefaultScratchpad() {
        cells.clear();
        nextExecutionCount = 1;

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
        if (!ifs.is_open()) return false;
        std::ostringstream ss;
        ss << ifs.rdbuf();
        ifs.close();
        filePath = path;
        title = std::filesystem::path(path).filename().wstring();
        return FromJson(ss.str());
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

            // Extract source lines
            size_t srcPos = cellStr.find("\"source\"");
            if (srcPos != std::string::npos) {
                size_t sArrStart = cellStr.find('[', srcPos);
                if (sArrStart != std::string::npos) {
                    size_t sArrEnd = std::string::npos;
                    int aDepth = 0;
                    bool aStr = false;
                    for (size_t k = sArrStart; k < cellStr.size(); ++k) {
                        if (cellStr[k] == '"' && (k == 0 || cellStr[k - 1] != '\\')) {
                            aStr = !aStr;
                        } else if (!aStr) {
                            if (cellStr[k] == '[') aDepth++;
                            else if (cellStr[k] == ']') {
                                aDepth--;
                                if (aDepth == 0) {
                                    sArrEnd = k;
                                    break;
                                }
                            }
                        }
                    }

                    if (sArrEnd != std::string::npos) {
                        std::wstring fullSource;
                        size_t p = sArrStart;
                        while (p < sArrEnd) {
                            size_t strStart = cellStr.find('"', p);
                            if (strStart == std::string::npos || strStart >= sArrEnd) break;
                            size_t strEnd = strStart + 1;
                            while (strEnd < sArrEnd) {
                                if (cellStr[strEnd] == '"' && cellStr[strEnd - 1] != '\\') break;
                                strEnd++;
                            }
                            std::string raw = cellStr.substr(strStart + 1, strEnd - strStart - 1);
                            std::string unescaped;
                            for (size_t k = 0; k < raw.size(); ++k) {
                                if (raw[k] == '\\' && k + 1 < raw.size()) {
                                    if (raw[k + 1] == 'n') { unescaped += '\n'; k++; }
                                    else if (raw[k + 1] == '"') { unescaped += '"'; k++; }
                                    else if (raw[k + 1] == '\\') { unescaped += '\\'; k++; }
                                    else if (raw[k + 1] == 't') { unescaped += '\t'; k++; }
                                    else { unescaped += raw[k]; }
                                } else {
                                    unescaped += raw[k];
                                }
                            }
                            fullSource += Utf8ToWide(unescaped);
                            p = strEnd + 1;
                        }
                        cell.inputBuffer = Buffer(fullSource);
                    }
                }
            }

            // Extract outputs for code cell
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
                            size_t textPos = cellStr.find("\"text\"", outArrStart);
                            if (textPos != std::string::npos && textPos < outArrEnd) {
                                size_t tStart = cellStr.find('[', textPos);
                                if (tStart != std::string::npos) {
                                    size_t tEnd = std::string::npos;
                                    int tDepth = 0;
                                    bool tStr = false;
                                    for (size_t k = tStart; k < outArrEnd; ++k) {
                                        if (cellStr[k] == '"' && (k == 0 || cellStr[k - 1] != '\\')) {
                                            tStr = !tStr;
                                        } else if (!tStr) {
                                            if (cellStr[k] == '[') tDepth++;
                                            else if (cellStr[k] == ']') {
                                                tDepth--;
                                                if (tDepth == 0) {
                                                    tEnd = k;
                                                    break;
                                                }
                                            }
                                        }
                                    }

                                    if (tEnd != std::string::npos) {
                                        std::wstring outText;
                                        size_t p = tStart;
                                        while (p < tEnd) {
                                            size_t s1 = cellStr.find('"', p);
                                            if (s1 == std::string::npos || s1 >= tEnd) break;
                                            size_t s2 = s1 + 1;
                                            while (s2 < tEnd) {
                                                if (cellStr[s2] == '"' && cellStr[s2 - 1] != '\\') break;
                                                s2++;
                                            }
                                            std::string raw = cellStr.substr(s1 + 1, s2 - s1 - 1);
                                            std::string unesc;
                                            for (size_t k = 0; k < raw.size(); ++k) {
                                                if (raw[k] == '\\' && k + 1 < raw.size()) {
                                                    if (raw[k + 1] == 'n') { unesc += '\n'; k++; }
                                                    else if (raw[k + 1] == '"') { unesc += '"'; k++; }
                                                    else if (raw[k + 1] == '\\') { unesc += '\\'; k++; }
                                                    else if (raw[k + 1] == 't') { unesc += '\t'; k++; }
                                                    else { unesc += raw[k]; }
                                                } else {
                                                    unesc += raw[k];
                                                }
                                            }
                                            outText += Utf8ToWide(unesc);
                                            p = s2 + 1;
                                        }
                                        if (!outText.empty()) {
                                            CellOutput co;
                                            co.streamName = L"stdout";
                                            co.text = outText;
                                            cell.outputs.push_back(std::move(co));
                                            cell.executionCount = nextExecutionCount++;
                                        }
                                    }
                                }
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
