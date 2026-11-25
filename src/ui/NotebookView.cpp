/**
 * @file NotebookView.cpp
 * @brief Implementation of the interactive Jupyter / Colab-style notebook interface.
 */

#include "phide/ui/NotebookView.hpp"
#include "phide/ui/EditorCanvas.hpp"
#include "phide/editor/SyntaxValidator.hpp"
#include "phide/editor/BracketPairAnalyzer.hpp"
#include <commdlg.h>
#include <shellapi.h>
#include <winhttp.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <cwctype>

namespace {
std::wstring NbUtf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string UnescapeJsonField(const std::string& s, size_t& pos) {
    if (pos >= s.size() || s[pos] != '"') return "";
    ++pos;
    std::string out;
    while (pos < s.size()) {
        char c = s[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < s.size()) {
            char esc = s[pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case 'n': out.push_back(' '); break;
                case 'r': break;
                case 't': out.push_back(' '); break;
                default: out.push_back(esc); break;
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::vector<phide::editor::NotebookVariableInfo> ParseNotebookVarsJson(const std::string& jsonStr) {
    std::vector<phide::editor::NotebookVariableInfo> vars;
    size_t i = 0;
    while (i < jsonStr.size() && vars.size() < 64u) {
        size_t objStart = jsonStr.find('{', i);
        if (objStart == std::string::npos) break;
        size_t objEnd = jsonStr.find('}', objStart);
        if (objEnd == std::string::npos) break;
        phide::editor::NotebookVariableInfo info;
        size_t p = objStart;
        while (p < objEnd) {
            size_t q = jsonStr.find('"', p);
            if (q == std::string::npos || q >= objEnd) break;
            std::string key = UnescapeJsonField(jsonStr, q);
            size_t colon = jsonStr.find(':', q);
            if (colon == std::string::npos || colon >= objEnd) break;
            size_t vq = jsonStr.find('"', colon + 1);
            if (vq == std::string::npos || vq >= objEnd) {
                p = colon + 1;
                continue;
            }
            std::string val = UnescapeJsonField(jsonStr, vq);
            if (key == "name") info.name = NbUtf8ToWide(val);
            else if (key == "type") info.typeName = NbUtf8ToWide(val);
            else if (key == "value") info.valuePreview = NbUtf8ToWide(val);
            p = vq;
        }
        if (!info.name.empty()) {
            vars.push_back(std::move(info));
        }
        i = objEnd + 1;
    }
    return vars;
}

std::wstring FormatKernelShortLabel(const std::wstring& exePath, const std::wstring& workspacePath = L"") {
    if (exePath.empty()) return L"Python";
    std::filesystem::path p(exePath);
    std::wstring parent = p.parent_path().filename().wstring();
    std::wstring grand = p.parent_path().parent_path().filename().wstring();
    if (_wcsicmp(parent.c_str(), L"Scripts") == 0 && !grand.empty()) {
        std::filesystem::path projectDir = p.parent_path().parent_path().parent_path();
        std::wstring projName = projectDir.filename().wstring();
        if (!workspacePath.empty() && !projName.empty()) {
            std::filesystem::path wsObj(workspacePath);
            std::wstring wsName = wsObj.filename().wstring();
            if (!wsName.empty() && _wcsicmp(projName.c_str(), wsName.c_str()) != 0) {
                return grand + L" (" + projName + L")";
            }
        }
        return grand;
    }
    if (!parent.empty() && _wcsicmp(parent.c_str(), L"bin") != 0) {
        return parent;
    }
    return p.filename().wstring();
}
void SetClipboardText(const std::wstring& text, HWND hwnd) {
    if (text.empty()) return;
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t bytes = (text.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* pMem = GlobalLock(hMem);
            if (pMem) {
                memcpy(pMem, text.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
}

std::wstring GetClipboardText(HWND hwnd) {
    std::wstring result;
    if (OpenClipboard(hwnd)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            LPCWSTR pText = static_cast<LPCWSTR>(GlobalLock(hData));
            if (pText) {
                result = pText;
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }
    return result;
}

struct NotebookOutputSpan {
    std::wstring text;
    D2D1_COLOR_F color;
    bool bold = false;
};

std::vector<NotebookOutputSpan> ParseNotebookOutputLine(
    const std::wstring& rawLine,
    D2D1_COLOR_F defaultColor,
    bool isCellError
) {
    // 1. Check for ANSI escape sequences (\x1b[)
    if (rawLine.find(L"\x1b[") != std::wstring::npos) {
        std::vector<NotebookOutputSpan> spans;
        D2D1_COLOR_F curColor = isCellError ? D2D1::ColorF(0.95f, 0.35f, 0.35f, 1.0f) : defaultColor;
        bool bold = false;
        std::wstring curText;
        size_t i = 0;

        while (i < rawLine.length()) {
            if (rawLine[i] == L'\x1b' && i + 1 < rawLine.length() && rawLine[i + 1] == L'[') {
                size_t j = i + 2;
                while (j < rawLine.length() && (rawLine[j] < 0x40 || rawLine[j] > 0x7E)) {
                    j++;
                }
                if (j < rawLine.length()) {
                    wchar_t cmd = rawLine[j];
                    std::wstring seq = rawLine.substr(i + 2, j - (i + 2));

                    if (!curText.empty()) {
                        spans.push_back({ curText, curColor, bold });
                        curText.clear();
                    }

                    if (cmd == L'm') {
                        std::wstringstream sss(seq.empty() ? L"0" : seq);
                        std::wstring codeStr;
                        while (std::getline(sss, codeStr, L';')) {
                            int code = 0;
                            try { if (!codeStr.empty()) code = std::stoi(codeStr); } catch (...) {}
                            if (code == 0) {
                                curColor = defaultColor;
                                bold = false;
                            } else if (code == 1) {
                                bold = true;
                            } else if (code == 22) {
                                bold = false;
                            } else if (code == 30) {
                                curColor = D2D1::ColorF(0.25f, 0.25f, 0.28f, 1.0f);
                            } else if (code == 31 || code == 91) {
                                curColor = D2D1::ColorF(0.95f, 0.35f, 0.35f, 1.0f); // Bright Red
                            } else if (code == 32 || code == 92) {
                                curColor = D2D1::ColorF(0.35f, 0.82f, 0.45f, 1.0f); // Bright Green
                            } else if (code == 33 || code == 93) {
                                curColor = D2D1::ColorF(0.95f, 0.80f, 0.25f, 1.0f); // Yellow
                            } else if (code == 34 || code == 94) {
                                curColor = D2D1::ColorF(0.38f, 0.68f, 0.98f, 1.0f); // Blue
                            } else if (code == 35 || code == 95) {
                                curColor = D2D1::ColorF(0.82f, 0.50f, 0.90f, 1.0f); // Magenta
                            } else if (code == 36 || code == 96) {
                                curColor = D2D1::ColorF(0.30f, 0.80f, 0.88f, 1.0f); // Cyan
                            } else if (code == 37 || code == 97) {
                                curColor = D2D1::ColorF(0.95f, 0.95f, 0.95f, 1.0f); // White
                            } else if (code == 90) {
                                curColor = D2D1::ColorF(0.55f, 0.58f, 0.62f, 1.0f); // Gray
                            } else if (code == 39) {
                                curColor = defaultColor;
                            }
                        }
                    } else if (cmd == L'K') {
                        spans.clear();
                        curText.clear();
                    }

                    i = j + 1;
                    continue;
                }
            }

            if (rawLine[i] != L'\r' && rawLine[i] != L'\n') {
                curText += rawLine[i];
            }
            i++;
        }

        if (!curText.empty()) {
            spans.push_back({ curText, curColor, bold });
        }
        return spans;
    }

    // 2. Colab-style Semantic Highlighting for plain text outputs (pip, errors, warnings)
    std::wstring clean = rawLine;
    while (!clean.empty() && (clean.back() == L'\r' || clean.back() == L'\n')) {
        clean.pop_back();
    }

    if (clean.rfind(L"Requirement already satisfied:", 0) == 0) {
        std::wstring prefix = L"Requirement already satisfied:";
        std::wstring rest = clean.substr(prefix.length());
        std::vector<NotebookOutputSpan> spans;
        spans.push_back({ prefix, D2D1::ColorF(0.35f, 0.82f, 0.45f, 1.0f), true });
        size_t inPos = rest.find(L" in ");
        if (inPos != std::wstring::npos) {
            spans.push_back({ rest.substr(0, inPos), D2D1::ColorF(0.38f, 0.68f, 0.98f, 1.0f), false });
            spans.push_back({ rest.substr(inPos), D2D1::ColorF(0.58f, 0.62f, 0.66f, 1.0f), false });
        } else {
            spans.push_back({ rest, defaultColor, false });
        }
        return spans;
    }

    if (clean.rfind(L"Successfully installed", 0) == 0) {
        std::wstring prefix = L"Successfully installed";
        std::vector<NotebookOutputSpan> spans;
        spans.push_back({ prefix, D2D1::ColorF(0.35f, 0.82f, 0.45f, 1.0f), true });
        spans.push_back({ clean.substr(prefix.length()), D2D1::ColorF(0.30f, 0.80f, 0.88f, 1.0f), false });
        return spans;
    }

    if (clean.rfind(L"Collecting ", 0) == 0) {
        std::vector<NotebookOutputSpan> spans;
        spans.push_back({ L"Collecting", D2D1::ColorF(0.38f, 0.68f, 0.98f, 1.0f), true });
        spans.push_back({ clean.substr(10), defaultColor, false });
        return spans;
    }

    if (clean.find(L"Downloading ") != std::wstring::npos) {
        size_t pos = clean.find(L"Downloading ");
        std::vector<NotebookOutputSpan> spans;
        if (pos > 0) {
            spans.push_back({ clean.substr(0, pos), defaultColor, false });
        }
        spans.push_back({ L"Downloading", D2D1::ColorF(0.38f, 0.68f, 0.98f, 1.0f), true });
        spans.push_back({ clean.substr(pos + 11), defaultColor, false });
        return spans;
    }

    if (clean.find(L"Using cached ") != std::wstring::npos) {
        size_t pos = clean.find(L"Using cached ");
        std::vector<NotebookOutputSpan> spans;
        if (pos > 0) {
            spans.push_back({ clean.substr(0, pos), defaultColor, false });
        }
        spans.push_back({ L"Using cached", D2D1::ColorF(0.35f, 0.82f, 0.45f, 1.0f), false });
        spans.push_back({ clean.substr(pos + 12), defaultColor, false });
        return spans;
    }

    if (clean.rfind(L"ERROR:", 0) == 0 || clean.rfind(L"Error:", 0) == 0 || isCellError) {
        return { { clean, D2D1::ColorF(0.95f, 0.35f, 0.35f, 1.0f), true } };
    }

    if (clean.rfind(L"WARNING:", 0) == 0 || clean.rfind(L"Warning:", 0) == 0) {
        return { { clean, D2D1::ColorF(0.95f, 0.80f, 0.25f, 1.0f), true } };
    }

    return { { clean, defaultColor, false } };
}
} // anonymous namespace

namespace phide::ui {

float NotebookView::EnsureCharWidth(Theme& theme, const app::AppConfig& config) {
    if (m_charWidth <= 0.0f && theme.dwriteFactory && theme.fontCode) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                L"M", 1, theme.fontCode.Get(), 1000.0f, 100.0f, &pLayout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                m_charWidth = tm.widthIncludingTrailingWhitespace;
            }
        }
    }
    return (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.8f);
}

float NotebookView::ComputeCellLineNumberGutterWidth(
    const editor::NotebookCell& cell, const app::AppConfig& config, float charW) const noexcept {
    if (cell.type == editor::NotebookCellType::Markdown) {
        return config.ScaleF(6.0f);
    }
    size_t lineCount = (std::max)(size_t(1), cell.inputBuffer.GetLineCount());
    size_t digits = 1;
    while (lineCount >= 10) {
        lineCount /= 10;
        ++digits;
    }
    return (std::max)(config.ScaleF(34.0f), static_cast<float>(digits) * charW + config.ScaleF(16.0f));
}

bool NotebookView::HitTestCodeArea(const editor::NotebookCell& cell, POINT clientPt, const app::AppConfig& config,
                                   size_t& outLine, size_t& outCol) const {
    const float lineH = config.ScaleF(20.0f);
    const float charW = (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.8f);
    const float lineNumW = ComputeCellLineNumberGutterWidth(cell, config, charW);
    const float textX = cell.codeAreaBounds.left + lineNumW + config.ScaleF(10.0f) - cell.codeScrollX;
    float relY = static_cast<float>(clientPt.y) - cell.codeAreaBounds.top - config.ScaleF(6.0f);
    if (relY < 0.0f) relY = 0.0f;

    size_t totalLines = cell.inputBuffer.GetLineCount();
    if (totalLines == 0) {
        outLine = 0;
        outCol = 0;
        return true;
    }

    size_t lineIdx = static_cast<size_t>(relY / lineH);
    if (lineIdx >= totalLines) {
        lineIdx = totalLines - 1;
    }

    const auto& lineStr = cell.inputBuffer.GetLine(lineIdx);
    const float relX = static_cast<float>(clientPt.x) - textX;
    if (relX <= 0.0f) {
        outLine = lineIdx;
        outCol = 0;
        return true;
    }

    const float swatchSpan = config.ScaleF(14.0f);
    std::vector<LineColorSpan> lineColors;
    if (lineStr.length() <= 2048) {
        lineColors = EditorCanvas::ScanLineColors(lineStr, false);
    }
    const int safeTab = (config.tabSize > 0) ? config.tabSize : 4;
    outLine = lineIdx;
    outCol = EditorCanvas::HitTestVisualLineColumn(lineStr, lineColors, relX, charW, swatchSpan, safeTab);
    return true;
}

namespace {
void CollectDefinedPythonNames(const editor::Buffer& buf, std::unordered_set<std::wstring>& outNames) {
    auto isIdStart = [](wchar_t ch) { return iswalpha(ch) || ch == L'_'; };
    auto isIdCont = [](wchar_t ch) { return iswalnum(ch) || ch == L'_'; };
    const size_t nLines = buf.GetLineCount();
    for (size_t li = 0; li < nLines; ++li) {
        const auto& raw = buf.GetLine(li);
        std::wstring line;
        for (wchar_t ch : raw) {
            if (ch == L'#') break;
            line.push_back(ch);
        }
        size_t s = 0;
        while (s < line.size() && (line[s] == L' ' || line[s] == L'\t')) ++s;
        if (s >= line.size()) continue;
        std::wstring trimmed = line.substr(s);

        if (trimmed.rfind(L"import ", 0) == 0) {
            std::wstring rest = trimmed.substr(7);
            size_t pos = 0;
            while (pos < rest.size()) {
                size_t comma = rest.find(L',', pos);
                std::wstring item = (comma == std::wstring::npos) ? rest.substr(pos) : rest.substr(pos, comma - pos);
                size_t asPos = item.find(L" as ");
                std::wstring bound;
                if (asPos != std::wstring::npos) {
                    bound = item.substr(asPos + 4);
                } else {
                    size_t dotPos = item.find(L'.');
                    bound = (dotPos == std::wstring::npos) ? item : item.substr(0, dotPos);
                }
                size_t b = 0;
                while (b < bound.size() && iswspace(bound[b])) ++b;
                size_t e = b;
                while (e < bound.size() && isIdCont(bound[e])) ++e;
                if (e > b && isIdStart(bound[b])) outNames.insert(bound.substr(b, e - b));
                if (comma == std::wstring::npos) break;
                pos = comma + 1;
            }
        } else if (trimmed.rfind(L"from ", 0) == 0) {
            size_t impPos = trimmed.find(L" import ");
            if (impPos != std::wstring::npos) {
                std::wstring rest = trimmed.substr(impPos + 8);
                rest.erase(std::remove(rest.begin(), rest.end(), L'('), rest.end());
                rest.erase(std::remove(rest.begin(), rest.end(), L')'), rest.end());
                size_t pos = 0;
                while (pos < rest.size()) {
                    size_t comma = rest.find(L',', pos);
                    std::wstring item = (comma == std::wstring::npos) ? rest.substr(pos) : rest.substr(pos, comma - pos);
                    size_t asPos = item.find(L" as ");
                    std::wstring bound = (asPos != std::wstring::npos) ? item.substr(asPos + 4) : item;
                    size_t b = 0;
                    while (b < bound.size() && iswspace(bound[b])) ++b;
                    size_t e = b;
                    while (e < bound.size() && isIdCont(bound[e])) ++e;
                    if (e > b && isIdStart(bound[b])) outNames.insert(bound.substr(b, e - b));
                    if (comma == std::wstring::npos) break;
                    pos = comma + 1;
                }
            }
        } else if (trimmed.rfind(L"def ", 0) == 0 || trimmed.rfind(L"class ", 0) == 0 || trimmed.rfind(L"async def ", 0) == 0) {
            size_t off = (trimmed.rfind(L"async def ", 0) == 0) ? 10 : ((trimmed.rfind(L"class ", 0) == 0) ? 6 : 4);
            while (off < trimmed.size() && iswspace(trimmed[off])) ++off;
            size_t endId = off;
            while (endId < trimmed.size() && isIdCont(trimmed[endId])) ++endId;
            if (endId > off && isIdStart(trimmed[off])) outNames.insert(trimmed.substr(off, endId - off));
        } else if (s == 0 && isIdStart(trimmed[0])) {
            // Simple top-level targets like `x = ...` or `a, b = ...`
            size_t eqPos = trimmed.find(L'=');
            if (eqPos != std::wstring::npos && (eqPos + 1 >= trimmed.size() || trimmed[eqPos + 1] != L'=')) {
                std::wstring lhs = trimmed.substr(0, eqPos);
                size_t p = 0;
                while (p < lhs.size()) {
                    if (isIdStart(lhs[p])) {
                        size_t e = p + 1;
                        while (e < lhs.size() && isIdCont(lhs[e])) ++e;
                        outNames.insert(lhs.substr(p, e - p));
                        p = e;
                    } else {
                        ++p;
                    }
                }
            }
        }
    }
}
} // namespace

void NotebookView::RefreshCellAnalysis(editor::OpenDocument& doc, size_t cellIdx) {
    if (!doc.notebookDoc || cellIdx >= doc.notebookDoc->cells.size()) return;
    auto nb = doc.notebookDoc;
    const auto& cell = nb->cells[cellIdx];
    if (cell.type != editor::NotebookCellType::Code) return;

    uint64_t ctxVer = 14695981039346656037ULL;
    auto mixVal = [&](uint64_t v) {
        ctxVer ^= v;
        ctxVer *= 1099511628211ULL;
    };
    mixVal(nb->cells.size());
    for (size_t i = 0; i < nb->cells.size(); ++i) {
        if (i == cellIdx || nb->cells[i].type != editor::NotebookCellType::Code) continue;
        for (char c : nb->cells[i].id) mixVal(static_cast<uint64_t>(static_cast<unsigned char>(c)));
        mixVal(nb->cells[i].inputBuffer.GetVersion());
    }
    mixVal(nb->inspectorVariables.size());
    for (const auto& iv : nb->inspectorVariables) {
        for (wchar_t ch : iv.name) mixVal(static_cast<uint64_t>(ch));
    }

    auto& entry = m_cellAnalysisCache[cell.id];
    const uint64_t cellVer = cell.inputBuffer.GetVersion();
    const std::wstring scopePy = !nb->selectedPythonExe.empty() ? nb->selectedPythonExe : m_workspacePath;

    const bool needDiags = (entry.cellId != cell.id || entry.cellVersion != cellVer || entry.contextVersion != ctxVer);
    const bool needCosts = m_importCostFn && (entry.cellId != cell.id || entry.cellVersion != cellVer ||
                                              entry.importCostVersion != m_importCostVersion || entry.pythonExe != scopePy);

    if (needDiags) {
        auto diags = editor::SyntaxValidator::Validate(L"cell.py", cell.inputBuffer);
        std::unordered_set<std::wstring> nbScopeNames;
        for (size_t i = 0; i < nb->cells.size(); ++i) {
            if (i == cellIdx || nb->cells[i].type != editor::NotebookCellType::Code) continue;
            CollectDefinedPythonNames(nb->cells[i].inputBuffer, nbScopeNames);
        }
        for (const auto& v : nb->inspectorVariables) {
            if (!v.name.empty()) nbScopeNames.insert(v.name);
        }

        if (!nbScopeNames.empty()) {
            const std::wstring prefix = L"Python: NameError: name '";
            diags.erase(
                std::remove_if(diags.begin(), diags.end(), [&](const lsp::Diagnostic& d) {
                    if (d.message.rfind(prefix, 0) == 0) {
                        size_t qEnd = d.message.find(L'\'', prefix.length());
                        if (qEnd != std::wstring::npos) {
                            std::wstring sym = d.message.substr(prefix.length(), qEnd - prefix.length());
                            return nbScopeNames.count(sym) > 0;
                        }
                    }
                    return false;
                }),
                diags.end());
        }
        entry.diagnostics = std::move(diags);
        entry.cellVersion = cellVer;
        entry.contextVersion = ctxVer;
    }

    if (needCosts) {
        std::wstring scopeFile = !doc.filePath.empty() ? doc.filePath : m_workspacePath;
        if (scopeFile.empty() && !nb->selectedPythonExe.empty()) {
            scopeFile = nb->selectedPythonExe;
        }
        entry.importCostMap = m_importCostFn(cell.inputBuffer, scopeFile);
        entry.importCostVersion = m_importCostVersion;
        entry.pythonExe = scopePy;
    }
    entry.cellId = cell.id;
}

std::vector<lsp::Diagnostic> NotebookView::GetCellDiagnosticsForTest(const std::string& cellId) const {
    auto it = m_cellAnalysisCache.find(cellId);
    if (it != m_cellAnalysisCache.end()) {
        return it->second.diagnostics;
    }
    return {};
}

void NotebookView::BuildCombinedNotebookLines(
    const editor::NotebookDocument& nb, size_t activeCellIdx,
    std::vector<std::wstring>& outLines, size_t& outLineOffset) const {
    outLines.clear();
    outLineOffset = 0;
    for (size_t i = 0; i < nb.cells.size(); ++i) {
        if (nb.cells[i].type != editor::NotebookCellType::Code) continue;
        if (i == activeCellIdx) {
            outLineOffset = outLines.size();
        }
        const auto& cLines = nb.cells[i].inputBuffer.GetLines();
        outLines.insert(outLines.end(), cLines.begin(), cLines.end());
    }
}

void NotebookView::QueryActiveCellAutocomplete(editor::OpenDocument& doc) {
    if (!m_completionFn || !doc.notebookDoc || doc.notebookDoc->cells.empty()) {
        DismissAutocomplete();
        return;
    }
    auto nb = doc.notebookDoc;
    if (nb->activeCellIdx < 0 || nb->activeCellIdx >= static_cast<int>(nb->cells.size())) {
        DismissAutocomplete();
        return;
    }
    const size_t activeIdx = static_cast<size_t>(nb->activeCellIdx);
    const auto& cell = nb->cells[activeIdx];
    if (cell.type != editor::NotebookCellType::Code || cell.isWaitingForInput) {
        DismissAutocomplete();
        return;
    }
    const auto cur = cell.inputBuffer.GetCursor();
    const auto& lines = cell.inputBuffer.GetLines();
    if (cur.line >= lines.size()) {
        DismissAutocomplete();
        return;
    }
    const std::wstring& lineText = lines[cur.line];
    // Do not suggest inside a # comment
    bool inSq = false, inDq = false;
    for (size_t i = 0; i < cur.col && i < lineText.size(); ++i) {
        wchar_t ch = lineText[i];
        if (ch == L'\\' && (inSq || inDq)) { ++i; continue; }
        if (!inDq && ch == L'\'') inSq = !inSq;
        else if (!inSq && ch == L'"') inDq = !inDq;
        else if (!inSq && !inDq && ch == L'#') {
            DismissAutocomplete();
            return;
        }
    }

    std::vector<std::wstring> combined;
    size_t cellOffset = 0;
    BuildCombinedNotebookLines(*nb, activeIdx, combined, cellOffset);
    auto items = m_completionFn(combined, cellOffset + cur.line, cur.col, lineText);

    // Inject live kernel variables from nb->inspectorVariables matching prefix
    std::wstring prefix = editor::IntelliSenseEngine::ExtractQueryPrefix(lineText, cur.col);
    bool afterDot = (cur.col > prefix.length() && lineText[cur.col - prefix.length() - 1] == L'.');
    if (!afterDot && !nb->inspectorVariables.empty()) {
        std::unordered_set<std::wstring> seen;
        for (const auto& it : items) seen.insert(it.label);
        std::wstring pLow = prefix;
        for (auto& c : pLow) c = static_cast<wchar_t>(towlower(c));
        for (const auto& kv : nb->inspectorVariables) {
            if (kv.name.empty() || !seen.insert(kv.name).second) continue;
            std::wstring nLow = kv.name;
            for (auto& c : nLow) c = static_cast<wchar_t>(towlower(c));
            if (pLow.empty() || nLow.find(pLow) != std::wstring::npos) {
                editor::IntelliSenseItem vi;
                vi.label = kv.name;
                vi.insertText = kv.name;
                vi.detail = kv.typeName + L" = " + kv.valuePreview;
                vi.kind = editor::CompletionKind::Variable;
                vi.score = (pLow.empty() || nLow.rfind(pLow, 0) == 0) ? 1050 : 900;
                vi.matchLen = prefix.length();
                vi.origin = L"kernel";
                items.push_back(std::move(vi));
            }
        }
    }

    if (items.empty()) {
        DismissAutocomplete();
        return;
    }
    m_nbAutocompleteItems = std::move(items);
    m_nbAutocompleteSelectedIdx = 0;
    m_nbAutocompleteScrollOffset = 0;
    m_nbAutocompleteVisible = true;
    m_nbAutocompleteNavigated = false;
}

bool NotebookView::CommitAutocompleteSelection(editor::OpenDocument& doc, HWND hwnd) {
    if (!m_nbAutocompleteVisible || m_nbAutocompleteItems.empty() ||
        m_nbAutocompleteSelectedIdx >= m_nbAutocompleteItems.size() ||
        !doc.notebookDoc) {
        DismissAutocomplete();
        return false;
    }
    auto nb = doc.notebookDoc;
    if (nb->activeCellIdx < 0 || nb->activeCellIdx >= static_cast<int>(nb->cells.size())) {
        DismissAutocomplete();
        return false;
    }
    auto& activeCell = nb->cells[nb->activeCellIdx];
    const auto item = m_nbAutocompleteItems[m_nbAutocompleteSelectedIdx];
    const auto cur = activeCell.inputBuffer.GetCursor();
    const auto& lines = activeCell.inputBuffer.GetLines();
    if (cur.line >= lines.size()) {
        DismissAutocomplete();
        return false;
    }
    const std::wstring& lineText = lines[cur.line];
    const std::wstring prefix = editor::IntelliSenseEngine::ExtractQueryPrefix(lineText, cur.col);

    activeCell.inputBuffer.PushUndo();
    if (activeCell.HasSelection()) {
        activeCell.DeleteSelection();
    }
    for (size_t i = 0; i < prefix.length(); ++i) {
        activeCell.inputBuffer.Backspace();
    }
    std::wstring toInsert = !item.insertText.empty() ? item.insertText : item.label;
    size_t caretOffset = std::wstring::npos;
    size_t d0 = toInsert.find(L"$0");
    if (d0 != std::wstring::npos) {
        caretOffset = d0;
        toInsert.erase(d0, 2);
    } else {
        // Strip `${1:placeholder}` style markers cleanly
        std::wstring cleaned;
        for (size_t i = 0; i < toInsert.size(); ++i) {
            if (toInsert[i] == L'$' && i + 1 < toInsert.size() && toInsert[i + 1] == L'{') {
                size_t colPos = toInsert.find(L':', i + 2);
                size_t closeB = toInsert.find(L'}', i + 2);
                if (closeB != std::wstring::npos) {
                    if (caretOffset == std::wstring::npos) caretOffset = cleaned.size();
                    if (colPos != std::wstring::npos && colPos < closeB) {
                        cleaned += toInsert.substr(colPos + 1, closeB - colPos - 1);
                    }
                    i = closeB;
                    continue;
                }
            }
            cleaned.push_back(toInsert[i]);
        }
        toInsert = std::move(cleaned);
    }

    auto beforeIns = activeCell.inputBuffer.GetCursor();
    activeCell.inputBuffer.InsertText(toInsert);
    if (caretOffset != std::wstring::npos && toInsert.find(L'\n') == std::wstring::npos) {
        activeCell.inputBuffer.SetCursor(beforeIns.line, beforeIns.col + caretOffset);
    }
    nb->isDirty = true;
    DismissAutocomplete();
    m_caretVisible = true;
    m_lastBlinkTime = GetTickCount64();
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

void NotebookView::EnsureActiveCellCursorVisible(editor::OpenDocument& doc) noexcept {
    if (!doc.notebookDoc) return;
    auto nb = doc.notebookDoc;
    if (nb->activeCellIdx < 0 || nb->activeCellIdx >= static_cast<int>(nb->cells.size())) return;
    auto& cell = nb->cells[nb->activeCellIdx];
    const float charW = (m_charWidth > 0.0f) ? m_charWidth : 7.8f;
    const auto cur = cell.inputBuffer.GetCursor();
    const auto& lines = cell.inputBuffer.GetLines();
    if (cur.line < lines.size() && cell.codeAreaBounds.right > cell.codeAreaBounds.left) {
        const float lineNumW = (std::max)(34.0f, static_cast<float>(std::to_wstring(lines.size()).size()) * charW + 16.0f);
        const float availW = (cell.codeAreaBounds.right - 8.0f) - (cell.codeAreaBounds.left + lineNumW + 10.0f);
        if (availW > 20.0f) {
            const auto& lineStr = lines[cur.line];
            std::vector<LineColorSpan> lc = (lineStr.length() <= 2048)
                ? EditorCanvas::ScanLineColors(lineStr, false)
                : std::vector<LineColorSpan>{};
            const float curRelX = EditorCanvas::GetColumnVisualOffset(lineStr, lc, cur.col, charW, 14.0f, m_lastTabSize);
            const float margin = charW * 3.0f;
            if (curRelX < cell.codeScrollX) {
                cell.codeScrollX = (std::max)(0.0f, curRelX - margin);
            } else if (curRelX > cell.codeScrollX + availW - margin) {
                cell.codeScrollX = (std::min)(cell.codeMaxScrollX, (std::max)(0.0f, curRelX - availW + margin * 2.0f));
            }
        }
    }
    if (m_lastViewHeight > 80.0f && cell.codeAreaBounds.bottom > cell.codeAreaBounds.top) {
        const float lineH = (m_lastLineH > 8.0f) ? m_lastLineH : 20.0f;
        const float caretAbsY = cell.codeAreaBounds.top + 6.0f + static_cast<float>(cur.line) * lineH;
        const float viewTop = m_lastViewTop + 44.0f;
        const float viewBot = m_lastViewTop + m_lastViewHeight;
        if (caretAbsY < viewTop + 12.0f) {
            m_scrollY = (std::max)(0.0f, m_scrollY - (viewTop + 12.0f - caretAbsY));
            nb->scrollY = m_scrollY;
        } else if (caretAbsY + lineH > viewBot - 16.0f) {
            m_scrollY += (caretAbsY + lineH - (viewBot - 16.0f));
            nb->scrollY = m_scrollY;
        }
    }
}

std::unordered_map<size_t, std::wstring> NotebookView::GetCellImportCostsForTest(const std::string& cellId) const {
    auto it = m_cellAnalysisCache.find(cellId);
    if (it != m_cellAnalysisCache.end()) {
        return it->second.importCostMap;
    }
    return {};
}

std::vector<std::wstring> NotebookView::GetOutputLines(const editor::NotebookCell& cell) const {
    std::vector<std::wstring> lines;
    for (const auto& out : cell.outputs) {
        std::wstringstream ss(editor::NotebookDocument::StripAnsi(out.text));
        std::wstring l;
        while (std::getline(ss, l)) {
            lines.push_back(l);
        }
    }
    return lines;
}

bool NotebookView::HitTestOutputArea(const editor::NotebookCell& cell, POINT clientPt, const app::AppConfig& config,
                                    size_t& outLine, size_t& outCol) const {
    auto lines = GetOutputLines(cell);
    if (lines.empty()) {
        outLine = 0;
        outCol = 0;
        return false;
    }
    float outTop = cell.codeAreaBounds.bottom + config.ScaleF(6.0f);
    float textPadX = config.ScaleF(12.0f);
    float textPadY = config.ScaleF(6.0f);
    float outLineH = config.ScaleF(18.0f);
    float charW = (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.8f);
    float availW = (cell.outputAreaBounds.right - cell.outputAreaBounds.left) - textPadX * 2.0f;

    float curY = outTop + textPadY - cell.outputScrollY;
    float drawX = cell.outputAreaBounds.left + textPadX - cell.outputScrollX;
    float clickY = static_cast<float>(clientPt.y);

    size_t lineIdx = 0;
    size_t textCursor = 0;
    bool matchedText = false;
    for (const auto& out : cell.outputs) {
        if (!out.imagePngBytes.empty()) {
            float dw = 0.0f, dh = 0.0f;
            ComputeScaledImageSize(out, availW, cell.imageZoomScale, config, dw, dh);
            curY += dh + config.ScaleF(8.0f);
        }
        std::wstringstream ss(editor::NotebookDocument::StripAnsi(out.text));
        std::wstring l;
        while (std::getline(ss, l)) {
            if (!matchedText && clickY >= curY && clickY < curY + outLineH) {
                lineIdx = textCursor;
                matchedText = true;
            }
            curY += outLineH;
            textCursor++;
        }
    }
    if (!matchedText) {
        if (clickY >= curY && !lines.empty()) {
            lineIdx = lines.size() - 1;
        } else {
            lineIdx = 0;
        }
    }
    if (lineIdx >= lines.size()) lineIdx = lines.size() - 1;

    float relX = static_cast<float>(clientPt.x) - drawX;
    size_t col = static_cast<size_t>((std::max)(0.0f, relX / charW + 0.5f));
    if (col > lines[lineIdx].length()) col = lines[lineIdx].length();

    outLine = lineIdx;
    outCol = col;
    return true;
}

void NotebookView::SubmitCellInput(editor::NotebookCell& cell, HWND hwnd) {
    if (!cell.isWaitingForInput) return;
    std::lock_guard<std::recursive_mutex> viewLock(m_outputsMutex);
    std::wstring val = cell.inputFieldValue;
    m_kernel.SendStdin(val + L"\n");
    std::wstring echo = cell.inputPrompt + val + L"\n";
    if (cell.outputs.empty()) {
        editor::CellOutput out;
        out.streamName = L"stdout";
        out.text = echo;
        out.isError = false;
        cell.outputs.push_back(std::move(out));
    } else {
        cell.outputs.back().text += echo;
    }
    cell.isWaitingForInput = false;
    cell.inputFieldValue.clear();
    cell.isInputFieldFocused = false;
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

NotebookView::NotebookView() {
    m_lastBlinkTime = GetTickCount64();
    m_lastAnimTick = GetTickCount64();
}

NotebookView::~NotebookView() {
    m_isDestroying.store(true);
    StopExecution_Internal();
    if (m_execThread.joinable()) {
        m_execThread.join();
    }
    for (int waitMs = 0; m_activeWorkers.load() > 0 && waitMs < 1000; waitMs += 10) {
        Sleep(10);
    }
    DiscardDeviceResources();
    m_pWicFactory.Reset();
}

void NotebookView::DiscardDeviceResources() {
    m_cachedNbRT = nullptr;
    m_bitmapLru.clear();
    m_bitmapIndex.clear();
    m_brushNbInner.Reset();
    m_brushNbStop.Reset();
    m_brushNbSpin.Reset();
    m_brushNbIdleBg.Reset();
    m_brushNbIdleHover.Reset();
    m_brushNbErrRed.Reset();
    m_brushNbOkGreen.Reset();
    m_brushNbTrack.Reset();
    m_brushNbThumb.Reset();
    m_brushNbThumbDrag.Reset();
    m_brushNbBlueText.Reset();
    m_brushNbPillBgDark.Reset();
    m_brushNbPillBgLight.Reset();
    m_brushNbPopupBg.Reset();
    m_brushNbRowHover.Reset();
    m_brushNbScopeGuide.Reset();
    m_brushNbSelBg.Reset();
    m_brushNbWordOccBg.Reset();
    m_brushNbGutterSep.Reset();
    m_cachedFmtDpi = 0.0f;
    m_fmtNbBtn.Reset();
    m_fmtNbActionGlyph.Reset();
    m_fmtNbModalTitle.Reset();
    m_fmtNbUiLeading.Reset();
    m_fmtNbMdProse.Reset();
    m_fmtNbMdBold.Reset();
    m_fmtNbMdItalic.Reset();
    m_fmtNbMonoSmall.Reset();
}

void NotebookView::EnsureNbTextFormats(Theme& theme, const app::AppConfig& config) {
    if (!theme.dwriteFactory) return;
    const float dpi = (config.dpiScale > 0.1f) ? config.dpiScale : 1.0f;
    if (m_fmtNbBtn && m_fmtNbActionGlyph && m_fmtNbModalTitle && m_fmtNbUiLeading &&
        m_fmtNbMdProse && m_fmtNbMdBold && m_fmtNbMdItalic && m_fmtNbMonoSmall &&
        std::fabs(m_cachedFmtDpi - dpi) <= 0.01f) {
        return;
    }
    m_cachedFmtDpi = dpi;
    m_fmtNbBtn.Reset();
    m_fmtNbActionGlyph.Reset();
    m_fmtNbModalTitle.Reset();
    m_fmtNbUiLeading.Reset();
    m_fmtNbMdProse.Reset();
    m_fmtNbMdBold.Reset();
    m_fmtNbMdItalic.Reset();
    m_fmtNbMonoSmall.Reset();

    theme.dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 12.0f * dpi, L"en-US", &m_fmtNbBtn);
    if (m_fmtNbBtn) {
        m_fmtNbBtn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fmtNbBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fmtNbBtn->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    theme.dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 14.0f * dpi, L"en-US", &m_fmtNbActionGlyph);
    if (m_fmtNbActionGlyph) {
        m_fmtNbActionGlyph->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fmtNbActionGlyph->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fmtNbActionGlyph->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    theme.dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 15.0f * dpi, L"en-US", &m_fmtNbModalTitle);
    if (m_fmtNbModalTitle) {
        m_fmtNbModalTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_fmtNbModalTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fmtNbModalTitle->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    theme.dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 12.0f * dpi, L"en-US", &m_fmtNbUiLeading);
    if (m_fmtNbUiLeading) {
        m_fmtNbUiLeading->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_fmtNbUiLeading->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fmtNbUiLeading->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    theme.dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 13.5f * dpi, L"en-US", &m_fmtNbMdProse);
    if (m_fmtNbMdProse) {
        m_fmtNbMdProse->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_fmtNbMdProse->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        m_fmtNbMdProse->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }

    theme.dwriteFactory->CreateTextFormat(
        L"Georgia", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 13.5f * dpi, L"en-US", &m_fmtNbMdBold);
    if (m_fmtNbMdBold) {
        m_fmtNbMdBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fmtNbMdBold->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fmtNbMdBold->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    theme.dwriteFactory->CreateTextFormat(
        L"Georgia", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC,
        DWRITE_FONT_STRETCH_NORMAL, 14.0f * dpi, L"en-US", &m_fmtNbMdItalic);
    if (m_fmtNbMdItalic) {
        m_fmtNbMdItalic->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fmtNbMdItalic->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fmtNbMdItalic->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    theme.dwriteFactory->CreateTextFormat(
        L"Consolas", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 11.0f * dpi, L"en-US", &m_fmtNbMonoSmall);
    if (m_fmtNbMonoSmall) {
        m_fmtNbMonoSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_fmtNbMonoSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_fmtNbMonoSmall->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
}

void NotebookView::EnsureNbBrushes(ID2D1RenderTarget* pRT) {
    if (!pRT) return;
    if (m_cachedNbRT != pRT) {
        DiscardDeviceResources();
        m_cachedNbRT = pRT;
    }
    if (!m_brushNbInner) pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f), &m_brushNbInner);
    if (!m_brushNbStop) pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.98f, 1.0f), &m_brushNbStop);
    if (!m_brushNbSpin) pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.65f, 0.98f, 1.0f), &m_brushNbSpin);
    if (!m_brushNbIdleBg) pRT->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.15f, 0.18f, 0.9f), &m_brushNbIdleBg);
    if (!m_brushNbIdleHover) pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.24f, 0.28f, 1.0f), &m_brushNbIdleHover);
    if (!m_brushNbErrRed) pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.32f, 0.32f, 1.0f), &m_brushNbErrRed);
    if (!m_brushNbOkGreen) pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.82f, 0.48f, 1.0f), &m_brushNbOkGreen);
    if (!m_brushNbTrack) pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f), &m_brushNbTrack);
    if (!m_brushNbThumb) pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f), &m_brushNbThumb);
    if (!m_brushNbThumbDrag) pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.65f), &m_brushNbThumbDrag);
    if (!m_brushNbBlueText) pRT->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.60f, 0.95f, 1.0f), &m_brushNbBlueText);
    if (!m_brushNbPillBgDark) pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f), &m_brushNbPillBgDark);
    if (!m_brushNbPillBgLight) pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_brushNbPillBgLight);
    if (!m_brushNbPopupBg) pRT->CreateSolidColorBrush(D2D1::ColorF(0.13f, 0.14f, 0.17f, 0.98f), &m_brushNbPopupBg);
    if (!m_brushNbRowHover) pRT->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.28f, 0.38f, 0.65f), &m_brushNbRowHover);
    if (!m_brushNbScopeGuide) pRT->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.70f, 0.78f, 0.16f), &m_brushNbScopeGuide);
    if (!m_brushNbSelBg) pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.42f, 0.75f, 0.45f), &m_brushNbSelBg);
    if (!m_brushNbWordOccBg) pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.45f, 0.60f, 0.28f), &m_brushNbWordOccBg);
    if (!m_brushNbGutterSep) pRT->CreateSolidColorBrush(D2D1::ColorF(0.50f, 0.54f, 0.62f, 0.18f), &m_brushNbGutterSep);
}

ID2D1Bitmap* NotebookView::GetOrCreateOutputBitmap(ID2D1RenderTarget* pRT, uint64_t imageId,
                                                   const std::vector<BYTE>& pngBytes, float& outW, float& outH) {
    outW = 0.0f;
    outH = 0.0f;
    if (!pRT || imageId == 0 || pngBytes.empty()) return nullptr;
    if (m_cachedNbRT != pRT) {
        DiscardDeviceResources();
        m_cachedNbRT = pRT;
    }

    auto it = m_bitmapIndex.find(imageId);
    if (it != m_bitmapIndex.end()) {
        m_bitmapLru.splice(m_bitmapLru.begin(), m_bitmapLru, it->second);
        outW = it->second->pxW;
        outH = it->second->pxH;
        return it->second->bitmap.Get();
    }

    if (!m_pWicFactory) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pWicFactory));
    }
    if (!m_pWicFactory) return nullptr;

    Microsoft::WRL::ComPtr<IWICStream> pStream;
    if (FAILED(m_pWicFactory->CreateStream(&pStream)) || !pStream) return nullptr;
    if (FAILED(pStream->InitializeFromMemory(const_cast<BYTE*>(pngBytes.data()), static_cast<DWORD>(pngBytes.size())))) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDecoder;
    if (FAILED(m_pWicFactory->CreateDecoderFromStream(pStream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder)) || !pDecoder) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
    if (FAILED(pDecoder->GetFrame(0, &pFrame)) || !pFrame) return nullptr;

    UINT w = 0, h = 0;
    if (FAILED(pFrame->GetSize(&w, &h)) || w == 0 || h == 0) return nullptr;

    Microsoft::WRL::ComPtr<IWICFormatConverter> pConv;
    if (FAILED(m_pWicFactory->CreateFormatConverter(&pConv)) || !pConv) return nullptr;
    if (FAILED(pConv->Initialize(pFrame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom))) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<ID2D1Bitmap> pBmp;
    if (FAILED(pRT->CreateBitmapFromWicBitmap(pConv.Get(), nullptr, &pBmp)) || !pBmp) return nullptr;

    CachedNbBitmap entry;
    entry.imageId = imageId;
    entry.pxW = static_cast<float>(w);
    entry.pxH = static_cast<float>(h);
    entry.bitmap = pBmp;
    m_bitmapLru.push_front(std::move(entry));
    m_bitmapIndex[imageId] = m_bitmapLru.begin();
    while (m_bitmapLru.size() > kMaxCachedNbBitmaps) {
        uint64_t evictId = m_bitmapLru.back().imageId;
        m_bitmapIndex.erase(evictId);
        m_bitmapLru.pop_back();
    }
    outW = static_cast<float>(w);
    outH = static_cast<float>(h);
    return m_bitmapLru.front().bitmap.Get();
}

void NotebookView::DrawRunMorphButton(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     const D2D1_RECT_F& bounds, bool isExecuting, bool isQueued, bool hasError, bool isHovered) {
    if (!pRT) return;
    EnsureNbBrushes(pRT);
    const float cx = (bounds.left + bounds.right) / 2.0f;
    const float cy = (bounds.top + bounds.bottom) / 2.0f;
    const float r = (std::min)(bounds.right - bounds.left, bounds.bottom - bounds.top) / 2.0f;

    if (isExecuting || isQueued) {
        const float innerR = r * 0.72f;
        if (m_brushNbInner) {
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), innerR, innerR), m_brushNbInner.Get());
        }
        const float sqSize = r * 0.58f;
        D2D1_RECT_F sqRc = D2D1::RectF(cx - sqSize / 2.0f, cy - sqSize / 2.0f, cx + sqSize / 2.0f, cy + sqSize / 2.0f);
        if (m_brushNbStop) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(sqRc, 1.5f, 1.5f), m_brushNbStop.Get());
        }
        const float ringR = r * 0.90f;
        if (m_brushNbSpin) {
            if (isQueued) {
                const int dashCount = 10;
                const float arcSweep = 360.0f / static_cast<float>(dashCount);
                const float dashSweep = arcSweep * 0.50f;
                for (int i = 0; i < dashCount; ++i) {
                    float deg1 = m_spinAngle + static_cast<float>(i) * arcSweep;
                    float deg2 = deg1 + dashSweep;
                    float rad1 = deg1 * 3.14159265f / 180.0f;
                    float rad2 = deg2 * 3.14159265f / 180.0f;
                    D2D1_POINT_2F p1 = D2D1::Point2F(cx + ringR * cosf(rad1), cy + ringR * sinf(rad1));
                    D2D1_POINT_2F p2 = D2D1::Point2F(cx + ringR * cosf(rad2), cy + ringR * sinf(rad2));
                    pRT->DrawLine(p1, p2, m_brushNbSpin.Get(), config.ScaleF(1.8f));
                }
            } else {
                const int segments = 32;
                const float sweepAngle = 270.0f;
                float baseRad = m_spinAngle * 3.14159265f / 180.0f;
                float sweepRad = sweepAngle * 3.14159265f / 180.0f;
                for (int s = 0; s < segments; ++s) {
                    float a1 = baseRad + sweepRad * (static_cast<float>(s) / segments);
                    float a2 = baseRad + sweepRad * (static_cast<float>(s + 1) / segments);
                    D2D1_POINT_2F p1 = D2D1::Point2F(cx + ringR * cosf(a1), cy + ringR * sinf(a1));
                    D2D1_POINT_2F p2 = D2D1::Point2F(cx + ringR * cosf(a2), cy + ringR * sinf(a2));
                    pRT->DrawLine(p1, p2, m_brushNbSpin.Get(), config.ScaleF(2.0f));
                }
            }
        }
    } else {
        const float circleR = r * 0.80f;
        if (isHovered && theme.brushItemHover) {
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), circleR, circleR), theme.brushItemHover.Get());
        } else if (isHovered && m_brushNbIdleHover) {
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), circleR, circleR), m_brushNbIdleHover.Get());
        }

        if (hasError && m_brushNbErrRed) {
            pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), circleR, circleR), m_brushNbErrRed.Get(), 1.3f);
        }

        // Crisp flat play triangle (minimalist hollow outline at rest, filled accent on hover)
        ID2D1Brush* glyphBrush = hasError
            ? static_cast<ID2D1Brush*>(m_brushNbErrRed.Get())
            : (isHovered
                ? static_cast<ID2D1Brush*>(m_brushNbOkGreen.Get())
                : (theme.brushTextSecondary ? theme.brushTextSecondary.Get() : static_cast<ID2D1Brush*>(m_brushNbOkGreen.Get())));
        if (glyphBrush) {
            float triW = circleR * 0.62f;
            float triH = circleR * 0.72f;
            float triLeft = cx - triW * 0.36f;
            Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
            pRT->GetFactory(&pFactory);
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> pTri;
            if (pFactory && SUCCEEDED(pFactory->CreatePathGeometry(&pTri))) {
                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(pTri->Open(&sink))) {
                    sink->BeginFigure(D2D1::Point2F(triLeft, cy - triH / 2.0f), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(triLeft + triW, cy));
                    sink->AddLine(D2D1::Point2F(triLeft, cy + triH / 2.0f));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    if (isHovered || hasError) {
                        pRT->FillGeometry(pTri.Get(), glyphBrush);
                    } else {
                        pRT->DrawGeometry(pTri.Get(), glyphBrush, config.ScaleF(1.35f));
                    }
                }
            }
        }
    }
}

void NotebookView::RenderTooltip(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                 const D2D1_RECT_F& targetBounds, const std::wstring& text,
                                 float canvasLeft, float canvasWidth) {
    if (!pRT || text.empty()) return;
    IDWriteTextFormat* tf = theme.fontTooltip.Get() ? theme.fontTooltip.Get() : theme.fontBadge.Get();
    const float tipH = config.ScaleF(24.0f);
    const float tipPadX = config.ScaleF(10.0f);
    float textW = static_cast<float>(text.length()) * config.ScaleF(7.2f);
    if (tf && theme.dwriteFactory) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                text.c_str(), static_cast<UINT32>(text.length()), tf, 1000.0f, tipH, &pLayout)) && pLayout) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                textW = (std::ceil)(tm.widthIncludingTrailingWhitespace);
            }
        }
    }
    const float tipW = textW + tipPadX * 2.0f;
    const float minX = canvasLeft + config.ScaleF(8.0f);
    float tipX = targetBounds.left + ((targetBounds.right - targetBounds.left) - tipW) * 0.5f;
    if (canvasWidth > 0.0f) {
        const float maxX = (std::max)(minX, canvasLeft + canvasWidth - tipW - config.ScaleF(8.0f));
        if (tipX > maxX) tipX = maxX;
    }
    if (tipX < minX) tipX = minX;
    float tipY = targetBounds.bottom + config.ScaleF(6.0f);

    D2D1_RECT_F rcTip = D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH);
    m_lastTooltipRect = rcTip;
    D2D1_ROUNDED_RECT rrcTip = D2D1::RoundedRect(rcTip, 4.0f, 4.0f);

    // Draw Tooltip Card Background & Border
    if (theme.brushTooltipBg) {
        pRT->FillRoundedRectangle(rrcTip, theme.brushTooltipBg.Get());
    } else {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.15f, 0.17f, 0.96f), &brushBg);
        if (brushBg) pRT->FillRoundedRectangle(rrcTip, brushBg.Get());
    }
    if (theme.brushTooltipBorder) {
        pRT->DrawRoundedRectangle(rrcTip, theme.brushTooltipBorder.Get(), 1.0f);
    } else {
        pRT->DrawRoundedRectangle(rrcTip, theme.brushBorderSubtle.Get(), 1.0f);
    }

    D2D1_RECT_F rcText = D2D1::RectF(tipX + tipPadX, tipY, tipX + tipW - tipPadX, tipY + tipH);
    ID2D1Brush* textBrush = theme.brushTooltipText ? theme.brushTooltipText.Get() : theme.brushTextPrimary.Get();
    if (tf && textBrush) {
        DWRITE_TEXT_ALIGNMENT prevAlign = tf->GetTextAlignment();
        DWRITE_PARAGRAPH_ALIGNMENT prevPAlign = tf->GetParagraphAlignment();
        tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        pRT->DrawText(text.c_str(), static_cast<UINT32>(text.length()), tf, rcText, textBrush);
        tf->SetTextAlignment(prevAlign);
        tf->SetParagraphAlignment(prevPAlign);
    }
}

float NotebookView::ComputeScaledImageSize(const editor::CellOutput& out, float availW, float imageZoomScale,
                                           const app::AppConfig& config, float& outW, float& outH) const noexcept {
    uint32_t pxW = out.imagePixelW;
    uint32_t pxH = out.imagePixelH;
    if ((pxW == 0 || pxH == 0) && !out.imagePngBytes.empty()) {
        editor::ReadPngDimensions(out.imagePngBytes, pxW, pxH);
    }
    if (pxW == 0 || pxH == 0) {
        pxW = 720;
        pxH = 540;
    }
    const float natW = static_cast<float>(pxW);
    const float natH = static_cast<float>(pxH);
    const float clampedAvail = (std::max)(config.ScaleF(200.0f), availW - config.ScaleF(16.0f));
    const float maxDrawW = (std::min)(clampedAvail, config.ScaleF(720.0f) * imageZoomScale);
    const float maxDrawH = config.ScaleF(520.0f) * imageZoomScale;
    float scale = (std::min)(maxDrawW / natW, maxDrawH / natH);
    if (scale > 1.25f) scale = 1.25f;
    outW = (std::max)(config.ScaleF(96.0f), natW * scale);
    outH = (std::max)(config.ScaleF(72.0f), natH * scale);
    return outH;
}

bool NotebookView::CopyPngToClipboard(const std::vector<BYTE>& pngBytes, HWND hwnd) {
    if (pngBytes.empty()) return false;
    if (!m_pWicFactory) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pWicFactory));
    }
    if (!m_pWicFactory) return false;

    Microsoft::WRL::ComPtr<IWICStream> pStream;
    if (FAILED(m_pWicFactory->CreateStream(&pStream)) || !pStream) return false;
    if (FAILED(pStream->InitializeFromMemory(const_cast<BYTE*>(pngBytes.data()), static_cast<DWORD>(pngBytes.size())))) return false;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDec;
    if (FAILED(m_pWicFactory->CreateDecoderFromStream(pStream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &pDec)) || !pDec) return false;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
    if (FAILED(pDec->GetFrame(0, &pFrame)) || !pFrame) return false;

    UINT w = 0, h = 0;
    if (FAILED(pFrame->GetSize(&w, &h)) || w == 0 || h == 0 || w > 8192 || h > 8192) return false;

    Microsoft::WRL::ComPtr<IWICFormatConverter> pConv;
    if (FAILED(m_pWicFactory->CreateFormatConverter(&pConv)) || !pConv) return false;
    if (FAILED(pConv->Initialize(pFrame.Get(), GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom))) return false;

    const SIZE_T stride = static_cast<SIZE_T>(w) * 4u;
    const SIZE_T pixelBytes = stride * static_cast<SIZE_T>(h);
    const SIZE_T totalSize = sizeof(BITMAPINFOHEADER) + pixelBytes;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, totalSize);
    if (!hMem) return false;

    BYTE* pBuf = static_cast<BYTE*>(GlobalLock(hMem));
    if (!pBuf) {
        GlobalFree(hMem);
        return false;
    }

    BITMAPINFOHEADER* bih = reinterpret_cast<BITMAPINFOHEADER*>(pBuf);
    ZeroMemory(bih, sizeof(BITMAPINFOHEADER));
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = static_cast<LONG>(w);
    bih->biHeight = -static_cast<LONG>(h); // top-down DIB matches WIC CopyPixels scanline order
    bih->biPlanes = 1;
    bih->biBitCount = 32;
    bih->biCompression = BI_RGB;
    bih->biSizeImage = static_cast<DWORD>(pixelBytes);

    HRESULT hr = pConv->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(pixelBytes), pBuf + sizeof(BITMAPINFOHEADER));
    GlobalUnlock(hMem);
    if (FAILED(hr)) {
        GlobalFree(hMem);
        return false;
    }

    if (!OpenClipboard(hwnd)) {
        GlobalFree(hMem);
        return false;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_DIB, hMem)) {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

bool NotebookView::SavePngWithDialog(const std::vector<BYTE>& pngBytes, HWND hwnd) {
    if (pngBytes.empty()) return false;
    wchar_t fileBuf[MAX_PATH] = L"notebook_plot.png";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"PNG Image (*.png)\0*.png\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return false;

    std::ofstream ofs(fileBuf, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char*>(pngBytes.data()), static_cast<std::streamsize>(pngBytes.size()));
    return ofs.good();
}

void NotebookView::OpenImageLightbox(const editor::CellOutput& out) {
    if (out.imagePngBytes.empty() || out.imageId == 0) return;
    m_lightboxOpen = true;
    m_lightboxImageId = out.imageId;
    m_lightboxPngBytes = out.imagePngBytes;
    m_lightboxPixelW = out.imagePixelW;
    m_lightboxPixelH = out.imagePixelH;
    if (m_lightboxPixelW == 0 || m_lightboxPixelH == 0) {
        editor::ReadPngDimensions(m_lightboxPngBytes, m_lightboxPixelW, m_lightboxPixelH);
    }
    m_lightboxZoom = 1.0f;
    m_lightboxPanX = 0.0f;
    m_lightboxPanY = 0.0f;
    m_lightboxDragging = false;
    m_lightboxHoveredBtn = 0;
}

void NotebookView::RenderImageLightbox(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                       float left, float width, float top, float height) {
    if (!pRT || !m_lightboxOpen) return;
    EnsureNbBrushes(pRT);
    EnsureNbTextFormats(theme, config);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> backdropBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.02f, 0.02f, 0.04f, 0.78f), &backdropBrush);
    if (backdropBrush) {
        pRT->FillRectangle(D2D1::RectF(left, top, left + width, top + height), backdropBrush.Get());
    }

    const float margin = config.ScaleF(24.0f);
    const float cardW = (std::max)(config.ScaleF(540.0f), (std::min)(width - margin * 2.0f, config.ScaleF(1120.0f)));
    const float cardH = (std::max)(config.ScaleF(340.0f), (std::min)(height - margin * 2.0f, config.ScaleF(800.0f)));
    const float cardX = left + (width - cardW) * 0.5f;
    const float cardY = top + (height - cardH) * 0.5f;
    m_lightboxCardBounds = D2D1::RectF(cardX, cardY, cardX + cardW, cardY + cardH);

    D2D1_ROUNDED_RECT rrcCard = D2D1::RoundedRect(m_lightboxCardBounds, config.ScaleF(8.0f), config.ScaleF(8.0f));
    if (m_brushNbPopupBg) pRT->FillRoundedRectangle(rrcCard, m_brushNbPopupBg.Get());
    if (theme.brushBorderHover) pRT->DrawRoundedRectangle(rrcCard, theme.brushBorderHover.Get(), 1.2f);

    const float hdrH = config.ScaleF(52.0f);
    pRT->DrawLine(D2D1::Point2F(cardX, cardY + hdrH), D2D1::Point2F(cardX + cardW, cardY + hdrH),
                  theme.brushBorderSubtle ? theme.brushBorderSubtle.Get() : theme.brushTextSecondary.Get(), 1.0f);

    float bx = cardX + cardW - config.ScaleF(14.0f);
    const float btnH = config.ScaleF(32.0f);
    const float btnY = cardY + (hdrH - btnH) * 0.5f;
    IDWriteTextFormat* btnFmt = m_fmtNbBtn ? m_fmtNbBtn.Get() : (theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontCode.Get());
    IDWriteTextFormat* glyphFmt = m_fmtNbActionGlyph ? m_fmtNbActionGlyph.Get() : btnFmt;
    auto drawLbBtn = [&](D2D1_RECT_F& outRc, float bw, const wchar_t* label, int hoverId, bool accent = false, bool isGlyph = false) {
        bx -= bw;
        outRc = D2D1::RectF(bx, btnY, bx + bw, btnY + btnH);
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(outRc, config.ScaleF(5.0f), config.ScaleF(5.0f));
        if (m_lightboxHoveredBtn == hoverId) {
            if (theme.brushItemHover) pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
        } else if (accent && theme.brushBrandAccent) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accBg;
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.45f, 0.85f, 0.42f), &accBg);
            if (accBg) pRT->FillRoundedRectangle(rrc, accBg.Get());
        }
        if (theme.brushBorderSubtle) pRT->DrawRoundedRectangle(rrc, theme.brushBorderSubtle.Get(), 1.0f);
        IDWriteTextFormat* useFmt = isGlyph ? glyphFmt : btnFmt;
        if (useFmt && theme.brushTextPrimary) {
            DWRITE_TEXT_ALIGNMENT pa = useFmt->GetTextAlignment();
            DWRITE_PARAGRAPH_ALIGNMENT pp = useFmt->GetParagraphAlignment();
            useFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            useFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            pRT->DrawText(label, static_cast<UINT32>(wcslen(label)), useFmt, outRc, theme.brushTextPrimary.Get());
            useFmt->SetTextAlignment(pa);
            useFmt->SetParagraphAlignment(pp);
        }
        bx -= config.ScaleF(8.0f);
    };

    drawLbBtn(m_lightboxBtnClose, config.ScaleF(36.0f), L"\x2715", 6, false, true);
    drawLbBtn(m_lightboxBtnSave, config.ScaleF(96.0f), L"Save PNG", 5, true, false);
    drawLbBtn(m_lightboxBtnCopy, config.ScaleF(92.0f), L"Copy Img", 4, false, false);
    drawLbBtn(m_lightboxBtnZoomIn, config.ScaleF(36.0f), L"+", 3, false, true);
    drawLbBtn(m_lightboxBtnReset, config.ScaleF(54.0f), L"Fit", 2, false, false);
    drawLbBtn(m_lightboxBtnZoomOut, config.ScaleF(36.0f), L"\x2212", 1, false, true);

    const int zoomPct = static_cast<int>(std::round(m_lightboxZoom * 100.0f));
    std::wstring title = L"Plot Inspector";
    if (m_lightboxPixelW > 0 && m_lightboxPixelH > 0) {
        title += L" \x2014 " + std::to_wstring(m_lightboxPixelW) + L" \x00D7 " + std::to_wstring(m_lightboxPixelH) + L" px";
    }
    title += L"  (" + std::to_wstring(zoomPct) + L"%)";
    D2D1_RECT_F rcTitle = D2D1::RectF(cardX + config.ScaleF(18.0f), cardY, (std::max)(cardX + config.ScaleF(120.0f), bx - config.ScaleF(10.0f)), cardY + hdrH);
    IDWriteTextFormat* titleFmt = m_fmtNbModalTitle ? m_fmtNbModalTitle.Get() : (theme.fontBody ? theme.fontBody.Get() : theme.fontCode.Get());
    if (titleFmt && theme.brushTextPrimary) {
        DWRITE_PARAGRAPH_ALIGNMENT prevP = titleFmt->GetParagraphAlignment();
        titleFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        pRT->DrawText(title.c_str(), static_cast<UINT32>(title.length()), titleFmt, rcTitle, theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        titleFmt->SetParagraphAlignment(prevP);
    }

    const float cPad = config.ScaleF(12.0f);
    m_lightboxCanvasBounds = D2D1::RectF(cardX + cPad, cardY + hdrH + cPad, cardX + cardW - cPad, cardY + cardH - cPad);
    pRT->PushAxisAlignedClip(m_lightboxCanvasBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float bmpW = 0.0f, bmpH = 0.0f;
    ID2D1Bitmap* pBmp = GetOrCreateOutputBitmap(pRT, m_lightboxImageId, m_lightboxPngBytes, bmpW, bmpH);
    if (pBmp && bmpW > 0.0f && bmpH > 0.0f) {
        const float availW = (std::max)(40.0f, (m_lightboxCanvasBounds.right - m_lightboxCanvasBounds.left) - config.ScaleF(16.0f));
        const float availH = (std::max)(40.0f, (m_lightboxCanvasBounds.bottom - m_lightboxCanvasBounds.top) - config.ScaleF(16.0f));
        float fitScale = (std::min)(availW / bmpW, availH / bmpH);
        const float drawW = bmpW * fitScale * m_lightboxZoom;
        const float drawH = bmpH * fitScale * m_lightboxZoom;
        const float cx = (m_lightboxCanvasBounds.left + m_lightboxCanvasBounds.right) * 0.5f + m_lightboxPanX;
        const float cy = (m_lightboxCanvasBounds.top + m_lightboxCanvasBounds.bottom) * 0.5f + m_lightboxPanY;
        D2D1_RECT_F rcDraw = D2D1::RectF(cx - drawW * 0.5f, cy - drawH * 0.5f, cx + drawW * 0.5f, cy + drawH * 0.5f);
        if (m_brushNbPillBgLight) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(rcDraw, 6.0f, 6.0f), m_brushNbPillBgLight.Get());
        }
        pRT->DrawBitmap(pBmp, rcDraw, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    pRT->PopAxisAlignedClip();
}

void NotebookView::ApplyMarkdownFormat(editor::NotebookDocument& nb, int cellIdx, MarkdownFormatAction action, HWND hwnd) {
    if (cellIdx < 0 || cellIdx >= static_cast<int>(nb.cells.size())) return;
    auto& cell = nb.cells[cellIdx];
    if (cell.type != editor::NotebookCellType::Markdown) return;
    cell.isEditingMarkdown = true;
    cell.isInputCollapsed = false;
    cell.inputBuffer.PushUndo();

    auto cur = cell.inputBuffer.GetCursor();
    size_t sL = cur.line, sC = cur.col, eL = cur.line, eC = cur.col;
    const bool hasSel = cell.HasSelection();
    if (hasSel) {
        cell.GetSelectionRange(sL, sC, eL, eC);
    }

    auto wrapInline = [&](const std::wstring& prefix, const std::wstring& suffix, const std::wstring& placeholder, bool selectSuffix) {
        if (sL >= cell.inputBuffer.GetLineCount()) return;
        std::wstring ln = cell.inputBuffer.GetLine(sL);
        if (hasSel && sL == eL && eC > sC && eC <= ln.size()) {
            std::wstring mid = ln.substr(sC, eC - sC);
            // Toggle off for identical symmetric wrapper (Bold / Italic)
            if (prefix == suffix &&
                sC >= prefix.size() && eC + suffix.size() <= ln.size() &&
                ln.compare(sC - prefix.size(), prefix.size(), prefix) == 0 &&
                ln.compare(eC, suffix.size(), suffix) == 0) {
                std::wstring newLn = ln.substr(0, sC - prefix.size()) + mid + ln.substr(eC + suffix.size());
                cell.inputBuffer.SetLine(sL, newLn);
                size_t ns = sC - prefix.size();
                cell.selAnchor = { sL, ns };
                cell.selEnd = { sL, ns + mid.size() };
                cell.hasSelection = true;
                cell.inputBuffer.SetCursor(sL, ns + mid.size());
                return;
            }
            std::wstring newLn = ln.substr(0, sC) + prefix + mid + suffix + ln.substr(eC);
            cell.inputBuffer.SetLine(sL, newLn);
            if (selectSuffix) {
                size_t urlOff = suffix.find(L"https://");
                if (urlOff == std::wstring::npos) urlOff = suffix.find(L"image_url");
                if (urlOff != std::wstring::npos) {
                    size_t uLen = (suffix.find(L"https://") != std::wstring::npos) ? 8 : 9;
                    size_t selStart = sC + prefix.size() + mid.size() + urlOff;
                    cell.selAnchor = { sL, selStart };
                    cell.selEnd = { sL, selStart + uLen };
                    cell.hasSelection = true;
                    cell.inputBuffer.SetCursor(sL, selStart + uLen);
                    return;
                }
            }
            size_t newStart = sC + prefix.size();
            cell.selAnchor = { sL, newStart };
            cell.selEnd = { sL, newStart + mid.size() };
            cell.hasSelection = true;
            cell.inputBuffer.SetCursor(sL, newStart + mid.size());
        } else {
            size_t pos = (std::min)(sC, ln.size());
            std::wstring inserted = prefix + placeholder + suffix;
            std::wstring newLn = ln.substr(0, pos) + inserted + ln.substr(pos);
            cell.inputBuffer.SetLine(sL, newLn);
            if (selectSuffix) {
                size_t urlOff = suffix.find(L"image_url");
                if (urlOff != std::wstring::npos) {
                    size_t selStart = pos + prefix.size() + placeholder.size() + urlOff;
                    cell.selAnchor = { sL, selStart };
                    cell.selEnd = { sL, selStart + 9 };
                    cell.hasSelection = true;
                    cell.inputBuffer.SetCursor(sL, selStart + 9);
                    return;
                }
            }
            size_t phStart = pos + prefix.size();
            cell.selAnchor = { sL, phStart };
            cell.selEnd = { sL, phStart + placeholder.size() };
            cell.hasSelection = true;
            cell.inputBuffer.SetCursor(sL, phStart + placeholder.size());
        }
    };

    switch (action) {
        case MarkdownFormatAction::Bold:
            wrapInline(L"**", L"**", L"bold text", false);
            break;
        case MarkdownFormatAction::Italic:
            wrapInline(L"*", L"*", L"italic text", false);
            break;
        case MarkdownFormatAction::Link:
            wrapInline(L"[", L"](https://)", L"link text", true);
            break;
        case MarkdownFormatAction::Image:
            wrapInline(L"![", L"](image_url)", L"image", true);
            break;
        case MarkdownFormatAction::Quote: {
            std::vector<std::wstring> lines = cell.inputBuffer.GetLines();
            bool allQuoted = true;
            for (size_t li = sL; li <= eL && li < lines.size(); ++li) {
                if (lines[li].rfind(L"> ", 0) != 0 && lines[li] != L">") { allQuoted = false; break; }
            }
            for (size_t li = sL; li <= eL && li < lines.size(); ++li) {
                if (allQuoted) {
                    lines[li].erase(0, lines[li].rfind(L"> ", 0) == 0 ? 2 : 1);
                } else {
                    lines[li].insert(0, L"> ");
                }
            }
            cell.inputBuffer.SetLines(lines);
            cell.ClearSelection();
            if (eL < lines.size()) cell.inputBuffer.SetCursor(eL, lines[eL].size());
            break;
        }
        case MarkdownFormatAction::BulletList: {
            std::vector<std::wstring> lines = cell.inputBuffer.GetLines();
            bool allBulleted = true;
            for (size_t li = sL; li <= eL && li < lines.size(); ++li) {
                if (lines[li].rfind(L"- ", 0) != 0 && lines[li].rfind(L"* ", 0) != 0) { allBulleted = false; break; }
            }
            for (size_t li = sL; li <= eL && li < lines.size(); ++li) {
                if (allBulleted) {
                    lines[li].erase(0, 2);
                } else {
                    lines[li].insert(0, L"- ");
                }
            }
            cell.inputBuffer.SetLines(lines);
            cell.ClearSelection();
            if (eL < lines.size()) cell.inputBuffer.SetCursor(eL, lines[eL].size());
            break;
        }
        case MarkdownFormatAction::NumberList: {
            std::vector<std::wstring> lines = cell.inputBuffer.GetLines();
            auto matchNumPrefix = [](const std::wstring& l, size_t& prefixLen) -> bool {
                size_t k = 0;
                while (k < l.size() && iswdigit(l[k])) ++k;
                if (k > 0 && k + 1 < l.size() && l[k] == L'.' && l[k + 1] == L' ') {
                    prefixLen = k + 2;
                    return true;
                }
                return false;
            };
            bool allNumbered = true;
            for (size_t li = sL; li <= eL && li < lines.size(); ++li) {
                size_t pLen = 0;
                if (!matchNumPrefix(lines[li], pLen)) { allNumbered = false; break; }
            }
            size_t itemNum = 1;
            for (size_t li = sL; li <= eL && li < lines.size(); ++li) {
                size_t pLen = 0;
                if (allNumbered && matchNumPrefix(lines[li], pLen)) {
                    lines[li].erase(0, pLen);
                } else {
                    lines[li].insert(0, std::to_wstring(itemNum++) + L". ");
                }
            }
            cell.inputBuffer.SetLines(lines);
            cell.ClearSelection();
            if (eL < lines.size()) cell.inputBuffer.SetCursor(eL, lines[eL].size());
            break;
        }
        case MarkdownFormatAction::Heading: {
            std::vector<std::wstring> lines = cell.inputBuffer.GetLines();
            if (sL < lines.size()) {
                std::wstring& ln = lines[sL];
                size_t hashes = 0;
                while (hashes < ln.size() && ln[hashes] == L'#') ++hashes;
                size_t trim = hashes;
                while (trim < ln.size() && ln[trim] == L' ') ++trim;
                std::wstring rest = ln.substr(trim);
                if (hashes == 0) ln = L"## " + rest;
                else if (hashes == 1) ln = L"## " + rest;
                else if (hashes == 2) ln = L"### " + rest;
                else ln = rest;
                cell.inputBuffer.SetLines(lines);
                cell.ClearSelection();
                cell.inputBuffer.SetCursor(sL, ln.size());
            }
            break;
        }
    }

    nb.isDirty = true;
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

namespace {

struct MdInlineSpan {
    DWRITE_TEXT_RANGE rng{};
    bool bold = false;
    bool italic = false;
    bool code = false;
    bool link = false;
    bool math = false;
    bool mathSubOrSuper = false;
    std::wstring url;
};

struct MdImageRef {
    std::wstring alt;
    std::wstring target;
};

std::wstring TrimMdTarget(std::wstring t) {
    auto trimWs = [](std::wstring& s) {
        size_t a = 0;
        while (a < s.size() && iswspace(s[a])) ++a;
        size_t b = s.size();
        while (b > a && iswspace(s[b - 1])) --b;
        s = s.substr(a, b - a);
    };
    trimWs(t);
    if (t.size() >= 2 && t.front() == L'<' && t.back() == L'>') {
        t = t.substr(1, t.size() - 2);
        trimWs(t);
    }
    // Strip optional Markdown title suffix: url "title" or url 'title'
    for (size_t i = 0; i < t.size(); ++i) {
        if (iswspace(t[i])) {
            size_t j = i;
            while (j < t.size() && iswspace(t[j])) ++j;
            if (j < t.size() && (t[j] == L'"' || t[j] == L'\'')) {
                t = t.substr(0, i);
                break;
            }
        }
    }
    trimWs(t);
    return t;
}

bool IsHttpUrl(const std::wstring& s) {
    return _wcsnicmp(s.c_str(), L"http://", 7) == 0 ||
           _wcsnicmp(s.c_str(), L"https://", 8) == 0;
}

std::wstring DecodePercentUtf8(const std::wstring& in) {
    std::string bytes;
    bytes.reserve(in.size());
    auto hexVal = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
        if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
        return -1;
    };
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == L'%' && i + 2 < in.size()) {
            int h1 = hexVal(in[i + 1]);
            int h2 = hexVal(in[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                bytes.push_back(static_cast<char>((h1 << 4) | h2));
                i += 2;
                continue;
            }
        } else if (in[i] == L'+') {
            bytes.push_back(' ');
            continue;
        }
        if (in[i] < 0x80) {
            bytes.push_back(static_cast<char>(in[i]));
        } else {
            char utf8Buf[8]{};
            wchar_t wc = in[i];
            int n = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, utf8Buf, sizeof(utf8Buf), nullptr, nullptr);
            for (int k = 0; k < n; ++k) bytes.push_back(utf8Buf[k]);
        }
    }
    return NbUtf8ToWide(bytes);
}

std::wstring UnescapeHtmlUrlEntities(std::wstring s) {
    auto repAll = [&](const std::wstring& from, const std::wstring& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::wstring::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };
    repAll(L"&amp;", L"&");
    repAll(L"&#38;", L"&");
    repAll(L"\\u0026", L"&");
    return s;
}

std::wstring UnwrapImageSearchUrl(std::wstring url) {
    url = UnescapeHtmlUrlEntities(TrimMdTarget(std::move(url)));
    if (!IsHttpUrl(url)) return url;

    // Extract wrapped target URL from Google Images /imgres?imgurl=..., Bing mediaurl=..., DDG uddg=/iai=...
    size_t qPos = url.find(L'?');
    if (qPos != std::wstring::npos) {
        std::wstring query = url.substr(qPos + 1);
        const wchar_t* paramKeys[] = { L"imgurl=", L"mediaurl=", L"iai=", L"uddg=" };
        for (const auto* key : paramKeys) {
            size_t kLen = wcslen(key);
            size_t p = 0;
            while (p < query.size()) {
                size_t amp = query.find(L'&', p);
                std::wstring pair = (amp == std::wstring::npos) ? query.substr(p) : query.substr(p, amp - p);
                if (_wcsnicmp(pair.c_str(), key, kLen) == 0) {
                    std::wstring extracted = TrimMdTarget(DecodePercentUtf8(pair.substr(kLen)));
                    if (IsHttpUrl(extracted)) {
                        return UnescapeHtmlUrlEntities(extracted);
                    }
                }
                if (amp == std::wstring::npos) break;
                p = amp + 1;
            }
        }
    }

    // Convert GitHub /blob/<branch>/... URL to raw image URL
    if (_wcsnicmp(url.c_str(), L"https://github.com/", 19) == 0 &&
        url.find(L"/blob/") != std::wstring::npos &&
        url.find(L"raw=") == std::wstring::npos) {
        url += (url.find(L'?') == std::wstring::npos) ? L"?raw=true" : L"&raw=true";
    }
    return url;
}

std::wstring ExtractOgImageFromHtml(const std::vector<BYTE>& htmlBytes, const std::wstring& pageUrl) {
    size_t scanLen = (std::min<size_t>)(htmlBytes.size(), 192u * 1024u);
    std::string html(reinterpret_cast<const char*>(htmlBytes.data()), scanLen);
    std::string lower = html;
    for (char& ch : lower) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));

    const char* metaNeedles[] = {
        "property=\"og:image\"", "property='og:image'",
        "name=\"twitter:image\"", "name='twitter:image'",
        "rel=\"image_src\"", "rel='image_src'"
    };
    for (const char* needle : metaNeedles) {
        size_t pos = lower.find(needle);
        if (pos == std::string::npos) continue;
        size_t tagStart = lower.rfind('<', pos);
        size_t tagEnd = lower.find('>', pos);
        if (tagStart == std::string::npos || tagEnd == std::string::npos) continue;
        std::string tag = html.substr(tagStart, tagEnd - tagStart + 1);
        std::string tagLow = lower.substr(tagStart, tagEnd - tagStart + 1);
        for (const char* attr : { "content=\"", "content='", "href=\"", "href='" }) {
            size_t ap = tagLow.find(attr);
            if (ap == std::string::npos) continue;
            char q = attr[strlen(attr) - 1];
            size_t vs = ap + strlen(attr);
            size_t ve = tag.find(q, vs);
            if (ve == std::string::npos || ve <= vs) continue;
            std::wstring imgUrl = UnescapeHtmlUrlEntities(TrimMdTarget(NbUtf8ToWide(tag.substr(vs, ve - vs))));
            if (imgUrl.rfind(L"//", 0) == 0) imgUrl = L"https:" + imgUrl;
            else if (!imgUrl.empty() && imgUrl.front() == L'/') {
                size_t sch = pageUrl.find(L"://");
                if (sch != std::wstring::npos) {
                    size_t sl = pageUrl.find(L'/', sch + 3);
                    std::wstring origin = (sl == std::wstring::npos) ? pageUrl : pageUrl.substr(0, sl);
                    imgUrl = origin + imgUrl;
                }
            }
            if (IsHttpUrl(imgUrl)) return imgUrl;
        }
    }
    return L"";
}

bool DownloadUrlWinHttp(const std::wstring& url,
                        std::vector<BYTE>& outBytes,
                        std::wstring& outContentType,
                        std::wstring& outError) {
    outBytes.clear();
    outContentType.clear();
    outError.clear();

    const size_t hostCap = (std::max<size_t>)(512, url.size() + 32);
    const size_t pathCap = (std::max<size_t>)(4096, url.size() + 64);
    std::vector<wchar_t> hostBuf(hostCap, L'\0');
    std::vector<wchar_t> pathBuf(pathCap, L'\0');

    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = hostBuf.data();
    uc.dwHostNameLength = static_cast<DWORD>(hostCap);
    uc.lpszUrlPath = pathBuf.data();
    uc.dwUrlPathLength = static_cast<DWORD>(pathCap);

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || hostBuf[0] == L'\0') {
        outError = L"Invalid URL";
        return false;
    }

    HINTERNET hSess = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) {
        outError = L"HTTP init failed";
        return false;
    }

    WinHttpSetTimeouts(hSess, 5000, 6000, 6000, 9000);
    HINTERNET hConn = WinHttpConnect(hSess, hostBuf.data(), uc.nPort, 0);
    bool ok = false;
    if (hConn) {
        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        const wchar_t* reqPath = (pathBuf[0] != L'\0') ? pathBuf.data() : L"/";
        HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", reqPath, nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (hReq) {
            DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
            std::wstring headers =
                L"Accept: image/png,image/jpeg,image/gif,image/bmp,image/tiff;q=0.9,*/*;q=0.5\r\n"
                L"Referer: https://" + std::wstring(hostBuf.data()) + L"/\r\n";
            if (WinHttpSendRequest(hReq, headers.c_str(), static_cast<DWORD>(headers.size()),
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hReq, nullptr)) {
                DWORD statusCode = 0, szCode = sizeof(statusCode);
                WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &szCode, WINHTTP_NO_HEADER_INDEX);
                wchar_t ctBuf[256]{};
                DWORD ctSz = sizeof(ctBuf);
                if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_TYPE,
                                        WINHTTP_HEADER_NAME_BY_INDEX, ctBuf, &ctSz, WINHTTP_NO_HEADER_INDEX)) {
                    outContentType = ctBuf;
                }
                if (statusCode >= 200 && statusCode < 300) {
                    constexpr size_t kMaxBytes = 12u * 1024u * 1024u;
                    DWORD avail = 0;
                    ok = true;
                    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                        if (outBytes.size() + avail > kMaxBytes) {
                            outBytes.clear();
                            outError = L"Image exceeds 12 MB limit";
                            ok = false;
                            break;
                        }
                        size_t cur = outBytes.size();
                        outBytes.resize(cur + avail);
                        DWORD rd = 0;
                        if (!WinHttpReadData(hReq, outBytes.data() + cur, avail, &rd)) {
                            outBytes.clear();
                            ok = false;
                            break;
                        }
                        outBytes.resize(cur + rd);
                    }
                    if (ok && outBytes.empty()) {
                        outError = L"Empty response";
                        ok = false;
                    }
                } else {
                    outError = L"HTTP " + std::to_wstring(statusCode);
                }
            } else {
                outError = L"Network request failed";
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hConn);
    }
    WinHttpCloseHandle(hSess);
    return ok;
}

// ============================================================================
// Native LaTeX Math Formatter ($...$ / $$...$$) -> Unicode + Style Runs
// ============================================================================
struct MathOutput {
    std::wstring text;
    std::vector<MdInlineSpan> spans; // relative to text.start=0
};

wchar_t MapUnicodeSuperscript(wchar_t c) {
    switch (c) {
        case L'0': return L'\x2070'; case L'1': return L'\x00B9'; case L'2': return L'\x00B2';
        case L'3': return L'\x00B3'; case L'4': return L'\x2074'; case L'5': return L'\x2075';
        case L'6': return L'\x2076'; case L'7': return L'\x2077'; case L'8': return L'\x2078';
        case L'9': return L'\x2079'; case L'+': return L'\x207A'; case L'-': return L'\x207B';
        case L'=': return L'\x207C'; case L'(': return L'\x207D'; case L')': return L'\x207E';
        case L'n': return L'\x207F'; case L'i': return L'\x2071'; case L'T': return L'\x1D40';
        case L'a': return L'\x1D43'; case L'b': return L'\x1D47'; case L'c': return L'\x1D9C';
        case L'd': return L'\x1D48'; case L'e': return L'\x1D49'; case L'f': return L'\x1DA0';
        case L'g': return L'\x1D4D'; case L'h': return L'\x02B0'; case L'j': return L'\x02B2';
        case L'k': return L'\x1D4F'; case L'l': return L'\x02E1'; case L'm': return L'\x1D50';
        case L'o': return L'\x1D52'; case L'p': return L'\x1D56'; case L'r': return L'\x02B3';
        case L's': return L'\x02E2'; case L't': return L'\x1D57'; case L'u': return L'\x1D58';
        case L'v': return L'\x1D5B'; case L'w': return L'\x02B7'; case L'x': return L'\x02E3';
        case L'y': return L'\x02B8'; case L'z': return L'\x1DBB';
        case L'*': return L'*'; case L'\'': return L'\x2032';
        default: return 0;
    }
}

wchar_t MapUnicodeSubscript(wchar_t c) {
    switch (c) {
        case L'0': return L'\x2080'; case L'1': return L'\x2081'; case L'2': return L'\x2082';
        case L'3': return L'\x2083'; case L'4': return L'\x2084'; case L'5': return L'\x2085';
        case L'6': return L'\x2086'; case L'7': return L'\x2087'; case L'8': return L'\x2088';
        case L'9': return L'\x2089'; case L'+': return L'\x208A'; case L'-': return L'\x208B';
        case L'=': return L'\x208C'; case L'(': return L'\x208D'; case L')': return L'\x208E';
        case L'a': return L'\x2090'; case L'e': return L'\x2091'; case L'h': return L'\x2095';
        case L'i': return L'\x1D62'; case L'j': return L'\x2C7C'; case L'k': return L'\x2096';
        case L'l': return L'\x2097'; case L'm': return L'\x2098'; case L'n': return L'\x2099';
        case L'o': return L'\x2092'; case L'p': return L'\x209A'; case L'r': return L'\x1D63';
        case L's': return L'\x209B'; case L't': return L'\x209C'; case L'u': return L'\x1D64';
        case L'v': return L'\x1D65'; case L'x': return L'\x2093';
        default: return 0;
    }
}

std::wstring LookupLatexCommandSymbol(const std::wstring& cmd, bool& outIsUprightFunc) {
    outIsUprightFunc = false;
    static const std::unordered_map<std::wstring, std::wstring> kSym = {
        { L"alpha", L"\x03B1" }, { L"beta", L"\x03B2" }, { L"gamma", L"\x03B3" }, { L"delta", L"\x03B4" },
        { L"epsilon", L"\x03B5" }, { L"varepsilon", L"\x03B5" }, { L"zeta", L"\x03B6" }, { L"eta", L"\x03B7" },
        { L"theta", L"\x03B8" }, { L"vartheta", L"\x03D1" }, { L"iota", L"\x03B9" }, { L"kappa", L"\x03BA" },
        { L"lambda", L"\x03BB" }, { L"mu", L"\x03BC" }, { L"nu", L"\x03BD" }, { L"xi", L"\x03BE" },
        { L"pi", L"\x03C0" }, { L"rho", L"\x03C1" }, { L"sigma", L"\x03C3" }, { L"tau", L"\x03C4" },
        { L"upsilon", L"\x03C5" }, { L"phi", L"\x03C6" }, { L"varphi", L"\x03D5" }, { L"chi", L"\x03C7" },
        { L"psi", L"\x03C8" }, { L"omega", L"\x03C9" },
        { L"Gamma", L"\x0393" }, { L"Delta", L"\x0394" }, { L"Theta", L"\x0398" }, { L"Lambda", L"\x039B" },
        { L"Xi", L"\x039E" }, { L"Pi", L"\x03A0" }, { L"Sigma", L"\x03A3" }, { L"Phi", L"\x03A6" },
        { L"Psi", L"\x03A8" }, { L"Omega", L"\x03A9" },
        { L"top", L"T" }, { L"perp", L"\x22A5" }, { L"partial", L"\x2202" }, { L"nabla", L"\x2207" },
        { L"infty", L"\x221E" }, { L"cdot", L"\x00B7" }, { L"times", L"\x00D7" }, { L"div", L"\x00F7" },
        { L"pm", L"\x00B1" }, { L"mp", L"\x2213" }, { L"leq", L"\x2264" }, { L"le", L"\x2264" },
        { L"geq", L"\x2265" }, { L"ge", L"\x2265" }, { L"neq", L"\x2260" }, { L"ne", L"\x2260" },
        { L"approx", L"\x2248" }, { L"equiv", L"\x2261" }, { L"sim", L"\x223C" }, { L"propto", L"\x221D" },
        { L"in", L"\x2208" }, { L"notin", L"\x2209" }, { L"subset", L"\x2282" }, { L"subseteq", L"\x2286" },
        { L"cup", L"\x222A" }, { L"cap", L"\x2229" }, { L"emptyset", L"\x2205" }, { L"forall", L"\x2200" },
        { L"exists", L"\x2203" }, { L"to", L"\x2192" }, { L"rightarrow", L"\x2192" }, { L"leftarrow", L"\x2190" },
        { L"Rightarrow", L"\x21D2" }, { L"Leftrightarrow", L"\x21D4" }, { L"mapsto", L"\x21A6" },
        { L"sum", L"\x2211" }, { L"prod", L"\x220F" }, { L"int", L"\x222B" },
        { L"dots", L"\x2026" }, { L"ldots", L"\x2026" }, { L"cdots", L"\x22EF" },
        { L"|", L"\x2016" }, { L"langle", L"\x27E8" }, { L"rangle", L"\x27E9" }, { L"ell", L"\x2113" },
        { L"prime", L"\x2032" }, { L"ast", L"*" }, { L"circ", L"\x2218" },
        { L"quad", L"  " }, { L"qquad", L"    " }, { L",", L" " }, { L";", L" " }, { L":", L" " }, { L"!", L"" },
        { L"left", L"" }, { L"right", L"" }, { L"big", L"" }, { L"Big", L"" }, { L"bigg", L"" }, { L"Bigg", L"" },
        { L"displaystyle", L"" }, { L"limits", L"" }, { L"nolimits", L"" }
    };
    auto it = kSym.find(cmd);
    if (it != kSym.end()) return it->second;

    static const std::unordered_set<std::wstring> kFuncs = {
        L"sin", L"cos", L"tan", L"cot", L"sec", L"csc",
        L"sinh", L"cosh", L"tanh", L"arcsin", L"arccos", L"arctan",
        L"log", L"ln", L"lg", L"exp", L"max", L"min", L"argmax", L"argmin",
        L"sup", L"inf", L"lim", L"limsup", L"liminf", L"det", L"tr", L"diag", L"dim", L"Pr", L"softmax", L"relu"
    };
    if (kFuncs.count(cmd)) {
        outIsUprightFunc = true;
        return cmd;
    }
    return L"";
}

std::wstring ReadLatexArgOrAtom(const std::wstring& s, size_t& pos) {
    while (pos < s.size() && s[pos] == L' ') ++pos;
    if (pos >= s.size()) return L"";
    if (s[pos] == L'{') {
        size_t start = ++pos;
        int depth = 1;
        while (pos < s.size() && depth > 0) {
            if (s[pos] == L'\\' && pos + 1 < s.size()) {
                pos += 2;
                continue;
            }
            if (s[pos] == L'{') ++depth;
            else if (s[pos] == L'}') --depth;
            if (depth == 0) {
                std::wstring grp = s.substr(start, pos - start);
                ++pos;
                return grp;
            }
            ++pos;
        }
        return s.substr(start);
    }
    if (s[pos] == L'\\') {
        size_t start = pos++;
        if (pos < s.size() && !iswalpha(s[pos])) {
            ++pos;
            return s.substr(start, pos - start);
        }
        while (pos < s.size() && iswalpha(s[pos])) ++pos;
        return s.substr(start, pos - start);
    }
    return std::wstring(1, s[pos++]);
}

void ConvertLatexMathRecursive(const std::wstring& src, bool forceUpright, bool forceBold,
                               MathOutput& out, bool inheritedItalicDefault = true) {
    auto pushRun = [&](const std::wstring& piece, bool italic, bool bold, bool subSup) {
        if (piece.empty()) return;
        UINT32 st = static_cast<UINT32>(out.text.size());
        out.text += piece;
        MdInlineSpan sp{};
        sp.rng = { st, static_cast<UINT32>(piece.size()) };
        sp.math = true;
        sp.italic = italic && !forceUpright;
        sp.bold = bold || forceBold;
        sp.mathSubOrSuper = subSup;
        out.spans.push_back(sp);
    };

    for (size_t i = 0; i < src.size();) {
        wchar_t ch = src[i];
        if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
            if (!out.text.empty() && out.text.back() != L' ') {
                pushRun(L" ", false, false, false);
            }
            ++i;
            continue;
        }
        if (ch == L'{' || ch == L'}') {
            ++i;
            continue;
        }
        if (ch == L'^' || ch == L'_') {
            bool isSup = (ch == L'^');
            ++i;
            std::wstring rawArg = ReadLatexArgOrAtom(src, i);
            MathOutput subOut;
            ConvertLatexMathRecursive(rawArg, forceUpright, forceBold, subOut, inheritedItalicDefault);
            std::wstring mapped;
            bool allMapped = !subOut.text.empty();
            for (wchar_t mc : subOut.text) {
                if (mc == L' ') continue;
                wchar_t rep = isSup ? MapUnicodeSuperscript(mc) : MapUnicodeSubscript(mc);
                if (rep == 0) {
                    allMapped = false;
                    break;
                }
                mapped.push_back(rep);
            }
            if (allMapped) {
                pushRun(mapped, false, false, false);
            } else if (!subOut.text.empty()) {
                pushRun(subOut.text, false, false, true);
            }
            continue;
        }
        if (ch == L'\\') {
            ++i;
            if (i >= src.size()) break;
            std::wstring cmd;
            if (!iswalpha(src[i])) {
                cmd = std::wstring(1, src[i++]);
            } else {
                size_t cs = i;
                while (i < src.size() && iswalpha(src[i])) ++i;
                cmd = src.substr(cs, i - cs);
            }
            if (cmd == L"operatorname" || cmd == L"mathrm" || cmd == L"text" || cmd == L"mbox") {
                std::wstring inner = ReadLatexArgOrAtom(src, i);
                if (cmd == L"operatorname" && !out.text.empty() && !iswspace(out.text.back()) && out.text.back() != L'(') {
                    pushRun(L" ", false, false, false);
                }
                ConvertLatexMathRecursive(inner, true, forceBold, out, false);
                if (cmd == L"operatorname" && i < src.size() && src[i] != L'(' && src[i] != L'^' && src[i] != L'_') {
                    pushRun(L" ", false, false, false);
                }
                continue;
            }
            if (cmd == L"mathbf" || cmd == L"boldsymbol" || cmd == L"textbf" || cmd == L"bm") {
                std::wstring inner = ReadLatexArgOrAtom(src, i);
                ConvertLatexMathRecursive(inner, (cmd == L"mathbf" || cmd == L"textbf"), true, out, inheritedItalicDefault);
                continue;
            }
            if (cmd == L"mathit") {
                std::wstring inner = ReadLatexArgOrAtom(src, i);
                ConvertLatexMathRecursive(inner, false, forceBold, out, true);
                continue;
            }
            if (cmd == L"mathbb") {
                std::wstring inner = ReadLatexArgOrAtom(src, i);
                std::wstring bb;
                for (wchar_t c : inner) {
                    if (c == L'R') bb += L"\x211D";
                    else if (c == L'N') bb += L"\x2115";
                    else if (c == L'Z') bb += L"\x2124";
                    else if (c == L'C') bb += L"\x2102";
                    else if (c == L'Q') bb += L"\x211A";
                    else if (c == L'E') bb += L"\U0001D53C";
                    else if (c == L'P') bb += L"\x2119";
                    else bb.push_back(c);
                }
                pushRun(bb, false, false, false);
                continue;
            }
            if (cmd == L"frac" || cmd == L"dfrac" || cmd == L"tfrac") {
                std::wstring num = ReadLatexArgOrAtom(src, i);
                std::wstring den = ReadLatexArgOrAtom(src, i);
                MathOutput nOut, dOut;
                ConvertLatexMathRecursive(num, forceUpright, forceBold, nOut, inheritedItalicDefault);
                ConvertLatexMathRecursive(den, forceUpright, forceBold, dOut, inheritedItalicDefault);
                if (nOut.text.size() > 1) pushRun(L"(", false, false, false);
                ConvertLatexMathRecursive(num, forceUpright, forceBold, out, inheritedItalicDefault);
                if (nOut.text.size() > 1) pushRun(L")", false, false, false);
                pushRun(L" / ", false, false, false);
                if (dOut.text.size() > 1) pushRun(L"(", false, false, false);
                ConvertLatexMathRecursive(den, forceUpright, forceBold, out, inheritedItalicDefault);
                if (dOut.text.size() > 1) pushRun(L")", false, false, false);
                continue;
            }
            if (cmd == L"sqrt") {
                if (i < src.size() && src[i] == L'[') {
                    size_t rb = src.find(L']', i + 1);
                    if (rb != std::wstring::npos) i = rb + 1;
                }
                std::wstring inner = ReadLatexArgOrAtom(src, i);
                pushRun(L"\x221A", false, false, false);
                MathOutput innerOut;
                ConvertLatexMathRecursive(inner, forceUpright, forceBold, innerOut, inheritedItalicDefault);
                if (innerOut.text.size() > 1) pushRun(L"(", false, false, false);
                ConvertLatexMathRecursive(inner, forceUpright, forceBold, out, inheritedItalicDefault);
                if (innerOut.text.size() > 1) pushRun(L")", false, false, false);
                continue;
            }
            if (cmd == L"hat" || cmd == L"bar" || cmd == L"vec" || cmd == L"tilde") {
                std::wstring inner = ReadLatexArgOrAtom(src, i);
                ConvertLatexMathRecursive(inner, forceUpright, forceBold, out, inheritedItalicDefault);
                wchar_t comb = (cmd == L"hat") ? L'\x0302' : (cmd == L"bar") ? L'\x0304' : (cmd == L"vec") ? L'\x20D7' : L'\x0303';
                pushRun(std::wstring(1, comb), false, false, false);
                continue;
            }

            bool isUprightFn = false;
            std::wstring sym = LookupLatexCommandSymbol(cmd, isUprightFn);
            if (!sym.empty()) {
                if (isUprightFn && !out.text.empty() && !iswspace(out.text.back()) && out.text.back() != L'(') {
                    pushRun(L" ", false, false, false);
                }
                bool isSymItalic = (!isUprightFn && cmd.size() >= 2 && iswlower(cmd[0]) &&
                                    sym.size() == 1 && sym[0] >= 0x03B1 && sym[0] <= 0x03D5);
                pushRun(sym, isSymItalic, false, false);
                if (isUprightFn && i < src.size() && src[i] != L'(' && src[i] != L'^' && src[i] != L'_' && !iswspace(src[i])) {
                    pushRun(L" ", false, false, false);
                }
            }
            continue;
        }

        // Plain math characters: variables italic, numbers & operators upright
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z')) {
            pushRun(std::wstring(1, ch), inheritedItalicDefault, false, false);
        } else if (ch == L'=' || ch == L'+' || ch == L'<' || ch == L'>') {
            if (!out.text.empty() && !iswspace(out.text.back())) pushRun(L" ", false, false, false);
            pushRun(std::wstring(1, ch), false, false, false);
            if (i + 1 < src.size() && !iswspace(src[i + 1])) pushRun(L" ", false, false, false);
        } else {
            pushRun(std::wstring(1, ch), false, false, false);
        }
        ++i;
    }
}

MathOutput FormatLatexMath(const std::wstring& latex) {
    MathOutput res;
    ConvertLatexMathRecursive(latex, false, false, res, true);
    return res;
}

// Finds closing delimiter matching CommonMark-safe inline rules (skips backtick spans)
size_t FindClosingInlineDelim(const std::wstring& s, size_t from, size_t end, const std::wstring& delim) {
    for (size_t i = from; i + delim.size() <= end;) {
        if (s[i] == L'\\' && i + 1 < end) {
            i += 2;
            continue;
        }
        if (s[i] == L'`') {
            size_t bt = s.find(L'`', i + 1);
            if (bt != std::wstring::npos && bt < end) {
                i = bt + 1;
                continue;
            }
        }
        if (s.compare(i, delim.size(), delim) == 0) {
            // For underscores avoid matching mid-identifier like var_name
            if (delim == L"_" && i + 1 < end && iswalnum(s[i + 1])) {
                ++i;
                continue;
            }
            return i;
        }
        ++i;
    }
    return std::wstring::npos;
}

struct MdInheritedStyle {
    bool bold = false;
    bool italic = false;
    bool link = false;
    std::wstring url;
};

void AppendMdSpanRange(const std::wstring& text, size_t start, size_t end,
                       const MdInheritedStyle& style,
                       std::wstring& outClean,
                       std::vector<MdInlineSpan>& outSpans,
                       std::vector<MdImageRef>& outImages) {
    auto pushStyledChar = [&](wchar_t c) {
        UINT32 st = static_cast<UINT32>(outClean.size());
        outClean.push_back(c);
        if (style.bold || style.italic || style.link) {
            if (!outSpans.empty()) {
                auto& prev = outSpans.back();
                if (!prev.code && !prev.math &&
                    prev.rng.startPosition + prev.rng.length == st &&
                    prev.bold == style.bold && prev.italic == style.italic &&
                    prev.link == style.link && prev.url == style.url) {
                    prev.rng.length += 1;
                    return;
                }
            }
            outSpans.push_back({ { st, 1 }, style.bold, style.italic, false, style.link, false, false, style.url });
        }
    };

    for (size_t i = start; i < end;) {
        // Escaped character
        if (text[i] == L'\\' && i + 1 < end &&
            (text[i + 1] == L'*' || text[i + 1] == L'$' || text[i + 1] == L'_' ||
             text[i + 1] == L'`' || text[i + 1] == L'[' || text[i + 1] == L']' || text[i + 1] == L'\\')) {
            pushStyledChar(text[i + 1]);
            i += 2;
            continue;
        }

        // 1. Inline code span (`...` or ``...``) takes precedence over *, $, _
        if (text[i] == L'`') {
            size_t btLen = 1;
            while (i + btLen < end && text[i + btLen] == L'`') ++btLen;
            std::wstring btDelim(btLen, L'`');
            size_t close = text.find(btDelim, i + btLen);
            if (close != std::wstring::npos && close < end && close > i + btLen) {
                std::wstring codeBody = text.substr(i + btLen, close - (i + btLen));
                if (codeBody.size() >= 2 && codeBody.front() == L' ' && codeBody.back() == L' ') {
                    bool allSpace = true;
                    for (wchar_t ch : codeBody) if (ch != L' ') { allSpace = false; break; }
                    if (!allSpace) codeBody = codeBody.substr(1, codeBody.size() - 2);
                }
                UINT32 st = static_cast<UINT32>(outClean.size());
                outClean += codeBody;
                outSpans.push_back({ { st, static_cast<UINT32>(codeBody.size()) },
                                     style.bold, style.italic, true, style.link, false, false, style.url });
                i = close + btLen;
                continue;
            }
        }

        // 2. Inline math $...$ or inline $$...$$ (avoid false trigger on bare currency like "$10 and $20")
        if (text[i] == L'$') {
            const bool dbl = (i + 1 < end && text[i + 1] == L'$');
            const size_t openLen = dbl ? 2 : 1;
            size_t close = std::wstring::npos;
            for (size_t k = i + openLen; k < end; ++k) {
                if (text[k] == L'\\' && k + 1 < end) { ++k; continue; }
                if (dbl) {
                    if (k + 1 < end && text[k] == L'$' && text[k + 1] == L'$') { close = k; break; }
                } else {
                    if (text[k] == L'$') { close = k; break; }
                }
            }
            if (close != std::wstring::npos && close > i + openLen) {
                std::wstring expr = text.substr(i + openLen, close - (i + openLen));
                const bool leadingSpace = iswspace(expr.front());
                const bool trailingSpace = iswspace(expr.back());
                if (dbl || (!leadingSpace && !trailingSpace)) {
                    MathOutput mout = FormatLatexMath(expr);
                    UINT32 baseOffset = static_cast<UINT32>(outClean.size());
                    outClean += mout.text;
                    for (auto msp : mout.spans) {
                        msp.rng.startPosition += baseOffset;
                        msp.bold = msp.bold || style.bold;
                        msp.link = style.link;
                        msp.url = style.url;
                        outSpans.push_back(std::move(msp));
                    }
                    i = close + openLen;
                    continue;
                }
            }
        }

        // 3. Markdown image ![alt](target)
        if (i + 1 < end && text[i] == L'!' && text[i + 1] == L'[') {
            size_t closeBr = text.find(L']', i + 2);
            if (closeBr != std::wstring::npos && closeBr + 1 < end && text[closeBr + 1] == L'(') {
                size_t closePar = text.find(L')', closeBr + 2);
                if (closePar != std::wstring::npos && closePar < end) {
                    std::wstring alt = text.substr(i + 2, closeBr - (i + 2));
                    std::wstring tgt = UnwrapImageSearchUrl(text.substr(closeBr + 2, closePar - (closeBr + 2)));
                    outImages.push_back({ std::move(alt), std::move(tgt) });
                    i = closePar + 1;
                    continue;
                }
            }
        }

        // 4. Hyperlink [label](url) with recursive nested formatting inside label
        if (text[i] == L'[') {
            size_t closeBr = std::wstring::npos;
            for (size_t k = i + 1; k < end; ++k) {
                if (text[k] == L'`') {
                    size_t bt = text.find(L'`', k + 1);
                    if (bt != std::wstring::npos && bt < end) { k = bt; continue; }
                }
                if (text[k] == L']') { closeBr = k; break; }
            }
            if (closeBr != std::wstring::npos && closeBr + 1 < end && text[closeBr + 1] == L'(') {
                size_t closePar = text.find(L')', closeBr + 2);
                if (closePar != std::wstring::npos && closePar < end && closeBr > i + 1) {
                    MdInheritedStyle linkStyle = style;
                    linkStyle.link = true;
                    linkStyle.url = TrimMdTarget(text.substr(closeBr + 2, closePar - (closeBr + 2)));
                    AppendMdSpanRange(text, i + 1, closeBr, linkStyle, outClean, outSpans, outImages);
                    i = closePar + 1;
                    continue;
                }
            }
        }

        // 5. Bold (**...** or __...__)
        if (i + 3 < end && ((text[i] == L'*' && text[i + 1] == L'*') || (text[i] == L'_' && text[i + 1] == L'_'))) {
            std::wstring d = text.substr(i, 2);
            size_t close = FindClosingInlineDelim(text, i + 2, end, d);
            if (close != std::wstring::npos && close > i + 2) {
                MdInheritedStyle boldStyle = style;
                boldStyle.bold = true;
                AppendMdSpanRange(text, i + 2, close, boldStyle, outClean, outSpans, outImages);
                i = close + 2;
                continue;
            }
        }

        // 6. Italic (*...* or word-bounded _..._)
        if (text[i] == L'*' ||
            (text[i] == L'_' && (i == start || !iswalnum(text[i - 1])))) {
            std::wstring d(1, text[i]);
            size_t close = FindClosingInlineDelim(text, i + 1, end, d);
            if (close != std::wstring::npos && close > i + 1 && !iswspace(text[i + 1]) && !iswspace(text[close - 1])) {
                MdInheritedStyle itStyle = style;
                itStyle.italic = true;
                AppendMdSpanRange(text, i + 1, close, itStyle, outClean, outSpans, outImages);
                i = close + 1;
                continue;
            }
        }

        pushStyledChar(text[i++]);
    }
}

void ParseMdInlineAndImages(const std::wstring& text,
                            std::wstring& outClean,
                            std::vector<MdInlineSpan>& outSpans,
                            std::vector<MdImageRef>& outImages) {
    outClean.clear();
    outSpans.clear();
    outImages.clear();
    AppendMdSpanRange(text, 0, text.size(), MdInheritedStyle{}, outClean, outSpans, outImages);
}

void ComputeMdImageDrawSize(uint32_t pxW, uint32_t pxH, float availW, const phide::app::AppConfig& config,
                            float& outW, float& outH) {
    if (pxW == 0 || pxH == 0) {
        outW = config.ScaleF(220.0f);
        outH = config.ScaleF(40.0f);
        return;
    }
    float w = static_cast<float>(pxW);
    float h = static_cast<float>(pxH);
    const float maxW = (std::max)(config.ScaleF(80.0f), availW);
    const float maxH = config.ScaleF(360.0f);
    if (w > maxW) {
        float sc = maxW / w;
        w *= sc;
        h *= sc;
    }
    if (h > maxH) {
        float sc = maxH / h;
        w *= sc;
        h *= sc;
    }
    outW = (std::max)(config.ScaleF(24.0f), w);
    outH = (std::max)(config.ScaleF(24.0f), h);
}

Microsoft::WRL::ComPtr<IDWriteTextLayout> CreateMdStyledLayout(
    IDWriteFactory* dwriteFactory, IDWriteTextFormat* baseFmt,
    const std::wstring& clean, const std::vector<MdInlineSpan>& spans,
    float maxWidth, float fontSize, bool isHeading, bool isQuote,
    bool centerAlign, const phide::app::AppConfig& config,
    ID2D1Brush* codeBrush, ID2D1Brush* linkBrush, float& outMeasuredH) {
    outMeasuredH = (isHeading ? fontSize * 1.45f : fontSize * 1.55f);
    if (!dwriteFactory || !baseFmt || clean.empty()) return nullptr;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
    if (FAILED(dwriteFactory->CreateTextLayout(
            clean.c_str(), static_cast<UINT32>(clean.length()), baseFmt,
            (std::max)(40.0f, maxWidth), 2400.0f, &pLayout)) || !pLayout) {
        return nullptr;
    }
    DWRITE_TEXT_RANGE allRng = { 0, static_cast<UINT32>(clean.length()) };
    pLayout->SetFontSize(fontSize, allRng);
    pLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    pLayout->SetTextAlignment(centerAlign ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);

    const float lineStep = isHeading ? (fontSize * 1.32f) : (fontSize * 1.56f);
    const float baseline = lineStep * 0.78f;
    pLayout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lineStep, baseline);

    if (isHeading) pLayout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, allRng);
    if (isQuote) pLayout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, allRng);

    for (const auto& sp : spans) {
        if (sp.rng.length == 0) continue;
        if (sp.bold) pLayout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, sp.rng);
        if (sp.italic) pLayout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, sp.rng);
        if (sp.code) {
            pLayout->SetFontFamilyName(L"Consolas", sp.rng);
            pLayout->SetFontSize(fontSize * 0.88f, sp.rng);
            if (codeBrush) pLayout->SetDrawingEffect(codeBrush, sp.rng);
        } else if (sp.math) {
            pLayout->SetFontFamilyName(L"Georgia", sp.rng);
            if (sp.mathSubOrSuper) {
                pLayout->SetFontSize(fontSize * 0.76f, sp.rng);
            } else {
                pLayout->SetFontSize(fontSize * 1.03f, sp.rng);
            }
        }
        if (sp.link) {
            pLayout->SetUnderline(TRUE, sp.rng);
            if (linkBrush) pLayout->SetDrawingEffect(linkBrush, sp.rng);
        }
    }

    DWRITE_TEXT_METRICS tm{};
    if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
        float extraBottom = isHeading ? config.ScaleF(6.0f) : config.ScaleF(4.0f);
        outMeasuredH = (std::max)(lineStep, std::ceil(tm.height) + extraBottom);
    }
    return pLayout;
}

} // namespace

std::wstring NotebookView::FormatLatexMathForTest(const std::wstring& latex) {
    return FormatLatexMath(latex).text;
}

std::wstring NotebookView::ResolveMarkdownImageUrlForTest(const std::wstring& rawUrl) {
    return UnwrapImageSearchUrl(rawUrl);
}

void NotebookView::SeedMarkdownImageForTest(const std::wstring& target, const std::vector<BYTE>& bytes, uint32_t w, uint32_t h) {
    std::wstring norm = UnwrapImageSearchUrl(target);
    if (norm.empty()) return;
    CachedMdImage item;
    item.status = MdImageStatus::Ready;
    item.bytes = bytes;
    item.pixelW = w;
    item.pixelH = h;
    item.imageId = editor::NextCellImageId();
    std::lock_guard<std::mutex> lk(m_mdImageCacheMutex);
    m_mdImageCache[norm] = std::move(item);
}

bool NotebookView::ProbeWicImageDimensions(const std::vector<BYTE>& bytes, uint32_t& outW, uint32_t& outH) {
    outW = 0;
    outH = 0;
    if (bytes.empty()) return false;
    if (!m_pWicFactory) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_pWicFactory));
    }
    if (!m_pWicFactory) return false;
    Microsoft::WRL::ComPtr<IWICStream> pStream;
    if (FAILED(m_pWicFactory->CreateStream(&pStream)) || !pStream) return false;
    if (FAILED(pStream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())))) return false;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> pDec;
    if (FAILED(m_pWicFactory->CreateDecoderFromStream(pStream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &pDec)) || !pDec) return false;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> pFrame;
    if (FAILED(pDec->GetFrame(0, &pFrame)) || !pFrame) return false;
    UINT w = 0, h = 0;
    if (FAILED(pFrame->GetSize(&w, &h)) || w == 0 || h == 0) return false;
    outW = w;
    outH = h;
    return true;
}

NotebookView::CachedMdImage NotebookView::EnsureMarkdownImage(const editor::OpenDocument& doc, const std::wstring& rawTarget) {
    std::wstring target = UnwrapImageSearchUrl(rawTarget);
    if (target.empty() || _wcsicmp(target.c_str(), L"image_url") == 0) {
        CachedMdImage ph;
        ph.status = MdImageStatus::Failed;
        ph.errorMsg = L"placeholder";
        return ph;
    }

    // Strip file:/// URI scheme if present
    std::wstring resolvedTarget = target;
    if (_wcsnicmp(resolvedTarget.c_str(), L"file:///", 8) == 0) {
        resolvedTarget = resolvedTarget.substr(8);
        std::replace(resolvedTarget.begin(), resolvedTarget.end(), L'/', L'\\');
    }

    // Cache key distinguishes workspace/doc base for relative paths
    std::wstring docDir;
    if (doc.notebookDoc && !doc.notebookDoc->filePath.empty()) {
        docDir = std::filesystem::path(doc.notebookDoc->filePath).parent_path().wstring();
    } else if (!doc.filePath.empty()) {
        docDir = std::filesystem::path(doc.filePath).parent_path().wstring();
    } else {
        docDir = m_workspacePath;
    }
    const bool isRemote = IsHttpUrl(resolvedTarget);
    std::wstring cacheKey = isRemote ? resolvedTarget : (docDir + L"|" + resolvedTarget);

    {
        std::lock_guard<std::mutex> lk(m_mdImageCacheMutex);
        auto it = m_mdImageCache.find(cacheKey);
        if (it != m_mdImageCache.end()) {
            return it->second;
        }
        auto itRaw = m_mdImageCache.find(target);
        if (itRaw != m_mdImageCache.end()) {
            return itRaw->second;
        }
        if (isRemote) {
            CachedMdImage pending;
            pending.status = MdImageStatus::Pending;
            m_mdImageCache[cacheKey] = pending;
        }
    }

    if (!isRemote) {
        CachedMdImage res;
        res.status = MdImageStatus::Failed;
        std::error_code ec;
        std::filesystem::path p(resolvedTarget);
        std::vector<std::filesystem::path> candidates;
        if (p.is_absolute()) {
            candidates.push_back(p);
        } else {
            if (!docDir.empty()) candidates.push_back(std::filesystem::path(docDir) / p);
            if (!m_workspacePath.empty() && m_workspacePath != docDir) candidates.push_back(std::filesystem::path(m_workspacePath) / p);
            candidates.push_back(p);
        }
        for (const auto& cand : candidates) {
            if (std::filesystem::exists(cand, ec) && std::filesystem::is_regular_file(cand, ec)) {
                uintmax_t fsz = std::filesystem::file_size(cand, ec);
                if (!ec && fsz > 0 && fsz <= 16u * 1024u * 1024u) {
                    std::ifstream ifs(cand, std::ios::binary);
                    if (ifs) {
                        res.bytes.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
                        if (ProbeWicImageDimensions(res.bytes, res.pixelW, res.pixelH)) {
                            res.status = MdImageStatus::Ready;
                            res.imageId = editor::NextCellImageId();
                        } else {
                            res.bytes.clear();
                            res.errorMsg = L"Unsupported image format";
                        }
                        break;
                    }
                }
            }
        }
        if (res.status == MdImageStatus::Failed && res.errorMsg.empty()) {
            res.errorMsg = L"File not found";
        }
        std::lock_guard<std::mutex> lk(m_mdImageCacheMutex);
        m_mdImageCache[cacheKey] = res;
        return res;
    }

    // Remote HTTP/HTTPS image: download asynchronously via WinHTTP (with HTML og:image fallback)
    if (!m_isDestroying.load()) {
        m_activeWorkers.fetch_add(1);
        std::thread([this, cacheKey, resolvedTarget]() {
            struct WorkerGuard {
                std::atomic<int>& counter;
                ~WorkerGuard() { counter.fetch_sub(1); }
            } guard{ m_activeWorkers };

            CachedMdImage fetched;
            fetched.status = MdImageStatus::Failed;

            std::vector<BYTE> body;
            std::wstring contentType;
            std::wstring dlErr;
            if (DownloadUrlWinHttp(resolvedTarget, body, contentType, dlErr)) {
                std::wstring ctLow = contentType;
                for (wchar_t& c : ctLow) c = static_cast<wchar_t>(towlower(c));
                bool startsWithAngle = false;
                for (size_t k = 0; k < body.size() && k < 64; ++k) {
                    if (body[k] == ' ' || body[k] == '\t' || body[k] == '\r' || body[k] == '\n') continue;
                    startsWithAngle = (body[k] == '<');
                    break;
                }
                const bool looksHtml = (ctLow.find(L"text/html") != std::wstring::npos) || startsWithAngle;
                if (looksHtml) {
                    std::wstring ogUrl = ExtractOgImageFromHtml(body, resolvedTarget);
                    if (!ogUrl.empty()) {
                        std::vector<BYTE> ogBody;
                        std::wstring ogCt, ogErr;
                        if (DownloadUrlWinHttp(ogUrl, ogBody, ogCt, ogErr) && !ogBody.empty()) {
                            body = std::move(ogBody);
                            contentType = std::move(ogCt);
                        } else {
                            dlErr = ogErr.empty() ? L"URL is a webpage, not a direct image" : ogErr;
                        }
                    } else {
                        dlErr = L"URL is a webpage, not a direct image";
                    }
                }

                if (!body.empty() && dlErr.empty()) {
                    Microsoft::WRL::ComPtr<IWICImagingFactory> wicLocal;
                    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
                        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicLocal))) && wicLocal) {
                            Microsoft::WRL::ComPtr<IWICStream> strm;
                            Microsoft::WRL::ComPtr<IWICBitmapDecoder> dec;
                            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frm;
                            UINT pw = 0, ph = 0;
                            if (SUCCEEDED(wicLocal->CreateStream(&strm)) && strm &&
                                SUCCEEDED(strm->InitializeFromMemory(body.data(), static_cast<DWORD>(body.size()))) &&
                                SUCCEEDED(wicLocal->CreateDecoderFromStream(strm.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) && dec &&
                                SUCCEEDED(dec->GetFrame(0, &frm)) && frm &&
                                SUCCEEDED(frm->GetSize(&pw, &ph)) && pw > 0 && ph > 0) {
                                fetched.bytes = std::move(body);
                                fetched.pixelW = pw;
                                fetched.pixelH = ph;
                                fetched.imageId = editor::NextCellImageId();
                                fetched.status = MdImageStatus::Ready;
                            } else {
                                fetched.errorMsg = (contentType.find(L"avif") != std::wstring::npos)
                                    ? L"Unsupported AVIF codec"
                                    : L"Invalid or HTML image response";
                            }
                        }
                        CoUninitialize();
                    }
                } else {
                    fetched.errorMsg = dlErr;
                }
            } else {
                fetched.errorMsg = dlErr;
            }
            if (fetched.status == MdImageStatus::Failed && fetched.errorMsg.empty()) {
                fetched.errorMsg = L"Unable to fetch image";
            }
            if (!m_isDestroying.load()) {
                std::lock_guard<std::mutex> lk(m_mdImageCacheMutex);
                m_mdImageCache[cacheKey] = std::move(fetched);
            }
            if (!m_isDestroying.load() && m_hostHwnd && IsWindow(m_hostHwnd)) {
                InvalidateRect(m_hostHwnd, nullptr, FALSE);
            }
        }).detach();
    }

    CachedMdImage pending;
    pending.status = MdImageStatus::Pending;
    return pending;
}

float NotebookView::MeasureRenderedMarkdownHeight(Theme& theme, const editor::OpenDocument& doc, const editor::NotebookCell& cell,
                                                  const app::AppConfig& config, float maxAvailW) {
    EnsureNbTextFormats(theme, config);
    IDWriteTextFormat* proseFmt = m_fmtNbMdProse ? m_fmtNbMdProse.Get() : (theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontCode.Get());
    const auto& lines = cell.inputBuffer.GetLines();
    if (lines.empty()) return config.ScaleF(40.0f);

    float totalH = config.ScaleF(20.0f);
    bool inFence = false;
    bool inDisplayMath = false;
    std::wstring displayMathBuf;
    const float maxW = (std::max)(config.ScaleF(80.0f), maxAvailW - config.ScaleF(32.0f));

    for (const auto& raw : lines) {
        size_t s = 0;
        while (s < raw.size() && (raw[s] == L' ' || raw[s] == L'\t')) ++s;
        if (s >= raw.size()) {
            totalH += inDisplayMath ? config.ScaleF(8.0f) : config.ScaleF(9.0f);
            continue;
        }
        std::wstring text = raw.substr(s);
        if (text.compare(0, 3, L"```") == 0) {
            inFence = !inFence;
            totalH += config.ScaleF(10.0f);
            continue;
        }
        if (inFence) {
            totalH += config.ScaleF(20.0f);
            continue;
        }
        // Single-line or multi-line $$...$$ display math
        if (text.compare(0, 2, L"$$") == 0) {
            if (!inDisplayMath && text.size() >= 4 && text.rfind(L"$$") >= 2) {
                std::wstring expr = text.substr(2, text.rfind(L"$$") - 2);
                MathOutput mout = FormatLatexMath(expr);
                float mh = config.ScaleF(34.0f);
                CreateMdStyledLayout(theme.dwriteFactory.Get(), proseFmt, mout.text, mout.spans, maxW, config.ScaleF(16.0f), false, false, true, config, nullptr, nullptr, mh);
                totalH += mh + config.ScaleF(8.0f);
                continue;
            }
            if (!inDisplayMath) {
                inDisplayMath = true;
                displayMathBuf = text.substr(2);
                continue;
            } else {
                inDisplayMath = false;
                MathOutput mout = FormatLatexMath(displayMathBuf);
                float mh = config.ScaleF(34.0f);
                CreateMdStyledLayout(theme.dwriteFactory.Get(), proseFmt, mout.text, mout.spans, maxW, config.ScaleF(16.0f), false, false, true, config, nullptr, nullptr, mh);
                totalH += mh + config.ScaleF(8.0f);
                displayMathBuf.clear();
                continue;
            }
        }
        if (inDisplayMath) {
            if (!displayMathBuf.empty()) displayMathBuf += L" ";
            displayMathBuf += text;
            continue;
        }
        if (text.compare(0, 3, L"---") == 0 || text.compare(0, 3, L"***") == 0) {
            totalH += config.ScaleF(16.0f);
            continue;
        }

        float fontSize = config.ScaleF(13.5f);
        bool isHeading = false;
        bool isQuote = false;
        float indentX = 0.0f;
        size_t numDigits = 0;
        while (numDigits < text.size() && iswdigit(text[numDigits])) ++numDigits;

        if (text.compare(0, 3, L"###") == 0) {
            text = text.substr(3);
            while (!text.empty() && text.front() == L' ') text.erase(text.begin());
            fontSize = config.ScaleF(15.0f);
            isHeading = true;
        } else if (text.compare(0, 2, L"##") == 0) {
            text = text.substr(2);
            while (!text.empty() && text.front() == L' ') text.erase(text.begin());
            fontSize = config.ScaleF(17.5f);
            isHeading = true;
        } else if (text.front() == L'#') {
            text = text.substr(1);
            while (!text.empty() && text.front() == L' ') text.erase(text.begin());
            fontSize = config.ScaleF(21.0f);
            isHeading = true;
        } else if (text.front() == L'>') {
            text = text.substr(1);
            while (!text.empty() && text.front() == L' ') text.erase(text.begin());
            isQuote = true;
            indentX = config.ScaleF(16.0f);
        } else if ((text.size() >= 2 && (text[0] == L'-' || text[0] == L'*') && text[1] == L' ')) {
            text = text.substr(2);
            indentX = config.ScaleF(20.0f);
        } else if (numDigits > 0 && numDigits + 1 < text.size() && text[numDigits] == L'.' && text[numDigits + 1] == L' ') {
            text = text.substr(numDigits + 2);
            indentX = config.ScaleF(26.0f);
        }

        std::wstring clean;
        std::vector<MdInlineSpan> spans;
        std::vector<MdImageRef> images;
        ParseMdInlineAndImages(text, clean, spans, images);

        bool hasVisibleText = false;
        for (wchar_t ch : clean) {
            if (!iswspace(ch)) { hasVisibleText = true; break; }
        }
        if (hasVisibleText) {
            float measuredH = fontSize * 1.56f;
            CreateMdStyledLayout(theme.dwriteFactory.Get(), proseFmt, clean, spans,
                                 (std::max)(40.0f, maxW - indentX), fontSize, isHeading, isQuote, false,
                                 config, nullptr, nullptr, measuredH);
            totalH += measuredH;
        }

        for (const auto& imgRef : images) {
            CachedMdImage img = EnsureMarkdownImage(doc, imgRef.target);
            if (img.status == MdImageStatus::Ready) {
                float dw = 0.0f, dh = 0.0f;
                ComputeMdImageDrawSize(img.pixelW, img.pixelH, (std::max)(40.0f, maxW - indentX), config, dw, dh);
                totalH += dh + config.ScaleF(10.0f);
            } else {
                totalH += config.ScaleF(42.0f);
            }
        }
    }
    return (std::max)(config.ScaleF(40.0f), totalH);
}

void NotebookView::RenderMarkdownPreview(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                         const editor::OpenDocument& doc, const editor::NotebookCell& cell,
                                         const D2D1_RECT_F& bounds, int cellIdx) {
    if (!pRT) return;
    IDWriteTextFormat* proseFmt = m_fmtNbMdProse ? m_fmtNbMdProse.Get() : (theme.fontMenuItem ? theme.fontMenuItem.Get() : theme.fontCode.Get());
    IDWriteTextFormat* monoFmt = theme.fontCode ? theme.fontCode.Get() : proseFmt;
    if (!proseFmt) return;
    const auto& lines = cell.inputBuffer.GetLines();
    const bool dark = theme.IsDarkMode();

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> codePillBg, codePillBorder, codeFgBrush;
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.055f), &codePillBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.11f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.09f), &codePillBorder);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.90f, 0.92f, 0.96f, 1.0f) : D2D1::ColorF(0.14f, 0.16f, 0.21f, 1.0f), &codeFgBrush);

    pRT->PushAxisAlignedClip(bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float y = bounds.top + config.ScaleF(10.0f);
    const float padL = bounds.left + config.ScaleF(16.0f);
    const float maxW = (std::max)(80.0f, bounds.right - padL - config.ScaleF(16.0f));
    bool inFence = false;
    bool inDisplayMath = false;
    std::wstring displayMathBuf;

    auto drawLayoutAndCollectHits = [&](IDWriteTextLayout* pLayout, const std::vector<MdInlineSpan>& spans,
                                        float drawX, float drawY, ID2D1Brush* fgBrush) {
        if (!pLayout || !fgBrush) return;
        // Draw subtle inline code pill backgrounds BEFORE drawing text glyphs
        for (const auto& sp : spans) {
            if (!sp.code || sp.rng.length == 0) continue;
            UINT32 hitCount = 0;
            pLayout->HitTestTextRange(sp.rng.startPosition, sp.rng.length, drawX, drawY, nullptr, 0, &hitCount);
            if (hitCount > 0) {
                std::vector<DWRITE_HIT_TEST_METRICS> htm(hitCount);
                if (SUCCEEDED(pLayout->HitTestTextRange(sp.rng.startPosition, sp.rng.length, drawX, drawY, htm.data(), hitCount, &hitCount))) {
                    for (const auto& m : htm) {
                        D2D1_RECT_F pill = D2D1::RectF(
                            m.left - config.ScaleF(2.5f), m.top + config.ScaleF(1.0f),
                            m.left + m.width + config.ScaleF(2.5f), m.top + m.height - config.ScaleF(1.0f));
                        D2D1_ROUNDED_RECT rrcP = D2D1::RoundedRect(pill, config.ScaleF(3.5f), config.ScaleF(3.5f));
                        if (codePillBg) pRT->FillRoundedRectangle(rrcP, codePillBg.Get());
                        if (codePillBorder) pRT->DrawRoundedRectangle(rrcP, codePillBorder.Get(), 0.75f);
                    }
                }
            }
        }
        pRT->DrawTextLayout(D2D1::Point2F(drawX, drawY), pLayout, fgBrush);

        for (const auto& sp : spans) {
            if (!sp.link || sp.rng.length == 0 || sp.url.empty()) continue;
            UINT32 hitCount = 0;
            pLayout->HitTestTextRange(sp.rng.startPosition, sp.rng.length, drawX, drawY, nullptr, 0, &hitCount);
            if (hitCount > 0) {
                std::vector<DWRITE_HIT_TEST_METRICS> htm(hitCount);
                if (SUCCEEDED(pLayout->HitTestTextRange(sp.rng.startPosition, sp.rng.length, drawX, drawY, htm.data(), hitCount, &hitCount))) {
                    for (const auto& m : htm) {
                        D2D1_RECT_F rcLk = D2D1::RectF(
                            (std::max)(bounds.left, m.left),
                            (std::max)(bounds.top, m.top),
                            (std::min)(bounds.right, m.left + m.width),
                            (std::min)(bounds.bottom, m.top + m.height));
                        if (rcLk.right > rcLk.left && rcLk.bottom > rcLk.top) {
                            m_visibleMdLinks.push_back({ rcLk, cellIdx, sp.url });
                        }
                    }
                }
            }
        }
    };

    ID2D1Brush* linkFg = m_brushNbBlueText ? static_cast<ID2D1Brush*>(m_brushNbBlueText.Get()) : theme.brushBrandAccent.Get();

    for (const auto& raw : lines) {
        size_t s = 0;
        while (s < raw.size() && (raw[s] == L' ' || raw[s] == L'\t')) ++s;
        if (s >= raw.size()) {
            y += inDisplayMath ? config.ScaleF(8.0f) : config.ScaleF(9.0f);
            continue;
        }

        std::wstring text = raw.substr(s);
        if (text.compare(0, 3, L"```") == 0) {
            inFence = !inFence;
            y += config.ScaleF(10.0f);
            continue;
        }
        if (inFence) {
            float rowH = config.ScaleF(20.0f);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> codeBg;
            pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.08f, 0.09f, 0.11f, 0.90f)
                                            : D2D1::ColorF(0.92f, 0.93f, 0.95f, 0.90f), &codeBg);
            if (codeBg) {
                pRT->FillRectangle(D2D1::RectF(padL, y, padL + maxW, y + rowH), codeBg.Get());
            }
            D2D1_RECT_F rcC = D2D1::RectF(padL + config.ScaleF(10.0f), y, padL + maxW - config.ScaleF(8.0f), y + rowH);
            if (monoFmt && theme.brushTextPrimary) {
                pRT->DrawText(text.c_str(), static_cast<UINT32>(text.size()), monoFmt, rcC, theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            y += rowH;
            continue;
        }

        // Display Math block $$...$$
        if (text.compare(0, 2, L"$$") == 0) {
            auto renderDisplayMath = [&](const std::wstring& expr) {
                MathOutput mout = FormatLatexMath(expr);
                float mh = config.ScaleF(34.0f);
                auto pML = CreateMdStyledLayout(theme.dwriteFactory.Get(), proseFmt, mout.text, mout.spans,
                                                maxW, config.ScaleF(16.0f), false, false, true, config,
                                                codeFgBrush.Get(), linkFg, mh);
                if (pML && theme.brushTextPrimary) {
                    pRT->DrawTextLayout(D2D1::Point2F(padL, y + config.ScaleF(4.0f)), pML.Get(), theme.brushTextPrimary.Get());
                }
                y += mh + config.ScaleF(8.0f);
            };
            if (!inDisplayMath && text.size() >= 4 && text.rfind(L"$$") >= 2) {
                renderDisplayMath(text.substr(2, text.rfind(L"$$") - 2));
                continue;
            }
            if (!inDisplayMath) {
                inDisplayMath = true;
                displayMathBuf = text.substr(2);
                continue;
            } else {
                inDisplayMath = false;
                renderDisplayMath(displayMathBuf);
                displayMathBuf.clear();
                continue;
            }
        }
        if (inDisplayMath) {
            if (!displayMathBuf.empty()) displayMathBuf += L" ";
            displayMathBuf += text;
            continue;
        }

        float fontSize = config.ScaleF(13.5f);
        bool isHeading = false;
        bool isQuote = false;
        bool isBullet = false;
        float indentX = 0.0f;
        std::wstring numPrefix;

        size_t numDigits = 0;
        while (numDigits < text.size() && iswdigit(text[numDigits])) ++numDigits;

        if (text.compare(0, 3, L"---") == 0 || text.compare(0, 3, L"***") == 0) {
            float ry = y + config.ScaleF(8.0f);
            if (theme.brushBorderSubtle) {
                pRT->DrawLine(D2D1::Point2F(padL, ry), D2D1::Point2F(bounds.right - config.ScaleF(16.0f), ry), theme.brushBorderSubtle.Get(), 1.0f);
            }
            y += config.ScaleF(16.0f);
            continue;
        } else if (text.compare(0, 3, L"###") == 0) {
            text = text.substr(3);
            while (!text.empty() && text.front() == L' ') text.erase(text.begin());
            fontSize = config.ScaleF(15.0f);
            isHeading = true;
        } else if (text.compare(0, 2, L"##") == 0) {
            text = text.substr(2);
            while (!text.empty() && text.front() == L' ') text.erase(text.begin());
            fontSize = config.ScaleF(17.5f);
            isHeading = true;
        } else if (text.front() == L'#') {
            text = text.substr(1);
            while (!text.empty() && text.front() == L' ') text.erase(text.begin());
            fontSize = config.ScaleF(21.0f);
            isHeading = true;
        } else if (text.front() == L'>') {
            text = text.substr(1);
            while (!text.empty() && text.front() == L' ') text.erase(text.begin());
            isQuote = true;
            indentX = config.ScaleF(16.0f);
        } else if ((text.size() >= 2 && (text[0] == L'-' || text[0] == L'*') && text[1] == L' ')) {
            text = text.substr(2);
            isBullet = true;
            indentX = config.ScaleF(20.0f);
        } else if (numDigits > 0 && numDigits + 1 < text.size() && text[numDigits] == L'.' && text[numDigits + 1] == L' ') {
            numPrefix = text.substr(0, numDigits + 1);
            text = text.substr(numDigits + 2);
            indentX = config.ScaleF(26.0f);
        }

        std::wstring clean;
        std::vector<MdInlineSpan> spans;
        std::vector<MdImageRef> images;
        ParseMdInlineAndImages(text, clean, spans, images);

        bool hasVisibleText = false;
        for (wchar_t ch : clean) {
            if (!iswspace(ch)) { hasVisibleText = true; break; }
        }

        if (isBullet) {
            ID2D1Brush* dotB = theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushTextPrimary.Get();
            if (dotB) {
                pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(padL + config.ScaleF(8.0f), y + fontSize * 0.78f),
                                               config.ScaleF(2.5f), config.ScaleF(2.5f)), dotB);
            }
        } else if (!numPrefix.empty()) {
            float dummyH = 0.0f;
            auto pNumL = CreateMdStyledLayout(theme.dwriteFactory.Get(), proseFmt, numPrefix, {},
                                              config.ScaleF(22.0f), fontSize, false, false, false,
                                              config, nullptr, nullptr, dummyH);
            if (pNumL && theme.brushTextSecondary) {
                pRT->DrawTextLayout(D2D1::Point2F(padL, y), pNumL.Get(), theme.brushTextSecondary.Get());
            }
        }

        if (hasVisibleText) {
            ID2D1Brush* drawBrush = isQuote
                ? (theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushTextPrimary.Get())
                : theme.brushTextPrimary.Get();
            float measuredH = fontSize * 1.56f;
            auto pLayout = CreateMdStyledLayout(theme.dwriteFactory.Get(), proseFmt, clean, spans,
                                                (std::max)(40.0f, maxW - indentX), fontSize, isHeading, isQuote, false,
                                                config, codeFgBrush ? codeFgBrush.Get() : drawBrush, linkFg, measuredH);
            if (pLayout) {
                if (isQuote) {
                    ID2D1Brush* qB = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushBorderHover.Get();
                    if (qB) {
                        pRT->FillRoundedRectangle(D2D1::RoundedRect(
                            D2D1::RectF(padL + config.ScaleF(2.0f), y + config.ScaleF(2.0f),
                                        padL + config.ScaleF(5.5f), y + measuredH - config.ScaleF(4.0f)),
                            1.5f, 1.5f), qB);
                    }
                }
                drawLayoutAndCollectHits(pLayout.Get(), spans, padL + indentX, y, drawBrush);
            } else {
                D2D1_RECT_F rcR = D2D1::RectF(padL + indentX, y, padL + maxW, y + measuredH);
                pRT->DrawText(clean.c_str(), static_cast<UINT32>(clean.length()), proseFmt, rcR, drawBrush);
            }
            y += measuredH;
        }

        // Render Markdown images ![alt](path_or_url) cleanly without forcing white backing behind alpha transparency
        for (const auto& imgRef : images) {
            CachedMdImage img = EnsureMarkdownImage(doc, imgRef.target);
            if (img.status == MdImageStatus::Ready && !img.bytes.empty() && img.imageId != 0) {
                float dw = 0.0f, dh = 0.0f;
                ComputeMdImageDrawSize(img.pixelW, img.pixelH, (std::max)(40.0f, maxW - indentX), config, dw, dh);
                float bmpW = 0.0f, bmpH = 0.0f;
                ID2D1Bitmap* pBmp = GetOrCreateOutputBitmap(pRT, img.imageId, img.bytes, bmpW, bmpH);
                if (pBmp) {
                    D2D1_RECT_F rcImg = D2D1::RectF(padL + indentX, y + config.ScaleF(2.0f),
                                                    padL + indentX + dw, y + config.ScaleF(2.0f) + dh);
                    pRT->DrawBitmap(pBmp, rcImg, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    if (theme.brushBorderSubtle) {
                        pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcImg, config.ScaleF(6.0f), config.ScaleF(6.0f)), theme.brushBorderSubtle.Get(), 0.9f);
                    }
                    D2D1_RECT_F rcHit = D2D1::RectF(
                        (std::max)(bounds.left, rcImg.left), (std::max)(bounds.top, rcImg.top),
                        (std::min)(bounds.right, rcImg.right), (std::min)(bounds.bottom, rcImg.bottom));
                    if (rcHit.right > rcHit.left && rcHit.bottom > rcHit.top) {
                        m_visibleMdImages.push_back({ rcHit, cellIdx, imgRef.target, img.imageId, img.bytes, img.pixelW, img.pixelH });
                    }
                    y += dh + config.ScaleF(10.0f);
                    continue;
                }
            }

            // Loading / Placeholder / Error state card
            const float boxW = (std::min)(maxW - indentX, config.ScaleF(460.0f));
            const float boxH = config.ScaleF(34.0f);
            D2D1_RECT_F rcCard = D2D1::RectF(padL + indentX, y + config.ScaleF(2.0f), padL + indentX + boxW, y + config.ScaleF(2.0f) + boxH);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> boxBg;
            pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.04f)
                                            : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.04f), &boxBg);
            if (boxBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcCard, config.ScaleF(6.0f), config.ScaleF(6.0f)), boxBg.Get());
            if (theme.brushBorderSubtle) pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcCard, config.ScaleF(6.0f), config.ScaleF(6.0f)), theme.brushBorderSubtle.Get(), 1.0f);

            std::wstring statusTxt;
            if (img.status == MdImageStatus::Pending) {
                statusTxt = L"Loading image: " + (imgRef.alt.empty() ? imgRef.target : imgRef.alt);
            } else if (img.errorMsg == L"placeholder") {
                statusTxt = (imgRef.alt.empty() ? L"Image" : imgRef.alt) + std::wstring(L"  (paste URL or file path in parentheses)");
            } else {
                statusTxt = (imgRef.alt.empty() ? imgRef.target : imgRef.alt) + L"  [" + img.errorMsg + L"]";
            }
            D2D1_RECT_F rcTxt = D2D1::RectF(rcCard.left + config.ScaleF(10.0f), rcCard.top, rcCard.right - config.ScaleF(10.0f), rcCard.bottom);
            IDWriteTextFormat* statusFmt = m_fmtNbUiLeading ? m_fmtNbUiLeading.Get() : proseFmt;
            if (statusFmt && theme.brushTextSecondary) {
                pRT->DrawText(statusTxt.c_str(), static_cast<UINT32>(statusTxt.size()), statusFmt, rcTxt,
                              theme.brushTextSecondary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            if (IsHttpUrl(imgRef.target)) {
                m_visibleMdLinks.push_back({ rcCard, cellIdx, imgRef.target });
            }
            y += config.ScaleF(42.0f);
        }
    }

    pRT->PopAxisAlignedClip();
}

namespace nb_icons {

inline void DrawPlusIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    pRT->DrawLine(D2D1::Point2F(cx - r, cy), D2D1::Point2F(cx + r, cy), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx, cy - r), D2D1::Point2F(cx, cy + r), b, sw);
}

inline void DrawAddMdIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    // Three short text lines + mini plus in bottom-right
    pRT->DrawLine(D2D1::Point2F(cx - r, cy - r * 0.65f), D2D1::Point2F(cx + r * 0.75f, cy - r * 0.65f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - r, cy), D2D1::Point2F(cx + r * 0.25f, cy), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - r, cy + r * 0.65f), D2D1::Point2F(cx + r * 0.05f, cy + r * 0.65f), b, sw);
    float px = cx + r * 0.62f;
    float py = cy + r * 0.55f;
    float pr = r * 0.36f;
    pRT->DrawLine(D2D1::Point2F(px - pr, py), D2D1::Point2F(px + pr, py), b, sw);
    pRT->DrawLine(D2D1::Point2F(px, py - pr), D2D1::Point2F(px, py + pr), b, sw);
}

inline void DrawScissorsIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    float hr = r * 0.32f;
    pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx - r * 0.45f, cy + r * 0.48f), hr, hr), b, sw);
    pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * 0.45f, cy + r * 0.48f), hr, hr), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - r * 0.25f, cy + r * 0.25f), D2D1::Point2F(cx + r * 0.55f, cy - r * 0.78f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx + r * 0.25f, cy + r * 0.25f), D2D1::Point2F(cx - r * 0.55f, cy - r * 0.78f), b, sw);
}

inline void DrawCopyIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    D2D1_RECT_F front = D2D1::RectF(cx - r * 0.20f, cy - r * 0.20f, cx + r * 0.80f, cy + r * 0.85f);
    pRT->DrawRoundedRectangle(D2D1::RoundedRect(front, 1.5f, 1.5f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - r * 0.72f, cy + r * 0.35f), D2D1::Point2F(cx - r * 0.72f, cy - r * 0.75f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - r * 0.72f, cy - r * 0.75f), D2D1::Point2F(cx + r * 0.35f, cy - r * 0.75f), b, sw);
}

inline void DrawPasteIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    D2D1_RECT_F board = D2D1::RectF(cx - r * 0.65f, cy - r * 0.55f, cx + r * 0.65f, cy + r * 0.88f);
    pRT->DrawRoundedRectangle(D2D1::RoundedRect(board, 1.8f, 1.8f), b, sw);
    D2D1_RECT_F clip = D2D1::RectF(cx - r * 0.32f, cy - r * 0.82f, cx + r * 0.32f, cy - r * 0.38f);
    pRT->DrawRoundedRectangle(D2D1::RoundedRect(clip, 1.0f, 1.0f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - r * 0.32f, cy + r * 0.08f), D2D1::Point2F(cx + r * 0.32f, cy + r * 0.08f), b, sw * 0.85f);
    pRT->DrawLine(D2D1::Point2F(cx - r * 0.32f, cy + r * 0.42f), D2D1::Point2F(cx + r * 0.20f, cy + r * 0.42f), b, sw * 0.85f);
}

inline void DrawPlayOutlineIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    float xL = cx - r * 0.40f;
    float xR = cx + r * 0.58f;
    float yT = cy - r * 0.62f;
    float yB = cy + r * 0.62f;
    pRT->DrawLine(D2D1::Point2F(xL, yT), D2D1::Point2F(xR, cy), b, sw);
    pRT->DrawLine(D2D1::Point2F(xR, cy), D2D1::Point2F(xL, yB), b, sw);
    pRT->DrawLine(D2D1::Point2F(xL, yB), D2D1::Point2F(xL, yT), b, sw);
}

inline void DrawFastForwardOutlineIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    DrawPlayOutlineIcon(pRT, cx - r * 0.46f, cy, r * 0.80f, b, sw);
    DrawPlayOutlineIcon(pRT, cx + r * 0.36f, cy, r * 0.80f, b, sw);
}

inline void DrawChevronDownIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    pRT->DrawLine(D2D1::Point2F(cx - r * 0.65f, cy - r * 0.30f), D2D1::Point2F(cx, cy + r * 0.35f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx, cy + r * 0.35f), D2D1::Point2F(cx + r * 0.65f, cy - r * 0.30f), b, sw);
}

inline void DrawChevronRightIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    pRT->DrawLine(D2D1::Point2F(cx - r * 0.30f, cy - r * 0.60f), D2D1::Point2F(cx + r * 0.35f, cy), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx + r * 0.35f, cy), D2D1::Point2F(cx - r * 0.30f, cy + r * 0.60f), b, sw);
}

inline void DrawArrowUpIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    pRT->DrawLine(D2D1::Point2F(cx, cy + r * 0.72f), D2D1::Point2F(cx, cy - r * 0.68f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - r * 0.52f, cy - r * 0.16f), D2D1::Point2F(cx, cy - r * 0.68f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx + r * 0.52f, cy - r * 0.16f), D2D1::Point2F(cx, cy - r * 0.68f), b, sw);
}

inline void DrawArrowDownIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    pRT->DrawLine(D2D1::Point2F(cx, cy - r * 0.72f), D2D1::Point2F(cx, cy + r * 0.68f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - r * 0.52f, cy + r * 0.16f), D2D1::Point2F(cx, cy + r * 0.68f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx + r * 0.52f, cy + r * 0.16f), D2D1::Point2F(cx, cy + r * 0.68f), b, sw);
}

inline void DrawTrashIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    float w = r * 0.78f;
    float h = r * 0.85f;
    pRT->DrawLine(D2D1::Point2F(cx - w - 1.0f, cy - h * 0.52f), D2D1::Point2F(cx + w + 1.0f, cy - h * 0.52f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - w * 0.36f, cy - h * 0.88f), D2D1::Point2F(cx + w * 0.36f, cy - h * 0.88f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - w * 0.36f, cy - h * 0.88f), D2D1::Point2F(cx - w * 0.42f, cy - h * 0.52f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx + w * 0.36f, cy - h * 0.88f), D2D1::Point2F(cx + w * 0.42f, cy - h * 0.52f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - w * 0.80f, cy - h * 0.52f), D2D1::Point2F(cx - w * 0.62f, cy + h * 0.88f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx + w * 0.80f, cy - h * 0.52f), D2D1::Point2F(cx + w * 0.62f, cy + h * 0.88f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - w * 0.62f, cy + h * 0.88f), D2D1::Point2F(cx + w * 0.62f, cy + h * 0.88f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx - w * 0.24f, cy - h * 0.16f), D2D1::Point2F(cx - w * 0.20f, cy + h * 0.52f), b, sw * 0.85f);
    pRT->DrawLine(D2D1::Point2F(cx + w * 0.24f, cy - h * 0.16f), D2D1::Point2F(cx + w * 0.20f, cy + h * 0.52f), b, sw * 0.85f);
}

inline void DrawPowerInterruptIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    const int segs = 18;
    const float rad = r * 0.82f;
    const float startDeg = -55.0f;
    const float endDeg = 235.0f;
    for (int i = 0; i < segs; ++i) {
        float d1 = startDeg + (endDeg - startDeg) * (static_cast<float>(i) / segs);
        float d2 = startDeg + (endDeg - startDeg) * (static_cast<float>(i + 1) / segs);
        float a1 = d1 * 3.14159265f / 180.0f;
        float a2 = d2 * 3.14159265f / 180.0f;
        pRT->DrawLine(D2D1::Point2F(cx + rad * cosf(a1), cy + rad * sinf(a1)),
                      D2D1::Point2F(cx + rad * cosf(a2), cy + rad * sinf(a2)), b, sw);
    }
    pRT->DrawLine(D2D1::Point2F(cx, cy - rad * 1.10f), D2D1::Point2F(cx, cy + rad * 0.05f), b, sw);
}

inline void DrawRestartIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    const int segs = 18;
    const float rad = r * 0.78f;
    const float startDeg = -45.0f;
    const float endDeg = 255.0f;
    for (int i = 0; i < segs; ++i) {
        float d1 = startDeg + (endDeg - startDeg) * (static_cast<float>(i) / segs);
        float d2 = startDeg + (endDeg - startDeg) * (static_cast<float>(i + 1) / segs);
        float a1 = d1 * 3.14159265f / 180.0f;
        float a2 = d2 * 3.14159265f / 180.0f;
        pRT->DrawLine(D2D1::Point2F(cx + rad * cosf(a1), cy + rad * sinf(a1)),
                      D2D1::Point2F(cx + rad * cosf(a2), cy + rad * sinf(a2)), b, sw);
    }
    float hx = cx + rad * cosf(-45.0f * 3.14159265f / 180.0f);
    float hy = cy + rad * sinf(-45.0f * 3.14159265f / 180.0f);
    pRT->DrawLine(D2D1::Point2F(hx, hy), D2D1::Point2F(hx - r * 0.40f, hy - r * 0.08f), b, sw);
    pRT->DrawLine(D2D1::Point2F(hx, hy), D2D1::Point2F(hx - r * 0.02f, hy - r * 0.44f), b, sw);
}

inline void DrawGripDotsIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b) {
    float dx = r * 0.34f;
    float dy = r * 0.42f;
    float dotR = (std::max)(1.0f, r * 0.13f);
    for (int row = -1; row <= 1; ++row) {
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - dx, cy + row * dy), dotR, dotR), b);
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + dx, cy + row * dy), dotR, dotR), b);
    }
}

inline void DrawLinkIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    pRT->DrawLine(D2D1::Point2F(cx - r * 0.32f, cy + r * 0.32f), D2D1::Point2F(cx + r * 0.32f, cy - r * 0.32f), b, sw);
    pRT->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - r * 0.78f, cy - r * 0.15f, cx - r * 0.05f, cy + r * 0.58f), 2.5f, 2.5f), b, sw);
    pRT->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx + r * 0.05f, cy - r * 0.58f, cx + r * 0.78f, cy + r * 0.15f), 2.5f, 2.5f), b, sw);
}

inline void DrawImageIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    D2D1_RECT_F fr = D2D1::RectF(cx - r * 0.80f, cy - r * 0.64f, cx + r * 0.80f, cy + r * 0.64f);
    pRT->DrawRoundedRectangle(D2D1::RoundedRect(fr, 1.8f, 1.8f), b, sw);
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - r * 0.36f, cy - r * 0.22f), r * 0.14f, r * 0.14f), b);
    pRT->DrawLine(D2D1::Point2F(cx - r * 0.60f, cy + r * 0.50f), D2D1::Point2F(cx + r * 0.08f, cy - r * 0.06f), b, sw);
    pRT->DrawLine(D2D1::Point2F(cx + r * 0.08f, cy - r * 0.06f), D2D1::Point2F(cx + r * 0.68f, cy + r * 0.50f), b, sw);
}

inline void DrawBulletListIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    float dotR = (std::max)(1.0f, r * 0.14f);
    float ys[3] = { cy - r * 0.52f, cy, cy + r * 0.52f };
    for (float yy : ys) {
        pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - r * 0.65f, yy), dotR, dotR), b);
        pRT->DrawLine(D2D1::Point2F(cx - r * 0.26f, yy), D2D1::Point2F(cx + r * 0.75f, yy), b, sw);
    }
}

inline void DrawNumberListIcon(ID2D1RenderTarget* pRT, float cx, float cy, float r, ID2D1Brush* b, float sw) {
    float ys[3] = { cy - r * 0.52f, cy, cy + r * 0.52f };
    for (float yy : ys) {
        pRT->DrawLine(D2D1::Point2F(cx - r * 0.72f, yy - r * 0.08f), D2D1::Point2F(cx - r * 0.48f, yy + r * 0.08f), b, sw);
        pRT->DrawLine(D2D1::Point2F(cx - r * 0.22f, yy), D2D1::Point2F(cx + r * 0.75f, yy), b, sw);
    }
}

} // namespace nb_icons

void NotebookView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                          editor::OpenDocument& doc, float left, float width, float top, float height) {
    if (!pRT) return;
    EnsureNbBrushes(pRT);
    EnsureNbTextFormats(theme, config);
    m_lastTooltipRect = {};
    m_visibleColorSwatches.clear();
    m_visibleDiagHits.clear();
    m_visibleMdLinks.clear();
    m_visibleMdImages.clear();
    m_lastViewLeft = left;
    m_lastViewWidth = width;
    m_lastViewTop = top;
    m_lastViewHeight = height;
    m_lastTabSize = (config.tabSize > 0) ? config.tabSize : 4;

    if (!doc.notebookDoc) {
        doc.notebookDoc = std::make_shared<editor::NotebookDocument>();
        if (!doc.filePath.empty()) {
            doc.notebookDoc->LoadFromFile(doc.filePath);
        }
    }
    if (doc.notebookDoc->cells.empty()) {
        doc.notebookDoc->InitDefaultScratchpad();
    }
    m_scrollY = doc.notebookDoc->scrollY;

    auto nb = doc.notebookDoc;
    std::lock_guard<std::recursive_mutex> viewLock(m_outputsMutex);
    std::unique_lock<std::recursive_mutex> docLock(nb->outputsMutex ? *nb->outputsMutex : m_outputsMutex);
    m_viewHeight = height;

    // Blink timer update
    ULONGLONG now = GetTickCount64();
    if (now - m_lastBlinkTime >= 500) {
        m_caretVisible = !m_caretVisible;
        m_lastBlinkTime = now;
    }

    // Animation tick update for spinner (spin if executing OR if any cell is queued)
    bool isAnySpinning = m_isExecuting.load();
    if (!isAnySpinning && nb) {
        for (const auto& c : nb->cells) {
            if (c.isQueued || c.isExecuting) {
                isAnySpinning = true;
                break;
            }
        }
    }
    if (isAnySpinning) {
        float dt = static_cast<float>(now - m_lastAnimTick) / 1000.0f;
        m_spinAngle += dt * 360.0f;
        if (m_spinAngle >= 360.0f) m_spinAngle -= 360.0f;
    }
    m_lastAnimTick = now;

    // 1. Fill notebook canvas background
    D2D1_RECT_F rcCanvas = D2D1::RectF(left, top, left + width, top + height);
    if (theme.brushCanvasBg) {
        pRT->FillRectangle(rcCanvas, theme.brushCanvasBg.Get());
    } else {
        pRT->FillRectangle(rcCanvas, theme.brushWindowBg.Get());
    }

    // 2. Render Modern Flat Top Toolbar
    const float toolbarH = config.ScaleF(40.0f);
    D2D1_RECT_F rcToolbar = D2D1::RectF(left, top, left + width, top + toolbarH);
    const bool dark = theme.IsDarkMode();

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTbBg, brushCanvasNeutral, brushBorderHair, brushBtnHover, brushBtnActiveBg,
        brushFgMuted, brushFgStrong, brushCardFill, brushMdStripBg, brushPillBg, brushAccentBar;
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.088f, 0.092f, 0.108f, 1.0f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brushTbBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.068f, 0.072f, 0.086f, 1.0f) : D2D1::ColorF(0.992f, 0.993f, 0.996f, 1.0f), &brushCanvasNeutral);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.09f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.09f), &brushBorderHair);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.06f), &brushBtnHover);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.24f, 0.48f, 0.92f, 0.18f) : D2D1::ColorF(0.14f, 0.39f, 0.88f, 0.11f), &brushBtnActiveBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.66f, 0.69f, 0.75f, 1.0f) : D2D1::ColorF(0.32f, 0.34f, 0.39f, 1.0f), &brushFgMuted);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.93f, 0.94f, 0.97f, 1.0f) : D2D1::ColorF(0.10f, 0.11f, 0.14f, 1.0f), &brushFgStrong);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.108f, 0.112f, 0.132f, 1.0f) : D2D1::ColorF(0.953f, 0.957f, 0.967f, 1.0f), &brushCardFill);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.082f, 0.086f, 0.102f, 1.0f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brushMdStripBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.135f, 0.140f, 0.165f, 0.98f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f), &brushPillBg);
    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.38f, 0.65f, 0.98f, 1.0f) : D2D1::ColorF(0.10f, 0.11f, 0.14f, 1.0f), &brushAccentBar);

    if (brushCanvasNeutral) {
        pRT->FillRectangle(rcCanvas, brushCanvasNeutral.Get());
    }
    if (brushTbBg) {
        pRT->FillRectangle(rcToolbar, brushTbBg.Get());
    } else if (theme.brushWindowBg) {
        pRT->FillRectangle(rcToolbar, theme.brushWindowBg.Get());
    }
    if (brushBorderHair) {
        pRT->DrawLine(D2D1::Point2F(left, top + toolbarH - 0.5f),
                      D2D1::Point2F(left + width, top + toolbarH - 0.5f), brushBorderHair.Get(), 1.0f);
    }

    float curBtnX = left + config.ScaleF(12.0f);
    const float btnH = config.ScaleF(28.0f);
    const float btnY = top + (toolbarH - btnH) / 2.0f;
    IDWriteTextFormat* tbBtnFmt = m_fmtNbBtn ? m_fmtNbBtn.Get() : (theme.fontMenu ? theme.fontMenu.Get() : theme.fontMenuItem.Get());
    IDWriteTextFormat* tbLeadFmt = m_fmtNbUiLeading ? m_fmtNbUiLeading.Get() : (theme.fontMenuItem ? theme.fontMenuItem.Get() : tbBtnFmt);
    IDWriteTextFormat* actFmt = m_fmtNbActionGlyph ? m_fmtNbActionGlyph.Get() : tbBtnFmt;
    const float tbStroke = config.ScaleF(1.35f);

    auto drawFlatHoverBg = [&](const D2D1_RECT_F& rc, bool hot, bool active = false) {
        if (active && brushBtnActiveBg) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(rc, config.ScaleF(6.0f), config.ScaleF(6.0f)), brushBtnActiveBg.Get());
        } else if (hot && brushBtnHover) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(rc, config.ScaleF(6.0f), config.ScaleF(6.0f)), brushBtnHover.Get());
        }
    };
    auto drawTbSep = [&](float x) {
        if (brushBorderHair) {
            pRT->DrawLine(D2D1::Point2F(x, btnY + config.ScaleF(5.0f)),
                          D2D1::Point2F(x, btnY + btnH - config.ScaleF(5.0f)), brushBorderHair.Get(), 1.0f);
        }
    };

    // [+ Add Code] & [+ Add Markdown] compact flat icon buttons
    m_btnAddCode = D2D1::RectF(curBtnX, btnY, curBtnX + btnH, btnY + btnH);
    drawFlatHoverBg(m_btnAddCode, m_hoveredToolbarBtn == 3);
    if (ID2D1Brush* fg = (m_hoveredToolbarBtn == 3) ? brushFgStrong.Get() : brushFgMuted.Get()) {
        nb_icons::DrawPlusIcon(pRT, (m_btnAddCode.left + m_btnAddCode.right) * 0.5f, (m_btnAddCode.top + m_btnAddCode.bottom) * 0.5f,
                               config.ScaleF(5.2f), fg, tbStroke);
    }
    curBtnX += btnH + config.ScaleF(3.0f);

    m_btnAddText = D2D1::RectF(curBtnX, btnY, curBtnX + btnH, btnY + btnH);
    drawFlatHoverBg(m_btnAddText, m_hoveredToolbarBtn == 4);
    if (ID2D1Brush* fg = (m_hoveredToolbarBtn == 4) ? brushFgStrong.Get() : brushFgMuted.Get()) {
        nb_icons::DrawAddMdIcon(pRT, (m_btnAddText.left + m_btnAddText.right) * 0.5f, (m_btnAddText.top + m_btnAddText.bottom) * 0.5f,
                                config.ScaleF(6.2f), fg, tbStroke);
    }
    curBtnX += btnH + config.ScaleF(6.0f);
    drawTbSep(curBtnX);
    curBtnX += config.ScaleF(7.0f);

    // [Cut] [Copy] [Paste] Cell Clipboard Group
    m_btnCutCell = D2D1::RectF(curBtnX, btnY, curBtnX + btnH, btnY + btnH);
    drawFlatHoverBg(m_btnCutCell, m_hoveredToolbarBtn == 9);
    if (ID2D1Brush* fg = (m_hoveredToolbarBtn == 9) ? brushFgStrong.Get() : brushFgMuted.Get()) {
        nb_icons::DrawScissorsIcon(pRT, (m_btnCutCell.left + m_btnCutCell.right) * 0.5f, (m_btnCutCell.top + m_btnCutCell.bottom) * 0.5f,
                                   config.ScaleF(6.4f), fg, tbStroke);
    }
    curBtnX += btnH + config.ScaleF(3.0f);

    m_btnCopyCell = D2D1::RectF(curBtnX, btnY, curBtnX + btnH, btnY + btnH);
    drawFlatHoverBg(m_btnCopyCell, m_hoveredToolbarBtn == 10);
    if (ID2D1Brush* fg = (m_hoveredToolbarBtn == 10) ? brushFgStrong.Get() : brushFgMuted.Get()) {
        nb_icons::DrawCopyIcon(pRT, (m_btnCopyCell.left + m_btnCopyCell.right) * 0.5f, (m_btnCopyCell.top + m_btnCopyCell.bottom) * 0.5f,
                               config.ScaleF(6.2f), fg, tbStroke);
    }
    curBtnX += btnH + config.ScaleF(3.0f);

    m_btnPasteCell = D2D1::RectF(curBtnX, btnY, curBtnX + btnH, btnY + btnH);
    drawFlatHoverBg(m_btnPasteCell, m_hoveredToolbarBtn == 11);
    if (ID2D1Brush* fg = (m_hoveredToolbarBtn == 11) ? brushFgStrong.Get() : brushFgMuted.Get()) {
        nb_icons::DrawPasteIcon(pRT, (m_btnPasteCell.left + m_btnPasteCell.right) * 0.5f, (m_btnPasteCell.top + m_btnPasteCell.bottom) * 0.5f,
                                config.ScaleF(6.5f), fg, tbStroke);
    }
    curBtnX += btnH + config.ScaleF(6.0f);
    drawTbSep(curBtnX);
    curBtnX += config.ScaleF(7.0f);

    // [▷ Run Cell] & [▷▷ Run All]
    m_btnRunCell = D2D1::RectF(curBtnX, btnY, curBtnX + btnH, btnY + btnH);
    drawFlatHoverBg(m_btnRunCell, m_hoveredToolbarBtn == 12);
    if (ID2D1Brush* fg = (m_hoveredToolbarBtn == 12) ? brushFgStrong.Get() : brushFgMuted.Get()) {
        nb_icons::DrawPlayOutlineIcon(pRT, (m_btnRunCell.left + m_btnRunCell.right) * 0.5f, (m_btnRunCell.top + m_btnRunCell.bottom) * 0.5f,
                                      config.ScaleF(6.4f), fg, tbStroke);
    }
    curBtnX += btnH + config.ScaleF(4.0f);

    const float runAllW = config.ScaleF(78.0f);
    m_btnRunAll = D2D1::RectF(curBtnX, btnY, curBtnX + runAllW, btnY + btnH);
    const bool anyExec = m_isExecuting.load();
    if (anyExec) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> stopBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.30f, 0.32f, 0.16f), &stopBg);
        if (stopBg) pRT->FillRoundedRectangle(D2D1::RoundedRect(m_btnRunAll, config.ScaleF(6.0f), config.ScaleF(6.0f)), stopBg.Get());
        if (m_brushNbErrRed) {
            float sx = m_btnRunAll.left + config.ScaleF(13.0f);
            float sy = (m_btnRunAll.top + m_btnRunAll.bottom) * 0.5f;
            float sr = config.ScaleF(4.0f);
            pRT->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sx - sr, sy - sr, sx + sr, sy + sr), 1.5f, 1.5f), m_brushNbErrRed.Get());
            D2D1_RECT_F rcStopLbl = D2D1::RectF(sx + config.ScaleF(8.0f), btnY, m_btnRunAll.right - config.ScaleF(6.0f), btnY + btnH);
            if (tbLeadFmt) pRT->DrawText(L"Stop", 4, tbLeadFmt, rcStopLbl, m_brushNbErrRed.Get());
        }
    } else {
        drawFlatHoverBg(m_btnRunAll, m_hoveredToolbarBtn == 1);
        if (ID2D1Brush* fg = (m_hoveredToolbarBtn == 1) ? brushFgStrong.Get() : brushFgMuted.Get()) {
            float fx = m_btnRunAll.left + config.ScaleF(15.0f);
            float fy = (m_btnRunAll.top + m_btnRunAll.bottom) * 0.5f;
            nb_icons::DrawFastForwardOutlineIcon(pRT, fx, fy, config.ScaleF(6.4f), fg, tbStroke);
            D2D1_RECT_F rcAllLbl = D2D1::RectF(fx + config.ScaleF(10.0f), btnY, m_btnRunAll.right - config.ScaleF(4.0f), btnY + btnH);
            if (tbLeadFmt) pRT->DrawText(L"Run All", 7, tbLeadFmt, rcAllLbl, fg);
        }
    }
    curBtnX += runAllW + config.ScaleF(6.0f);
    drawTbSep(curBtnX);
    curBtnX += config.ScaleF(7.0f);

    // [Code ▾ / Markdown ▾] Active Cell Type Selector Dropdown
    bool activeIsMd = (nb && nb->activeCellIdx >= 0 && nb->activeCellIdx < static_cast<int>(nb->cells.size()) &&
                       nb->cells[nb->activeCellIdx].type == editor::NotebookCellType::Markdown);
    const wchar_t* typeLabel = activeIsMd ? L"Markdown" : L"Code";
    const float btnTypeW = activeIsMd ? config.ScaleF(98.0f) : config.ScaleF(74.0f);
    if (curBtnX + btnTypeW <= left + width - config.ScaleF(12.0f)) {
        m_btnCellTypePicker = D2D1::RectF(curBtnX, btnY, curBtnX + btnTypeW, btnY + btnH);
        drawFlatHoverBg(m_btnCellTypePicker, m_hoveredToolbarBtn == 13 || m_cellTypeDropdownOpen, m_cellTypeDropdownOpen);
        ID2D1Brush* fg = (m_hoveredToolbarBtn == 13 || m_cellTypeDropdownOpen) ? brushFgStrong.Get() : brushFgMuted.Get();
        if (fg && tbLeadFmt) {
            D2D1_RECT_F rcTLbl = D2D1::RectF(m_btnCellTypePicker.left + config.ScaleF(10.0f), btnY,
                                             m_btnCellTypePicker.right - config.ScaleF(20.0f), btnY + btnH);
            pRT->DrawText(typeLabel, static_cast<UINT32>(wcslen(typeLabel)), tbLeadFmt, rcTLbl, fg, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            nb_icons::DrawChevronDownIcon(pRT, m_btnCellTypePicker.right - config.ScaleF(11.0f),
                                          (btnY + btnY + btnH) * 0.5f + config.ScaleF(0.5f), config.ScaleF(4.0f), fg, tbStroke);
        }
        curBtnX += btnTypeW + config.ScaleF(8.0f);
    } else {
        m_btnCellTypePicker = {};
    }

    // Initialize / resync discovered Python kernels whenever notebook first renders or workspace folder changes
    if (nb && (nb->discoveredPythonExes.empty() || nb->discoveredWorkspaceVersion != m_workspaceVersion)) {
        RefreshPythonKernels(doc);
    }

    // Right-Aligned Kernel & Inspector Controls (laid out cleanly right-to-left without clipping left controls)
    float rightEdge = left + width - config.ScaleF(12.0f);
    const float minRightX = curBtnX + config.ScaleF(8.0f);

    // 1. [Clear All Outputs] icon button (far right)
    if (rightEdge - btnH >= minRightX) {
        m_btnClearAll = D2D1::RectF(rightEdge - btnH, btnY, rightEdge, btnY + btnH);
        drawFlatHoverBg(m_btnClearAll, m_hoveredToolbarBtn == 2);
        if (ID2D1Brush* fg = (m_hoveredToolbarBtn == 2) ? brushFgStrong.Get() : brushFgMuted.Get()) {
            nb_icons::DrawTrashIcon(pRT, (m_btnClearAll.left + m_btnClearAll.right) * 0.5f,
                                    (m_btnClearAll.top + m_btnClearAll.bottom) * 0.5f, config.ScaleF(6.2f), fg, tbStroke);
        }
        rightEdge -= btnH + config.ScaleF(4.0f);
    } else {
        m_btnClearAll = {};
    }

    // 2. [Restart Kernel ↻]
    if (rightEdge - btnH >= minRightX) {
        m_btnRestartKernel = D2D1::RectF(rightEdge - btnH, btnY, rightEdge, btnY + btnH);
        drawFlatHoverBg(m_btnRestartKernel, m_hoveredToolbarBtn == 6);
        if (ID2D1Brush* fg = (m_hoveredToolbarBtn == 6) ? brushFgStrong.Get() : brushFgMuted.Get()) {
            nb_icons::DrawRestartIcon(pRT, (m_btnRestartKernel.left + m_btnRestartKernel.right) * 0.5f,
                                      (m_btnRestartKernel.top + m_btnRestartKernel.bottom) * 0.5f, config.ScaleF(6.5f), fg, tbStroke);
        }
        rightEdge -= btnH + config.ScaleF(4.0f);
    } else {
        m_btnRestartKernel = {};
    }

    // 3. [Interrupt / Stop Kernel] power icon
    if (rightEdge - btnH >= minRightX) {
        m_btnInterruptKernel = D2D1::RectF(rightEdge - btnH, btnY, rightEdge, btnY + btnH);
        drawFlatHoverBg(m_btnInterruptKernel, m_hoveredToolbarBtn == 14, anyExec);
        ID2D1Brush* pFg = anyExec
            ? (m_brushNbErrRed ? static_cast<ID2D1Brush*>(m_brushNbErrRed.Get()) : brushFgStrong.Get())
            : ((m_hoveredToolbarBtn == 14) ? brushFgStrong.Get() : brushFgMuted.Get());
        if (pFg) {
            nb_icons::DrawPowerInterruptIcon(pRT, (m_btnInterruptKernel.left + m_btnInterruptKernel.right) * 0.5f,
                                             (m_btnInterruptKernel.top + m_btnInterruptKernel.bottom) * 0.5f, config.ScaleF(6.5f), pFg, tbStroke);
        }
        rightEdge -= btnH + config.ScaleF(6.0f);
    } else {
        m_btnInterruptKernel = {};
    }

    // 4. [⊞ Table]
    const float btnTableW = config.ScaleF(64.0f);
    if (rightEdge - btnTableW >= minRightX) {
        m_btnVarTable = D2D1::RectF(rightEdge - btnTableW, btnY, rightEdge, btnY + btnH);
        drawFlatHoverBg(m_btnVarTable, m_hoveredToolbarBtn == 8, m_varTableModal.isOpen);
        ID2D1Brush* tBrush = m_varTableModal.isOpen
            ? (m_brushNbBlueText ? static_cast<ID2D1Brush*>(m_brushNbBlueText.Get()) : brushFgStrong.Get())
            : ((m_hoveredToolbarBtn == 8) ? brushFgStrong.Get() : brushFgMuted.Get());
        if (tBrush && tbBtnFmt) {
            pRT->DrawText(L"\u229E Table", 7, tbBtnFmt, m_btnVarTable, tBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        rightEdge -= btnTableW + config.ScaleF(4.0f);
    } else {
        m_btnVarTable = {};
    }

    // 5. [{x} Vars]
    const float btnVarsW = config.ScaleF(66.0f);
    if (rightEdge - btnVarsW >= minRightX) {
        m_btnVariables = D2D1::RectF(rightEdge - btnVarsW, btnY, rightEdge, btnY + btnH);
        const bool varsOpen = (nb && nb->showVariableInspector);
        drawFlatHoverBg(m_btnVariables, m_hoveredToolbarBtn == 7, varsOpen);
        ID2D1Brush* vBrush = varsOpen
            ? (m_brushNbBlueText ? static_cast<ID2D1Brush*>(m_brushNbBlueText.Get()) : brushFgStrong.Get())
            : ((m_hoveredToolbarBtn == 7) ? brushFgStrong.Get() : brushFgMuted.Get());
        if (vBrush && tbBtnFmt) {
            pRT->DrawText(L"{x} Vars", 8, tbBtnFmt, m_btnVariables, vBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        rightEdge -= btnVarsW + config.ScaleF(6.0f);
    } else {
        m_btnVariables = {};
    }

    // 6. [● .venv ▾] Kernel Picker Status Pill
    const float availKernelW = rightEdge - minRightX;
    const float btnKernelW = (std::min)(config.ScaleF(122.0f), availKernelW);
    if (btnKernelW >= config.ScaleF(72.0f)) {
        m_btnKernelPicker = D2D1::RectF(rightEdge - btnKernelW, btnY, rightEdge, btnY + btnH);
        drawFlatHoverBg(m_btnKernelPicker, m_hoveredToolbarBtn == 5 || m_kernelDropdownOpen, m_kernelDropdownOpen);

        const bool hasPython = !(nb && nb->selectedPythonExe.empty() && nb->discoveredPythonExes.empty());
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dotBrush;
        if (!hasPython) {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.45f, 0.47f, 0.52f, 1.0f), &dotBrush);
        } else if (anyExec) {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.55f, 0.98f, 1.0f), &dotBrush);
        } else {
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.78f, 0.45f, 1.0f), &dotBrush);
        }
        if (dotBrush) {
            float dotX = m_btnKernelPicker.left + config.ScaleF(10.0f);
            float dotY = (btnY + btnY + btnH) * 0.5f;
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), config.ScaleF(3.5f), config.ScaleF(3.5f)), dotBrush.Get());
        }

        std::wstring kShort = !hasPython
            ? L"No Python"
            : FormatKernelShortLabel(nb ? nb->selectedPythonExe : L"", m_workspacePath);
        if (kShort.size() > 15) kShort = kShort.substr(0, 13) + L"..";
        ID2D1Brush* kBrush = (m_hoveredToolbarBtn == 5 || m_kernelDropdownOpen) ? brushFgStrong.Get() : brushFgMuted.Get();
        if (kBrush && tbLeadFmt) {
            D2D1_RECT_F rcKLbl = D2D1::RectF(m_btnKernelPicker.left + config.ScaleF(18.0f), btnY,
                                             m_btnKernelPicker.right - config.ScaleF(18.0f), btnY + btnH);
            pRT->DrawText(kShort.c_str(), static_cast<UINT32>(kShort.length()), tbLeadFmt, rcKLbl, kBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            nb_icons::DrawChevronDownIcon(pRT, m_btnKernelPicker.right - config.ScaleF(10.0f),
                                          (btnY + btnY + btnH) * 0.5f + config.ScaleF(0.5f), config.ScaleF(3.8f), kBrush, tbStroke);
        }
    } else {
        m_btnKernelPicker = {};
    }

    // 3. Render Notebook Cells Area with Scrolling
    const float contentTop = top + toolbarH;
    const float contentH = height - toolbarH;

    // Viewport scroll clamping
    if (m_totalHeight > 0.0f) {
        float maxScroll = (std::max)(0.0f, m_totalHeight - contentH + config.ScaleF(40.0f));
        if (m_scrollY > maxScroll) {
            m_scrollY = maxScroll;
            doc.notebookDoc->scrollY = m_scrollY;
        }
    }
    if (m_scrollY < 0.0f) {
        m_scrollY = 0.0f;
        doc.notebookDoc->scrollY = 0.0f;
    }

    pRT->PushAxisAlignedClip(D2D1::RectF(left, contentTop, left + width, top + height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const float padX = config.ScaleF(16.0f);
    const float cardLeft = left + padX;
    const float cardRight = left + width - config.ScaleF(20.0f) - (m_totalHeight > contentH ? config.ScaleF(12.0f) : 0.0f);
    const float lineH = config.ScaleF(20.0f);

    float curY = contentTop + config.ScaleF(22.0f) - m_scrollY;

    for (size_t ci = 0; ci < nb->cells.size(); ++ci) {
        auto& cell = nb->cells[ci];
        bool isActive = (static_cast<int>(ci) == nb->activeCellIdx);
        bool isHovered = (static_cast<int>(ci) == m_hoveredCellIdx);
        const bool isCodeCell = (cell.type == editor::NotebookCellType::Code);
        const bool isMdEdit = (!isCodeCell && cell.isEditingMarkdown && !cell.isInputCollapsed);
        const bool isMarkdownPreview = (!isCodeCell && !cell.isEditingMarkdown && !cell.isInputCollapsed);
        const float mdBarH = isMdEdit ? config.ScaleF(32.0f) : 0.0f;

        const float gutterW = config.ScaleF(54.0f);
        const float cardContentLeft = cardLeft + gutterW + config.ScaleF(4.0f);
        const float mdAvailW = (std::max)(config.ScaleF(160.0f), cardRight - cardContentLeft);

        float codeH = 0.0f;
        if (cell.isInputCollapsed) {
            codeH = config.ScaleF(32.0f);
        } else if (isMarkdownPreview) {
            codeH = MeasureRenderedMarkdownHeight(theme, doc, cell, config, mdAvailW);
        } else {
            size_t lineCount = (std::max)(size_t(1), cell.inputBuffer.GetLineCount());
            codeH = static_cast<float>(lineCount) * lineH + config.ScaleF(16.0f);
        }

        const float outBoxEstW = (std::max)(config.ScaleF(240.0f), (cardRight - cardContentLeft) - config.ScaleF(24.0f));
        float outputH = 0.0f;
        bool hasImages = false;
        if (!cell.outputs.empty() || cell.isWaitingForInput) {
            if (cell.isOutputCollapsed) {
                outputH = config.ScaleF(36.0f);
            } else {
                float rawOutH = config.ScaleF(14.0f);
                const float outLineH = config.ScaleF(18.0f);
                for (const auto& out : cell.outputs) {
                    if (!out.imagePngBytes.empty()) {
                        hasImages = true;
                        float dw = 0.0f, dh = 0.0f;
                        ComputeScaledImageSize(out, outBoxEstW, cell.imageZoomScale, config, dw, dh);
                        rawOutH += dh + config.ScaleF(10.0f);
                    }
                    if (!out.text.empty()) {
                        size_t linesInSeg = std::count(out.text.begin(), out.text.end(), L'\n') + 1;
                        rawOutH += static_cast<float>(linesInSeg) * outLineH;
                    }
                }
                if (cell.isWaitingForInput) {
                    rawOutH += outLineH * 2.0f;
                }
                float maxOutCap = hasImages ? config.ScaleF(720.0f) : config.ScaleF(300.0f);
                outputH = (std::min)(maxOutCap, (std::max)(config.ScaleF(38.0f), rawOutH));
            }
        }

        const bool hasTimingStatus = (isCodeCell && !cell.isInputCollapsed &&
                                      (cell.hasError || (cell.executionCount >= 1 && !cell.isExecuting && !cell.isQueued)));
        float minCodeBand = config.ScaleF(34.0f);
        if (isCodeCell && !cell.isInputCollapsed) {
            minCodeBand = hasTimingStatus ? config.ScaleF(66.0f) : config.ScaleF(50.0f);
        }
        const float effectiveCodeH = (std::max)(codeH, minCodeBand);
        const float inputBoxH = effectiveCodeH + mdBarH;
        const float cardH = inputBoxH + (outputH > 0.0f ? (outputH + config.ScaleF(8.0f)) : 0.0f);

        cell.bounds = D2D1::RectF(cardLeft, curY, cardRight, curY + cardH);
        const bool isCellVisible = (cell.bounds.bottom + config.ScaleF(36.0f) >= contentTop &&
                                    cell.bounds.top - config.ScaleF(20.0f) <= top + height);
        if (!isCellVisible) {
            cell.collapseInputBtnBounds = {};
            cell.runBtnBounds = {};
            cell.deleteBtnBounds = {};
            cell.duplicateBtnBounds = {};
            cell.moveUpBtnBounds = {};
            cell.moveDownBtnBounds = {};
            cell.dragHandleBounds = {};
            cell.editMarkdownBtnBounds = {};
            cell.outputCopyBtnBounds = {};
            cell.outputCollapseBtnBounds = {};
            cell.outputClearBtnBounds = {};
            cell.bottomDividerBounds = {};
            cell.bottomAddCodeBounds = {};
            cell.bottomAddTextBounds = {};
            cell.mdToolbarBounds = {};
            cell.mdBtnBoldBounds = {};
            cell.mdBtnItalicBounds = {};
            cell.mdBtnQuoteBounds = {};
            cell.mdBtnLinkBounds = {};
            cell.mdBtnImageBounds = {};
            cell.mdBtnBulletListBounds = {};
            cell.mdBtnNumberListBounds = {};
            cell.mdBtnHeadingBounds = {};
            cell.mdBtnDoneBounds = {};
        }

        // Only draw if visible in viewport
        if (isCellVisible) {
            // 3.1 Left Vertical Active Indicator Bar (Modern Minimalist Jupyter/Kaggle Spine)
            const bool isDraggingThis = (m_isDraggingCell && m_draggedCellIdx == static_cast<int>(ci));
            if (isActive || isDraggingThis) {
                D2D1_RECT_F rcBar = D2D1::RectF(cardLeft, cell.bounds.top + config.ScaleF(2.0f),
                                                cardLeft + config.ScaleF(4.0f), cell.bounds.bottom - config.ScaleF(2.0f));
                if (brushAccentBar) {
                    pRT->FillRoundedRectangle(D2D1::RoundedRect(rcBar, config.ScaleF(2.0f), config.ScaleF(2.0f)), brushAccentBar.Get());
                }
            } else if (isHovered && brushBorderHair) {
                D2D1_RECT_F rcBar = D2D1::RectF(cardLeft, cell.bounds.top + config.ScaleF(4.0f),
                                                cardLeft + config.ScaleF(3.0f), cell.bounds.bottom - config.ScaleF(4.0f));
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcBar, config.ScaleF(1.5f), config.ScaleF(1.5f)), brushBorderHair.Get());
            }

            // 3.2 Minimalist Left Gutter (▷ Play trigger, [N] execution count, status/elapsed)
            // Markdown cells MUST leave runBtnBounds empty (0,0,0,0)
            const float gutterX = cardLeft + config.ScaleF(6.0f);
            const float gutterInnerW = gutterW - config.ScaleF(8.0f);
            const float gutterY = cell.bounds.top + config.ScaleF(4.0f);

            if (isCodeCell) {
                const float runBtnSize = config.ScaleF(26.0f);
                const float runBtnX = gutterX + (gutterInnerW - runBtnSize) * 0.5f;
                cell.runBtnBounds = D2D1::RectF(runBtnX, gutterY, runBtnX + runBtnSize, gutterY + runBtnSize);
                const bool isRunHover = (isHovered && m_hoveredBtn == 1);
                DrawRunMorphButton(pRT, theme, config, cell.runBtnBounds, cell.isExecuting, cell.isQueued, cell.hasError, isRunHover);

                // Monotonic execution count badge [1], [2], ... cleanly below ▷
                std::wstring inStr;
                if (cell.isExecuting || cell.isQueued || cell.executionCount == -2) {
                    inStr = L"[*]";
                } else if (cell.executionCount >= 1) {
                    inStr = L"[" + std::to_wstring(cell.executionCount) + L"]";
                } else {
                    inStr = L"[ ]";
                }
                D2D1_RECT_F rcIn = D2D1::RectF(gutterX, gutterY + runBtnSize + config.ScaleF(1.0f),
                                               gutterX + gutterInnerW, gutterY + runBtnSize + config.ScaleF(16.0f));
                ID2D1Brush* inBrush = (cell.executionCount >= 1 || cell.isExecuting || cell.isQueued)
                    ? (brushFgStrong ? brushFgStrong.Get() : theme.brushTextPrimary.Get())
                    : (theme.brushLineNumber ? theme.brushLineNumber.Get() : brushFgMuted.Get());
                IDWriteTextFormat* monoFmt = m_fmtNbMonoSmall ? m_fmtNbMonoSmall.Get() : tbBtnFmt;
                if (monoFmt && inBrush) {
                    pRT->DrawText(inStr.c_str(), static_cast<UINT32>(inStr.length()), monoFmt, rcIn, inBrush);
                }

                // Compact status / elapsed timing neatly below [N]
                if (!cell.isInputCollapsed && !cell.isExecuting && !cell.isQueued && (cell.hasError || cell.executionCount >= 1)) {
                    auto fmtElapsed = [](ULONGLONG ms) -> std::wstring {
                        if (ms == 0) return L"0s";
                        if (ms < 100) return std::to_wstring(ms) + L"ms";
                        if (ms < 1000) return L"0." + std::to_wstring(ms / 100) + L"s";
                        return std::to_wstring(ms / 1000) + L"s";
                    };
                    std::wstring statStr = (cell.hasError ? L"! " : L"\x2713 ") + fmtElapsed(cell.lastElapsedMs);
                    D2D1_RECT_F rcStat = D2D1::RectF(gutterX - config.ScaleF(2.0f), rcIn.bottom,
                                                     gutterX + gutterInnerW + config.ScaleF(2.0f), rcIn.bottom + config.ScaleF(15.0f));
                    ID2D1Brush* statBrush = cell.hasError
                        ? static_cast<ID2D1Brush*>(m_brushNbErrRed.Get())
                        : static_cast<ID2D1Brush*>(m_brushNbOkGreen.Get());
                    if (monoFmt && statBrush) {
                        pRT->DrawText(statStr.c_str(), static_cast<UINT32>(statStr.length()), monoFmt, rcStat, statBrush);
                    }
                }
            } else {
                cell.runBtnBounds = D2D1::RectF(0, 0, 0, 0);
            }

            // 3.3 Code / Markdown Area
            const float charW = EnsureCharWidth(theme, config);
            cell.codeAreaBounds = D2D1::RectF(
                cardContentLeft,
                cell.bounds.top,
                cardRight,
                cell.bounds.top + effectiveCodeH
            );
            const D2D1_RECT_F rcInputOuter = D2D1::RectF(
                cardContentLeft,
                cell.bounds.top,
                cardRight,
                cell.bounds.top + inputBoxH
            );
            const float cardRadius = config.ScaleF(8.0f);

            if (cell.isInputCollapsed) {
                cell.codeHScrollTrackBounds = {};
                cell.codeHScrollThumbBounds = {};
                cell.codeMaxScrollX = 0.0f;
                cell.codeScrollX = 0.0f;
                cell.mdToolbarBounds = {};
                cell.mdBtnBoldBounds = {};
                cell.mdBtnItalicBounds = {};
                cell.mdBtnQuoteBounds = {};
                cell.mdBtnLinkBounds = {};
                cell.mdBtnImageBounds = {};
                cell.mdBtnBulletListBounds = {};
                cell.mdBtnNumberListBounds = {};
                cell.mdBtnHeadingBounds = {};
                cell.mdBtnDoneBounds = {};

                D2D1_ROUNDED_RECT rrcFold = D2D1::RoundedRect(cell.codeAreaBounds, cardRadius, cardRadius);
                if (brushCardFill) pRT->FillRoundedRectangle(rrcFold, brushCardFill.Get());
                if (brushBorderHair) pRT->DrawRoundedRectangle(rrcFold, brushBorderHair.Get(), 1.0f);

                std::wstring firstLn = cell.inputBuffer.GetLineCount() > 0 ? cell.inputBuffer.GetLine(0) : L"";
                size_t fs = 0;
                while (fs < firstLn.size() && iswspace(firstLn[fs])) ++fs;
                firstLn = firstLn.substr(fs);
                if (firstLn.size() > 56) firstLn = firstLn.substr(0, 53) + L"...";
                std::wstring foldedSummary = (isCodeCell ? L"Code (" : L"Markdown (") +
                    std::to_wstring((std::max<size_t>)(1, cell.inputBuffer.GetLineCount())) + L" lines)" +
                    (firstLn.empty() ? L"" : L"  \x2022  " + firstLn);
                D2D1_RECT_F rcFolded = D2D1::RectF(
                    cell.codeAreaBounds.left + config.ScaleF(12.0f), cell.codeAreaBounds.top,
                    cell.codeAreaBounds.right - config.ScaleF(12.0f), cell.codeAreaBounds.bottom);
                if (tbLeadFmt && theme.brushTextSecondary) {
                    pRT->DrawText(foldedSummary.c_str(), static_cast<UINT32>(foldedSummary.size()),
                                  tbLeadFmt, rcFolded, theme.brushTextSecondary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
            } else if (isMarkdownPreview) {
                cell.codeHScrollTrackBounds = {};
                cell.codeHScrollThumbBounds = {};
                cell.codeMaxScrollX = 0.0f;
                cell.codeScrollX = 0.0f;
                cell.mdToolbarBounds = {};
                cell.mdBtnBoldBounds = {};
                cell.mdBtnItalicBounds = {};
                cell.mdBtnQuoteBounds = {};
                cell.mdBtnLinkBounds = {};
                cell.mdBtnImageBounds = {};
                cell.mdBtnBulletListBounds = {};
                cell.mdBtnNumberListBounds = {};
                cell.mdBtnHeadingBounds = {};
                cell.mdBtnDoneBounds = {};

                D2D1_ROUNDED_RECT rrcMdPrev = D2D1::RoundedRect(cell.codeAreaBounds, cardRadius, cardRadius);
                if (isHovered && brushBtnHover) {
                    pRT->FillRoundedRectangle(rrcMdPrev, brushBtnHover.Get());
                } else if (isActive && brushBorderHair) {
                    pRT->DrawRoundedRectangle(rrcMdPrev, brushBorderHair.Get(), 0.8f);
                }
                RenderMarkdownPreview(pRT, theme, config, doc, cell, cell.codeAreaBounds, static_cast<int>(ci));
            } else {

            // Flat rounded input card background (covers both editor & Markdown formatting bar when in mdEdit)
            D2D1_ROUNDED_RECT rrcCodeBox = D2D1::RoundedRect(rcInputOuter, cardRadius, cardRadius);
            if (brushCardFill) {
                pRT->FillRoundedRectangle(rrcCodeBox, brushCardFill.Get());
            } else if (theme.brushCanvasBg) {
                pRT->FillRoundedRectangle(rrcCodeBox, theme.brushCanvasBg.Get());
            }

            // Refresh cached live SyntaxValidator diagnostics & Python import costs for this cell
            if (isCodeCell) {
                RefreshCellAnalysis(doc, ci);
            }
            const auto analysisIt = m_cellAnalysisCache.find(cell.id);
            const std::vector<lsp::Diagnostic>* pCellDiags =
                (analysisIt != m_cellAnalysisCache.end()) ? &analysisIt->second.diagnostics : nullptr;
            const std::unordered_map<size_t, std::wstring>* pCellImportCosts =
                (analysisIt != m_cellAnalysisCache.end()) ? &analysisIt->second.importCostMap : nullptr;

            const float lineNumW = ComputeCellLineNumberGutterWidth(cell, config, charW);
            const float codeTextStartX = cell.codeAreaBounds.left + lineNumW + config.ScaleF(10.0f);
            const float swatchSpan = config.ScaleF(14.0f);
            const float swatchSize = config.ScaleF(10.5f);
            const int safeTab = (config.tabSize > 0) ? config.tabSize : 4;
            const auto& lines = cell.inputBuffer.GetLines();
            auto curPos = cell.inputBuffer.GetCursor();
            const std::wstring codeExt = isCodeCell ? L".py" : L".md";

            // Pre-scan color swatches once per line in this cell
            std::vector<std::vector<LineColorSpan>> cellLineColors(lines.size());
            float maxCodeW = 0.0f;
            bool hasTwoSpaceIndent = false;
            for (size_t li = 0; li < lines.size(); ++li) {
                const auto& cl = lines[li];
                if (cl.length() <= 2048) {
                    cellLineColors[li] = EditorCanvas::ScanLineColors(cl, false);
                }
                if (!hasTwoSpaceIndent && cl.size() >= 3 && cl[0] == L' ' && cl[1] == L' ' && cl[2] != L' ') {
                    hasTwoSpaceIndent = true;
                }
                float lw = EditorCanvas::GetColumnVisualOffset(cl, cellLineColors[li], cl.length(), charW, swatchSpan, safeTab);
                if (pCellImportCosts) {
                    auto icIt = pCellImportCosts->find(li);
                    if (icIt != pCellImportCosts->end()) {
                        lw += config.ScaleF(16.0f) + static_cast<float>(icIt->second.length()) * charW * 0.85f;
                    }
                }
                if (lw > maxCodeW) maxCodeW = lw;
            }
            float availCodeW = (cell.codeAreaBounds.right - config.ScaleF(8.0f)) - codeTextStartX;
            if (availCodeW > 0.0f && maxCodeW > availCodeW) {
                cell.codeMaxScrollX = maxCodeW - availCodeW + config.ScaleF(24.0f);
            } else {
                cell.codeMaxScrollX = 0.0f;
                cell.codeScrollX = 0.0f;
            }

            // Gutter separator divider & current active line background highlight
            if (isCodeCell && m_brushNbGutterSep) {
                float divX = std::round(cell.codeAreaBounds.left + lineNumW) + 0.5f;
                pRT->DrawLine(D2D1::Point2F(divX, cell.codeAreaBounds.top + config.ScaleF(3.0f)),
                              D2D1::Point2F(divX, cell.codeAreaBounds.bottom - config.ScaleF(3.0f)),
                              m_brushNbGutterSep.Get(), 1.0f);
            }
            if (isActive && curPos.line < lines.size() && theme.brushCurrentLineBg) {
                float activeLineY = cell.codeAreaBounds.top + config.ScaleF(6.0f) + static_cast<float>(curPos.line) * lineH;
                D2D1_RECT_F rcCurLine = D2D1::RectF(
                    cell.codeAreaBounds.left + lineNumW + config.ScaleF(2.0f),
                    activeLineY,
                    cell.codeAreaBounds.right - config.ScaleF(2.0f),
                    activeLineY + lineH);
                pRT->FillRectangle(rcCurLine, theme.brushCurrentLineBg.Get());
            }

            // Pre-index diagnostics by line & draw line numbers first (Code cells only)
            std::unordered_map<size_t, std::vector<const lsp::Diagnostic*>> diagsByLine;
            if (pCellDiags) {
                for (const auto& d : *pCellDiags) {
                    if (!d.isUnnecessary && d.severity < 4) {
                        diagsByLine[d.line].push_back(&d);
                    }
                }
            }
            if (isCodeCell) {
                for (size_t li = 0; li < lines.size(); ++li) {
                    float lineY = cell.codeAreaBounds.top + config.ScaleF(6.0f) + static_cast<float>(li) * lineH;
                    D2D1_RECT_F rcNum = D2D1::RectF(cell.codeAreaBounds.left + config.ScaleF(4.0f), lineY,
                                                   cell.codeAreaBounds.left + lineNumW - config.ScaleF(6.0f), lineY + lineH);
                    std::wstring numStr = std::to_wstring(li + 1);
                    const bool isActLine = (isActive && li == curPos.line);
                    IDWriteTextFormat* numFmt = (isActLine && theme.fontLineNumberActive)
                        ? theme.fontLineNumberActive.Get()
                        : (theme.fontLineNumber ? theme.fontLineNumber.Get() : theme.fontCode.Get());
                    ID2D1Brush* numBrush = isActLine
                        ? theme.brushTextPrimary.Get()
                        : theme.brushLineNumber.Get();
                    pRT->DrawText(numStr.c_str(), static_cast<UINT32>(numStr.length()), numFmt, rcNum, numBrush);
                }
            }

            // Rainbow Bracket Analysis & Active Bracket Pair lookup for this cell
            editor::BracketAnalysisResult bracketAnalysis;
            std::unordered_map<size_t, std::vector<const editor::BracketItem*>> bracketsByLine;
            if (isCodeCell && lines.size() <= 4000) {
                const size_t qLine = isActive ? curPos.line : SIZE_MAX;
                const size_t qCol = isActive ? curPos.col : SIZE_MAX;
                bracketAnalysis = editor::BracketPairAnalyzer::Analyze(cell.inputBuffer, qLine, qCol, L".py");
                if (config.enableBracketPairColorization) {
                    for (const auto& b : bracketAnalysis.brackets) {
                        if (b.line < lines.size()) bracketsByLine[b.line].push_back(&b);
                    }
                }
            }

            // Caret-word occurrence highlight (matches selected word OR identifier under caret)
            std::wstring occWord = cell.selectedWord;
            bool occWholeWord = false;
            if (occWord.empty() && !cell.HasSelection() && isActive && curPos.line < lines.size()) {
                const auto& curLine = lines[curPos.line];
                size_t probe = curPos.col;
                if (probe >= curLine.length() && probe > 0 && editor::OpenDocument::IsWordChar(curLine[probe - 1])) {
                    --probe;
                } else if (probe > 0 && probe < curLine.length() && !editor::OpenDocument::IsWordChar(curLine[probe]) && editor::OpenDocument::IsWordChar(curLine[probe - 1])) {
                    --probe;
                }
                if (probe < curLine.length() && editor::OpenDocument::IsWordChar(curLine[probe])) {
                    size_t ws = probe, we = probe + 1;
                    while (ws > 0 && editor::OpenDocument::IsWordChar(curLine[ws - 1])) --ws;
                    while (we < curLine.length() && editor::OpenDocument::IsWordChar(curLine[we])) ++we;
                    if (we - ws >= 2) {
                        occWord = curLine.substr(ws, we - ws);
                        occWholeWord = true;
                    }
                }
            }

            const auto docComments = isCodeCell
                ? EditorCanvas::ComputeDocCommentSummary(cell.inputBuffer, codeExt)
                : EditorCanvas::DocCommentSummary{};

            // Push axis-aligned clip for code text area to guarantee NO OVERFLOW OR LEAKING
            D2D1_RECT_F rcCodeClip = D2D1::RectF(
                cell.codeAreaBounds.left + lineNumW + config.ScaleF(2.0f),
                cell.codeAreaBounds.top + config.ScaleF(2.0f),
                cell.codeAreaBounds.right - config.ScaleF(4.0f),
                cell.codeAreaBounds.bottom - config.ScaleF(2.0f)
            );
            pRT->PushAxisAlignedClip(rcCodeClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            const size_t indentStepCols = hasTwoSpaceIndent ? 2 : static_cast<size_t>(safeTab);
            const size_t safeTabSizeT = static_cast<size_t>(safeTab);
            auto countLeadCols = [&](const std::wstring& s) -> size_t {
                size_t cols = 0;
                for (wchar_t ch : s) {
                    if (ch == L' ') ++cols;
                    else if (ch == L'\t') cols += safeTabSizeT - (cols % safeTabSizeT);
                    else break;
                }
                return cols;
            };

            // Active matching bracket skeletal frames (EditorCanvas style)
            if (isActive && bracketAnalysis.activePairIdx >= 0 &&
                bracketAnalysis.activePairIdx < static_cast<int>(bracketAnalysis.pairs.size())) {
                const auto& pair = bracketAnalysis.pairs[static_cast<size_t>(bracketAnalysis.activePairIdx)];
                if (pair.openLine < lines.size() && pair.closeLine < lines.size()) {
                    const float openLineY = cell.codeAreaBounds.top + config.ScaleF(6.0f) + static_cast<float>(pair.openLine) * lineH;
                    const float closeLineY = cell.codeAreaBounds.top + config.ScaleF(6.0f) + static_cast<float>(pair.closeLine) * lineH;
                    const float ox = codeTextStartX - cell.codeScrollX +
                        EditorCanvas::GetColumnVisualOffset(lines[pair.openLine], cellLineColors[pair.openLine], pair.openCol, charW, swatchSpan, safeTab);
                    const float cx = codeTextStartX - cell.codeScrollX +
                        EditorCanvas::GetColumnVisualOffset(lines[pair.closeLine], cellLineColors[pair.closeLine], pair.closeCol, charW, swatchSpan, safeTab);
                    EditorCanvas::DrawActiveBracketPairFrames(pRT, theme, config, pair, ox, openLineY, cx, closeLineY, charW, lineH, true, true);
                }
            }

            const float codeRightX = cell.codeAreaBounds.right - config.ScaleF(8.0f);

            for (size_t li = 0; li < lines.size(); ++li) {
                const float lineY = cell.codeAreaBounds.top + config.ScaleF(6.0f) + static_cast<float>(li) * lineH;
                const float textX = codeTextStartX - cell.codeScrollX;
                const auto& codeLine = lines[li];
                const auto& lineColors = cellLineColors[li];

                auto getColVisualX = [&](size_t col) -> float {
                    return textX + EditorCanvas::GetColumnVisualOffset(codeLine, lineColors, col, charW, swatchSpan, safeTab);
                };

                // 0. Indentation / Scope Guide Lines
                if (m_brushNbScopeGuide && charW > 0.0f && isCodeCell) {
                    const bool isBlankLine = (codeLine.find_first_not_of(L" \t\r\n") == std::wstring::npos);
                    size_t guideCols = 0;
                    if (!isBlankLine) {
                        guideCols = countLeadCols(codeLine);
                    } else {
                        size_t prevLead = 0, nextLead = 0;
                        for (size_t b = 1; b <= 8 && li >= b; ++b) {
                            if (lines[li - b].find_first_not_of(L" \t\r\n") != std::wstring::npos) {
                                prevLead = countLeadCols(lines[li - b]);
                                break;
                            }
                        }
                        for (size_t f = 1; f <= 8 && li + f < lines.size(); ++f) {
                            if (lines[li + f].find_first_not_of(L" \t\r\n") != std::wstring::npos) {
                                nextLead = countLeadCols(lines[li + f]);
                                break;
                            }
                        }
                        guideCols = (std::min)(prevLead, nextLead);
                    }
                    for (size_t step = indentStepCols; step < guideCols; step += indentStepCols) {
                        float gx = textX + static_cast<float>(step) * charW;
                        pRT->DrawLine(D2D1::Point2F(gx, lineY), D2D1::Point2F(gx, lineY + lineH), m_brushNbScopeGuide.Get(), 1.0f);
                    }
                }

                // 1. Render active text selection background on code line
                if (cell.HasSelection()) {
                    size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                    cell.GetSelectionRange(sLine, sCol, eLine, eCol);
                    if (li >= sLine && li <= eLine) {
                        float selX1 = textX;
                        float selX2 = textX;
                        if (li == sLine && li == eLine) {
                            selX1 = getColVisualX(sCol);
                            selX2 = getColVisualX(eCol);
                        } else if (li == sLine) {
                            selX1 = getColVisualX(sCol);
                            selX2 = getColVisualX(codeLine.length()) + config.ScaleF(8.0f);
                        } else if (li == eLine) {
                            selX1 = textX;
                            selX2 = getColVisualX(eCol);
                        } else {
                            selX1 = textX;
                            selX2 = (std::max)(getColVisualX(codeLine.length()) + config.ScaleF(8.0f),
                                               textX + config.ScaleF(24.0f));
                        }

                        if (selX2 > selX1 && m_brushNbSelBg) {
                            D2D1_RECT_F rcSel = D2D1::RectF(selX1, lineY + 0.5f, selX2, lineY + lineH - 0.5f);
                            pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSel, 3.0f, 3.0f), m_brushNbSelBg.Get());
                        }
                    }
                }

                // Matching word occurrences highlight across lines (selection or word under caret)
                if (!occWord.empty() && occWord.length() > 1 && !codeLine.empty() && m_brushNbWordOccBg) {
                    size_t wpos = 0;
                    while ((wpos = codeLine.find(occWord, wpos)) != std::wstring::npos) {
                        bool wordOk = true;
                        if (occWholeWord) {
                            bool lOk = (wpos == 0) || !editor::OpenDocument::IsWordChar(codeLine[wpos - 1]);
                            bool rOk = (wpos + occWord.length() >= codeLine.length()) || !editor::OpenDocument::IsWordChar(codeLine[wpos + occWord.length()]);
                            wordOk = lOk && rOk;
                        }
                        bool isCurrentSelection = false;
                        if (cell.HasSelection()) {
                            size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                            cell.GetSelectionRange(sLine, sCol, eLine, eCol);
                            if (li == sLine && li == eLine && wpos == sCol) {
                                isCurrentSelection = true;
                            }
                        }
                        if (wordOk && !isCurrentSelection) {
                            float wx = getColVisualX(wpos);
                            float ww = (std::max)(charW, getColVisualX(wpos + occWord.length()) - wx);
                            D2D1_RECT_F rcWord = D2D1::RectF(wx - 1.0f, lineY + 1.0f, wx + ww + 1.0f, lineY + lineH - 1.0f);
                            pRT->FillRoundedRectangle(D2D1::RoundedRect(rcWord, 2.0f, 2.0f), m_brushNbWordOccBg.Get());
                        }
                        wpos += occWord.length();
                    }
                }

                // 2. Full Syntax Token + Better Comment + Rainbow Bracket Rendering + Color Swatch Segment Splitting
                const auto& tokens = cell.inputBuffer.GetTokensForLineRef(li, codeExt);
                EditorCanvas::BetterCommentType inhComment = EditorCanvas::BetterCommentType::Normal;
                if (auto inhIt = docComments.inheritedLineTypes.find(li); inhIt != docComments.inheritedLineTypes.end()) {
                    inhComment = inhIt->second;
                }

                if (!codeLine.empty() && theme.dwriteFactory && theme.fontCode) {
                    std::vector<size_t> splitCols;
                    splitCols.reserve(lineColors.size());
                    for (const auto& cs : lineColors) {
                        if (cs.col > 0 && cs.col < codeLine.length()) splitCols.push_back(cs.col);
                    }
                    std::sort(splitCols.begin(), splitCols.end());
                    splitCols.erase(std::unique(splitCols.begin(), splitCols.end()), splitCols.end());

                    std::vector<std::pair<size_t, size_t>> segments;
                    size_t curSegStart = 0;
                    for (size_t sc : splitCols) {
                        if (sc > curSegStart) segments.push_back({ curSegStart, sc });
                        curSegStart = sc;
                    }
                    if (curSegStart < codeLine.length()) segments.push_back({ curSegStart, codeLine.length() });

                    for (const auto& seg : segments) {
                        const size_t segStart = seg.first;
                        const size_t segEnd = seg.second;
                        if (segEnd <= segStart) continue;
                        std::wstring segText = codeLine.substr(segStart, segEnd - segStart);
                        float segVisualX = getColVisualX(segStart);
                        float segMaxW = (std::max)(120.0f, static_cast<float>(segText.length()) * charW + config.ScaleF(48.0f));
                        Microsoft::WRL::ComPtr<IDWriteTextLayout> pSegLayout;
                        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                                segText.c_str(), static_cast<UINT32>(segText.length()),
                                theme.fontCode.Get(), segMaxW, lineH, &pSegLayout)) && pSegLayout) {
                            pSegLayout->SetIncrementalTabStop(charW * static_cast<float>(safeTab));
                            for (const auto& tok : tokens) {
                                const size_t tokEnd = tok.startCol + tok.length;
                                if (tok.startCol >= segEnd || tokEnd <= segStart || tokEnd > codeLine.length()) continue;
                                const size_t oStart = (std::max)(tok.startCol, segStart);
                                const size_t oEnd = (std::min)(tokEnd, segEnd);
                                if (oEnd <= oStart) continue;
                                ID2D1Brush* tokBrush = EditorCanvas::ResolveSyntaxTokenBrush(
                                    theme, tok, li, codeLine, bracketsByLine, inhComment, config.enableBracketPairColorization);
                                if (tokBrush) {
                                    DWRITE_TEXT_RANGE rng = { static_cast<UINT32>(oStart - segStart), static_cast<UINT32>(oEnd - oStart) };
                                    pSegLayout->SetDrawingEffect(tokBrush, rng);
                                }
                            }
                            pRT->DrawTextLayout(D2D1::Point2F(segVisualX, lineY), pSegLayout.Get(), theme.brushTextPrimary.Get());
                        }
                    }

                    // Better Comments DONE strikethrough
                    for (const auto& tok : tokens) {
                        if (tok.type == editor::TokenType::Comment) {
                            auto cInfo = EditorCanvas::ClassifyBetterComment(codeLine, tok.startCol, tok.length, inhComment);
                            if (cInfo.type == EditorCanvas::BetterCommentType::Done && theme.brushCommentDone) {
                                float midY = lineY + lineH * 0.5f;
                                pRT->DrawLine(D2D1::Point2F(getColVisualX(tok.startCol), midY),
                                              D2D1::Point2F(getColVisualX(tok.startCol + tok.length), midY),
                                              theme.brushCommentDone.Get(), config.ScaleF(1.2f));
                            }
                        }
                    }
                } else if (!codeLine.empty()) {
                    float layoutMaxW = (std::max)(120.0f, static_cast<float>(codeLine.length()) * charW + config.ScaleF(40.0f));
                    D2D1_RECT_F rcLineText = D2D1::RectF(textX, lineY, textX + layoutMaxW, lineY + lineH);
                    pRT->DrawText(codeLine.c_str(), static_cast<UINT32>(codeLine.length()),
                                  theme.fontCode.Get(), rcLineText, theme.brushTextPrimary.Get());
                }

                // 3. Inline Color Preview Swatches (e.g. "blue", "#1500F1")
                for (const auto& cs : lineColors) {
                    float swatchBaseX = getColVisualX(cs.col) - swatchSpan;
                    D2D1_RECT_F rcSwatch{};
                    EditorCanvas::DrawInlineColorSwatch(pRT, cs, swatchBaseX, lineY, lineH, swatchSpan, swatchSize, config, rcSwatch);
                    m_visibleColorSwatches.push_back({ rcSwatch, static_cast<int>(ci), li, cs.col, cs.len, cs.colorStr });
                }

                // 4. Live SyntaxValidator Diagnostics + Execution Error Squiggly Underlines (~~~~)
                bool drewExecErrOnLine = false;
                if (auto dIt = diagsByLine.find(li); dIt != diagsByLine.end()) {
                    for (const auto* pDiag : dIt->second) {
                        const auto& diag = *pDiag;
                        float errX1 = getColVisualX(diag.col);
                        size_t errLen = (diag.length > 0) ? diag.length : (std::max)(size_t(1), codeLine.length() > diag.col ? codeLine.length() - diag.col : size_t(1));
                        float errX2 = (std::max)(getColVisualX(diag.col + errLen), errX1 + config.ScaleF(14.0f));
                        if (errX1 < codeRightX) {
                            errX2 = (std::min)(errX2, codeRightX);
                            ID2D1Brush* pErrBrush = (diag.severity == 1)
                                ? (theme.brushIconRed ? theme.brushIconRed.Get() : (m_brushNbErrRed ? m_brushNbErrRed.Get() : theme.brushTextPrimary.Get()))
                                : (theme.brushIconOrange ? theme.brushIconOrange.Get() : theme.brushSyntaxNumber.Get());
                            EditorCanvas::DrawWavySquiggle(pRT, errX1, errX2, lineY, lineH, pErrBrush, config, 1.4f);
                            m_visibleDiagHits.push_back({ D2D1::RectF(errX1 - 2.0f, lineY, errX2 + 2.0f, lineY + lineH),
                                                            static_cast<int>(ci), li, diag.severity, diag.message });
                            if (diag.severity == 1) drewExecErrOnLine = true;
                        }
                    }
                }
                if (cell.hasError && cell.errorLine >= 0 && li == static_cast<size_t>(cell.errorLine) && !drewExecErrOnLine) {
                    size_t firstNonSpace = 0;
                    while (firstNonSpace < codeLine.length() && iswspace(codeLine[firstNonSpace])) {
                        firstNonSpace++;
                    }
                    float errX1 = getColVisualX(firstNonSpace);
                    float errX2 = getColVisualX(codeLine.length());
                    if (errX2 <= errX1) errX2 = errX1 + config.ScaleF(24.0f);
                    ID2D1Brush* brushToUse = m_brushNbErrRed ? m_brushNbErrRed.Get() : theme.brushIconRed.Get();
                    EditorCanvas::DrawWavySquiggle(pRT, errX1, errX2, lineY, lineH, brushToUse, config, 1.6f);
                    std::wstring execErrMsg = L"Runtime error on line " + std::to_wstring(li + 1);
                    for (const auto& out : cell.outputs) {
                        if (out.isError && !out.text.empty()) {
                            std::wstring cleanOut = editor::NotebookDocument::StripAnsi(out.text);
                            size_t lastNl = cleanOut.find_last_not_of(L"\r\n ");
                            if (lastNl != std::wstring::npos) {
                                size_t lineBeg = cleanOut.rfind(L'\n', lastNl);
                                execErrMsg = (lineBeg == std::wstring::npos)
                                    ? cleanOut.substr(0, lastNl + 1)
                                    : cleanOut.substr(lineBeg + 1, lastNl - lineBeg);
                            }
                            break;
                        }
                    }
                    m_visibleDiagHits.push_back({ D2D1::RectF(errX1 - 2.0f, lineY, errX2 + 2.0f, lineY + lineH),
                                                    static_cast<int>(ci), li, 1, execErrMsg });
                }

                // 5. Python Import Cost Inline Annotation (exact EditorCanvas styling)
                if (pCellImportCosts) {
                    auto itCost = pCellImportCosts->find(li);
                    if (itCost != pCellImportCosts->end()) {
                        const std::wstring& costText = itCost->second;
                        D2D1_COLOR_F costColor = D2D1::ColorF(0.45f, 0.79f, 0.57f, 0.88f);
                        if (costText.find(L"MB") != std::wstring::npos) {
                            costColor = D2D1::ColorF(0.88f, 0.42f, 0.46f, 0.9f);
                        } else {
                            size_t kbPos = costText.find(L" KB");
                            if (kbPos != std::wstring::npos) {
                                size_t numStart = costText.rfind(L' ', kbPos - 1);
                                if (numStart != std::wstring::npos) {
                                    float kbVal = static_cast<float>(_wtof(costText.substr(numStart + 1, kbPos - numStart - 1).c_str()));
                                    if (kbVal >= 100.0f) costColor = D2D1::ColorF(0.88f, 0.42f, 0.46f, 0.9f);
                                    else if (kbVal >= 50.0f) costColor = D2D1::ColorF(0.90f, 0.75f, 0.48f, 0.9f);
                                }
                            }
                        }
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushCost;
                        pRT->CreateSolidColorBrush(costColor, &brushCost);
                        IDWriteTextFormat* costFmt = theme.fontHint ? theme.fontHint.Get() : theme.fontCode.Get();
                        if (costFmt && brushCost) {
                            float costTextX = getColVisualX(codeLine.length()) + config.ScaleF(16.0f);
                            float availCostW = codeRightX - costTextX;
                            if (availCostW >= config.ScaleF(30.0f)) {
                                if (theme.dwriteFactory) {
                                    Microsoft::WRL::ComPtr<IDWriteTextLayout> pCostLayout;
                                    if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(
                                            costText.c_str(), static_cast<UINT32>(costText.length()),
                                            costFmt, availCostW, lineH, &pCostLayout)) && pCostLayout) {
                                        pCostLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                                        pCostLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                                        pCostLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                                        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                                        Microsoft::WRL::ComPtr<IDWriteInlineObject> inlineEllipsis;
                                        if (SUCCEEDED(theme.dwriteFactory->CreateEllipsisTrimmingSign(costFmt, &inlineEllipsis))) {
                                            pCostLayout->SetTrimming(&trimming, inlineEllipsis.Get());
                                        }
                                        pRT->DrawTextLayout(D2D1::Point2F(costTextX, lineY), pCostLayout.Get(), brushCost.Get());
                                    }
                                } else {
                                    D2D1_RECT_F rcCost = D2D1::RectF(costTextX, lineY, costTextX + availCostW, lineY + lineH);
                                    pRT->DrawText(costText.c_str(), static_cast<UINT32>(costText.length()), costFmt, rcCost, brushCost.Get());
                                }
                            }
                        }
                    }
                }

                // 6. Track Caret Anchor & Render Blinking Caret in active cell
                if (isActive && li == curPos.line) {
                    const float caretX = getColVisualX(curPos.col);
                    m_lastCaretX = caretX;
                    m_lastCaretY = lineY;
                    m_lastLineH = lineH;
                    if (m_caretVisible) {
                        pRT->DrawLine(
                            D2D1::Point2F(caretX, lineY + 1.0f),
                            D2D1::Point2F(caretX, lineY + lineH - 2.0f),
                            theme.brushTextPrimary.Get(),
                            1.5f
                        );
                    }
                }
            }

            pRT->PopAxisAlignedClip();

            // Code box horizontal scrollbar
            if (cell.codeMaxScrollX > 0.0f && availCodeW > config.ScaleF(40.0f)) {
                float sbH = config.ScaleF(6.0f);
                float sbY = cell.codeAreaBounds.bottom - sbH - config.ScaleF(2.0f);
                float trackX = codeTextStartX;
                float trackW = availCodeW;
                float thumbW = (std::max)(config.ScaleF(28.0f), trackW * (availCodeW / (maxCodeW + availCodeW)));
                float clampedScrollX = (std::max)(0.0f, (std::min)(cell.codeScrollX, cell.codeMaxScrollX));
                float thumbX = trackX + (clampedScrollX / cell.codeMaxScrollX) * (trackW - thumbW);

                cell.codeHScrollTrackBounds = D2D1::RectF(trackX, sbY - config.ScaleF(1.0f), trackX + trackW, sbY + sbH + config.ScaleF(1.0f));
                cell.codeHScrollThumbBounds = D2D1::RectF(thumbX, sbY, thumbX + thumbW, sbY + sbH);

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTrackCode;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.18f), &brushTrackCode);
                if (brushTrackCode) {
                    pRT->FillRoundedRectangle(D2D1::RoundedRect(cell.codeHScrollTrackBounds, sbH / 2.0f, sbH / 2.0f), brushTrackCode.Get());
                }
                bool isDraggingCode = (m_isDraggingCodeHScroll && m_dragCodeCellIdx == static_cast<int>(ci));
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSbCode;
                pRT->CreateSolidColorBrush(isDraggingCode ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.65f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f), &brushSbCode);
                if (brushSbCode) {
                    pRT->FillRoundedRectangle(D2D1::RoundedRect(cell.codeHScrollThumbBounds, sbH / 2.0f, sbH / 2.0f), brushSbCode.Get());
                }
            } else {
                cell.codeHScrollTrackBounds = D2D1::RectF(0, 0, 0, 0);
                cell.codeHScrollThumbBounds = D2D1::RectF(0, 0, 0, 0);
            }

            // Markdown Edit Mode Bottom Formatting Toolbar (attached seamlessly inside unified card)
            if (isMdEdit) {
                cell.mdToolbarBounds = D2D1::RectF(
                    cell.codeAreaBounds.left, cell.codeAreaBounds.bottom,
                    cell.codeAreaBounds.right, cell.codeAreaBounds.bottom + mdBarH
                );
                // Clip bottom corners cleanly using subtle fill + hairline top separator
                if (brushMdStripBg) {
                    D2D1_RECT_F rcTopHalf = D2D1::RectF(cell.mdToolbarBounds.left, cell.mdToolbarBounds.top,
                                                        cell.mdToolbarBounds.right, cell.mdToolbarBounds.top + cardRadius);
                    pRT->FillRoundedRectangle(D2D1::RoundedRect(cell.mdToolbarBounds, cardRadius, cardRadius), brushMdStripBg.Get());
                    pRT->FillRectangle(rcTopHalf, brushMdStripBg.Get());
                }
                if (brushBorderHair) {
                    pRT->DrawLine(D2D1::Point2F(cell.mdToolbarBounds.left + 1.0f, cell.mdToolbarBounds.top + 0.5f),
                                  D2D1::Point2F(cell.mdToolbarBounds.right - 1.0f, cell.mdToolbarBounds.top + 0.5f),
                                  brushBorderHair.Get(), 1.0f);
                }

                float mdX = cell.mdToolbarBounds.left + config.ScaleF(10.0f);
                const float mdBtnW = config.ScaleF(26.0f);
                const float mdBtnH = config.ScaleF(22.0f);
                const float mdBtnY = cell.mdToolbarBounds.top + (mdBarH - mdBtnH) * 0.5f;
                const float mdStroke = config.ScaleF(1.30f);

                auto prepMdBtn = [&](D2D1_RECT_F& rcOut, int btnId) -> ID2D1Brush* {
                    rcOut = D2D1::RectF(mdX, mdBtnY, mdX + mdBtnW, mdBtnY + mdBtnH);
                    const bool hot = (isHovered && m_hoveredBtn == btnId);
                    if (hot && brushBtnHover) {
                        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcOut, config.ScaleF(4.0f), config.ScaleF(4.0f)), brushBtnHover.Get());
                    }
                    mdX += mdBtnW + config.ScaleF(4.0f);
                    return hot ? brushFgStrong.Get() : brushFgMuted.Get();
                };

                // B (Bold)
                if (ID2D1Brush* fg = prepMdBtn(cell.mdBtnBoldBounds, 12)) {
                    IDWriteTextFormat* bf = m_fmtNbMdBold ? m_fmtNbMdBold.Get() : tbBtnFmt;
                    if (bf) pRT->DrawText(L"B", 1, bf, cell.mdBtnBoldBounds, fg);
                }
                // I (Italic)
                if (ID2D1Brush* fg = prepMdBtn(cell.mdBtnItalicBounds, 13)) {
                    IDWriteTextFormat* itf = m_fmtNbMdItalic ? m_fmtNbMdItalic.Get() : tbBtnFmt;
                    if (itf) pRT->DrawText(L"I", 1, itf, cell.mdBtnItalicBounds, fg);
                }
                // ❝ (Blockquote)
                if (ID2D1Brush* fg = prepMdBtn(cell.mdBtnQuoteBounds, 14)) {
                    IDWriteTextFormat* bf = m_fmtNbMdBold ? m_fmtNbMdBold.Get() : tbBtnFmt;
                    if (bf) pRT->DrawText(L"\x275D", 1, bf, cell.mdBtnQuoteBounds, fg);
                }
                // 🔗 (Link)
                if (ID2D1Brush* fg = prepMdBtn(cell.mdBtnLinkBounds, 15)) {
                    nb_icons::DrawLinkIcon(pRT, (cell.mdBtnLinkBounds.left + cell.mdBtnLinkBounds.right) * 0.5f,
                                           mdBtnY + mdBtnH * 0.5f, config.ScaleF(5.8f), fg, mdStroke);
                }
                // 🖼 (Image)
                if (ID2D1Brush* fg = prepMdBtn(cell.mdBtnImageBounds, 16)) {
                    nb_icons::DrawImageIcon(pRT, (cell.mdBtnImageBounds.left + cell.mdBtnImageBounds.right) * 0.5f,
                                            mdBtnY + mdBtnH * 0.5f, config.ScaleF(6.2f), fg, mdStroke);
                }
                // •≡ (Bullet List)
                if (ID2D1Brush* fg = prepMdBtn(cell.mdBtnBulletListBounds, 17)) {
                    nb_icons::DrawBulletListIcon(pRT, (cell.mdBtnBulletListBounds.left + cell.mdBtnBulletListBounds.right) * 0.5f,
                                                 mdBtnY + mdBtnH * 0.5f, config.ScaleF(6.2f), fg, mdStroke);
                }
                // 1≡ (Numbered List)
                if (ID2D1Brush* fg = prepMdBtn(cell.mdBtnNumberListBounds, 18)) {
                    nb_icons::DrawNumberListIcon(pRT, (cell.mdBtnNumberListBounds.left + cell.mdBtnNumberListBounds.right) * 0.5f,
                                                 mdBtnY + mdBtnH * 0.5f, config.ScaleF(6.2f), fg, mdStroke);
                }
                // H (Heading)
                if (ID2D1Brush* fg = prepMdBtn(cell.mdBtnHeadingBounds, 19)) {
                    IDWriteTextFormat* bf = m_fmtNbMdBold ? m_fmtNbMdBold.Get() : tbBtnFmt;
                    if (bf) pRT->DrawText(L"H", 1, bf, cell.mdBtnHeadingBounds, fg);
                }

                // Right-aligned [✓ Done] toggle inside Markdown bar
                const float doneW = config.ScaleF(58.0f);
                if (cell.mdToolbarBounds.right - doneW - config.ScaleF(8.0f) > mdX + config.ScaleF(8.0f)) {
                    cell.mdBtnDoneBounds = D2D1::RectF(
                        cell.mdToolbarBounds.right - doneW - config.ScaleF(8.0f), mdBtnY,
                        cell.mdToolbarBounds.right - config.ScaleF(8.0f), mdBtnY + mdBtnH
                    );
                    const bool hotDone = (isHovered && m_hoveredBtn == 20);
                    if (hotDone && brushBtnHover) {
                        pRT->FillRoundedRectangle(D2D1::RoundedRect(cell.mdBtnDoneBounds, config.ScaleF(4.0f), config.ScaleF(4.0f)), brushBtnHover.Get());
                    }
                    ID2D1Brush* dfg = hotDone ? brushFgStrong.Get() : brushFgMuted.Get();
                    if (dfg && tbBtnFmt) {
                        pRT->DrawText(L"\x2713 Done", 6, tbBtnFmt, cell.mdBtnDoneBounds, dfg);
                    }
                } else {
                    cell.mdBtnDoneBounds = {};
                }
            } else {
                cell.mdToolbarBounds = {};
                cell.mdBtnBoldBounds = {};
                cell.mdBtnItalicBounds = {};
                cell.mdBtnQuoteBounds = {};
                cell.mdBtnLinkBounds = {};
                cell.mdBtnImageBounds = {};
                cell.mdBtnBulletListBounds = {};
                cell.mdBtnNumberListBounds = {};
                cell.mdBtnHeadingBounds = {};
                cell.mdBtnDoneBounds = {};
            }

            // Crisp hairline border around unified rounded input card
            if (isActive && m_brushNbRowHover && brushBorderHair) {
                pRT->DrawRoundedRectangle(rrcCodeBox, brushBorderHair.Get(), 1.1f);
            } else if (brushBorderHair) {
                pRT->DrawRoundedRectangle(rrcCodeBox, brushBorderHair.Get(), 0.9f);
            }
            } // end if (isMarkdownPreview) ... else

            // 3.4 Output Area (Colab/Jupyter standard: ordered text + large readable plots, collapse/clear/copy actions, interactive hover image tools)
            cell.outputCopyBtnBounds = {};
            cell.outputCollapseBtnBounds = {};
            cell.outputClearBtnBounds = {};
            if (!cell.outputs.empty() || cell.isWaitingForInput) {
                float outTop = rcInputOuter.bottom + config.ScaleF(8.0f);
                cell.outputAreaBounds = D2D1::RectF(cardContentLeft, outTop, cardRight, outTop + outputH);

                D2D1_ROUNDED_RECT rrcOut = D2D1::RoundedRect(cell.outputAreaBounds, config.ScaleF(8.0f), config.ScaleF(8.0f));
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushOutBg;
                if (cell.hasError) {
                    pRT->CreateSolidColorBrush(dark ? D2D1::ColorF(0.12f, 0.06f, 0.06f, 1.0f) : D2D1::ColorF(0.99f, 0.94f, 0.94f, 1.0f), &brushOutBg);
                } else if (dark) {
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.055f, 0.058f, 0.072f, 1.0f), &brushOutBg);
                } else {
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.975f, 0.978f, 0.985f, 1.0f), &brushOutBg);
                }
                if (brushOutBg) pRT->FillRoundedRectangle(rrcOut, brushOutBg.Get());
                if (brushBorderHair && !cell.hasError) {
                    pRT->DrawRoundedRectangle(rrcOut, brushBorderHair.Get(), 0.8f);
                }

                // Left red accent stripe on error
                if (cell.hasError) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushStripe;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.32f, 0.32f, 1.0f), &brushStripe);
                    if (brushStripe) {
                        pRT->DrawLine(
                            D2D1::Point2F(cell.outputAreaBounds.left + 1.5f, cell.outputAreaBounds.top + 3.0f),
                            D2D1::Point2F(cell.outputAreaBounds.left + 1.5f, cell.outputAreaBounds.bottom - 3.0f),
                            brushStripe.Get(), 3.0f
                        );
                    }
                }

                if (cell.isOutputCollapsed) {
                    size_t sumLines = 0, sumImgs = 0;
                    for (auto& out : cell.outputs) {
                        out.renderedImageBounds = {};
                        out.imgZoomBtnBounds = {};
                        out.imgCopyBtnBounds = {};
                        out.imgSaveBtnBounds = {};
                        if (!out.imagePngBytes.empty()) sumImgs++;
                        if (!out.text.empty()) sumLines += std::count(out.text.begin(), out.text.end(), L'\n') + 1;
                    }
                    std::wstring sumStr = L"\x25B8 Output hidden (";
                    if (sumLines > 0) sumStr += std::to_wstring(sumLines) + L" line" + (sumLines == 1 ? L"" : L"s");
                    if (sumLines > 0 && sumImgs > 0) sumStr += L", ";
                    if (sumImgs > 0) sumStr += std::to_wstring(sumImgs) + L" plot" + (sumImgs == 1 ? L"" : L"s");
                    if (sumLines == 0 && sumImgs == 0) sumStr += L"empty";
                    sumStr += L") \x2014 Click to expand";

                    const float clearW = config.ScaleF(28.0f);
                    cell.outputCollapseBtnBounds = D2D1::RectF(
                        cell.outputAreaBounds.left + config.ScaleF(10.0f),
                        cell.outputAreaBounds.top + config.ScaleF(4.0f),
                        cell.outputAreaBounds.right - clearW - config.ScaleF(12.0f),
                        cell.outputAreaBounds.bottom - config.ScaleF(4.0f)
                    );
                    cell.outputClearBtnBounds = D2D1::RectF(
                        cell.outputAreaBounds.right - clearW - config.ScaleF(6.0f),
                        cell.outputAreaBounds.top + config.ScaleF(4.0f),
                        cell.outputAreaBounds.right - config.ScaleF(6.0f),
                        cell.outputAreaBounds.bottom - config.ScaleF(4.0f)
                    );
                    if (tbLeadFmt) {
                        pRT->DrawText(sumStr.c_str(), static_cast<UINT32>(sumStr.length()), tbLeadFmt, cell.outputCollapseBtnBounds,
                                      (isHovered && m_hoveredBtn == 8) ? theme.brushTextPrimary.Get() : theme.brushTextSecondary.Get(),
                                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    }
                    if (isHovered && m_hoveredBtn == 5 && theme.brushItemHover) {
                        pRT->FillRoundedRectangle(D2D1::RoundedRect(cell.outputClearBtnBounds, config.ScaleF(4.0f), config.ScaleF(4.0f)), theme.brushItemHover.Get());
                    }
                    if (actFmt) {
                        pRT->DrawText(L"\x2715", 1, actFmt, cell.outputClearBtnBounds, theme.brushTextSecondary.Get());
                    }
                    cell.outputVScrollTrackBounds = {};
                    cell.outputVScrollThumbBounds = {};
                    cell.outputHScrollTrackBounds = {};
                    cell.outputHScrollThumbBounds = {};
                    cell.outputMaxScrollY = 0.0f;
                    cell.outputMaxScrollX = 0.0f;
                } else {

                float textPadX = config.ScaleF(12.0f);
                float textPadY = config.ScaleF(6.0f);
                float outLineH = config.ScaleF(18.0f);
                float availW = (cell.outputAreaBounds.right - cell.outputAreaBounds.left) - textPadX * 2.0f;

                // Precompute exact content height and max width to reserve scrollbar gutters
                float estContentH = textPadY * 2.0f;
                float estMaxW = 0.0f;
                bool anyOutText = false;
                for (const auto& out : cell.outputs) {
                    if (!out.imagePngBytes.empty()) {
                        float dw = 0.0f, dh = 0.0f;
                        ComputeScaledImageSize(out, availW, cell.imageZoomScale, config, dw, dh);
                        estContentH += dh + config.ScaleF(8.0f);
                        estMaxW = (std::max)(estMaxW, dw);
                    }
                    if (!out.text.empty()) {
                        anyOutText = true;
                        std::wstringstream wssPre(out.text);
                        std::wstring lPre;
                        while (std::getline(wssPre, lPre)) {
                            estContentH += outLineH;
                            size_t vLen = editor::NotebookDocument::StripAnsi(lPre).length();
                            float lw = static_cast<float>(vLen) * charW;
                            if (lw > estMaxW) estMaxW = lw;
                        }
                    }
                }
                if (cell.isWaitingForInput) estContentH += outLineH * 2.0f;

                bool hasVScroll = (estContentH > outputH && outputH > config.ScaleF(30.0f));
                bool hasHScroll = (estMaxW > availW && availW > config.ScaleF(30.0f));

                // Strict Direct2D axis-aligned clipping with scrollbar gutter reservation so text/images never overlap scrollbars
                D2D1_RECT_F textClip = cell.outputAreaBounds;
                if (hasVScroll) {
                    textClip.right -= config.ScaleF(15.0f); // Reserve right gutter for vertical scrollbar
                }
                if (hasHScroll) {
                    textClip.bottom -= config.ScaleF(15.0f); // Reserve bottom gutter for horizontal scrollbar
                }
                pRT->PushAxisAlignedClip(textClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                float curLineY = outTop + textPadY - cell.outputScrollY;

                float maxLineWidth = 0.0f;

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushErrText;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.38f, 0.38f, 1.0f), &brushErrText);

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSelBg;
                pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.42f, 0.75f, 0.45f), &brushSelBg);

                size_t outLineIdx = 0;
                size_t sL = 0, sC = 0, eL = 0, eC = 0;
                bool hasSel = cell.HasOutputSelection();
                if (hasSel) {
                    cell.GetOutputSelectionRange(sL, sC, eL, eC);
                }

                const float imgAvailW = (hasVScroll ? (availW - config.ScaleF(14.0f)) : availW);
                for (size_t oi = 0; oi < cell.outputs.size(); ++oi) {
                    auto& out = cell.outputs[oi];
                    out.renderedImageBounds = {};
                    out.imgZoomBtnBounds = {};
                    out.imgCopyBtnBounds = {};
                    out.imgSaveBtnBounds = {};

                    if (!out.imagePngBytes.empty() && out.imageId != 0) {
                        float bmpW = 0.0f, bmpH = 0.0f;
                        ID2D1Bitmap* pBmp = GetOrCreateOutputBitmap(pRT, out.imageId, out.imagePngBytes, bmpW, bmpH);
                        if (pBmp && bmpW > 0.0f && bmpH > 0.0f) {
                            if (out.imagePixelW == 0 || out.imagePixelH == 0) {
                                out.imagePixelW = static_cast<uint32_t>(bmpW);
                                out.imagePixelH = static_cast<uint32_t>(bmpH);
                            }
                            float drawW = 0.0f, drawH = 0.0f;
                            ComputeScaledImageSize(out, imgAvailW, cell.imageZoomScale, config, drawW, drawH);
                            if (drawW > maxLineWidth) maxLineWidth = drawW;
                            if (curLineY + drawH >= outTop && curLineY <= outTop + outputH) {
                                float imgX = cell.outputAreaBounds.left + textPadX - cell.outputScrollX;
                                D2D1_RECT_F rcImg = D2D1::RectF(imgX, curLineY, imgX + drawW, curLineY + drawH);
                                out.renderedImageBounds = D2D1::RectF(
                                    (std::max)(textClip.left, rcImg.left),
                                    (std::max)(textClip.top, rcImg.top),
                                    (std::min)(textClip.right, rcImg.right),
                                    (std::min)(textClip.bottom, rcImg.bottom)
                                );
                                if (m_brushNbPillBgLight) {
                                    pRT->FillRoundedRectangle(D2D1::RoundedRect(rcImg, 6.0f, 6.0f), m_brushNbPillBgLight.Get());
                                }
                                pRT->DrawBitmap(pBmp, rcImg, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                                if (theme.brushBorderSubtle) {
                                    pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcImg, 6.0f, 6.0f), theme.brushBorderSubtle.Get(), 1.0f);
                                }

                                // Sleek floating hover toolbar on top-right of plot image: [Inspect] [Copy] [Save]
                                if (m_hoveredImgCellIdx == static_cast<int>(ci) && m_hoveredImgOutIdx == static_cast<int>(oi)) {
                                    const float ovH = config.ScaleF(30.0f);
                                    const float ovY = rcImg.top + config.ScaleF(8.0f);
                                    float ovRight = (std::min)(textClip.right - config.ScaleF(8.0f), rcImg.right - config.ScaleF(10.0f));
                                    IDWriteTextFormat* bf = m_fmtNbBtn ? m_fmtNbBtn.Get() : (theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontCode.Get());
                                    auto drawImgOverlayBtn = [&](D2D1_RECT_F& btnRc, float bw, const wchar_t* lbl, int bId) {
                                        ovRight -= bw;
                                        btnRc = D2D1::RectF(ovRight, ovY, ovRight + bw, ovY + ovH);
                                        D2D1_ROUNDED_RECT rrcOv = D2D1::RoundedRect(btnRc, config.ScaleF(5.0f), config.ScaleF(5.0f));
                                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgB;
                                        pRT->CreateSolidColorBrush((m_hoveredImgBtn == bId)
                                            ? D2D1::ColorF(0.18f, 0.42f, 0.78f, 0.96f)
                                            : D2D1::ColorF(0.08f, 0.09f, 0.12f, 0.88f), &bgB);
                                        if (bgB) pRT->FillRoundedRectangle(rrcOv, bgB.Get());
                                        if (theme.brushBorderSubtle) pRT->DrawRoundedRectangle(rrcOv, theme.brushBorderSubtle.Get(), 0.9f);
                                        if (bf && theme.brushTextWhite) {
                                            DWRITE_TEXT_ALIGNMENT pa = bf->GetTextAlignment();
                                            DWRITE_PARAGRAPH_ALIGNMENT pp = bf->GetParagraphAlignment();
                                            bf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                                            bf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                                            pRT->DrawText(lbl, static_cast<UINT32>(wcslen(lbl)), bf, btnRc, theme.brushTextWhite.Get());
                                            bf->SetTextAlignment(pa);
                                            bf->SetParagraphAlignment(pp);
                                        }
                                        ovRight -= config.ScaleF(6.0f);
                                    };
                                    drawImgOverlayBtn(out.imgSaveBtnBounds, config.ScaleF(62.0f), L"Save", 3);
                                    drawImgOverlayBtn(out.imgCopyBtnBounds, config.ScaleF(62.0f), L"Copy", 2);
                                    drawImgOverlayBtn(out.imgZoomBtnBounds, config.ScaleF(78.0f), L"Inspect", 1);
                                }
                            }
                            curLineY += drawH + config.ScaleF(8.0f);
                        }
                    }

                    D2D1_COLOR_F defColor = out.isError ? D2D1::ColorF(0.95f, 0.38f, 0.38f, 1.0f)
                                                        : (theme.brushTextPrimary ? theme.brushTextPrimary->GetColor() : D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.0f));

                    std::wstringstream wss(out.text);
                    std::wstring rawLine;
                    while (std::getline(wss, rawLine)) {
                        auto spans = ParseNotebookOutputLine(rawLine, defColor, out.isError);

                        size_t visibleLen = 0;
                        for (const auto& sp : spans) visibleLen += sp.text.length();
                        float lineW = static_cast<float>(visibleLen) * charW;
                        if (lineW > maxLineWidth) maxLineWidth = lineW;

                        if (curLineY + outLineH >= outTop && curLineY <= outTop + outputH) {
                            float drawX = cell.outputAreaBounds.left + textPadX - cell.outputScrollX;

                            // Render selection highlight background for this output line if selected
                            if (hasSel && outLineIdx >= sL && outLineIdx <= eL) {
                                float selX1 = drawX;
                                float selX2 = drawX;
                                if (outLineIdx == sL && outLineIdx == eL) {
                                    selX1 = drawX + static_cast<float>(sC) * charW;
                                    selX2 = drawX + static_cast<float>(eC) * charW;
                                } else if (outLineIdx == sL) {
                                    selX1 = drawX + static_cast<float>(sC) * charW;
                                    selX2 = drawX + static_cast<float>(visibleLen) * charW + config.ScaleF(4.0f);
                                } else if (outLineIdx == eL) {
                                    selX1 = drawX;
                                    selX2 = drawX + static_cast<float>(eC) * charW;
                                } else {
                                    selX1 = drawX;
                                    selX2 = (std::max)(drawX + static_cast<float>(visibleLen) * charW + config.ScaleF(4.0f), drawX + config.ScaleF(24.0f));
                                }
                                if (selX2 > selX1 && brushSelBg) {
                                    D2D1_RECT_F rcSel = D2D1::RectF(selX1, curLineY, selX2, curLineY + outLineH);
                                    pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSel, 2.0f, 2.0f), brushSelBg.Get());
                                }
                            }

                            // Render colored spans across the line
                            float spanX = drawX;
                            for (const auto& sp : spans) {
                                if (sp.text.empty()) continue;
                                float spanW = static_cast<float>(sp.text.length()) * charW;
                                D2D1_RECT_F rcSpan = D2D1::RectF(
                                    spanX,
                                    curLineY,
                                    spanX + spanW + config.ScaleF(4.0f),
                                    curLineY + outLineH
                                );
                                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> spanBrush;
                                pRT->CreateSolidColorBrush(sp.color, &spanBrush);
                                if (spanBrush) {
                                    pRT->DrawText(sp.text.c_str(), static_cast<UINT32>(sp.text.length()),
                                                  theme.fontCode.Get(), rcSpan, spanBrush.Get());
                                }
                                spanX += spanW;
                            }
                        }
                        curLineY += outLineH;
                        outLineIdx++;
                    }
                }

                // Render interactive input box when cell is waiting for input (Colab / Jupyter standard)
                if (cell.isWaitingForInput) {
                    float inputRowY = curLineY;
                    float promptW = static_cast<float>(cell.inputPrompt.length()) * charW;
                    float drawX = cell.outputAreaBounds.left + textPadX - cell.outputScrollX;

                    // Prompt label (e.g. "number: ")
                    if (!cell.inputPrompt.empty()) {
                        D2D1_RECT_F rcPrompt = D2D1::RectF(drawX, inputRowY, drawX + promptW, inputRowY + outLineH);
                        cell.inputPromptBounds = rcPrompt;
                        pRT->DrawText(cell.inputPrompt.c_str(), static_cast<UINT32>(cell.inputPrompt.length()),
                                      theme.fontCode.Get(), rcPrompt,
                                      theme.brushSyntaxKeyword ? theme.brushSyntaxKeyword.Get() : theme.brushTextPrimary.Get());
                    }

                    // Input edit box
                    float fieldX = drawX + promptW + config.ScaleF(6.0f);
                    float fieldW = (std::max)(config.ScaleF(160.0f), cell.outputAreaBounds.right - fieldX - config.ScaleF(70.0f));
                    float fieldH = outLineH + config.ScaleF(2.0f);
                    D2D1_RECT_F rcField = D2D1::RectF(fieldX, inputRowY - config.ScaleF(1.0f), fieldX + fieldW, inputRowY + fieldH - config.ScaleF(1.0f));
                    cell.inputFieldBounds = rcField;

                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushFieldBg;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.04f, 0.04f, 0.06f, 0.95f), &brushFieldBg);
                    if (brushFieldBg) {
                        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcField, 3.0f, 3.0f), brushFieldBg.Get());
                    }
                    ID2D1Brush* fieldBorderBrush = cell.isInputFieldFocused
                        ? (theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextPrimary.Get())
                        : theme.brushBorderSubtle.Get();
                    pRT->DrawRoundedRectangle(D2D1::RoundedRect(rcField, 3.0f, 3.0f), fieldBorderBrush, cell.isInputFieldFocused ? 1.5f : 1.0f);

                    // Text inside input box
                    float padValX = config.ScaleF(6.0f);
                    D2D1_RECT_F rcFieldText = D2D1::RectF(rcField.left + padValX, rcField.top + config.ScaleF(1.0f), rcField.right - padValX, rcField.bottom);
                    if (!cell.inputFieldValue.empty()) {
                        pRT->DrawText(cell.inputFieldValue.c_str(), static_cast<UINT32>(cell.inputFieldValue.length()),
                                      theme.fontCode.Get(), rcFieldText, theme.brushTextPrimary.Get());
                    }

                    // Caret inside input box
                    if (cell.isInputFieldFocused && m_caretVisible) {
                        float caretX = rcField.left + padValX + static_cast<float>(cell.inputFieldValue.length()) * charW;
                        if (caretX < rcField.right - padValX) {
                            pRT->DrawLine(D2D1::Point2F(caretX, rcField.top + config.ScaleF(2.0f)),
                                          D2D1::Point2F(caretX, rcField.bottom - config.ScaleF(2.0f)),
                                          theme.brushTextPrimary.Get(), 1.5f);
                        }
                    }

                    // Submit / Enter button
                    float btnSubX = rcField.right + config.ScaleF(6.0f);
                    float btnSubW = config.ScaleF(74.0f);
                    D2D1_RECT_F rcSub = D2D1::RectF(btnSubX, rcField.top - config.ScaleF(1.0f), btnSubX + btnSubW, rcField.bottom + config.ScaleF(1.0f));
                    cell.submitBtnBounds = rcSub;

                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSubBg;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.42f, 0.75f, 0.85f), &brushSubBg);
                    if (brushSubBg) {
                        pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSub, config.ScaleF(4.0f), config.ScaleF(4.0f)), brushSubBg.Get());
                    }
                    pRT->DrawText(L"\x21B5 Enter", 7, tbBtnFmt,
                                  rcSub, theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get());

                    curLineY += outLineH + config.ScaleF(6.0f);
                }

                pRT->PopAxisAlignedClip();

                float totalContentH = (curLineY - (outTop + textPadY - cell.outputScrollY)) + textPadY * 2.0f;
                bool actualHScroll = (maxLineWidth > availW && availW > config.ScaleF(30.0f));
                if (actualHScroll) {
                    totalContentH += config.ScaleF(14.0f); // Space for horizontal scrollbar at bottom
                }
                bool actualVScroll = (totalContentH > outputH && outputH > config.ScaleF(30.0f));

                // Prominent top-right output action strip on hover: [Copy] [Hide] [Clear]
                bool showCopied = (m_copiedFeedbackCellIdx == static_cast<int>(ci) && GetTickCount64() < m_copiedFeedbackUntil);
                if (isHovered || showCopied) {
                    float ctrlX = cell.outputAreaBounds.right - config.ScaleF(actualVScroll ? 18.0f : 8.0f);
                    const float ctrlY = cell.outputAreaBounds.top + config.ScaleF(6.0f);
                    const float ctrlH = config.ScaleF(26.0f);
                    IDWriteTextFormat* bf = m_fmtNbBtn ? m_fmtNbBtn.Get() : (theme.fontButtonCenter ? theme.fontButtonCenter.Get() : theme.fontCode.Get());
                    auto drawOutPill = [&](D2D1_RECT_F& btnRc, float bw, const wchar_t* lbl, int bId, bool isGlyph = false) {
                        ctrlX -= bw;
                        btnRc = D2D1::RectF(ctrlX, ctrlY, ctrlX + bw, ctrlY + ctrlH);
                        D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(btnRc, config.ScaleF(5.0f), config.ScaleF(5.0f));
                        if (m_hoveredBtn == bId && brushBtnHover) {
                            pRT->FillRoundedRectangle(rrcPill, brushPillBg ? brushPillBg.Get() : theme.brushItemHover.Get());
                            pRT->FillRoundedRectangle(rrcPill, brushBtnHover.Get());
                        } else if (brushPillBg) {
                            pRT->FillRoundedRectangle(rrcPill, brushPillBg.Get());
                        }
                        if (brushBorderHair) pRT->DrawRoundedRectangle(rrcPill, brushBorderHair.Get(), 0.9f);
                        IDWriteTextFormat* useFmt = (isGlyph && actFmt) ? actFmt : bf;
                        if (useFmt && theme.brushTextPrimary) {
                            DWRITE_TEXT_ALIGNMENT pa = useFmt->GetTextAlignment();
                            DWRITE_PARAGRAPH_ALIGNMENT pp = useFmt->GetParagraphAlignment();
                            useFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                            useFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                            pRT->DrawText(lbl, static_cast<UINT32>(wcslen(lbl)), useFmt, btnRc, theme.brushTextPrimary.Get());
                            useFmt->SetTextAlignment(pa);
                            useFmt->SetParagraphAlignment(pp);
                        }
                        ctrlX -= config.ScaleF(6.0f);
                    };
                    drawOutPill(cell.outputClearBtnBounds, config.ScaleF(28.0f), L"\x2715", 5, true);
                    drawOutPill(cell.outputCollapseBtnBounds, config.ScaleF(54.0f), L"Hide", 8, false);
                    if (anyOutText) {
                        drawOutPill(cell.outputCopyBtnBounds, config.ScaleF(showCopied ? 78.0f : 56.0f),
                                    showCopied ? L"\x2713 Copied" : L"Copy", 9, false);
                    }
                }

                // 1. Sleek Vertical Scrollbar when output exceeds clamped height (Thicker, easy-to-drag, with visual track)
                if (actualVScroll) {
                    float sbW = config.ScaleF(8.0f);
                    float sbX = cell.outputAreaBounds.right - sbW - config.ScaleF(3.0f);
                    float trackH = outputH - config.ScaleF(8.0f) - (actualHScroll ? config.ScaleF(12.0f) : 0.0f);
                    float thumbH = (std::max)(config.ScaleF(24.0f), trackH * (outputH / totalContentH));
                    float maxScrollY = totalContentH - outputH;
                    float clampedScrollY = (std::max)(0.0f, (std::min)(cell.outputScrollY, maxScrollY));
                    float thumbY = outTop + config.ScaleF(4.0f) + (clampedScrollY / (maxScrollY > 0.0f ? maxScrollY : 1.0f)) * (trackH - thumbH);

                    cell.outputVScrollTrackBounds = D2D1::RectF(sbX - config.ScaleF(2.0f), outTop + config.ScaleF(4.0f), sbX + sbW + config.ScaleF(2.0f), outTop + config.ScaleF(4.0f) + trackH);
                    cell.outputVScrollThumbBounds = D2D1::RectF(sbX, thumbY, sbX + sbW, thumbY + thumbH);
                    cell.outputMaxScrollY = maxScrollY;

                    // Subtle track background
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTrackY;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f), &brushTrackY);
                    if (brushTrackY) {
                        D2D1_ROUNDED_RECT rrcTrack = D2D1::RoundedRect(cell.outputVScrollTrackBounds, sbW / 2.0f, sbW / 2.0f);
                        pRT->FillRoundedRectangle(rrcTrack, brushTrackY.Get());
                    }

                    bool isDraggingThis = (m_isDraggingOutputVScroll && m_dragOutputCellIdx == static_cast<int>(ci));
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSbY;
                    pRT->CreateSolidColorBrush(isDraggingThis ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.65f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f), &brushSbY);
                    if (brushSbY) {
                        D2D1_ROUNDED_RECT rrcSb = D2D1::RoundedRect(cell.outputVScrollThumbBounds, sbW / 2.0f, sbW / 2.0f);
                        pRT->FillRoundedRectangle(rrcSb, brushSbY.Get());
                    }
                } else {
                    cell.outputVScrollTrackBounds = D2D1::RectF(0, 0, 0, 0);
                    cell.outputVScrollThumbBounds = D2D1::RectF(0, 0, 0, 0);
                    cell.outputMaxScrollY = 0.0f;
                }

                // 2. Sleek Horizontal Scrollbar when long lines exceed width (Thicker, easy-to-drag, with visual track)
                if (maxLineWidth > availW && availW > config.ScaleF(30.0f)) {
                    float sbH = config.ScaleF(8.0f);
                    float sbY = cell.outputAreaBounds.bottom - sbH - config.ScaleF(3.0f);
                    float trackW = availW - (actualVScroll ? config.ScaleF(12.0f) : 0.0f);
                    float thumbW = (std::max)(config.ScaleF(28.0f), trackW * (availW / maxLineWidth));
                    float maxScrollX = maxLineWidth - availW;
                    float clampedScrollX = (std::max)(0.0f, (std::min)(cell.outputScrollX, maxScrollX));
                    float thumbX = cell.outputAreaBounds.left + textPadX + (clampedScrollX / (maxScrollX > 0.0f ? maxScrollX : 1.0f)) * (trackW - thumbW);

                    cell.outputHScrollTrackBounds = D2D1::RectF(cell.outputAreaBounds.left + textPadX, sbY - config.ScaleF(2.0f), cell.outputAreaBounds.left + textPadX + trackW, sbY + sbH + config.ScaleF(2.0f));
                    cell.outputHScrollThumbBounds = D2D1::RectF(thumbX, sbY, thumbX + thumbW, sbY + sbH);
                    cell.outputMaxScrollX = maxScrollX;

                    // Subtle track background
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushTrackX;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f), &brushTrackX);
                    if (brushTrackX) {
                        D2D1_ROUNDED_RECT rrcTrackX = D2D1::RoundedRect(cell.outputHScrollTrackBounds, sbH / 2.0f, sbH / 2.0f);
                        pRT->FillRoundedRectangle(rrcTrackX, brushTrackX.Get());
                    }

                    bool isDraggingThis = (m_isDraggingOutputHScroll && m_dragOutputCellIdx == static_cast<int>(ci));
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSbX;
                    pRT->CreateSolidColorBrush(isDraggingThis ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.65f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f), &brushSbX);
                    if (brushSbX) {
                        D2D1_ROUNDED_RECT rrcSbX = D2D1::RoundedRect(cell.outputHScrollThumbBounds, sbH / 2.0f, sbH / 2.0f);
                        pRT->FillRoundedRectangle(rrcSbX, brushSbX.Get());
                    }
                } else {
                    cell.outputHScrollTrackBounds = D2D1::RectF(0, 0, 0, 0);
                    cell.outputHScrollThumbBounds = D2D1::RectF(0, 0, 0, 0);
                    cell.outputMaxScrollX = 0.0f;
                }
                } // end else (!isOutputCollapsed)
            } else {
                cell.outputAreaBounds = D2D1::RectF(0, 0, 0, 0);
                cell.outputVScrollTrackBounds = D2D1::RectF(0, 0, 0, 0);
                cell.outputVScrollThumbBounds = D2D1::RectF(0, 0, 0, 0);
                cell.outputHScrollTrackBounds = D2D1::RectF(0, 0, 0, 0);
                cell.outputHScrollThumbBounds = D2D1::RectF(0, 0, 0, 0);
                cell.outputMaxScrollY = 0.0f;
                cell.outputMaxScrollX = 0.0f;
            }

            // 3.4b Floating Top-Right Cell Action Pill ([Edit] | [⠿] [▾] [⧉] [↑] [↓] [🗑])
            const bool showPill = (isActive || isHovered || isDraggingThis);
            if (showPill) {
                const float pillBtnS = config.ScaleF(27.0f);
                const float pillGap = config.ScaleF(2.0f);
                const float pillPadX = config.ScaleF(5.0f);
                const float pillH = config.ScaleF(29.0f);
                const float pillTop = cell.bounds.top - pillH * 0.48f;
                const float mdEditChipW = (!isCodeCell && !cell.isEditingMarkdown) ? config.ScaleF(40.0f) : 0.0f;
                const int iconSlotCount = 6; // drag, collapse, duplicate, up, down, delete
                const float totalPillW = pillPadX * 2.0f +
                    (mdEditChipW > 0.0f ? (mdEditChipW + config.ScaleF(6.0f)) : 0.0f) +
                    static_cast<float>(iconSlotCount) * pillBtnS +
                    static_cast<float>(iconSlotCount - 1) * pillGap;
                const float pillRight = cardRight - config.ScaleF(10.0f);
                const float pillLeft = (std::max)(cardContentLeft + config.ScaleF(8.0f), pillRight - totalPillW);
                D2D1_RECT_F rcPill = D2D1::RectF(pillLeft, pillTop, pillRight, pillTop + pillH);
                D2D1_ROUNDED_RECT rrcPill = D2D1::RoundedRect(rcPill, pillH * 0.5f, pillH * 0.5f);

                if (brushPillBg) pRT->FillRoundedRectangle(rrcPill, brushPillBg.Get());
                if (brushBorderHair) pRT->DrawRoundedRectangle(rrcPill, brushBorderHair.Get(), 1.0f);

                float px = pillRight - pillPadX;
                const float pbY = pillTop + (pillH - pillBtnS) * 0.5f;
                const float pStroke = config.ScaleF(1.25f);
                auto nextPillSlot = [&](D2D1_RECT_F& btnRc, int btnId, bool accent = false) -> ID2D1Brush* {
                    btnRc = D2D1::RectF(px - pillBtnS, pbY, px, pbY + pillBtnS);
                    const bool hot = (isHovered && m_hoveredBtn == btnId) || accent;
                    if (hot && brushBtnHover) {
                        pRT->FillRoundedRectangle(D2D1::RoundedRect(btnRc, config.ScaleF(6.0f), config.ScaleF(6.0f)), brushBtnHover.Get());
                    }
                    px -= pillBtnS + pillGap;
                    if (accent && theme.brushBrandAccent) return theme.brushBrandAccent.Get();
                    return hot ? brushFgStrong.Get() : brushFgMuted.Get();
                };

                // Delete [🗑]
                if (ID2D1Brush* fg = nextPillSlot(cell.deleteBtnBounds, 2)) {
                    nb_icons::DrawTrashIcon(pRT, (cell.deleteBtnBounds.left + cell.deleteBtnBounds.right) * 0.5f,
                                            pbY + pillBtnS * 0.5f, config.ScaleF(5.2f), fg, pStroke);
                }
                // Move Down [↓]
                if (ID2D1Brush* fg = nextPillSlot(cell.moveDownBtnBounds, 4)) {
                    nb_icons::DrawArrowDownIcon(pRT, (cell.moveDownBtnBounds.left + cell.moveDownBtnBounds.right) * 0.5f,
                                                pbY + pillBtnS * 0.5f, config.ScaleF(5.2f), fg, pStroke);
                }
                // Move Up [↑]
                if (ID2D1Brush* fg = nextPillSlot(cell.moveUpBtnBounds, 3)) {
                    nb_icons::DrawArrowUpIcon(pRT, (cell.moveUpBtnBounds.left + cell.moveUpBtnBounds.right) * 0.5f,
                                              pbY + pillBtnS * 0.5f, config.ScaleF(5.2f), fg, pStroke);
                }
                // Duplicate [⧉]
                if (ID2D1Brush* fg = nextPillSlot(cell.duplicateBtnBounds, 7)) {
                    nb_icons::DrawCopyIcon(pRT, (cell.duplicateBtnBounds.left + cell.duplicateBtnBounds.right) * 0.5f,
                                           pbY + pillBtnS * 0.5f, config.ScaleF(5.2f), fg, pStroke);
                }
                // Collapse [▾/▸]
                if (ID2D1Brush* fg = nextPillSlot(cell.collapseInputBtnBounds, 11, cell.isInputCollapsed)) {
                    float ccx = (cell.collapseInputBtnBounds.left + cell.collapseInputBtnBounds.right) * 0.5f;
                    float ccy = pbY + pillBtnS * 0.5f;
                    if (cell.isInputCollapsed) nb_icons::DrawChevronRightIcon(pRT, ccx, ccy, config.ScaleF(4.8f), fg, pStroke);
                    else nb_icons::DrawChevronDownIcon(pRT, ccx, ccy, config.ScaleF(4.8f), fg, pStroke);
                }
                // Drag Grip [⠿]
                if (ID2D1Brush* fg = nextPillSlot(cell.dragHandleBounds, 6, isDraggingThis)) {
                    nb_icons::DrawGripDotsIcon(pRT, (cell.dragHandleBounds.left + cell.dragHandleBounds.right) * 0.5f,
                                               pbY + pillBtnS * 0.5f, config.ScaleF(5.5f), fg);
                }
                if (mdEditChipW > 0.0f) {
                    px -= config.ScaleF(4.0f);
                    cell.editMarkdownBtnBounds = D2D1::RectF(px - mdEditChipW, pbY, px, pbY + pillBtnS);
                    const bool hotEdit = (isHovered && m_hoveredBtn == 10);
                    if (hotEdit && brushBtnHover) {
                        pRT->FillRoundedRectangle(D2D1::RoundedRect(cell.editMarkdownBtnBounds, config.ScaleF(6.0f), config.ScaleF(6.0f)), brushBtnHover.Get());
                    }
                    if (tbBtnFmt) {
                        pRT->DrawText(L"Edit", 4, tbBtnFmt, cell.editMarkdownBtnBounds,
                                      hotEdit ? brushFgStrong.Get() : brushFgMuted.Get());
                    }
                } else {
                    cell.editMarkdownBtnBounds = {};
                }
            } else {
                cell.deleteBtnBounds = {};
                cell.moveDownBtnBounds = {};
                cell.moveUpBtnBounds = {};
                cell.duplicateBtnBounds = {};
                cell.collapseInputBtnBounds = {};
                cell.dragHandleBounds = {};
                cell.editMarkdownBtnBounds = {};
            }

            // 3.5 Inline [+ Code] [+ Markdown] Pills beneath active cell or centered in fixed divider gap
            // NOTE: inter-cell spacing depends ONLY on `isActive`, NEVER hover state, preventing layout jump!
            float bottomY = cell.bounds.bottom;
            const bool isDividerHovered = (!isActive && m_hoveredBottomCellIdx == static_cast<int>(ci));
            cell.bottomDividerBounds = D2D1::RectF(cardContentLeft, bottomY - config.ScaleF(1.0f), cardRight, bottomY + config.ScaleF(1.0f));

            if (isActive) {
                const float pillH = config.ScaleF(24.0f);
                const float pillRadius = pillH * 0.5f;
                const float codeW = config.ScaleF(74.0f);
                const float textW = config.ScaleF(98.0f);
                const float gap = config.ScaleF(8.0f);
                const float pillY = bottomY + config.ScaleF(8.0f);

                cell.bottomAddCodeBounds = D2D1::RectF(cardContentLeft, pillY, cardContentLeft + codeW, pillY + pillH);
                cell.bottomAddTextBounds = D2D1::RectF(cell.bottomAddCodeBounds.right + gap, pillY,
                                                       cell.bottomAddCodeBounds.right + gap + textW, pillY + pillH);

                // [+ Code] flat outlined pill
                D2D1_ROUNDED_RECT rrcCode = D2D1::RoundedRect(cell.bottomAddCodeBounds, pillRadius, pillRadius);
                if (brushMdStripBg) pRT->FillRoundedRectangle(rrcCode, brushMdStripBg.Get());
                if (m_hoveredBottomCellIdx == static_cast<int>(ci) && m_hoveredBottomBtn == 1 && brushBtnHover) {
                    pRT->FillRoundedRectangle(rrcCode, brushBtnHover.Get());
                }
                if (brushBorderHair) pRT->DrawRoundedRectangle(rrcCode, brushBorderHair.Get(), 1.0f);
                if (tbBtnFmt) {
                    pRT->DrawText(L"+ Code", 6, tbBtnFmt, cell.bottomAddCodeBounds,
                                  (m_hoveredBottomCellIdx == static_cast<int>(ci) && m_hoveredBottomBtn == 1) ? brushFgStrong.Get() : brushFgMuted.Get());
                }

                // [+ Markdown] flat outlined pill
                D2D1_ROUNDED_RECT rrcText = D2D1::RoundedRect(cell.bottomAddTextBounds, pillRadius, pillRadius);
                if (brushMdStripBg) pRT->FillRoundedRectangle(rrcText, brushMdStripBg.Get());
                if (m_hoveredBottomCellIdx == static_cast<int>(ci) && m_hoveredBottomBtn == 2 && brushBtnHover) {
                    pRT->FillRoundedRectangle(rrcText, brushBtnHover.Get());
                }
                if (brushBorderHair) pRT->DrawRoundedRectangle(rrcText, brushBorderHair.Get(), 1.0f);
                if (tbBtnFmt) {
                    pRT->DrawText(L"+ Markdown", 10, tbBtnFmt, cell.bottomAddTextBounds,
                                  (m_hoveredBottomCellIdx == static_cast<int>(ci) && m_hoveredBottomBtn == 2) ? brushFgStrong.Get() : brushFgMuted.Get());
                }
            } else if (isDividerHovered) {
                const float divY = bottomY + config.ScaleF(13.0f);
                if (brushBorderHair) {
                    pRT->DrawLine(D2D1::Point2F(cardContentLeft, divY),
                                  D2D1::Point2F(cardRight, divY), brushBorderHair.Get(), 1.0f);
                }
                const float pillH = config.ScaleF(22.0f);
                const float pillRadius = pillH * 0.5f;
                const float codeW = config.ScaleF(74.0f);
                const float textW = config.ScaleF(98.0f);
                const float gap = config.ScaleF(8.0f);
                const float totalW = codeW + gap + textW;
                const float midX = (cardContentLeft + cardRight) * 0.5f;
                const float pillY = divY - pillH * 0.5f;

                cell.bottomAddCodeBounds = D2D1::RectF(midX - totalW * 0.5f, pillY, midX - totalW * 0.5f + codeW, pillY + pillH);
                cell.bottomAddTextBounds = D2D1::RectF(cell.bottomAddCodeBounds.right + gap, pillY,
                                                       cell.bottomAddCodeBounds.right + gap + textW, pillY + pillH);

                D2D1_ROUNDED_RECT rrcCode = D2D1::RoundedRect(cell.bottomAddCodeBounds, pillRadius, pillRadius);
                if (brushMdStripBg) pRT->FillRoundedRectangle(rrcCode, brushMdStripBg.Get());
                if (m_hoveredBottomBtn == 1 && brushBtnHover) pRT->FillRoundedRectangle(rrcCode, brushBtnHover.Get());
                if (brushBorderHair) pRT->DrawRoundedRectangle(rrcCode, brushBorderHair.Get(), 1.0f);
                if (tbBtnFmt) {
                    pRT->DrawText(L"+ Code", 6, tbBtnFmt, cell.bottomAddCodeBounds,
                                  (m_hoveredBottomBtn == 1) ? brushFgStrong.Get() : brushFgMuted.Get());
                }

                D2D1_ROUNDED_RECT rrcText = D2D1::RoundedRect(cell.bottomAddTextBounds, pillRadius, pillRadius);
                if (brushMdStripBg) pRT->FillRoundedRectangle(rrcText, brushMdStripBg.Get());
                if (m_hoveredBottomBtn == 2 && brushBtnHover) pRT->FillRoundedRectangle(rrcText, brushBtnHover.Get());
                if (brushBorderHair) pRT->DrawRoundedRectangle(rrcText, brushBorderHair.Get(), 1.0f);
                if (tbBtnFmt) {
                    pRT->DrawText(L"+ Markdown", 10, tbBtnFmt, cell.bottomAddTextBounds,
                                  (m_hoveredBottomBtn == 2) ? brushFgStrong.Get() : brushFgMuted.Get());
                }
            } else {
                cell.bottomAddCodeBounds = D2D1::RectF(0, 0, 0, 0);
                cell.bottomAddTextBounds = D2D1::RectF(0, 0, 0, 0);
            }
        }

        curY += cardH + (isActive ? config.ScaleF(44.0f) : config.ScaleF(28.0f));
    }

    m_totalHeight = (curY + m_scrollY) - (contentTop + config.ScaleF(16.0f));
    if (m_scrollActiveCellIntoView && nb && nb->activeCellIdx >= 0 && nb->activeCellIdx < static_cast<int>(nb->cells.size())) {
        m_scrollActiveCellIntoView = false;
        const auto& ac = nb->cells[nb->activeCellIdx];
        if (ac.bounds.bottom > top + height - config.ScaleF(12.0f)) {
            float delta = ac.bounds.bottom - (top + height - config.ScaleF(20.0f));
            float maxScroll = (std::max)(0.0f, m_totalHeight - contentH + config.ScaleF(40.0f));
            m_scrollY = (std::min)(maxScroll, m_scrollY + delta);
            nb->scrollY = m_scrollY;
        } else if (ac.bounds.top < contentTop + config.ScaleF(8.0f)) {
            float delta = (contentTop + config.ScaleF(12.0f)) - ac.bounds.top;
            m_scrollY = (std::max)(0.0f, m_scrollY - delta);
            nb->scrollY = m_scrollY;
        }
    }

    // 3.4 Render drag-and-drop cell reordering indicator line
    if (m_isDraggingCell && nb && m_dropTargetCellIdx >= 0 &&
        m_dropTargetCellIdx < static_cast<int>(nb->cells.size()) &&
        m_dropTargetCellIdx != m_draggedCellIdx) {
        float lineY = 0.0f;
        if (m_dropTargetCellIdx < m_draggedCellIdx) {
            lineY = nb->cells[m_dropTargetCellIdx].bounds.top - config.ScaleF(4.0f);
        } else {
            lineY = nb->cells[m_dropTargetCellIdx].bounds.bottom + config.ScaleF(4.0f);
        }

        ID2D1Brush* brushDrop = theme.brushBrandAccent ? theme.brushBrandAccent.Get() : theme.brushTextPrimary.Get();
        if (brushDrop) {
            pRT->DrawLine(D2D1::Point2F(cardLeft, lineY), D2D1::Point2F(cardRight, lineY), brushDrop, 3.0f);
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cardLeft, lineY), 4.0f, 4.0f), brushDrop);
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cardRight, lineY), 4.0f, 4.0f), brushDrop);
        }
    }

    pRT->PopAxisAlignedClip();

    // 4. Custom Slim Scrollbar if content exceeds viewport
    if (m_totalHeight > contentH && contentH > 0.0f) {
        const float barW = config.ScaleF(8.0f);
        const float barX = left + width - barW - config.ScaleF(3.0f);
        m_scrollTrack = D2D1::RectF(barX, contentTop, barX + barW, top + height);

        float ratio = contentH / (m_totalHeight + contentH);
        float thumbH = (std::max)(config.ScaleF(28.0f), contentH * ratio);
        float maxScroll = (std::max)(0.0f, m_totalHeight - contentH + config.ScaleF(40.0f));
        float scrollPct = (maxScroll > 0.0f) ? (m_scrollY / maxScroll) : 0.0f;
        scrollPct = (std::clamp)(scrollPct, 0.0f, 1.0f);
        float thumbY = contentTop + scrollPct * (contentH - thumbH);

        m_scrollThumb = D2D1::RectF(barX, thumbY, barX + barW, thumbY + thumbH);

        D2D1_ROUNDED_RECT rrcThumb = D2D1::RoundedRect(m_scrollThumb, barW / 2.0f, barW / 2.0f);
        ID2D1Brush* thumbBrush = m_isDraggingScrollbar
            ? (theme.brushBorderHover ? theme.brushBorderHover.Get() : theme.brushBorderSubtle.Get())
            : (theme.brushBorderSubtle ? theme.brushBorderSubtle.Get() : theme.brushTextSecondary.Get());
        pRT->FillRoundedRectangle(rrcThumb, thumbBrush);
    } else {
        m_scrollTrack = D2D1::RectF(0, 0, 0, 0);
        m_scrollThumb = D2D1::RectF(0, 0, 0, 0);
    }

    // 5. Floating Tooltips for Toolbar & Markdown Strip Buttons
    if (m_hoveredToolbarBtn == 1) {
        std::wstring tip = m_isExecuting.load() ? L"Stop Execution" : L"Run All Cells";
        RenderTooltip(pRT, theme, config, m_btnRunAll, tip, left, width);
    } else if (m_hoveredToolbarBtn == 2) {
        RenderTooltip(pRT, theme, config, m_btnClearAll, L"Clear All Outputs", left, width);
    } else if (m_hoveredToolbarBtn == 3) {
        RenderTooltip(pRT, theme, config, m_btnAddCode, L"Add Code Cell", left, width);
    } else if (m_hoveredToolbarBtn == 4) {
        RenderTooltip(pRT, theme, config, m_btnAddText, L"Add Markdown Cell", left, width);
    } else if (m_hoveredToolbarBtn == 5) {
        RenderTooltip(pRT, theme, config, m_btnKernelPicker, L"Select Python Kernel / Environment", left, width);
    } else if (m_hoveredToolbarBtn == 6) {
        RenderTooltip(pRT, theme, config, m_btnRestartKernel, L"Restart Kernel Namespace", left, width);
    } else if (m_hoveredToolbarBtn == 7) {
        RenderTooltip(pRT, theme, config, m_btnVariables, L"Toggle Variable Inspector", left, width);
    } else if (m_hoveredToolbarBtn == 8) {
        RenderTooltip(pRT, theme, config, m_btnVarTable, L"Inspect Array / DataFrame Table", left, width);
    } else if (m_hoveredToolbarBtn == 9) {
        RenderTooltip(pRT, theme, config, m_btnCutCell, L"Cut Cell", left, width);
    } else if (m_hoveredToolbarBtn == 10) {
        RenderTooltip(pRT, theme, config, m_btnCopyCell, L"Copy Cell", left, width);
    } else if (m_hoveredToolbarBtn == 11) {
        RenderTooltip(pRT, theme, config, m_btnPasteCell, L"Paste Cell Below", left, width);
    } else if (m_hoveredToolbarBtn == 12) {
        RenderTooltip(pRT, theme, config, m_btnRunCell, L"Run Active Cell (Shift+Enter)", left, width);
    } else if (m_hoveredToolbarBtn == 13 && !m_cellTypeDropdownOpen) {
        RenderTooltip(pRT, theme, config, m_btnCellTypePicker, L"Change Cell Type", left, width);
    } else if (m_hoveredToolbarBtn == 14) {
        RenderTooltip(pRT, theme, config, m_btnInterruptKernel, L"Interrupt / Stop Kernel", left, width);
    } else if (m_hoveredCellIdx >= 0 && nb && m_hoveredCellIdx < static_cast<int>(nb->cells.size()) &&
               m_hoveredBtn >= 12 && m_hoveredBtn <= 20) {
        const auto& hc = nb->cells[m_hoveredCellIdx];
        if (m_hoveredBtn == 12) RenderTooltip(pRT, theme, config, hc.mdBtnBoldBounds, L"Bold (Ctrl+B)", left, width);
        else if (m_hoveredBtn == 13) RenderTooltip(pRT, theme, config, hc.mdBtnItalicBounds, L"Italic (Ctrl+I)", left, width);
        else if (m_hoveredBtn == 14) RenderTooltip(pRT, theme, config, hc.mdBtnQuoteBounds, L"Blockquote", left, width);
        else if (m_hoveredBtn == 15) RenderTooltip(pRT, theme, config, hc.mdBtnLinkBounds, L"Insert Link", left, width);
        else if (m_hoveredBtn == 16) RenderTooltip(pRT, theme, config, hc.mdBtnImageBounds, L"Insert Image", left, width);
        else if (m_hoveredBtn == 17) RenderTooltip(pRT, theme, config, hc.mdBtnBulletListBounds, L"Bulleted List", left, width);
        else if (m_hoveredBtn == 18) RenderTooltip(pRT, theme, config, hc.mdBtnNumberListBounds, L"Numbered List", left, width);
        else if (m_hoveredBtn == 19) RenderTooltip(pRT, theme, config, hc.mdBtnHeadingBounds, L"Heading", left, width);
        else if (m_hoveredBtn == 20) RenderTooltip(pRT, theme, config, hc.mdBtnDoneBounds, L"Render Markdown Preview", left, width);
    }

    // 5b. Floating Cell Type Dropdown Menu (Code / Markdown)
    m_cellTypeDropdownBounds = {};
    m_cellTypeItemCodeBounds = {};
    m_cellTypeItemMdBounds = {};
    if (m_cellTypeDropdownOpen && nb && m_btnCellTypePicker.right > m_btnCellTypePicker.left) {
        const float dropW = config.ScaleF(148.0f);
        const float itemH = config.ScaleF(36.0f);
        const float padD = config.ScaleF(5.0f);
        const float dropH = itemH * 2.0f + padD * 2.0f;
        float dropX = m_btnCellTypePicker.left;
        if (dropX + dropW > left + width - config.ScaleF(8.0f)) {
            dropX = (std::max)(left + config.ScaleF(8.0f), left + width - dropW - config.ScaleF(8.0f));
        }
        float dropY = m_btnCellTypePicker.bottom + config.ScaleF(5.0f);
        m_cellTypeDropdownBounds = D2D1::RectF(dropX, dropY, dropX + dropW, dropY + dropH);

        // Subtle soft shadow rim + clean floating rounded card
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushShadow;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, dark ? 0.35f : 0.09f), &brushShadow);
        if (brushShadow) {
            D2D1_RECT_F rcSh = D2D1::RectF(dropX - 1.0f, dropY + 2.0f, dropX + dropW + 1.0f, dropY + dropH + 4.0f);
            pRT->FillRoundedRectangle(D2D1::RoundedRect(rcSh, config.ScaleF(9.0f), config.ScaleF(9.0f)), brushShadow.Get());
        }
        D2D1_ROUNDED_RECT rrcTypeDrop = D2D1::RoundedRect(m_cellTypeDropdownBounds, config.ScaleF(8.0f), config.ScaleF(8.0f));
        if (brushPillBg) pRT->FillRoundedRectangle(rrcTypeDrop, brushPillBg.Get());
        else if (m_brushNbPopupBg) pRT->FillRoundedRectangle(rrcTypeDrop, m_brushNbPopupBg.Get());
        if (brushBorderHair) pRT->DrawRoundedRectangle(rrcTypeDrop, brushBorderHair.Get(), 1.0f);

        m_cellTypeItemCodeBounds = D2D1::RectF(dropX + padD, dropY + padD, dropX + dropW - padD, dropY + padD + itemH);
        m_cellTypeItemMdBounds = D2D1::RectF(dropX + padD, dropY + padD + itemH, dropX + dropW - padD, dropY + padD + itemH * 2.0f);

        auto drawTypeRow = [&](const D2D1_RECT_F& rcRow, const wchar_t* label, bool isSelected, bool isHot) {
            if (isHot && brushBtnHover) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcRow, config.ScaleF(5.0f), config.ScaleF(5.0f)), brushBtnHover.Get());
            } else if (isSelected && brushBtnActiveBg) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcRow, config.ScaleF(5.0f), config.ScaleF(5.0f)), brushBtnActiveBg.Get());
            }
            D2D1_RECT_F rcTxt = D2D1::RectF(rcRow.left + config.ScaleF(12.0f), rcRow.top, rcRow.right - config.ScaleF(8.0f), rcRow.bottom);
            if (tbLeadFmt) {
                pRT->DrawText(label, static_cast<UINT32>(wcslen(label)), tbLeadFmt, rcTxt,
                              (isSelected || isHot) ? brushFgStrong.Get() : brushFgMuted.Get());
            }
        };
        drawTypeRow(m_cellTypeItemCodeBounds, L"Code", !activeIsMd, m_hoveredCellTypeDropdownIdx == 0);
        drawTypeRow(m_cellTypeItemMdBounds, L"Markdown", activeIsMd, m_hoveredCellTypeDropdownIdx == 1);
    }

    // 6. Floating Kernel Dropdown Menu (if open)
    m_kernelDropdownItemBounds.clear();
    m_kernelDropdownBounds = {};
    if (m_kernelDropdownOpen && nb) {
        const float dropW = config.ScaleF(360.0f);
        const float itemH = config.ScaleF(26.0f);
        const size_t maxVis = nb->discoveredPythonExes.empty()
            ? 1u
            : (std::min)(nb->discoveredPythonExes.size(), static_cast<size_t>(10));
        const float dropH = static_cast<float>(maxVis) * itemH + config.ScaleF(8.0f);
        float dropX = m_btnKernelPicker.left;
        if (dropX + dropW > left + width - config.ScaleF(8.0f)) {
            dropX = (std::max)(left + config.ScaleF(8.0f), left + width - dropW - config.ScaleF(8.0f));
        }
        float dropY = m_btnKernelPicker.bottom + config.ScaleF(4.0f);
        m_kernelDropdownBounds = D2D1::RectF(dropX, dropY, dropX + dropW, dropY + dropH);
        D2D1_ROUNDED_RECT rrcDrop = D2D1::RoundedRect(m_kernelDropdownBounds, 6.0f, 6.0f);
        if (m_brushNbPopupBg) pRT->FillRoundedRectangle(rrcDrop, m_brushNbPopupBg.Get());
        if (theme.brushBorderSubtle) pRT->DrawRoundedRectangle(rrcDrop, theme.brushBorderSubtle.Get(), 1.0f);

        if (nb->discoveredPythonExes.empty()) {
            D2D1_RECT_F rcTxt = D2D1::RectF(dropX + config.ScaleF(12.0f), dropY + config.ScaleF(4.0f),
                                             dropX + dropW - config.ScaleF(10.0f), dropY + itemH + config.ScaleF(4.0f));
            ID2D1Brush* errBrush = theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushTextPrimary.Get();
            if (errBrush && tbLeadFmt) {
                const wchar_t* msg = L"No Python detected \x2014 please install Python";
                pRT->DrawText(msg, static_cast<UINT32>(wcslen(msg)), tbLeadFmt, rcTxt, errBrush);
            }
        } else {
            for (size_t ki = 0; ki < maxVis; ++ki) {
                const auto& pyPath = nb->discoveredPythonExes[ki];
                D2D1_RECT_F rcItem = D2D1::RectF(dropX + config.ScaleF(4.0f),
                                                  dropY + config.ScaleF(4.0f) + static_cast<float>(ki) * itemH,
                                                  dropX + dropW - config.ScaleF(4.0f),
                                                  dropY + config.ScaleF(4.0f) + static_cast<float>(ki + 1) * itemH);
                m_kernelDropdownItemBounds.push_back(rcItem);
                bool isSel = (!nb->selectedPythonExe.empty() && _wcsicmp(nb->selectedPythonExe.c_str(), pyPath.c_str()) == 0);
                if (isSel || m_hoveredKernelDropdownIdx == static_cast<int>(ki)) {
                    ID2D1Brush* bgB = isSel && theme.brushBrandAccent ? theme.brushBrandAccent.Get() : m_brushNbRowHover.Get();
                    if (bgB) pRT->FillRoundedRectangle(D2D1::RoundedRect(rcItem, 4.0f, 4.0f), bgB);
                }
                std::wstring prefix = isSel ? L"\x2713 " : L"   ";
                std::wstring disp = prefix + pyPath;
                if (disp.size() > 48) {
                    disp = prefix + L"..." + pyPath.substr(pyPath.size() - 42);
                }
                D2D1_RECT_F rcTxt = D2D1::RectF(rcItem.left + config.ScaleF(8.0f), rcItem.top,
                                                 rcItem.right - config.ScaleF(6.0f), rcItem.bottom);
                if (tbLeadFmt) {
                    pRT->DrawText(disp.c_str(), static_cast<UINT32>(disp.length()), tbLeadFmt, rcTxt, theme.brushTextPrimary.Get());
                }
            }
        }
    }

    // 7. Variable Inspector Panel (when toggled on)
    m_varInspectBtnBounds.clear();
    if (nb && nb->showVariableInspector) {
        const float panW = (std::min)(config.ScaleF(360.0f), width * 0.40f);
        const float panLeft = left + width - panW - config.ScaleF(14.0f);
        const float panTop = contentTop + config.ScaleF(10.0f);
        const size_t rows = (std::min)(nb->inspectorVariables.size(), static_cast<size_t>(12));
        const float rowH = config.ScaleF(24.0f);
        const float hdrH = config.ScaleF(30.0f);
        const float panH = hdrH + static_cast<float>(rows > 0 ? rows : 1) * rowH + config.ScaleF(10.0f);
        D2D1_RECT_F rcVarPanel = D2D1::RectF(panLeft, panTop, panLeft + panW, panTop + panH);
        D2D1_ROUNDED_RECT rrcVar = D2D1::RoundedRect(rcVarPanel, 6.0f, 6.0f);
        if (m_brushNbPopupBg) pRT->FillRoundedRectangle(rrcVar, m_brushNbPopupBg.Get());
        if (theme.brushBorderSubtle) pRT->DrawRoundedRectangle(rrcVar, theme.brushBorderSubtle.Get(), 1.0f);
        D2D1_RECT_F rcVarHdr = D2D1::RectF(panLeft + config.ScaleF(12.0f), panTop + config.ScaleF(2.0f),
                                            panLeft + panW - config.ScaleF(10.0f), panTop + hdrH);
        std::wstring hdrTxt = L"{x} Variable Inspector (" + std::to_wstring(nb->inspectorVariables.size()) + L")";
        if (tbLeadFmt) {
            pRT->DrawText(hdrTxt.c_str(), static_cast<UINT32>(hdrTxt.length()), tbLeadFmt,
                          rcVarHdr, m_brushNbBlueText ? static_cast<ID2D1Brush*>(m_brushNbBlueText.Get()) : theme.brushTextPrimary.Get());
        }
        if (nb->inspectorVariables.empty()) {
            D2D1_RECT_F rcEmpty = D2D1::RectF(panLeft + config.ScaleF(12.0f), panTop + hdrH, panLeft + panW - config.ScaleF(10.0f), panTop + hdrH + rowH);
            const wchar_t* emptyMsg = L"No user variables in namespace";
            if (tbLeadFmt) {
                pRT->DrawText(emptyMsg, static_cast<UINT32>(wcslen(emptyMsg)), tbLeadFmt, rcEmpty, theme.brushTextSecondary.Get());
            }
        } else {
            const float btnTblW = config.ScaleF(52.0f);
            for (size_t vi = 0; vi < rows; ++vi) {
                const auto& v = nb->inspectorVariables[vi];
                float ry = panTop + hdrH + static_cast<float>(vi) * rowH;
                D2D1_RECT_F rcTblBtn = D2D1::RectF(panLeft + panW - btnTblW - config.ScaleF(8.0f), ry + config.ScaleF(2.0f),
                                                   panLeft + panW - config.ScaleF(8.0f), ry + rowH - config.ScaleF(2.0f));
                m_varInspectBtnBounds.push_back(rcTblBtn);
                std::wstring rowTxt = v.name + L" (" + v.typeName + L") = " + v.valuePreview;
                if (rowTxt.size() > 36) rowTxt = rowTxt.substr(0, 33) + L"...";
                D2D1_RECT_F rcR = D2D1::RectF(panLeft + config.ScaleF(12.0f), ry, rcTblBtn.left - config.ScaleF(6.0f), ry + rowH);
                if (tbLeadFmt) {
                    pRT->DrawText(rowTxt.c_str(), static_cast<UINT32>(rowTxt.length()), tbLeadFmt, rcR, theme.brushTextPrimary.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                D2D1_ROUNDED_RECT rrcTb = D2D1::RoundedRect(rcTblBtn, 3.0f, 3.0f);
                if (m_hoveredVarInspectIdx == static_cast<int>(vi) && theme.brushItemHover) {
                    pRT->FillRoundedRectangle(rrcTb, theme.brushItemHover.Get());
                }
                if (theme.brushBorderSubtle) pRT->DrawRoundedRectangle(rrcTb, theme.brushBorderSubtle.Get(), 0.8f);
                if (tbBtnFmt && m_brushNbBlueText) {
                    pRT->DrawText(L"Table", 5, tbBtnFmt, rcTblBtn, m_brushNbBlueText.Get());
                }
            }
        }
    }

    // 7b. Hover diagnostic tooltip on squiggled code spans or hovered Markdown hyperlink
    if (m_hoveredDiagIdx >= 0 && m_hoveredDiagIdx < static_cast<int>(m_visibleDiagHits.size()) &&
        !m_kernelDropdownOpen && !m_cellTypeDropdownOpen && !m_nbAutocompleteVisible && m_hoveredToolbarBtn == 0) {
        const auto& dh = m_visibleDiagHits[static_cast<size_t>(m_hoveredDiagIdx)];
        if (!dh.message.empty()) {
            RenderTooltip(pRT, theme, config, dh.bounds, dh.message, left, width);
        }
    } else if (m_hoveredMdLinkIdx >= 0 && m_hoveredMdLinkIdx < static_cast<int>(m_visibleMdLinks.size()) &&
               !m_kernelDropdownOpen && !m_cellTypeDropdownOpen && !m_nbAutocompleteVisible && m_hoveredToolbarBtn == 0) {
        const auto& lk = m_visibleMdLinks[static_cast<size_t>(m_hoveredMdLinkIdx)];
        if (!lk.url.empty()) {
            RenderTooltip(pRT, theme, config, lk.bounds, L"Open " + lk.url, left, width);
        }
    }

    // 7c. Active Cell IntelliSense Autocomplete Popup (reuses EditorCanvas popup renderer)
    m_nbAutocompleteBounds = {};
    if (m_nbAutocompleteVisible && !m_nbAutocompleteItems.empty() && !m_lightboxOpen && !m_varTableModal.isOpen) {
        EditorCanvas::RenderAutocompletePopupPanel(
            pRT, theme, config,
            m_nbAutocompleteItems, m_nbAutocompleteSelectedIdx, m_nbAutocompleteScrollOffset,
            m_lastCaretX, m_lastCaretY, m_lastLineH, top + config.ScaleF(44.0f),
            left + width, top + height, m_nbAutocompleteBounds, m_icons);
    }

    // 8. Plot/Image Lightbox Modal Overlay OR Variable Table Spreadsheet Drilldown Modal (I3)
    if (m_varTableModal.isOpen) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> backdropB, hdrBgB, zebraB, cellTxtB, gridLineB;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.02f, 0.02f, 0.04f, 0.72f), &backdropB);
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.16f, 0.21f, 1.0f) : D2D1::ColorF(0.92f, 0.94f, 0.97f, 1.0f), &hdrBgB);
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.03f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.025f), &zebraB);
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.86f, 0.89f, 0.94f, 1.0f) : D2D1::ColorF(0.14f, 0.16f, 0.20f, 1.0f), &cellTxtB);
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.09f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.10f), &gridLineB);
        if (backdropB) pRT->FillRectangle(D2D1::RectF(left, top, left + width, top + height), backdropB.Get());

        const float modW = (std::min)(width - config.ScaleF(48.0f), config.ScaleF(860.0f));
        const float modH = (std::min)(height - config.ScaleF(48.0f), config.ScaleF(560.0f));
        const float modX = left + (width - modW) * 0.5f;
        const float modY = top + (height - modH) * 0.5f;
        m_varTableModal.rcModal = D2D1::RectF(modX, modY, modX + modW, modY + modH);
        D2D1_ROUNDED_RECT rrcMod = D2D1::RoundedRect(m_varTableModal.rcModal, config.ScaleF(8.0f), config.ScaleF(8.0f));
        if (m_brushNbPopupBg) pRT->FillRoundedRectangle(rrcMod, m_brushNbPopupBg.Get());
        if (theme.brushBorderHover) pRT->DrawRoundedRectangle(rrcMod, theme.brushBorderHover.Get(), 1.2f);

        const float topBarH = config.ScaleF(44.0f);
        m_varTableModal.rcCloseBtn = D2D1::RectF(modX + modW - config.ScaleF(38.0f), modY + config.ScaleF(8.0f),
                                                modX + modW - config.ScaleF(10.0f), modY + config.ScaleF(36.0f));
        D2D1_RECT_F rcTitle = D2D1::RectF(modX + config.ScaleF(16.0f), modY + config.ScaleF(6.0f),
                                          m_varTableModal.rcCloseBtn.left - config.ScaleF(8.0f), modY + topBarH);
        std::wstring titleStr = L"\u229E Variable Table: " + m_varTableModal.varName;
        if (!m_varTableModal.typeName.empty()) titleStr += L"  [" + m_varTableModal.typeName + L"]";
        if (!m_varTableModal.shapeLabel.empty()) titleStr += L"  " + m_varTableModal.shapeLabel;
        if (m_varTableModal.truncated) titleStr += L"  (bounded preview slice)";
        IDWriteTextFormat* titleFmt = m_fmtNbModalTitle ? m_fmtNbModalTitle.Get() : tbBtnFmt;
        if (titleFmt && cellTxtB) {
            pRT->DrawText(titleStr.c_str(), static_cast<UINT32>(titleStr.size()), titleFmt, rcTitle, cellTxtB.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        if (theme.brushItemHover) pRT->FillRoundedRectangle(D2D1::RoundedRect(m_varTableModal.rcCloseBtn, 5.0f, 5.0f), theme.brushItemHover.Get());
        if (tbBtnFmt && cellTxtB) pRT->DrawText(L"\u2715", 1, tbBtnFmt, m_varTableModal.rcCloseBtn, cellTxtB.Get());
        if (gridLineB) pRT->DrawLine(D2D1::Point2F(modX, modY + topBarH), D2D1::Point2F(modX + modW, modY + topBarH), gridLineB.Get(), 1.0f);

        m_varTableModal.rcViewport = D2D1::RectF(modX + config.ScaleF(10.0f), modY + topBarH + config.ScaleF(6.0f),
                                                modX + modW - config.ScaleF(10.0f), modY + modH - config.ScaleF(12.0f));
        pRT->PushAxisAlignedClip(m_varTableModal.rcViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        if (m_varTableModal.isLoading) {
            const wchar_t* ld = L"Querying live notebook kernel variable slice...";
            if (tbLeadFmt && theme.brushTextSecondary) pRT->DrawText(ld, static_cast<UINT32>(wcslen(ld)), tbLeadFmt, m_varTableModal.rcViewport, theme.brushTextSecondary.Get());
        } else if (!m_varTableModal.statusMessage.empty()) {
            if (tbLeadFmt && theme.brushTextSecondary) {
                pRT->DrawText(m_varTableModal.statusMessage.c_str(), static_cast<UINT32>(m_varTableModal.statusMessage.size()),
                              tbLeadFmt, m_varTableModal.rcViewport, theme.brushTextSecondary.Get());
            }
        } else if (!m_varTableModal.columns.empty()) {
            const float rowH = config.ScaleF(24.0f);
            const float hdrRowH = config.ScaleF(28.0f);
            const size_t nCols = m_varTableModal.columns.size();
            const float defaultColW = config.ScaleF(110.0f);
            float totalColsW = 0.0f;
            for (size_t c = 0; c < nCols; ++c) {
                float cw = (c < m_varTableModal.colWidths.size() && m_varTableModal.colWidths[c] > 0.0f)
                    ? m_varTableModal.colWidths[c]
                    : (c == 0 ? config.ScaleF(76.0f) : defaultColW);
                totalColsW += cw;
            }
            const float viewW = m_varTableModal.rcViewport.right - m_varTableModal.rcViewport.left;
            const float viewH = m_varTableModal.rcViewport.bottom - m_varTableModal.rcViewport.top;
            m_varTableModal.maxScrollX = (std::max)(0.0f, totalColsW - viewW);
            m_varTableModal.scrollX = (std::clamp)(m_varTableModal.scrollX, 0.0f, m_varTableModal.maxScrollX);
            const float bodyH = (std::max)(0.0f, viewH - hdrRowH);
            const float totalRowsH = static_cast<float>(m_varTableModal.rows.size()) * rowH;
            m_varTableModal.maxScrollY = (std::max)(0.0f, totalRowsH - bodyH);
            m_varTableModal.scrollY = (std::clamp)(m_varTableModal.scrollY, 0.0f, m_varTableModal.maxScrollY);

            // Column Header Bar
            float hx = m_varTableModal.rcViewport.left - m_varTableModal.scrollX;
            const float hy = m_varTableModal.rcViewport.top;
            if (hdrBgB) pRT->FillRectangle(D2D1::RectF(m_varTableModal.rcViewport.left, hy, m_varTableModal.rcViewport.right, hy + hdrRowH), hdrBgB.Get());
            for (size_t c = 0; c < nCols; ++c) {
                float cw = (c == 0 ? config.ScaleF(76.0f) : defaultColW);
                D2D1_RECT_F rcHdrCell = D2D1::RectF(hx + config.ScaleF(6.0f), hy, hx + cw - config.ScaleF(4.0f), hy + hdrRowH);
                if (tbLeadFmt && m_brushNbBlueText) {
                    pRT->DrawText(m_varTableModal.columns[c].c_str(), static_cast<UINT32>(m_varTableModal.columns[c].size()),
                                  tbLeadFmt, rcHdrCell, m_brushNbBlueText.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                if (gridLineB) pRT->DrawLine(D2D1::Point2F(hx + cw, hy), D2D1::Point2F(hx + cw, m_varTableModal.rcViewport.bottom), gridLineB.Get(), 0.8f);
                hx += cw;
            }
            if (gridLineB) pRT->DrawLine(D2D1::Point2F(m_varTableModal.rcViewport.left, hy + hdrRowH), D2D1::Point2F(m_varTableModal.rcViewport.right, hy + hdrRowH), gridLineB.Get(), 1.0f);

            // Visible Body Rows
            D2D1_RECT_F rcBodyClip = D2D1::RectF(m_varTableModal.rcViewport.left, hy + hdrRowH, m_varTableModal.rcViewport.right, m_varTableModal.rcViewport.bottom);
            pRT->PushAxisAlignedClip(rcBodyClip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            size_t firstRow = static_cast<size_t>((std::max)(0.0f, std::floor(m_varTableModal.scrollY / rowH)));
            for (size_t ri = firstRow; ri < m_varTableModal.rows.size(); ++ri) {
                float ry = hy + hdrRowH + static_cast<float>(ri) * rowH - m_varTableModal.scrollY;
                if (ry >= m_varTableModal.rcViewport.bottom) break;
                if (ry + rowH <= hy + hdrRowH) continue;
                if ((ri % 2) == 1 && zebraB) {
                    pRT->FillRectangle(D2D1::RectF(m_varTableModal.rcViewport.left, ry, m_varTableModal.rcViewport.right, ry + rowH), zebraB.Get());
                }
                if (gridLineB) {
                    pRT->DrawLine(D2D1::Point2F(m_varTableModal.rcViewport.left, ry + rowH), D2D1::Point2F(m_varTableModal.rcViewport.right, ry + rowH), gridLineB.Get(), 0.6f);
                }
                float cx = m_varTableModal.rcViewport.left - m_varTableModal.scrollX;
                const auto& rowVals = m_varTableModal.rows[ri];
                for (size_t c = 0; c < nCols; ++c) {
                    float cw = (c == 0 ? config.ScaleF(76.0f) : defaultColW);
                    if (c < rowVals.size() && cx + cw >= m_varTableModal.rcViewport.left && cx <= m_varTableModal.rcViewport.right) {
                        D2D1_RECT_F rcCell = D2D1::RectF(cx + config.ScaleF(6.0f), ry, cx + cw - config.ScaleF(4.0f), ry + rowH);
                        ID2D1Brush* useB = (c == 0 && theme.brushTextSecondary) ? theme.brushTextSecondary.Get() : cellTxtB.Get();
                        if (tbLeadFmt && useB) {
                            pRT->DrawText(rowVals[c].c_str(), static_cast<UINT32>(rowVals[c].size()), tbLeadFmt, rcCell, useB, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                        }
                    }
                    cx += cw;
                }
            }
            pRT->PopAxisAlignedClip();
        }
        pRT->PopAxisAlignedClip();
    } else if (m_lightboxOpen) {
        RenderImageLightbox(pRT, theme, config, left, width, top, height);
    }
}

bool NotebookView::HandleLButtonDown(POINT clientPt, editor::OpenDocument& doc, const app::AppConfig& config, HWND hwnd) {
    if (!doc.notebookDoc) return false;
    if (hwnd && IsWindow(hwnd)) m_hostHwnd = hwnd;
    auto nb = doc.notebookDoc;

    auto PointInRect = [](POINT pt, const D2D1_RECT_F& r) -> bool {
        return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
    };

    // Variable Table Drilldown modal or Lightbox modal takes highest priority over notebook canvas
    if (m_varTableModal.isOpen) {
        if (PointInRect(clientPt, m_varTableModal.rcCloseBtn) || !PointInRect(clientPt, m_varTableModal.rcModal)) {
            CloseVariableTableModal();
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        return true;
    }

    if (m_lightboxOpen) {
        if (PointInRect(clientPt, m_lightboxBtnClose) || !PointInRect(clientPt, m_lightboxCardBounds)) {
            m_lightboxOpen = false;
            m_lightboxDragging = false;
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (PointInRect(clientPt, m_lightboxBtnZoomOut)) {
            m_lightboxZoom = (std::max)(0.25f, m_lightboxZoom * 0.8f);
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (PointInRect(clientPt, m_lightboxBtnZoomIn)) {
            m_lightboxZoom = (std::min)(4.0f, m_lightboxZoom * 1.25f);
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (PointInRect(clientPt, m_lightboxBtnReset)) {
            m_lightboxZoom = 1.0f;
            m_lightboxPanX = 0.0f;
            m_lightboxPanY = 0.0f;
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (PointInRect(clientPt, m_lightboxBtnCopy)) {
            CopyPngToClipboard(m_lightboxPngBytes, hwnd);
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (PointInRect(clientPt, m_lightboxBtnSave)) {
            SavePngWithDialog(m_lightboxPngBytes, hwnd);
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (PointInRect(clientPt, m_lightboxCanvasBounds)) {
            m_lightboxDragging = true;
            m_lightboxDragStart = clientPt;
            m_lightboxPanStartX = m_lightboxPanX;
            m_lightboxPanStartY = m_lightboxPanY;
            if (hwnd) SetCapture(hwnd);
            return true;
        }
        return true;
    }

    // Active Cell IntelliSense Autocomplete Popup click handling
    if (IsAutocompleteVisible()) {
        int hitIdx = -1;
        if (EditorCanvas::HitTestAutocompletePanel(
                clientPt, m_nbAutocompleteBounds, m_nbAutocompleteItems.size(), m_nbAutocompleteScrollOffset, hitIdx)) {
            if (hitIdx >= 0 && hitIdx < static_cast<int>(m_nbAutocompleteItems.size())) {
                m_nbAutocompleteSelectedIdx = static_cast<size_t>(hitIdx);
                m_nbAutocompleteNavigated = true;
                CommitAutocompleteSelection(doc, hwnd);
            } else {
                DismissAutocomplete();
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            }
            return true;
        }
        DismissAutocomplete();
    }

    // Check scrollbar thumb
    if (clientPt.x >= m_scrollThumb.left && clientPt.x <= m_scrollThumb.right &&
        clientPt.y >= m_scrollThumb.top && clientPt.y <= m_scrollThumb.bottom) {
        m_isDraggingScrollbar = true;
        m_dragScrollStartY = static_cast<float>(clientPt.y);
        m_dragScrollStartOffset = m_scrollY;
        if (hwnd) SetCapture(hwnd);
        return true;
    }

    // Check cell output scrollbars (Vertical & Horizontal dragging)
    for (size_t ci = 0; ci < nb->cells.size(); ++ci) {
        auto& cell = nb->cells[ci];
        if (cell.outputMaxScrollY > 0.0f) {
            if (PointInRect(clientPt, cell.outputVScrollThumbBounds)) {
                m_isDraggingOutputVScroll = true;
                m_dragOutputCellIdx = static_cast<int>(ci);
                m_dragOutputStartPos = static_cast<float>(clientPt.y);
                m_dragOutputStartOffset = cell.outputScrollY;
                if (hwnd) SetCapture(hwnd);
                return true;
            } else if (PointInRect(clientPt, cell.outputVScrollTrackBounds)) {
                float step = config.ScaleF(80.0f);
                if (clientPt.y < cell.outputVScrollThumbBounds.top) {
                    cell.outputScrollY = (std::max)(0.0f, cell.outputScrollY - step);
                } else if (clientPt.y > cell.outputVScrollThumbBounds.bottom) {
                    cell.outputScrollY = (std::min)(cell.outputMaxScrollY, cell.outputScrollY + step);
                }
                return true;
            }
        }
        if (cell.outputMaxScrollX > 0.0f) {
            if (PointInRect(clientPt, cell.outputHScrollThumbBounds)) {
                m_isDraggingOutputHScroll = true;
                m_dragOutputCellIdx = static_cast<int>(ci);
                m_dragOutputStartPos = static_cast<float>(clientPt.x);
                m_dragOutputStartOffset = cell.outputScrollX;
                if (hwnd) SetCapture(hwnd);
                return true;
            } else if (PointInRect(clientPt, cell.outputHScrollTrackBounds)) {
                float step = config.ScaleF(80.0f);
                if (clientPt.x < cell.outputHScrollThumbBounds.left) {
                    cell.outputScrollX = (std::max)(0.0f, cell.outputScrollX - step);
                } else if (clientPt.x > cell.outputHScrollThumbBounds.right) {
                    cell.outputScrollX = (std::min)(cell.outputMaxScrollX, cell.outputScrollX + step);
                }
                return true;
            }
        }
        if (cell.codeMaxScrollX > 0.0f) {
            if (PointInRect(clientPt, cell.codeHScrollThumbBounds)) {
                m_isDraggingCodeHScroll = true;
                m_dragCodeCellIdx = static_cast<int>(ci);
                m_dragCodeStartPos = static_cast<float>(clientPt.x);
                m_dragCodeStartOffset = cell.codeScrollX;
                if (hwnd) SetCapture(hwnd);
                return true;
            } else if (PointInRect(clientPt, cell.codeHScrollTrackBounds)) {
                float step = config.ScaleF(80.0f);
                if (clientPt.x < cell.codeHScrollThumbBounds.left) {
                    cell.codeScrollX = (std::max)(0.0f, cell.codeScrollX - step);
                } else if (clientPt.x > cell.codeHScrollThumbBounds.right) {
                    cell.codeScrollX = (std::min)(cell.codeMaxScrollX, cell.codeScrollX + step);
                }
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }
        }
    }

    // Check Floating Cell Type Dropdown (Code / Markdown)
    if (m_cellTypeDropdownOpen) {
        if (PointInRect(clientPt, m_cellTypeItemCodeBounds)) {
            int idx = (nb->activeCellIdx >= 0 && nb->activeCellIdx < static_cast<int>(nb->cells.size())) ? nb->activeCellIdx : 0;
            nb->SetCellType(idx, editor::NotebookCellType::Code);
            m_cellTypeDropdownOpen = false;
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (PointInRect(clientPt, m_cellTypeItemMdBounds)) {
            int idx = (nb->activeCellIdx >= 0 && nb->activeCellIdx < static_cast<int>(nb->cells.size())) ? nb->activeCellIdx : 0;
            nb->SetCellType(idx, editor::NotebookCellType::Markdown);
            m_cellTypeDropdownOpen = false;
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (!PointInRect(clientPt, m_btnCellTypePicker)) {
            m_cellTypeDropdownOpen = false;
            if (PointInRect(clientPt, m_cellTypeDropdownBounds)) {
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    // Check Top Toolbar buttons & floating Kernel dropdown
    if (m_kernelDropdownOpen) {
        for (size_t ki = 0; ki < m_kernelDropdownItemBounds.size() && ki < nb->discoveredPythonExes.size(); ++ki) {
            if (PointInRect(clientPt, m_kernelDropdownItemBounds[ki])) {
                nb->selectedPythonExe = nb->discoveredPythonExes[ki];
                nb->userSelectedKernel = true;
                nb->discoveredWorkspaceVersion = m_workspaceVersion;
                m_kernelDropdownOpen = false;
                RestartKernel(doc, hwnd);
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }
        }
        if (!PointInRect(clientPt, m_btnKernelPicker)) {
            m_kernelDropdownOpen = false;
            if (PointInRect(clientPt, m_kernelDropdownBounds)) {
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    if (PointInRect(clientPt, m_btnRunAll)) {
        m_cellTypeDropdownOpen = false;
        if (m_isExecuting.load()) {
            StopExecution(doc);
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            RunAllCells(doc, hwnd);
        }
        return true;
    }
    if (PointInRect(clientPt, m_btnRunCell)) {
        m_cellTypeDropdownOpen = false;
        int idx = (nb->activeCellIdx >= 0 && nb->activeCellIdx < static_cast<int>(nb->cells.size())) ? nb->activeCellIdx : 0;
        if (idx >= 0 && idx < static_cast<int>(nb->cells.size())) {
            if (nb->cells[idx].type == editor::NotebookCellType::Markdown) {
                nb->cells[idx].isEditingMarkdown = !nb->cells[idx].isEditingMarkdown;
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            } else if (nb->cells[idx].isExecuting) {
                StopExecution(doc);
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            } else {
                RunCell(idx, doc, hwnd);
            }
        }
        return true;
    }
    if (PointInRect(clientPt, m_btnCutCell)) {
        m_cellTypeDropdownOpen = false;
        int idx = (nb->activeCellIdx >= 0 && nb->activeCellIdx < static_cast<int>(nb->cells.size())) ? nb->activeCellIdx : 0;
        if (idx >= 0 && idx < static_cast<int>(nb->cells.size())) {
            m_cellClipboard.hasCell = true;
            m_cellClipboard.type = nb->cells[idx].type;
            m_cellClipboard.language = nb->cells[idx].language;
            m_cellClipboard.text = nb->cells[idx].inputBuffer.GetText();
            SetClipboardText(m_cellClipboard.text, hwnd);
            nb->DeleteCell(idx);
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        }
        return true;
    }
    if (PointInRect(clientPt, m_btnCopyCell)) {
        m_cellTypeDropdownOpen = false;
        int idx = (nb->activeCellIdx >= 0 && nb->activeCellIdx < static_cast<int>(nb->cells.size())) ? nb->activeCellIdx : 0;
        if (idx >= 0 && idx < static_cast<int>(nb->cells.size())) {
            m_cellClipboard.hasCell = true;
            m_cellClipboard.type = nb->cells[idx].type;
            m_cellClipboard.language = nb->cells[idx].language;
            m_cellClipboard.text = nb->cells[idx].inputBuffer.GetText();
            SetClipboardText(m_cellClipboard.text, hwnd);
        }
        return true;
    }
    if (PointInRect(clientPt, m_btnPasteCell)) {
        m_cellTypeDropdownOpen = false;
        int targetIdx = nb->activeCellIdx >= 0 ? nb->activeCellIdx : static_cast<int>(nb->cells.size()) - 1;
        editor::NotebookCellType pasteType = m_cellClipboard.hasCell ? m_cellClipboard.type : editor::NotebookCellType::Code;
        std::wstring pasteText = m_cellClipboard.hasCell ? m_cellClipboard.text : GetClipboardText(hwnd);
        if (m_cellClipboard.hasCell || !pasteText.empty()) {
            if (auto* inserted = nb->AddCell(pasteType, targetIdx)) {
                inserted->inputBuffer.SetText(pasteText);
                if (m_cellClipboard.hasCell && !m_cellClipboard.language.empty()) {
                    inserted->language = m_cellClipboard.language;
                }
            }
            m_scrollActiveCellIntoView = true;
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        }
        return true;
    }
    if (PointInRect(clientPt, m_btnCellTypePicker)) {
        m_kernelDropdownOpen = false;
        m_cellTypeDropdownOpen = !m_cellTypeDropdownOpen;
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (PointInRect(clientPt, m_btnClearAll)) {
        m_cellTypeDropdownOpen = false;
        nb->ClearAllOutputs();
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (PointInRect(clientPt, m_btnInterruptKernel)) {
        m_cellTypeDropdownOpen = false;
        StopExecution(doc);
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (PointInRect(clientPt, m_btnAddCode)) {
        m_cellTypeDropdownOpen = false;
        int targetIdx = nb->activeCellIdx >= 0 ? nb->activeCellIdx : static_cast<int>(nb->cells.size()) - 1;
        nb->AddCell(editor::NotebookCellType::Code, targetIdx);
        m_scrollActiveCellIntoView = true;
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (PointInRect(clientPt, m_btnAddText)) {
        m_cellTypeDropdownOpen = false;
        int targetIdx = nb->activeCellIdx >= 0 ? nb->activeCellIdx : static_cast<int>(nb->cells.size()) - 1;
        nb->AddCell(editor::NotebookCellType::Markdown, targetIdx);
        m_scrollActiveCellIntoView = true;
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (PointInRect(clientPt, m_btnKernelPicker)) {
        m_cellTypeDropdownOpen = false;
        m_kernelDropdownOpen = !m_kernelDropdownOpen;
        if (m_kernelDropdownOpen) {
            RefreshPythonKernels(doc);
        }
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (PointInRect(clientPt, m_btnRestartKernel)) {
        m_cellTypeDropdownOpen = false;
        m_kernelDropdownOpen = false;
        RestartKernel(doc, hwnd);
        return true;
    }
    if (PointInRect(clientPt, m_btnVariables)) {
        m_kernelDropdownOpen = false;
        nb->showVariableInspector = !nb->showVariableInspector;
        if (nb->showVariableInspector) {
            QueryVariablesAsync(nb, hwnd);
        }
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (PointInRect(clientPt, m_btnVarTable)) {
        m_kernelDropdownOpen = false;
        if (!nb->inspectorVariables.empty()) {
            const editor::NotebookVariableInfo* pick = &nb->inspectorVariables[0];
            for (const auto& v : nb->inspectorVariables) {
                if (v.typeName.find(L"DataFrame") != std::wstring::npos ||
                    v.typeName.find(L"ndarray") != std::wstring::npos ||
                    v.typeName.find(L"Series") != std::wstring::npos) {
                    pick = &v;
                    break;
                }
            }
            InspectVariableTableAsync(pick->name, pick->typeName, hwnd);
        } else {
            nb->showVariableInspector = true;
            QueryVariablesAsync(nb, hwnd);
            std::lock_guard<std::recursive_mutex> lk(m_outputsMutex);
            m_varTableModal.isOpen = true;
            m_varTableModal.isLoading = false;
            m_varTableModal.varName = L"(namespace)";
            m_varTableModal.columns.clear();
            m_varTableModal.rows.clear();
            m_varTableModal.statusMessage = L"Run a code cell with an array, DataFrame, list, or dict first, then click [Table] on a variable.";
        }
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }

    if (nb->showVariableInspector && !m_varInspectBtnBounds.empty()) {
        for (size_t vi = 0; vi < m_varInspectBtnBounds.size() && vi < nb->inspectorVariables.size(); ++vi) {
            if (PointInRect(clientPt, m_varInspectBtnBounds[vi])) {
                const auto& v = nb->inspectorVariables[vi];
                InspectVariableTableAsync(v.name, v.typeName, hwnd);
                return true;
            }
        }
    }

    // Check Bottom hover pill buttons (+ Code, + Text)
    for (size_t ci = 0; ci < nb->cells.size(); ++ci) {
        auto& cell = nb->cells[ci];
        if (PointInRect(clientPt, cell.bottomAddCodeBounds)) {
            nb->AddCell(editor::NotebookCellType::Code, static_cast<int>(ci));
            m_scrollActiveCellIntoView = true;
            m_hoveredBottomCellIdx = -1;
            m_hoveredBottomBtn = 0;
            return true;
        }
        if (PointInRect(clientPt, cell.bottomAddTextBounds)) {
            nb->AddCell(editor::NotebookCellType::Markdown, static_cast<int>(ci));
            m_scrollActiveCellIntoView = true;
            m_hoveredBottomCellIdx = -1;
            m_hoveredBottomBtn = 0;
            return true;
        }
    }

    // Check cell buttons and cards (including floating top-right pill overhang)
    for (size_t ci = 0; ci < nb->cells.size(); ++ci) {
        auto& cell = nb->cells[ci];
        const bool hitFloatingPill =
            PointInRect(clientPt, cell.deleteBtnBounds) || PointInRect(clientPt, cell.moveDownBtnBounds) ||
            PointInRect(clientPt, cell.moveUpBtnBounds) || PointInRect(clientPt, cell.duplicateBtnBounds) ||
            PointInRect(clientPt, cell.dragHandleBounds) || PointInRect(clientPt, cell.collapseInputBtnBounds) ||
            PointInRect(clientPt, cell.editMarkdownBtnBounds);
        if (PointInRect(clientPt, cell.bounds) || hitFloatingPill) {
            for (size_t oi = 0; oi < nb->cells.size(); ++oi) {
                if (oi != ci && nb->cells[oi].type == editor::NotebookCellType::Markdown && nb->cells[oi].isEditingMarkdown) {
                    if (!nb->cells[oi].inputBuffer.GetText().empty()) {
                        nb->cells[oi].isEditingMarkdown = false;
                    }
                }
            }
            nb->activeCellIdx = static_cast<int>(ci);

            // Markdown bottom formatting toolbar clicks
            if (cell.type == editor::NotebookCellType::Markdown && cell.isEditingMarkdown) {
                if (PointInRect(clientPt, cell.mdBtnBoldBounds)) {
                    ApplyMarkdownFormat(*nb, static_cast<int>(ci), MarkdownFormatAction::Bold, hwnd);
                    return true;
                }
                if (PointInRect(clientPt, cell.mdBtnItalicBounds)) {
                    ApplyMarkdownFormat(*nb, static_cast<int>(ci), MarkdownFormatAction::Italic, hwnd);
                    return true;
                }
                if (PointInRect(clientPt, cell.mdBtnQuoteBounds)) {
                    ApplyMarkdownFormat(*nb, static_cast<int>(ci), MarkdownFormatAction::Quote, hwnd);
                    return true;
                }
                if (PointInRect(clientPt, cell.mdBtnLinkBounds)) {
                    ApplyMarkdownFormat(*nb, static_cast<int>(ci), MarkdownFormatAction::Link, hwnd);
                    return true;
                }
                if (PointInRect(clientPt, cell.mdBtnImageBounds)) {
                    ApplyMarkdownFormat(*nb, static_cast<int>(ci), MarkdownFormatAction::Image, hwnd);
                    return true;
                }
                if (PointInRect(clientPt, cell.mdBtnBulletListBounds)) {
                    ApplyMarkdownFormat(*nb, static_cast<int>(ci), MarkdownFormatAction::BulletList, hwnd);
                    return true;
                }
                if (PointInRect(clientPt, cell.mdBtnNumberListBounds)) {
                    ApplyMarkdownFormat(*nb, static_cast<int>(ci), MarkdownFormatAction::NumberList, hwnd);
                    return true;
                }
                if (PointInRect(clientPt, cell.mdBtnHeadingBounds)) {
                    ApplyMarkdownFormat(*nb, static_cast<int>(ci), MarkdownFormatAction::Heading, hwnd);
                    return true;
                }
                if (PointInRect(clientPt, cell.mdBtnDoneBounds)) {
                    cell.isEditingMarkdown = false;
                    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                    return true;
                }
            }

            // Run button: morphs into stop if executing, or cancels queue if queued
            if (PointInRect(clientPt, cell.runBtnBounds)) {
                if (cell.isExecuting) {
                    StopExecution(doc);
                    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                } else if (cell.isQueued) {
                    {
                        std::lock_guard<std::mutex> qLock(m_queueMutex);
                        m_cellQueue.erase(std::remove(m_cellQueue.begin(), m_cellQueue.end(), static_cast<int>(ci)), m_cellQueue.end());
                    }
                    cell.isQueued = false;
                    cell.executionCount = -1;
                    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                } else {
                    RunCell(static_cast<int>(ci), doc, hwnd);
                }
                return true;
            }
            // Collapse / Expand Cell Input
            if (PointInRect(clientPt, cell.collapseInputBtnBounds)) {
                cell.isInputCollapsed = !cell.isInputCollapsed;
                cell.ClearSelection();
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }
            // Edit / Done toggle for Markdown cell
            if (cell.type == editor::NotebookCellType::Markdown && PointInRect(clientPt, cell.editMarkdownBtnBounds)) {
                cell.isEditingMarkdown = !cell.isEditingMarkdown;
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }
            // Duplicate cell
            if (PointInRect(clientPt, cell.duplicateBtnBounds)) {
                nb->DuplicateCell(static_cast<int>(ci));
                m_scrollActiveCellIntoView = true;
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }
            // Delete button
            if (PointInRect(clientPt, cell.deleteBtnBounds)) {
                nb->DeleteCell(static_cast<int>(ci));
                return true;
            }
            // Move Up
            if (PointInRect(clientPt, cell.moveUpBtnBounds)) {
                if (ci > 0) nb->MoveCell(static_cast<int>(ci), static_cast<int>(ci - 1));
                return true;
            }
            // Move Down
            if (PointInRect(clientPt, cell.moveDownBtnBounds)) {
                if (ci + 1 < nb->cells.size()) nb->MoveCell(static_cast<int>(ci), static_cast<int>(ci + 1));
                return true;
            }

            // Output action bar buttons ([Copy] [Hide] [Clear])
            if (PointInRect(clientPt, cell.outputClearBtnBounds)) {
                std::lock_guard<std::recursive_mutex> viewLock(m_outputsMutex);
                std::unique_lock<std::recursive_mutex> docLock(nb->outputsMutex ? *nb->outputsMutex : m_outputsMutex);
                cell.outputs.clear();
                cell.hasError = false;
                cell.errorLine = -1;
                cell.isOutputCollapsed = false;
                cell.outputScrollY = 0.0f;
                cell.outputScrollX = 0.0f;
                cell.ClearOutputSelection();
                nb->isDirty = true;
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }
            if (PointInRect(clientPt, cell.outputCollapseBtnBounds)) {
                cell.isOutputCollapsed = !cell.isOutputCollapsed;
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }
            if (PointInRect(clientPt, cell.outputCopyBtnBounds)) {
                std::wstring allOut;
                for (const auto& out : cell.outputs) {
                    if (!out.text.empty()) {
                        if (!allOut.empty() && allOut.back() != L'\n') allOut += L"\n";
                        allOut += editor::NotebookDocument::StripAnsi(out.text);
                    }
                }
                if (!allOut.empty()) {
                    SetClipboardText(allOut, hwnd);
                    m_copiedFeedbackCellIdx = static_cast<int>(ci);
                    m_copiedFeedbackUntil = GetTickCount64() + 1800;
                }
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                return true;
            }

            // Drag grip inside floating pill must be checked before codeAreaBounds (since pill straddles top edge)
            if (PointInRect(clientPt, cell.dragHandleBounds)) {
                m_dragCandidateCellIdx = static_cast<int>(ci);
                m_dragStartPt = clientPt;
                if (hwnd) SetCapture(hwnd);
                return true;
            }

            // Click inside Markdown preview or code area
            if (PointInRect(clientPt, cell.codeAreaBounds)) {
                if (cell.isInputCollapsed) {
                    cell.isInputCollapsed = false;
                    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                    return true;
                }
                // Check inline color preview swatch click before placing text caret
                for (const auto& sw : m_visibleColorSwatches) {
                    if (sw.cellIdx == static_cast<int>(ci) &&
                        static_cast<float>(clientPt.x) >= sw.bounds.left - 3.0f &&
                        static_cast<float>(clientPt.x) <= sw.bounds.right + 3.0f &&
                        static_cast<float>(clientPt.y) >= sw.bounds.top - 3.0f &&
                        static_cast<float>(clientPt.y) <= sw.bounds.bottom + 3.0f) {
                        nb->activeCellIdx = static_cast<int>(ci);
                        if (m_openColorPickerFn) {
                            m_openColorPickerFn(sw);
                            return true;
                        }
                        break;
                    }
                }
                ULONGLONG now = GetTickCount64();
                int dblTime = static_cast<int>(GetDoubleClickTime());
                int ddx = abs(clientPt.x - m_lastLDownPt.x);
                int ddy = abs(clientPt.y - m_lastLDownPt.y);
                bool isDoubleClick = (now - m_lastLDownTime <= static_cast<ULONGLONG>(dblTime)) && (ddx <= 6 && ddy <= 6);
                m_lastLDownTime = now;
                m_lastLDownPt = clientPt;

                if (cell.type == editor::NotebookCellType::Markdown && !cell.isEditingMarkdown) {
                    if (isDoubleClick) {
                        cell.isEditingMarkdown = true;
                        size_t edL = 0, edC = 0;
                        HitTestCodeArea(cell, clientPt, config, edL, edC);
                        cell.ClearSelection();
                        cell.inputBuffer.SetCursor(edL, edC);
                        m_caretVisible = true;
                        m_lastBlinkTime = now;
                        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                        return true;
                    }
                    // Single click on Markdown hyperlink opens browser/safe URL
                    for (const auto& lk : m_visibleMdLinks) {
                        if (lk.cellIdx == static_cast<int>(ci) && PointInRect(clientPt, lk.bounds)) {
                            std::wstring openUrl = TrimMdTarget(lk.url);
                            if (!openUrl.empty() && openUrl.find(L"://") == std::wstring::npos &&
                                _wcsnicmp(openUrl.c_str(), L"www.", 4) == 0) {
                                openUrl = L"https://" + openUrl;
                            }
                            if (IsHttpUrl(openUrl)) {
                                m_lastOpenedMdLinkForTest = openUrl;
                                if (hwnd) {
                                    ShellExecuteW(hwnd, L"open", openUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                                }
                                return true;
                            }
                        }
                    }
                    // Single click on rendered Markdown image opens it in the Lightbox viewer
                    for (const auto& imgHit : m_visibleMdImages) {
                        if (imgHit.cellIdx == static_cast<int>(ci) && PointInRect(clientPt, imgHit.bounds) &&
                            !imgHit.imageBytes.empty()) {
                            editor::CellOutput imgTmp;
                            imgTmp.streamName = L"image/png";
                            imgTmp.imageId = imgHit.imageId;
                            imgTmp.imagePngBytes = imgHit.imageBytes;
                            imgTmp.imagePixelW = imgHit.pixelW;
                            imgTmp.imagePixelH = imgHit.pixelH;
                            OpenImageLightbox(imgTmp);
                            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                            return true;
                        }
                    }
                    return true;
                }

                size_t targetLine = 0, targetCol = 0;
                HitTestCodeArea(cell, clientPt, config, targetLine, targetCol);

                if (isDoubleClick) {
                    cell.SelectWordAt(targetLine, targetCol);
                    m_caretVisible = true;
                    m_lastBlinkTime = GetTickCount64();
                    return true;
                }

                bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (isShift) {
                    if (!cell.hasSelection) {
                        cell.selAnchor = cell.inputBuffer.GetCursor();
                    }
                    cell.selEnd = { targetLine, targetCol };
                    cell.hasSelection = (cell.selAnchor.line != cell.selEnd.line || cell.selAnchor.col != cell.selEnd.col);
                } else {
                    cell.ClearSelection();
                    cell.selAnchor = { targetLine, targetCol };
                    cell.selEnd = { targetLine, targetCol };
                }
                cell.inputBuffer.SetCursor(targetLine, targetCol);
                m_isSelectingText = true;
                if (hwnd) SetCapture(hwnd);
                m_caretVisible = true;
                m_lastBlinkTime = GetTickCount64();
                return true;
            }

            // Interactive input field or submit button click
            if (cell.isWaitingForInput) {
                if (PointInRect(clientPt, cell.submitBtnBounds)) {
                    SubmitCellInput(cell, hwnd);
                    return true;
                }
                if (PointInRect(clientPt, cell.inputFieldBounds) || PointInRect(clientPt, cell.inputPromptBounds)) {
                    cell.isInputFieldFocused = true;
                    m_caretVisible = true;
                    m_lastBlinkTime = GetTickCount64();
                    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                    return true;
                }
            }

            // Click inside output area: plot hover buttons / plot lightbox OR output text selection
            if (PointInRect(clientPt, cell.outputAreaBounds)) {
                for (const auto& out : cell.outputs) {
                    if (out.imagePngBytes.empty()) continue;
                    if (PointInRect(clientPt, out.imgSaveBtnBounds)) {
                        SavePngWithDialog(out.imagePngBytes, hwnd);
                        return true;
                    }
                    if (PointInRect(clientPt, out.imgCopyBtnBounds)) {
                        CopyPngToClipboard(out.imagePngBytes, hwnd);
                        m_copiedFeedbackCellIdx = static_cast<int>(ci);
                        m_copiedFeedbackUntil = GetTickCount64() + 1800;
                        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                        return true;
                    }
                    if (PointInRect(clientPt, out.imgZoomBtnBounds) || PointInRect(clientPt, out.renderedImageBounds)) {
                        OpenImageLightbox(out);
                        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                        return true;
                    }
                }
                size_t outLine = 0, outCol = 0;
                if (HitTestOutputArea(cell, clientPt, config, outLine, outCol)) {
                    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    if (isShift && cell.hasOutputSelection) {
                        cell.outputSelEndLine = outLine;
                        cell.outputSelEndCol = outCol;
                        cell.hasOutputSelection = (cell.outputSelAnchorLine != cell.outputSelEndLine ||
                                                   cell.outputSelAnchorCol != cell.outputSelEndCol);
                    } else {
                        cell.ClearOutputSelection();
                        cell.outputSelAnchorLine = outLine;
                        cell.outputSelAnchorCol = outCol;
                        cell.outputSelEndLine = outLine;
                        cell.outputSelEndCol = outCol;
                    }
                    cell.isSelectingOutput = true;
                    m_dragOutputCellIdx = static_cast<int>(ci);
                    if (hwnd) SetCapture(hwnd);
                    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
                    return true;
                }
            }

            // Drag handle or card header/margin click initiates drag candidate
            if (PointInRect(clientPt, cell.dragHandleBounds) ||
                (clientPt.y >= cell.bounds.top && clientPt.y <= cell.bounds.top + config.ScaleF(36.0f)) ||
                (clientPt.x >= cell.bounds.left && clientPt.x < cell.codeAreaBounds.left)) {
                m_dragCandidateCellIdx = static_cast<int>(ci);
                m_dragStartPt = clientPt;
                if (hwnd) SetCapture(hwnd);
                return true;
            }

            return true;
        }
    }

    return false;
}

bool NotebookView::HandleMouseMove(POINT clientPt, editor::OpenDocument& doc, const app::AppConfig& config) {
    auto PointInLightboxRect = [](POINT pt, const D2D1_RECT_F& r) -> bool {
        return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
    };
    if (m_varTableModal.isOpen) {
        return false;
    }
    if (m_lightboxOpen) {
        if (m_lightboxDragging) {
            m_lightboxPanX = m_lightboxPanStartX + static_cast<float>(clientPt.x - m_lightboxDragStart.x);
            m_lightboxPanY = m_lightboxPanStartY + static_cast<float>(clientPt.y - m_lightboxDragStart.y);
            return true;
        }
        int oldLbHover = m_lightboxHoveredBtn;
        if (PointInLightboxRect(clientPt, m_lightboxBtnClose)) m_lightboxHoveredBtn = 6;
        else if (PointInLightboxRect(clientPt, m_lightboxBtnSave)) m_lightboxHoveredBtn = 5;
        else if (PointInLightboxRect(clientPt, m_lightboxBtnCopy)) m_lightboxHoveredBtn = 4;
        else if (PointInLightboxRect(clientPt, m_lightboxBtnZoomIn)) m_lightboxHoveredBtn = 3;
        else if (PointInLightboxRect(clientPt, m_lightboxBtnReset)) m_lightboxHoveredBtn = 2;
        else if (PointInLightboxRect(clientPt, m_lightboxBtnZoomOut)) m_lightboxHoveredBtn = 1;
        else m_lightboxHoveredBtn = 0;
        return oldLbHover != m_lightboxHoveredBtn;
    }

    if (m_isDraggingScrollbar) {
        float deltaY = static_cast<float>(clientPt.y) - m_dragScrollStartY;
        float contentH = (std::max)(1.0f, m_viewHeight - config.ScaleF(40.0f));
        float maxScroll = (std::max)(0.0f, m_totalHeight - contentH + config.ScaleF(40.0f));
        float scrollDelta = (deltaY / contentH) * m_totalHeight;
        m_scrollY = (std::clamp)(m_dragScrollStartOffset + scrollDelta, 0.0f, maxScroll);
        if (doc.notebookDoc) doc.notebookDoc->scrollY = m_scrollY;
        return true;
    }

    if (m_isDraggingOutputVScroll && doc.notebookDoc && m_dragOutputCellIdx >= 0 && m_dragOutputCellIdx < static_cast<int>(doc.notebookDoc->cells.size())) {
        auto& cell = doc.notebookDoc->cells[m_dragOutputCellIdx];
        float deltaY = static_cast<float>(clientPt.y) - m_dragOutputStartPos;
        float trackH = cell.outputVScrollTrackBounds.bottom - cell.outputVScrollTrackBounds.top;
        float thumbH = cell.outputVScrollThumbBounds.bottom - cell.outputVScrollThumbBounds.top;
        float scrollable = (std::max)(1.0f, trackH - thumbH);
        float scrollDelta = (deltaY / scrollable) * cell.outputMaxScrollY;
        cell.outputScrollY = (std::clamp)(m_dragOutputStartOffset + scrollDelta, 0.0f, cell.outputMaxScrollY);
        return true;
    }

    if (m_isDraggingOutputHScroll && doc.notebookDoc && m_dragOutputCellIdx >= 0 && m_dragOutputCellIdx < static_cast<int>(doc.notebookDoc->cells.size())) {
        auto& cell = doc.notebookDoc->cells[m_dragOutputCellIdx];
        float deltaX = static_cast<float>(clientPt.x) - m_dragOutputStartPos;
        float trackW = cell.outputHScrollTrackBounds.right - cell.outputHScrollTrackBounds.left;
        float thumbW = cell.outputHScrollThumbBounds.right - cell.outputHScrollThumbBounds.left;
        float scrollable = (std::max)(1.0f, trackW - thumbW);
        float scrollDelta = (deltaX / scrollable) * cell.outputMaxScrollX;
        cell.outputScrollX = (std::clamp)(m_dragOutputStartOffset + scrollDelta, 0.0f, cell.outputMaxScrollX);
        return true;
    }

    if (m_isDraggingCodeHScroll && doc.notebookDoc && m_dragCodeCellIdx >= 0 && m_dragCodeCellIdx < static_cast<int>(doc.notebookDoc->cells.size())) {
        auto& cell = doc.notebookDoc->cells[m_dragCodeCellIdx];
        float deltaX = static_cast<float>(clientPt.x) - m_dragCodeStartPos;
        float trackW = cell.codeHScrollTrackBounds.right - cell.codeHScrollTrackBounds.left;
        float thumbW = cell.codeHScrollThumbBounds.right - cell.codeHScrollThumbBounds.left;
        float scrollable = (std::max)(1.0f, trackW - thumbW);
        float scrollDelta = (deltaX / scrollable) * cell.codeMaxScrollX;
        cell.codeScrollX = (std::clamp)(m_dragCodeStartOffset + scrollDelta, 0.0f, cell.codeMaxScrollX);
        return true;
    }

    // Drag-selection in output area with auto-scrolling
    if (doc.notebookDoc) {
        for (size_t ci = 0; ci < doc.notebookDoc->cells.size(); ++ci) {
            auto& cell = doc.notebookDoc->cells[ci];
            if (cell.isSelectingOutput) {
                // Auto-scroll output area vertically if mouse drags outside bounds
                if (clientPt.y < cell.outputAreaBounds.top && cell.outputScrollY > 0.0f) {
                    cell.outputScrollY = (std::max)(0.0f, cell.outputScrollY - config.ScaleF(16.0f));
                } else if (clientPt.y > cell.outputAreaBounds.bottom && cell.outputScrollY < cell.outputMaxScrollY) {
                    cell.outputScrollY = (std::min)(cell.outputMaxScrollY, cell.outputScrollY + config.ScaleF(16.0f));
                }
                // Auto-scroll notebook vertically if near window edges
                if (clientPt.y < config.ScaleF(40.0f) && m_scrollY > 0.0f) {
                    m_scrollY = (std::max)(0.0f, m_scrollY - config.ScaleF(20.0f));
                } else if (clientPt.y > m_viewHeight - config.ScaleF(40.0f)) {
                    m_scrollY += config.ScaleF(20.0f);
                }
                // Auto-scroll output horizontally
                if (clientPt.x < cell.outputAreaBounds.left && cell.outputScrollX > 0.0f) {
                    cell.outputScrollX = (std::max)(0.0f, cell.outputScrollX - config.ScaleF(20.0f));
                } else if (clientPt.x > cell.outputAreaBounds.right && cell.outputScrollX < cell.outputMaxScrollX) {
                    cell.outputScrollX = (std::min)(cell.outputMaxScrollX, cell.outputScrollX + config.ScaleF(20.0f));
                }

                size_t targetLine = 0, targetCol = 0;
                if (HitTestOutputArea(cell, clientPt, config, targetLine, targetCol)) {
                    cell.outputSelEndLine = targetLine;
                    cell.outputSelEndCol = targetCol;
                    cell.hasOutputSelection = (cell.outputSelAnchorLine != cell.outputSelEndLine ||
                                               cell.outputSelAnchorCol != cell.outputSelEndCol);

                    auto outLines = GetOutputLines(cell);
                    size_t sL = 0, sC = 0, eL = 0, eC = 0;
                    cell.GetOutputSelectionRange(sL, sC, eL, eC);
                    std::wstring selText;
                    for (size_t li = sL; li <= eL && li < outLines.size(); ++li) {
                        const auto& lStr = outLines[li];
                        size_t startCol = (li == sL) ? (std::min)(sC, lStr.length()) : 0;
                        size_t endCol = (li == eL) ? (std::min)(eC, lStr.length()) : lStr.length();
                        if (endCol > startCol) {
                            selText += lStr.substr(startCol, endCol - startCol);
                        }
                        if (li < eL) selText += L"\n";
                    }
                    cell.selectedOutputText = selText;
                }
                return true;
            }
        }
    }

    // Drag-selection of text across lines and columns in code editor with auto-scroll
    if (m_isSelectingText && doc.notebookDoc) {
        int activeIdx = doc.notebookDoc->activeCellIdx;
        if (activeIdx >= 0 && activeIdx < static_cast<int>(doc.notebookDoc->cells.size())) {
            auto& cell = doc.notebookDoc->cells[activeIdx];

            // Auto-scroll notebook vertically if dragging outside code box
            if (clientPt.y < cell.codeAreaBounds.top && m_scrollY > 0.0f) {
                m_scrollY = (std::max)(0.0f, m_scrollY - config.ScaleF(16.0f));
            } else if (clientPt.y > cell.codeAreaBounds.bottom) {
                m_scrollY += config.ScaleF(16.0f);
            }
            // Auto-scroll code horizontally if dragging outside code box
            if (clientPt.x < cell.codeAreaBounds.left + config.ScaleF(36.0f) && cell.codeScrollX > 0.0f) {
                cell.codeScrollX = (std::max)(0.0f, cell.codeScrollX - config.ScaleF(20.0f));
            } else if (clientPt.x > cell.codeAreaBounds.right && cell.codeScrollX < cell.codeMaxScrollX) {
                cell.codeScrollX = (std::min)(cell.codeMaxScrollX, cell.codeScrollX + config.ScaleF(20.0f));
            }

            size_t targetLine = 0, targetCol = 0;
            HitTestCodeArea(cell, clientPt, config, targetLine, targetCol);
            cell.selEnd = { targetLine, targetCol };
            cell.hasSelection = (cell.selAnchor.line != cell.selEnd.line || cell.selAnchor.col != cell.selEnd.col);
            cell.inputBuffer.SetCursor(targetLine, targetCol);
            return true;
        }
    }

    // Check if dragging cell should start (> 4px movement)
    if (m_dragCandidateCellIdx >= 0 && !m_isDraggingCell) {
        int dx = abs(clientPt.x - m_dragStartPt.x);
        int dy = abs(clientPt.y - m_dragStartPt.y);
        if (dx > 4 || dy > 4) {
            m_isDraggingCell = true;
            m_draggedCellIdx = m_dragCandidateCellIdx;
            m_dropTargetCellIdx = m_draggedCellIdx;
            m_dragCandidateCellIdx = -1;
        }
    }

    if (m_isDraggingCell && doc.notebookDoc) {
        const auto& cells = doc.notebookDoc->cells;
        int targetIdx = m_draggedCellIdx;
        float bestDist = 1e9f;

        for (size_t ci = 0; ci < cells.size(); ++ci) {
            float midY = (cells[ci].bounds.top + cells[ci].bounds.bottom) * 0.5f;
            float dist = std::abs(static_cast<float>(clientPt.y) - midY);
            if (dist < bestDist) {
                bestDist = dist;
                targetIdx = static_cast<int>(ci);
            }
        }
        if (targetIdx != m_dropTargetCellIdx) {
            m_dropTargetCellIdx = targetIdx;
            return true;
        }
        return true;
    }

    auto PointInRect = [](POINT pt, const D2D1_RECT_F& r) -> bool {
        return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
    };

    int oldToolbarBtn = m_hoveredToolbarBtn;
    if (PointInRect(clientPt, m_btnRunAll)) m_hoveredToolbarBtn = 1;
    else if (PointInRect(clientPt, m_btnClearAll)) m_hoveredToolbarBtn = 2;
    else if (PointInRect(clientPt, m_btnAddCode)) m_hoveredToolbarBtn = 3;
    else if (PointInRect(clientPt, m_btnAddText)) m_hoveredToolbarBtn = 4;
    else if (PointInRect(clientPt, m_btnKernelPicker)) m_hoveredToolbarBtn = 5;
    else if (PointInRect(clientPt, m_btnRestartKernel)) m_hoveredToolbarBtn = 6;
    else if (PointInRect(clientPt, m_btnVariables)) m_hoveredToolbarBtn = 7;
    else if (PointInRect(clientPt, m_btnVarTable)) m_hoveredToolbarBtn = 8;
    else if (PointInRect(clientPt, m_btnCutCell)) m_hoveredToolbarBtn = 9;
    else if (PointInRect(clientPt, m_btnCopyCell)) m_hoveredToolbarBtn = 10;
    else if (PointInRect(clientPt, m_btnPasteCell)) m_hoveredToolbarBtn = 11;
    else if (PointInRect(clientPt, m_btnRunCell)) m_hoveredToolbarBtn = 12;
    else if (PointInRect(clientPt, m_btnCellTypePicker)) m_hoveredToolbarBtn = 13;
    else if (PointInRect(clientPt, m_btnInterruptKernel)) m_hoveredToolbarBtn = 14;
    else m_hoveredToolbarBtn = 0;

    int oldTypeDropIdx = m_hoveredCellTypeDropdownIdx;
    m_hoveredCellTypeDropdownIdx = -1;
    if (m_cellTypeDropdownOpen) {
        if (PointInRect(clientPt, m_cellTypeItemCodeBounds)) m_hoveredCellTypeDropdownIdx = 0;
        else if (PointInRect(clientPt, m_cellTypeItemMdBounds)) m_hoveredCellTypeDropdownIdx = 1;
    }

    int oldVarIdx = m_hoveredVarInspectIdx;
    m_hoveredVarInspectIdx = -1;
    if (doc.notebookDoc && doc.notebookDoc->showVariableInspector) {
        for (size_t vi = 0; vi < m_varInspectBtnBounds.size(); ++vi) {
            if (PointInRect(clientPt, m_varInspectBtnBounds[vi])) {
                m_hoveredVarInspectIdx = static_cast<int>(vi);
                break;
            }
        }
    }

    m_hoveredKernelDropdownIdx = -1;
    if (m_kernelDropdownOpen) {
        for (size_t ki = 0; ki < m_kernelDropdownItemBounds.size(); ++ki) {
            if (PointInRect(clientPt, m_kernelDropdownItemBounds[ki])) {
                m_hoveredKernelDropdownIdx = static_cast<int>(ki);
                break;
            }
        }
    }

    if (IsAutocompleteVisible()) {
        int hitIdx = -1;
        if (EditorCanvas::HitTestAutocompletePanel(
                clientPt, m_nbAutocompleteBounds, m_nbAutocompleteItems.size(), m_nbAutocompleteScrollOffset, hitIdx)) {
            if (hitIdx >= 0 && hitIdx < static_cast<int>(m_nbAutocompleteItems.size()) &&
                hitIdx != static_cast<int>(m_nbAutocompleteSelectedIdx)) {
                m_nbAutocompleteSelectedIdx = static_cast<size_t>(hitIdx);
                return true;
            }
            return false;
        }
    }

    const int oldDiagIdx = m_hoveredDiagIdx;
    m_hoveredDiagIdx = -1;
    if (!m_isSelectingText && !m_isDraggingCell && !m_isDraggingScrollbar) {
        for (size_t di = 0; di < m_visibleDiagHits.size(); ++di) {
            if (PointInRect(clientPt, m_visibleDiagHits[di].bounds)) {
                m_hoveredDiagIdx = static_cast<int>(di);
                break;
            }
        }
    }

    const int oldMdLinkIdx = m_hoveredMdLinkIdx;
    m_hoveredMdLinkIdx = -1;
    if (!m_isSelectingText && !m_isDraggingCell && !m_isDraggingScrollbar) {
        for (size_t li = 0; li < m_visibleMdLinks.size(); ++li) {
            if (PointInRect(clientPt, m_visibleMdLinks[li].bounds)) {
                m_hoveredMdLinkIdx = static_cast<int>(li);
                break;
            }
        }
    }

    int oldCellIdx = m_hoveredCellIdx;
    int oldBtn = m_hoveredBtn;
    int oldBottomCell = m_hoveredBottomCellIdx;
    int oldBottomBtn = m_hoveredBottomBtn;
    m_hoveredCellIdx = -1;
    m_hoveredBtn = 0;
    m_hoveredBottomCellIdx = -1;
    m_hoveredBottomBtn = 0;

    if (doc.notebookDoc) {
        // 1. Check cell buttons (including floating top-right pill overhang), cards, and output image overlays FIRST
        int oldImgCell = m_hoveredImgCellIdx;
        int oldImgOut = m_hoveredImgOutIdx;
        int oldImgBtn = m_hoveredImgBtn;
        m_hoveredImgCellIdx = -1;
        m_hoveredImgOutIdx = -1;
        m_hoveredImgBtn = 0;

        bool hitAnyPillControl = false;
        for (size_t ci = 0; ci < doc.notebookDoc->cells.size(); ++ci) {
            const auto& cell = doc.notebookDoc->cells[ci];
            const bool hitFloatingPill =
                PointInRect(clientPt, cell.deleteBtnBounds) || PointInRect(clientPt, cell.moveDownBtnBounds) ||
                PointInRect(clientPt, cell.moveUpBtnBounds) || PointInRect(clientPt, cell.duplicateBtnBounds) ||
                PointInRect(clientPt, cell.dragHandleBounds) || PointInRect(clientPt, cell.collapseInputBtnBounds) ||
                PointInRect(clientPt, cell.editMarkdownBtnBounds);
            if (PointInRect(clientPt, cell.bounds) || hitFloatingPill) {
                m_hoveredCellIdx = static_cast<int>(ci);
                if (PointInRect(clientPt, cell.runBtnBounds)) m_hoveredBtn = 1;
                else if (PointInRect(clientPt, cell.deleteBtnBounds)) { m_hoveredBtn = 2; hitAnyPillControl = true; }
                else if (PointInRect(clientPt, cell.moveUpBtnBounds)) { m_hoveredBtn = 3; hitAnyPillControl = true; }
                else if (PointInRect(clientPt, cell.moveDownBtnBounds)) { m_hoveredBtn = 4; hitAnyPillControl = true; }
                else if (PointInRect(clientPt, cell.outputClearBtnBounds)) m_hoveredBtn = 5;
                else if (PointInRect(clientPt, cell.dragHandleBounds)) { m_hoveredBtn = 6; hitAnyPillControl = true; }
                else if (PointInRect(clientPt, cell.duplicateBtnBounds)) { m_hoveredBtn = 7; hitAnyPillControl = true; }
                else if (PointInRect(clientPt, cell.outputCollapseBtnBounds)) m_hoveredBtn = 8;
                else if (PointInRect(clientPt, cell.outputCopyBtnBounds)) m_hoveredBtn = 9;
                else if (PointInRect(clientPt, cell.editMarkdownBtnBounds)) { m_hoveredBtn = 10; hitAnyPillControl = true; }
                else if (PointInRect(clientPt, cell.collapseInputBtnBounds)) { m_hoveredBtn = 11; hitAnyPillControl = true; }
                else if (PointInRect(clientPt, cell.mdBtnBoldBounds)) m_hoveredBtn = 12;
                else if (PointInRect(clientPt, cell.mdBtnItalicBounds)) m_hoveredBtn = 13;
                else if (PointInRect(clientPt, cell.mdBtnQuoteBounds)) m_hoveredBtn = 14;
                else if (PointInRect(clientPt, cell.mdBtnLinkBounds)) m_hoveredBtn = 15;
                else if (PointInRect(clientPt, cell.mdBtnImageBounds)) m_hoveredBtn = 16;
                else if (PointInRect(clientPt, cell.mdBtnBulletListBounds)) m_hoveredBtn = 17;
                else if (PointInRect(clientPt, cell.mdBtnNumberListBounds)) m_hoveredBtn = 18;
                else if (PointInRect(clientPt, cell.mdBtnHeadingBounds)) m_hoveredBtn = 19;
                else if (PointInRect(clientPt, cell.mdBtnDoneBounds)) m_hoveredBtn = 20;

                for (size_t oi = 0; oi < cell.outputs.size(); ++oi) {
                    const auto& out = cell.outputs[oi];
                    if (out.imagePngBytes.empty()) continue;
                    if (PointInRect(clientPt, out.renderedImageBounds) ||
                        PointInRect(clientPt, out.imgZoomBtnBounds) ||
                        PointInRect(clientPt, out.imgCopyBtnBounds) ||
                        PointInRect(clientPt, out.imgSaveBtnBounds)) {
                        m_hoveredImgCellIdx = static_cast<int>(ci);
                        m_hoveredImgOutIdx = static_cast<int>(oi);
                        if (PointInRect(clientPt, out.imgZoomBtnBounds)) m_hoveredImgBtn = 1;
                        else if (PointInRect(clientPt, out.imgCopyBtnBounds)) m_hoveredImgBtn = 2;
                        else if (PointInRect(clientPt, out.imgSaveBtnBounds)) m_hoveredImgBtn = 3;
                        break;
                    }
                }
                break;
            }
        }

        // 2. Check bottom divider gap / [+ Code] [+ Markdown] pills ONLY when NOT over a floating pill
        if (!hitAnyPillControl) {
            for (size_t ci = 0; ci < doc.notebookDoc->cells.size(); ++ci) {
                const auto& cell = doc.notebookDoc->cells[ci];
                const bool overBottomPill =
                    PointInRect(clientPt, cell.bottomAddCodeBounds) || PointInRect(clientPt, cell.bottomAddTextBounds);
                const bool insideNextCell = (ci + 1 < doc.notebookDoc->cells.size() &&
                                             PointInRect(clientPt, doc.notebookDoc->cells[ci + 1].bounds));
                const bool nearBottomGap =
                    (!insideNextCell &&
                     clientPt.x >= cell.bounds.left && clientPt.x <= cell.bounds.right &&
                     clientPt.y >= cell.bounds.bottom - config.ScaleF(6.0f) &&
                     clientPt.y <= cell.bounds.bottom + config.ScaleF(20.0f));
                if (overBottomPill || nearBottomGap) {
                    m_hoveredBottomCellIdx = static_cast<int>(ci);
                    if (PointInRect(clientPt, cell.bottomAddCodeBounds)) m_hoveredBottomBtn = 1;
                    else if (PointInRect(clientPt, cell.bottomAddTextBounds)) m_hoveredBottomBtn = 2;
                    break;
                }
            }
        }

        return (oldToolbarBtn != m_hoveredToolbarBtn || oldTypeDropIdx != m_hoveredCellTypeDropdownIdx ||
                oldCellIdx != m_hoveredCellIdx || oldBtn != m_hoveredBtn ||
                oldBottomCell != m_hoveredBottomCellIdx || oldBottomBtn != m_hoveredBottomBtn ||
                oldImgCell != m_hoveredImgCellIdx || oldImgOut != m_hoveredImgOutIdx ||
                oldImgBtn != m_hoveredImgBtn || oldDiagIdx != m_hoveredDiagIdx ||
                oldMdLinkIdx != m_hoveredMdLinkIdx || oldVarIdx != m_hoveredVarInspectIdx);
    }

    return (oldToolbarBtn != m_hoveredToolbarBtn || oldTypeDropIdx != m_hoveredCellTypeDropdownIdx ||
            oldCellIdx != m_hoveredCellIdx || oldBtn != m_hoveredBtn ||
            oldBottomCell != m_hoveredBottomCellIdx || oldBottomBtn != m_hoveredBottomBtn ||
            oldDiagIdx != m_hoveredDiagIdx || oldMdLinkIdx != m_hoveredMdLinkIdx ||
            oldVarIdx != m_hoveredVarInspectIdx);
}

bool NotebookView::HandleLButtonUp(POINT clientPt, editor::OpenDocument& doc, const app::AppConfig& config) {
    if (m_lightboxDragging) {
        m_lightboxDragging = false;
        ReleaseCapture();
        return true;
    }
    if (doc.notebookDoc) {
        for (auto& cell : doc.notebookDoc->cells) {
            cell.isSelectingOutput = false;
        }
    }
    if (m_isDraggingScrollbar) {
        m_isDraggingScrollbar = false;
        ReleaseCapture();
        return true;
    }
    if (m_isDraggingOutputVScroll || m_isDraggingOutputHScroll) {
        m_isDraggingOutputVScroll = false;
        m_isDraggingOutputHScroll = false;
        m_dragOutputCellIdx = -1;
        ReleaseCapture();
        return true;
    }
    if (m_isDraggingCodeHScroll) {
        m_isDraggingCodeHScroll = false;
        m_dragCodeCellIdx = -1;
        ReleaseCapture();
        return true;
    }
    if (m_isSelectingText) {
        m_isSelectingText = false;
        ReleaseCapture();
        if (doc.notebookDoc && doc.notebookDoc->activeCellIdx >= 0 &&
            doc.notebookDoc->activeCellIdx < static_cast<int>(doc.notebookDoc->cells.size())) {
            auto& cell = doc.notebookDoc->cells[doc.notebookDoc->activeCellIdx];
            if (cell.selAnchor.line == cell.selEnd.line && cell.selAnchor.col == cell.selEnd.col) {
                cell.ClearSelection();
            }
        }
        return true;
    }
    if (m_isDraggingCell) {
        m_isDraggingCell = false;
        ReleaseCapture();
        int from = m_draggedCellIdx;
        int to = m_dropTargetCellIdx;
        m_draggedCellIdx = -1;
        m_dropTargetCellIdx = -1;
        m_dragCandidateCellIdx = -1;
        if (doc.notebookDoc && from >= 0 && to >= 0 && from != to) {
            doc.notebookDoc->MoveCell(from, to);
            return true;
        }
        return true;
    }
    if (m_dragCandidateCellIdx >= 0) {
        m_dragCandidateCellIdx = -1;
        ReleaseCapture();
    }
    return false;
}

bool NotebookView::HandleMouseWheel(short delta, POINT clientPt, editor::OpenDocument& doc, const app::AppConfig& config) {
    if (m_varTableModal.isOpen) {
        bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        float step = config.ScaleF(48.0f);
        if (isShift && m_varTableModal.maxScrollX > 0.0f) {
            m_varTableModal.scrollX = (std::clamp)(m_varTableModal.scrollX + (delta > 0 ? -step : step), 0.0f, m_varTableModal.maxScrollX);
        } else if (m_varTableModal.maxScrollY > 0.0f) {
            m_varTableModal.scrollY = (std::clamp)(m_varTableModal.scrollY + (delta > 0 ? -step : step), 0.0f, m_varTableModal.maxScrollY);
        }
        return true;
    }
    if (m_lightboxOpen) {
        float factor = (delta > 0) ? 1.15f : 0.87f;
        m_lightboxZoom = (std::clamp)(m_lightboxZoom * factor, 0.25f, 4.0f);
        return true;
    }

    // 1. If mouse is over a cell output area, scroll that output's X or Y
    if (doc.notebookDoc) {
        for (auto& cell : doc.notebookDoc->cells) {
            if (clientPt.x >= cell.codeAreaBounds.left && clientPt.x <= cell.codeAreaBounds.right &&
                clientPt.y >= cell.codeAreaBounds.top && clientPt.y <= cell.codeAreaBounds.bottom) {
                bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (isShift && cell.codeMaxScrollX > 0.0f) {
                    float step = config.ScaleF(40.0f);
                    if (delta > 0) {
                        cell.codeScrollX = (std::max)(0.0f, cell.codeScrollX - step);
                    } else {
                        cell.codeScrollX = (std::min)(cell.codeMaxScrollX, cell.codeScrollX + step);
                    }
                    return true;
                }
            }
            if (!cell.outputs.empty() && !cell.isOutputCollapsed &&
                clientPt.x >= cell.outputAreaBounds.left && clientPt.x <= cell.outputAreaBounds.right &&
                clientPt.y >= cell.outputAreaBounds.top && clientPt.y <= cell.outputAreaBounds.bottom) {

                float charW = (m_charWidth > 0.0f) ? m_charWidth : config.ScaleF(7.5f);
                float maxLineW = 0.0f;
                float outLineH = config.ScaleF(18.0f);
                float availW = (cell.outputAreaBounds.right - cell.outputAreaBounds.left) - config.ScaleF(24.0f);
                float totalContentH = config.ScaleF(12.0f);
                for (const auto& out : cell.outputs) {
                    if (!out.imagePngBytes.empty()) {
                        float dw = 0.0f, dh = 0.0f;
                        ComputeScaledImageSize(out, availW, cell.imageZoomScale, config, dw, dh);
                        totalContentH += dh + config.ScaleF(8.0f);
                        if (dw > maxLineW) maxLineW = dw;
                    }
                    std::wstringstream ss(out.text);
                    std::wstring l;
                    while (std::getline(ss, l)) {
                        totalContentH += outLineH;
                        float lw = static_cast<float>(l.length()) * charW;
                        if (lw > maxLineW) maxLineW = lw;
                    }
                }
                float outputH = cell.outputAreaBounds.bottom - cell.outputAreaBounds.top;

                bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (isShift) {
                    // Horizontal scroll
                    float maxScrollX = (std::max)(0.0f, maxLineW - availW);
                    if (maxScrollX > 0.0f) {
                        float step = config.ScaleF(40.0f);
                        if (delta > 0) {
                            if (cell.outputScrollX > 0.0f) {
                                cell.outputScrollX = (std::max)(0.0f, cell.outputScrollX - step);
                                return true;
                            }
                        } else {
                            if (cell.outputScrollX < maxScrollX) {
                                cell.outputScrollX = (std::min)(maxScrollX, cell.outputScrollX + step);
                                return true;
                            }
                        }
                    }
                } else {
                    // Vertical scroll with scroll chaining:
                    // If output is already at the top and scrolling UP, or already at the bottom and scrolling DOWN,
                    // propagate scroll to the whole notebook view!
                    float maxScrollY = (std::max)(0.0f, totalContentH - outputH);
                    if (maxScrollY > 0.0f) {
                        float step = config.ScaleF(40.0f);
                        if (delta > 0) {
                            if (cell.outputScrollY > 0.0f) {
                                cell.outputScrollY = (std::max)(0.0f, cell.outputScrollY - step);
                                return true;
                            }
                            // At top (outputScrollY <= 0) -> let scroll propagate to the notebook!
                        } else {
                            if (cell.outputScrollY < maxScrollY) {
                                cell.outputScrollY = (std::min)(maxScrollY, cell.outputScrollY + step);
                                return true;
                            }
                            // At bottom (outputScrollY >= maxScrollY) -> let scroll propagate to the notebook!
                        }
                    }
                }
            }
        }
    }

    // 2. Otherwise scroll notebook view vertically
    float contentH = (std::max)(1.0f, m_viewHeight - config.ScaleF(40.0f));
    float maxScroll = (std::max)(0.0f, m_totalHeight - contentH + config.ScaleF(40.0f));
    float scrollStep = config.ScaleF(60.0f);
    if (delta > 0) {
        m_scrollY = (std::max)(0.0f, m_scrollY - scrollStep);
    } else {
        m_scrollY = (std::min)(maxScroll, m_scrollY + scrollStep);
    }
    if (doc.notebookDoc) doc.notebookDoc->scrollY = m_scrollY;
    return true;
}

bool NotebookView::HandleKeyDown(WPARAM wParam, editor::OpenDocument& doc, HWND hwnd) {
    if (m_varTableModal.isOpen) {
        if (wParam == VK_ESCAPE) {
            CloseVariableTableModal();
        }
        return true;
    }
    if (m_lightboxOpen) {
        if (wParam == VK_ESCAPE) {
            m_lightboxOpen = false;
            m_lightboxDragging = false;
            return true;
        }
        if (wParam == VK_ADD || wParam == VK_OEM_PLUS) {
            m_lightboxZoom = (std::min)(4.0f, m_lightboxZoom * 1.2f);
            return true;
        }
        if (wParam == VK_SUBTRACT || wParam == VK_OEM_MINUS) {
            m_lightboxZoom = (std::max)(0.25f, m_lightboxZoom * 0.83f);
            return true;
        }
        if (wParam == '0') {
            m_lightboxZoom = 1.0f;
            m_lightboxPanX = 0.0f;
            m_lightboxPanY = 0.0f;
            return true;
        }
        return true;
    }

    if (wParam == VK_ESCAPE && m_isDraggingCell) {
        m_isDraggingCell = false;
        m_draggedCellIdx = -1;
        m_dropTargetCellIdx = -1;
        m_dragCandidateCellIdx = -1;
        ReleaseCapture();
        return true;
    }

    if (!doc.notebookDoc || doc.notebookDoc->cells.empty()) return false;
    auto nb = doc.notebookDoc;
    if (nb->activeCellIdx < 0 || nb->activeCellIdx >= static_cast<int>(nb->cells.size())) return false;

    auto& activeCell = nb->cells[nb->activeCellIdx];
    bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool isCtrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool isAlt   = (GetKeyState(VK_MENU) & 0x8000) != 0;

    // Autocomplete navigation and completion acceptance inside active code cell
    if (IsAutocompleteVisible() && activeCell.type == editor::NotebookCellType::Code && !activeCell.isWaitingForInput) {
        if (wParam == VK_ESCAPE) {
            DismissAutocomplete();
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (wParam == VK_UP && !isShift && !isCtrl && !isAlt) {
            if (m_nbAutocompleteSelectedIdx == 0) {
                m_nbAutocompleteSelectedIdx = m_nbAutocompleteItems.size() - 1;
            } else {
                --m_nbAutocompleteSelectedIdx;
            }
            m_nbAutocompleteNavigated = true;
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (wParam == VK_DOWN && !isShift && !isCtrl && !isAlt) {
            m_nbAutocompleteSelectedIdx = (m_nbAutocompleteSelectedIdx + 1) % m_nbAutocompleteItems.size();
            m_nbAutocompleteNavigated = true;
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        if (wParam == VK_TAB && !isCtrl && !isAlt) {
            m_suppressCharTarget = L'\t';
            CommitAutocompleteSelection(doc, hwnd);
            return true;
        }
        if (wParam == VK_RETURN && !isShift && !isCtrl && !isAlt) {
            if (m_nbAutocompleteNavigated) {
                CommitAutocompleteSelection(doc, hwnd);
                return true;
            }
            DismissAutocomplete();
        }
    }

    // Ctrl + Space: Trigger IntelliSense Autocomplete manually
    if (wParam == VK_SPACE && isCtrl && !isAlt && activeCell.type == editor::NotebookCellType::Code && !activeCell.isWaitingForInput) {
        m_suppressCharTarget = L' ';
        QueryActiveCellAutocomplete(doc);
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }

    // Ctrl + / (VK_OEM_2): Toggle # Line Comment (matches EditorCanvas)
    if (wParam == VK_OEM_2 && isCtrl && !isAlt && activeCell.type == editor::NotebookCellType::Code && !activeCell.isWaitingForInput) {
        m_suppressCharTarget = L'/';
        DismissAutocomplete();
        size_t sL = 0, sC = 0, eL = 0, eC = 0;
        if (activeCell.HasSelection()) {
            activeCell.GetSelectionRange(sL, sC, eL, eC);
            if (eL > sL && eC == 0) --eL;
        } else {
            auto cur = activeCell.inputBuffer.GetCursor();
            sL = cur.line;
            eL = cur.line;
        }
        std::vector<int> deltas;
        if (activeCell.inputBuffer.ToggleLineCommentRange(sL, eL, L"py", &deltas)) {
            auto applyDelta = [&](editor::CursorPosition& p) {
                if (p.line >= sL && p.line <= eL) {
                    size_t idx = p.line - sL;
                    if (idx < deltas.size()) {
                        int nc = static_cast<int>(p.col) + deltas[idx];
                        p.col = static_cast<size_t>((std::max)(0, nc));
                    }
                }
            };
            auto c = activeCell.inputBuffer.GetCursor();
            applyDelta(c);
            activeCell.inputBuffer.SetCursor(c.line, c.col);
            if (activeCell.HasSelection()) {
                applyDelta(activeCell.selAnchor);
                applyDelta(activeCell.selEnd);
            }
            nb->isDirty = true;
            EnsureActiveCellCursorVisible(doc);
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
        }
        return true;
    }

    // Interactive input handling when waiting for input
    if (activeCell.isWaitingForInput) {
        if (wParam == VK_RETURN) {
            SubmitCellInput(activeCell, hwnd);
            return true;
        } else if (wParam == VK_BACK) {
            if (!activeCell.inputFieldValue.empty()) {
                activeCell.inputFieldValue.pop_back();
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            }
            return true;
        } else if (wParam == VK_ESCAPE) {
            activeCell.isWaitingForInput = false;
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        } else if (isCtrl && (wParam == 'V' || wParam == 'v')) {
            std::wstring clip = GetClipboardText(hwnd);
            if (!clip.empty()) {
                activeCell.inputFieldValue += clip;
                if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            }
            return true;
        }
    }

    // Markdown preview cell: Enter opens edit mode, Esc exits edit mode
    if (activeCell.type == editor::NotebookCellType::Markdown) {
        if (!activeCell.isEditingMarkdown && wParam == VK_RETURN && !isShift && !isCtrl && !isAlt) {
            activeCell.isEditingMarkdown = true;
            m_caretVisible = true;
            m_lastBlinkTime = GetTickCount64();
            return true;
        }
        if (activeCell.isEditingMarkdown && wParam == VK_ESCAPE) {
            if (!activeCell.inputBuffer.GetText().empty()) {
                activeCell.isEditingMarkdown = false;
            }
            return true;
        }
    }

    // Alt + Enter: Run active cell and insert a new Code cell below
    if (wParam == VK_RETURN && isAlt) {
        int curIdx = nb->activeCellIdx;
        if (activeCell.type == editor::NotebookCellType::Markdown) {
            activeCell.isEditingMarkdown = false;
        } else {
            RunCell(curIdx, doc, hwnd);
        }
        nb->AddCell(editor::NotebookCellType::Code, curIdx);
        m_scrollActiveCellIntoView = true;
        return true;
    }

    // Shift + Enter: Run active cell and advance to next (classic Jupyter shortcut)
    if (wParam == VK_RETURN && isShift) {
        int curIdx = nb->activeCellIdx;
        if (activeCell.type == editor::NotebookCellType::Markdown) {
            activeCell.isEditingMarkdown = false;
        }
        int nextIdx = curIdx + 1;
        if (nextIdx >= static_cast<int>(nb->cells.size())) {
            nb->AddCell(editor::NotebookCellType::Code, curIdx);
        } else {
            nb->activeCellIdx = nextIdx;
        }
        m_scrollActiveCellIntoView = true;
        RunCell(curIdx, doc, hwnd);
        return true;
    }

    // Ctrl + Enter: Run active cell and stay focused
    if (wParam == VK_RETURN && isCtrl) {
        if (activeCell.type == editor::NotebookCellType::Markdown) {
            activeCell.isEditingMarkdown = false;
            return true;
        }
        RunCell(nb->activeCellIdx, doc, hwnd);
        return true;
    }

    // Ctrl + Shift + D: Duplicate active cell
    if (isCtrl && isShift && (wParam == 'D' || wParam == 'd')) {
        nb->DuplicateCell(nb->activeCellIdx);
        m_scrollActiveCellIntoView = true;
        return true;
    }

    // Ctrl + B / Ctrl + I: Bold / Italic formatting inside Markdown edit mode
    if (isCtrl && !isShift && !isAlt &&
        activeCell.type == editor::NotebookCellType::Markdown && activeCell.isEditingMarkdown) {
        if (wParam == 'B' || wParam == 'b') {
            ApplyMarkdownFormat(*nb, nb->activeCellIdx, MarkdownFormatAction::Bold, hwnd);
            return true;
        }
        if (wParam == 'I' || wParam == 'i') {
            ApplyMarkdownFormat(*nb, nb->activeCellIdx, MarkdownFormatAction::Italic, hwnd);
            return true;
        }
    }

    // Ctrl + Z: Undo / Ctrl + Shift + Z: Redo
    if (isCtrl && (wParam == 'Z' || wParam == 'z')) {
        if (isShift) {
            activeCell.ClearSelection();
            activeCell.inputBuffer.Redo();
        } else {
            activeCell.ClearSelection();
            activeCell.inputBuffer.Undo();
        }
        nb->isDirty = true;
        return true;
    }

    // Ctrl + Y: Redo
    if (isCtrl && (wParam == 'Y' || wParam == 'y')) {
        activeCell.ClearSelection();
        activeCell.inputBuffer.Redo();
        nb->isDirty = true;
        return true;
    }

    // Ctrl + A: Select All
    if (isCtrl && (wParam == 'A' || wParam == 'a')) {
        size_t lineCount = activeCell.inputBuffer.GetLineCount();
        if (lineCount > 0) {
            activeCell.selAnchor = { 0, 0 };
            size_t lastL = lineCount - 1;
            size_t lastC = activeCell.inputBuffer.GetLine(lastL).length();
            activeCell.selEnd = { lastL, lastC };
            activeCell.hasSelection = true;
            activeCell.inputBuffer.SetCursor(lastL, lastC);
        }
        return true;
    }

    // Ctrl + C: Copy (supports output text selection OR code selection)
    if (isCtrl && (wParam == 'C' || wParam == 'c')) {
        if (activeCell.HasOutputSelection()) {
            SetClipboardText(activeCell.selectedOutputText, hwnd);
            return true;
        } else if (activeCell.HasSelection()) {
            SetClipboardText(activeCell.GetSelectedText(), hwnd);
            return true;
        }
        return true;
    }

    // Ctrl + X: Cut
    if (isCtrl && (wParam == 'X' || wParam == 'x')) {
        if (activeCell.HasSelection()) {
            SetClipboardText(activeCell.GetSelectedText(), hwnd);
            activeCell.DeleteSelection();
            nb->isDirty = true;
        }
        return true;
    }

    // Ctrl + V: Paste
    if (isCtrl && (wParam == 'V' || wParam == 'v')) {
        std::wstring clip = GetClipboardText(hwnd);
        if (!clip.empty()) {
            if (activeCell.HasSelection()) {
                activeCell.DeleteSelection();
            }
            activeCell.inputBuffer.InsertText(clip);
            nb->isDirty = true;
        }
        return true;
    }

    m_caretVisible = true;
    m_lastBlinkTime = GetTickCount64();

    switch (wParam) {
        case VK_RETURN: {
            if (activeCell.HasSelection()) {
                activeCell.DeleteSelection();
            }
            auto cur = activeCell.inputBuffer.GetCursor();
            const auto& lines = activeCell.inputBuffer.GetLines();
            std::wstring baseIndent;
            bool shouldIndent = false;
            if (cur.line < lines.size()) {
                const auto& line = lines[cur.line];
                for (size_t col = 0; col < line.length() && (line[col] == L' ' || line[col] == L'\t'); ++col) {
                    baseIndent += line[col];
                }

                // Check if between braces: {|}
                if (cur.col > 0 && cur.col < line.length() && line[cur.col - 1] == L'{' && line[cur.col] == L'}') {
                    std::wstring midIndent = baseIndent + L"  ";
                    std::wstring expandStr = L"\n" + midIndent + L"\n" + baseIndent;
                    activeCell.inputBuffer.InsertText(expandStr);
                    activeCell.inputBuffer.SetCursor(cur.line + 1, midIndent.length());
                    nb->isDirty = true;
                    return true;
                }

                // Strictly inspect text before cursor on the CURRENT line (cur.line)
                std::wstring textBeforeCursor = (cur.col <= line.length()) ? line.substr(0, cur.col) : line;
                size_t endPos = textBeforeCursor.find_last_not_of(L" \t");
                if (endPos != std::wstring::npos && (textBeforeCursor[endPos] == L':' || textBeforeCursor[endPos] == L'{')) {
                    shouldIndent = true;
                }
            }
            if (shouldIndent) {
                // Auto-indent next line by 2 spaces relative to base indent (Colab/Jupyter standard)
                std::wstring indentStr = L"\n" + baseIndent + L"  ";
                activeCell.inputBuffer.InsertText(indentStr);
            } else if (!baseIndent.empty()) {
                std::wstring indentStr = L"\n" + baseIndent;
                activeCell.inputBuffer.InsertText(indentStr);
            } else {
                activeCell.inputBuffer.InsertChar(L'\n');
            }
            nb->isDirty = true;
            return true;
        }
        case VK_BACK: {
            const bool wasAcOpen = IsAutocompleteVisible();
            if (activeCell.HasSelection()) {
                activeCell.DeleteSelection();
            } else {
                auto cur = activeCell.inputBuffer.GetCursor();
                const auto& lines = activeCell.inputBuffer.GetLines();
                bool isPair = false;
                if (cur.line < lines.size()) {
                    const auto& line = lines[cur.line];
                    if (cur.col > 0 && cur.col < line.length()) {
                        wchar_t leftCh = line[cur.col - 1];
                        wchar_t rightCh = line[cur.col];
                        if ((leftCh == L'(' && rightCh == L')') ||
                            (leftCh == L'[' && rightCh == L']') ||
                            (leftCh == L'{' && rightCh == L'}') ||
                            (leftCh == L'"' && rightCh == L'"') ||
                            (leftCh == L'\'' && rightCh == L'\'') ||
                            (leftCh == L'`' && rightCh == L'`')) {
                            isPair = true;
                        }
                    }
                }
                if (isPair) {
                    activeCell.inputBuffer.MoveCursorRight();
                    activeCell.inputBuffer.Backspace();
                    activeCell.inputBuffer.Backspace();
                } else {
                    activeCell.inputBuffer.Backspace();
                }
            }
            nb->isDirty = true;
            EnsureActiveCellCursorVisible(doc);
            if (wasAcOpen) {
                QueryActiveCellAutocomplete(doc);
            }
            return true;
        }
        case VK_DELETE: {
            const bool wasAcOpen = IsAutocompleteVisible();
            if (activeCell.HasSelection()) {
                activeCell.DeleteSelection();
            } else {
                auto cur = activeCell.inputBuffer.GetCursor();
                const auto& lines = activeCell.inputBuffer.GetLines();
                if (cur.line < lines.size()) {
                    if (cur.col < lines[cur.line].length() || cur.line + 1 < lines.size()) {
                        activeCell.inputBuffer.MoveCursorRight();
                        activeCell.inputBuffer.Backspace();
                    }
                }
            }
            nb->isDirty = true;
            EnsureActiveCellCursorVisible(doc);
            if (wasAcOpen) {
                QueryActiveCellAutocomplete(doc);
            }
            return true;
        }
        case VK_TAB: {
            if (activeCell.type == editor::NotebookCellType::Markdown && !activeCell.isEditingMarkdown) {
                return true;
            }
            if (activeCell.HasSelection()) {
                size_t sL = 0, sC = 0, eL = 0, eC = 0;
                activeCell.GetSelectionRange(sL, sC, eL, eC);
                if (eL > sL || isShift) {
                    activeCell.inputBuffer.PushUndo();
                    std::vector<std::wstring> lines = activeCell.inputBuffer.GetLines();
                    for (size_t li = sL; li <= eL && li < lines.size(); ++li) {
                        if (isShift) {
                            size_t rm = 0;
                            while (rm < 2 && rm < lines[li].size() && lines[li][rm] == L' ') ++rm;
                            if (rm == 0 && !lines[li].empty() && lines[li][0] == L'\t') rm = 1;
                            if (rm > 0) lines[li].erase(0, rm);
                        } else {
                            lines[li].insert(0, L"  ");
                        }
                    }
                    activeCell.inputBuffer.SetLines(lines);
                    activeCell.selAnchor = { sL, 0 };
                    activeCell.selEnd = { eL, (eL < lines.size()) ? lines[eL].size() : 0 };
                    activeCell.inputBuffer.SetCursor(eL, (eL < lines.size()) ? lines[eL].size() : 0);
                    nb->isDirty = true;
                    return true;
                }
                activeCell.DeleteSelection();
            } else if (isShift) {
                auto cur = activeCell.inputBuffer.GetCursor();
                if (cur.line < activeCell.inputBuffer.GetLineCount()) {
                    std::wstring line = activeCell.inputBuffer.GetLine(cur.line);
                    size_t rm = 0;
                    while (rm < 2 && rm < line.size() && line[rm] == L' ') ++rm;
                    if (rm == 0 && !line.empty() && line[0] == L'\t') rm = 1;
                    if (rm > 0) {
                        activeCell.inputBuffer.PushUndo();
                        line.erase(0, rm);
                        activeCell.inputBuffer.SetLine(cur.line, line);
                        activeCell.inputBuffer.SetCursor(cur.line, cur.col > rm ? cur.col - rm : 0);
                        nb->isDirty = true;
                    }
                }
                return true;
            }
            activeCell.inputBuffer.InsertText(L"  "); // 2 spaces (Colab / Jupyter standard)
            nb->isDirty = true;
            return true;
        }
        case VK_LEFT:
            DismissAutocomplete();
            if (activeCell.HasSelection() && !isShift) {
                size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                activeCell.GetSelectionRange(sLine, sCol, eLine, eCol);
                activeCell.ClearSelection();
                activeCell.inputBuffer.SetCursor(sLine, sCol);
            } else {
                activeCell.inputBuffer.MoveCursorLeft();
            }
            EnsureActiveCellCursorVisible(doc);
            return true;
        case VK_RIGHT:
            DismissAutocomplete();
            if (activeCell.HasSelection() && !isShift) {
                size_t sLine = 0, sCol = 0, eLine = 0, eCol = 0;
                activeCell.GetSelectionRange(sLine, sCol, eLine, eCol);
                activeCell.ClearSelection();
                activeCell.inputBuffer.SetCursor(eLine, eCol);
            } else {
                activeCell.inputBuffer.MoveCursorRight();
            }
            EnsureActiveCellCursorVisible(doc);
            return true;
        case VK_UP:
            DismissAutocomplete();
            if (activeCell.HasSelection() && !isShift) activeCell.ClearSelection();
            activeCell.inputBuffer.MoveCursorUp();
            EnsureActiveCellCursorVisible(doc);
            return true;
        case VK_DOWN:
            DismissAutocomplete();
            if (activeCell.HasSelection() && !isShift) activeCell.ClearSelection();
            activeCell.inputBuffer.MoveCursorDown();
            EnsureActiveCellCursorVisible(doc);
            return true;
        case VK_HOME:
            DismissAutocomplete();
            if (activeCell.HasSelection() && !isShift) activeCell.ClearSelection();
            activeCell.inputBuffer.SetCursor(activeCell.inputBuffer.GetCursor().line, 0);
            EnsureActiveCellCursorVisible(doc);
            return true;
        case VK_END: {
            DismissAutocomplete();
            if (activeCell.HasSelection() && !isShift) activeCell.ClearSelection();
            auto cur = activeCell.inputBuffer.GetCursor();
            if (cur.line < activeCell.inputBuffer.GetLineCount()) {
                activeCell.inputBuffer.SetCursor(cur.line, activeCell.inputBuffer.GetLine(cur.line).length());
            }
            EnsureActiveCellCursorVisible(doc);
            return true;
        }
        default:
            break;
    }
    return false;
}

bool NotebookView::HandleChar(wchar_t ch, editor::OpenDocument& doc, HWND hwnd) {
    if (m_suppressCharTarget != 0) {
        wchar_t target = m_suppressCharTarget;
        m_suppressCharTarget = 0;
        if (ch == target) return true;
    }
    if (m_varTableModal.isOpen) return true;
    if (m_lightboxOpen) return true;
    if (!doc.notebookDoc || doc.notebookDoc->cells.empty()) return false;
    auto nb = doc.notebookDoc;
    if (nb->activeCellIdx < 0 || nb->activeCellIdx >= static_cast<int>(nb->cells.size())) return false;

    // Filter control characters handled in HandleKeyDown
    if (ch < 32 && ch != L'\t') return false;

    auto& activeCell = nb->cells[nb->activeCellIdx];
    if (activeCell.type == editor::NotebookCellType::Markdown && !activeCell.isEditingMarkdown && !activeCell.isWaitingForInput) {
        return false;
    }
    if (activeCell.isWaitingForInput) {
        if (ch >= 32) {
            activeCell.inputFieldValue += ch;
            m_caretVisible = true;
            m_lastBlinkTime = GetTickCount64();
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        return false;
    }

    // Auto-closing bracket/quote pair mapping
    wchar_t pairClose = 0;
    if (ch == L'(') pairClose = L')';
    else if (ch == L'[') pairClose = L']';
    else if (ch == L'{') pairClose = L'}';
    else if (ch == L'"') pairClose = L'"';
    else if (ch == L'\'') pairClose = L'\'';
    else if (ch == L'`') pairClose = L'`';

    // 1. Wrap active selection when typing bracket/quote opener
    if (activeCell.HasSelection()) {
        if (pairClose != 0) {
            std::wstring sel = activeCell.GetSelectedText();
            activeCell.DeleteSelection();
            std::wstring wrapped = std::wstring(1, ch) + sel + pairClose;
            activeCell.inputBuffer.InsertText(wrapped);
            nb->isDirty = true;
            m_caretVisible = true;
            m_lastBlinkTime = GetTickCount64();
            EnsureActiveCellCursorVisible(doc);
            DismissAutocomplete();
            return true;
        } else {
            activeCell.DeleteSelection();
        }
    }

    // 2. Overtype closing character if cursor is immediately before it
    auto cur = activeCell.inputBuffer.GetCursor();
    const auto& lines = activeCell.inputBuffer.GetLines();
    if (cur.line < lines.size()) {
        const auto& line = lines[cur.line];
        if (cur.col < line.length() && line[cur.col] == ch) {
            if (ch == L')' || ch == L']' || ch == L'}' || ch == L'"' || ch == L'\'' || ch == L'`') {
                // Step over closing character
                activeCell.inputBuffer.MoveCursorRight();
                nb->isDirty = true;
                m_caretVisible = true;
                m_lastBlinkTime = GetTickCount64();
                EnsureActiveCellCursorVisible(doc);
                DismissAutocomplete();
                return true;
            }
        }
    }

    // 3. Auto-insert pair and position cursor inside
    if (pairClose != 0) {
        std::wstring pairStr;
        pairStr += ch;
        pairStr += pairClose;
        activeCell.inputBuffer.InsertText(pairStr);
        activeCell.inputBuffer.MoveCursorLeft();
        nb->isDirty = true;
        m_caretVisible = true;
        m_lastBlinkTime = GetTickCount64();
        EnsureActiveCellCursorVisible(doc);
        DismissAutocomplete();
        return true;
    }

    // Normal character insertion
    activeCell.inputBuffer.InsertChar(ch);
    nb->isDirty = true;
    m_caretVisible = true;
    m_lastBlinkTime = GetTickCount64();
    EnsureActiveCellCursorVisible(doc);
    if (activeCell.type == editor::NotebookCellType::Code && (iswalnum(ch) || ch == L'_' || ch == L'.')) {
        QueryActiveCellAutocomplete(doc);
    } else {
        DismissAutocomplete();
    }
    return true;
}

LPCWSTR NotebookView::GetCursorForPoint(POINT pt, const editor::OpenDocument& doc) const {
    auto PointInRect = [](POINT p, const D2D1_RECT_F& r) -> bool {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    };

    if (IsAutocompleteVisible()) {
        int hitIdx = -1;
        if (EditorCanvas::HitTestAutocompletePanel(
                pt, m_nbAutocompleteBounds, m_nbAutocompleteItems.size(), m_nbAutocompleteScrollOffset, hitIdx)) {
            return (hitIdx >= 0) ? IDC_HAND : IDC_ARROW;
        }
    }

    for (const auto& sw : m_visibleColorSwatches) {
        if (static_cast<float>(pt.x) >= sw.bounds.left - 3.0f && static_cast<float>(pt.x) <= sw.bounds.right + 3.0f &&
            static_cast<float>(pt.y) >= sw.bounds.top - 3.0f && static_cast<float>(pt.y) <= sw.bounds.bottom + 3.0f) {
            return IDC_HAND;
        }
    }
    for (const auto& lk : m_visibleMdLinks) {
        if (PointInRect(pt, lk.bounds)) {
            return IDC_HAND;
        }
    }
    for (const auto& img : m_visibleMdImages) {
        if (PointInRect(pt, img.bounds)) {
            return IDC_HAND;
        }
    }

    if (m_varTableModal.isOpen) {
        return PointInRect(pt, m_varTableModal.rcCloseBtn) ? IDC_HAND : IDC_ARROW;
    }
    if (m_lightboxOpen) {
        if (PointInRect(pt, m_lightboxBtnClose) || PointInRect(pt, m_lightboxBtnSave) ||
            PointInRect(pt, m_lightboxBtnCopy) || PointInRect(pt, m_lightboxBtnZoomIn) ||
            PointInRect(pt, m_lightboxBtnReset) || PointInRect(pt, m_lightboxBtnZoomOut)) {
            return IDC_HAND;
        }
        if (PointInRect(pt, m_lightboxCanvasBounds)) {
            return IDC_SIZEALL;
        }
        return IDC_ARROW;
    }

    if (m_isDraggingCell) return IDC_SIZEALL;

    // Output and main scrollbar dragging cursors (Vertical ↕, Horizontal ↔)
    if (m_isDraggingScrollbar || m_isDraggingOutputVScroll) return IDC_SIZENS;
    if (m_isDraggingOutputHScroll) return IDC_SIZEWE;

    // Main notebook scrollbar thumb & track -> IDC_SIZENS (Vertical double-arrow ↕, Image 4)
    if (PointInRect(pt, m_scrollThumb) || PointInRect(pt, m_scrollTrack)) {
        return IDC_SIZENS;
    }

    if (PointInRect(pt, m_btnRunAll) || PointInRect(pt, m_btnRunCell) ||
        PointInRect(pt, m_btnClearAll) || PointInRect(pt, m_btnAddCode) ||
        PointInRect(pt, m_btnAddText) || PointInRect(pt, m_btnCutCell) ||
        PointInRect(pt, m_btnCopyCell) || PointInRect(pt, m_btnPasteCell) ||
        PointInRect(pt, m_btnCellTypePicker) || PointInRect(pt, m_btnKernelPicker) ||
        PointInRect(pt, m_btnRestartKernel) || PointInRect(pt, m_btnInterruptKernel) ||
        PointInRect(pt, m_btnVariables) || PointInRect(pt, m_btnVarTable)) {
        return IDC_HAND;
    }
    if (m_cellTypeDropdownOpen && PointInRect(pt, m_cellTypeDropdownBounds)) {
        return IDC_HAND;
    }
    if (doc.notebookDoc && doc.notebookDoc->showVariableInspector) {
        for (const auto& rcTb : m_varInspectBtnBounds) {
            if (PointInRect(pt, rcTb)) return IDC_HAND;
        }
    }
    if (m_kernelDropdownOpen && PointInRect(pt, m_kernelDropdownBounds)) {
        return IDC_HAND;
    }

    if (doc.notebookDoc) {
        for (const auto& cell : doc.notebookDoc->cells) {
            // Cell output vertical scrollbar -> IDC_SIZENS (Vertical double-arrow ↕, Image 4)
            if (cell.outputMaxScrollY > 0.0f) {
                if (PointInRect(pt, cell.outputVScrollThumbBounds) || PointInRect(pt, cell.outputVScrollTrackBounds)) {
                    return IDC_SIZENS;
                }
            }
            // Cell output horizontal scrollbar -> IDC_SIZEWE (Horizontal double-arrow ↔, Image 4)
            if (cell.outputMaxScrollX > 0.0f) {
                if (PointInRect(pt, cell.outputHScrollThumbBounds) || PointInRect(pt, cell.outputHScrollTrackBounds)) {
                    return IDC_SIZEWE;
                }
            }
            // Code box horizontal scrollbar -> IDC_SIZEWE
            if (cell.codeMaxScrollX > 0.0f) {
                if (PointInRect(pt, cell.codeHScrollThumbBounds) || PointInRect(pt, cell.codeHScrollTrackBounds)) {
                    return IDC_SIZEWE;
                }
            }

            if (PointInRect(pt, cell.dragHandleBounds)) {
                return IDC_SIZEALL;
            }
            if (PointInRect(pt, cell.submitBtnBounds)) {
                return IDC_HAND;
            }
            if (PointInRect(pt, cell.runBtnBounds) || PointInRect(pt, cell.deleteBtnBounds) ||
                PointInRect(pt, cell.collapseInputBtnBounds) ||
                PointInRect(pt, cell.duplicateBtnBounds) || PointInRect(pt, cell.editMarkdownBtnBounds) ||
                PointInRect(pt, cell.moveUpBtnBounds) || PointInRect(pt, cell.moveDownBtnBounds) ||
                PointInRect(pt, cell.outputCopyBtnBounds) || PointInRect(pt, cell.outputCollapseBtnBounds) ||
                PointInRect(pt, cell.outputClearBtnBounds) ||
                PointInRect(pt, cell.bottomAddCodeBounds) || PointInRect(pt, cell.bottomAddTextBounds) ||
                PointInRect(pt, cell.mdBtnBoldBounds) || PointInRect(pt, cell.mdBtnItalicBounds) ||
                PointInRect(pt, cell.mdBtnQuoteBounds) || PointInRect(pt, cell.mdBtnLinkBounds) ||
                PointInRect(pt, cell.mdBtnImageBounds) || PointInRect(pt, cell.mdBtnBulletListBounds) ||
                PointInRect(pt, cell.mdBtnNumberListBounds) || PointInRect(pt, cell.mdBtnHeadingBounds) ||
                PointInRect(pt, cell.mdBtnDoneBounds)) {
                return IDC_HAND;
            }
            for (const auto& out : cell.outputs) {
                if (out.imagePngBytes.empty()) continue;
                if (PointInRect(pt, out.imgZoomBtnBounds) || PointInRect(pt, out.imgCopyBtnBounds) ||
                    PointInRect(pt, out.imgSaveBtnBounds) || PointInRect(pt, out.renderedImageBounds)) {
                    return IDC_HAND;
                }
            }
            if (PointInRect(pt, cell.codeAreaBounds)) {
                return (cell.type == editor::NotebookCellType::Markdown && !cell.isEditingMarkdown) ? IDC_ARROW : IDC_IBEAM;
            }
            if (PointInRect(pt, cell.outputAreaBounds)) {
                return IDC_IBEAM;
            }
        }
    }
    return IDC_ARROW;
}

static bool IsValidPythonExe(const std::wstring& path) {
    if (path.empty()) return false;
    if (path.find(L"WindowsApps") != std::wstring::npos) return false;
    if (path.find(L"_uninstalling_") != std::wstring::npos) return false;
    if (path.find(L"Quantix") != std::wstring::npos) return false;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return false;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    auto sz = std::filesystem::file_size(path, ec);
    if (ec || sz < 10240) return false; // Exclude 0-byte reparse points / stubs
    return true;
}

std::vector<std::wstring> NotebookView::DiscoverPythonExecutables(const std::wstring& targetFilePath) const {
    std::vector<std::wstring> results;
    auto AddCandidate = [&results](const std::wstring& cand) {
        if (!IsValidPythonExe(cand)) return;
        for (const auto& existing : results) {
            if (_wcsicmp(existing.c_str(), cand.c_str()) == 0) return;
        }
        results.push_back(cand);
    };

    // 1. Check local virtual environments in the notebook's directory or parent directories
    std::vector<std::wstring> candidateDirs;
    if (!targetFilePath.empty()) {
        try {
            std::filesystem::path p(targetFilePath);
            auto cur = p.has_filename() ? p.parent_path() : p;
            for (int depth = 0; depth < 3 && !cur.empty(); ++depth) {
                candidateDirs.push_back(cur.wstring());
                auto parent = cur.parent_path();
                if (parent == cur) break;
                cur = parent;
            }
        } catch (...) {}
    }
    if (!m_workspacePath.empty()) {
        candidateDirs.push_back(m_workspacePath);
    }

    const wchar_t* venvNames[] = { L".venv", L"venv", L"env", L".env" };
    for (const auto& dir : candidateDirs) {
        for (const auto* vname : venvNames) {
            AddCandidate(dir + L"\\" + vname + L"\\Scripts\\python.exe");
        }
    }

    // 2. Check phide's own downloaded runtimes
    wchar_t exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    std::vector<std::filesystem::path> runtimeCandidates = {
        exeDir / L"runtimes" / L"python" / L"python.exe",
        exeDir.parent_path() / L"runtimes" / L"python" / L"python.exe",
        L"C:\\Program Files\\phide\\runtimes\\python\\python.exe"
    };
    for (const auto& rc : runtimeCandidates) {
        AddCandidate(rc.wstring());
    }

    // 3. Check User Local AppData Python installations sorted newest first
    wchar_t localApp[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH) > 0) {
        std::filesystem::path pyBase = std::filesystem::path(localApp) / L"Programs" / L"Python";
        std::error_code ec;
        if (std::filesystem::exists(pyBase, ec)) {
            try {
                std::vector<std::wstring> appCands;
                for (const auto& entry : std::filesystem::directory_iterator(pyBase, ec)) {
                    if (entry.is_directory()) {
                        std::wstring cand = (entry.path() / L"python.exe").wstring();
                        if (IsValidPythonExe(cand)) appCands.push_back(cand);
                    }
                }
                std::sort(appCands.begin(), appCands.end(), std::greater<std::wstring>());
                for (const auto& c : appCands) AddCandidate(c);
            } catch (...) {}
        }
    }

    // 4. Check python.exe on PATH (skipping WindowsApps execution alias and emsdk)
    wchar_t buf[MAX_PATH]{};
    if (SearchPathW(nullptr, L"python.exe", nullptr, MAX_PATH, buf, nullptr) > 0) {
        std::wstring pathPy = buf;
        if (pathPy.find(L"emsdk") == std::wstring::npos) {
            AddCandidate(pathPy);
        }
    }

    // 5. Check standard system installations
    const wchar_t* rootCandidates[] = {
        L"C:\\Program Files\\Python315\\python.exe",
        L"C:\\Program Files\\Python314\\python.exe",
        L"C:\\Program Files\\Python313\\python.exe",
        L"C:\\Program Files\\Python312\\python.exe",
        L"C:\\Program Files\\Python311\\python.exe",
        L"C:\\Program Files\\Python310\\python.exe",
        L"C:\\Python315\\python.exe",
        L"C:\\Python313\\python.exe",
        L"C:\\Python312\\python.exe",
        L"C:\\Python311\\python.exe"
    };
    for (const auto* rc : rootCandidates) {
        AddCandidate(rc);
    }

    // 6. Fallback: dedicated EMSDK Python if present
    AddCandidate(L"C:\\Users\\Administrator\\Downloads\\emsdk\\python\\3.13.3_64bit\\python.exe");
    return results;
}

std::wstring NotebookView::FindPythonExecutable(const std::wstring& targetFilePath) const {
    auto found = DiscoverPythonExecutables(targetFilePath);
    if (!found.empty()) {
        return found.front();
    }

    // Fallback: query py.exe -0p on PATH if no standard install was discovered
    wchar_t buf[MAX_PATH]{};
    if (SearchPathW(nullptr, L"py.exe", nullptr, MAX_PATH, buf, nullptr) > 0) {
        HANDLE hRead = nullptr, hWrite = nullptr;
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        if (CreatePipe(&hRead, &hWrite, &sa, 0)) {
            SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            si.wShowWindow = SW_HIDE;
            si.hStdOutput = hWrite;
            si.hStdError = hWrite;
            PROCESS_INFORMATION pi{};
            std::wstring pyCmd = L"\"" + std::wstring(buf) + L"\" -0p";
            std::vector<wchar_t> cmdBuf(pyCmd.begin(), pyCmd.end());
            cmdBuf.push_back(0);
            if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                CloseHandle(hWrite);
                hWrite = nullptr;
                char pipeBuf[512];
                DWORD bytesRead = 0;
                std::string raw;
                while (ReadFile(hRead, pipeBuf, sizeof(pipeBuf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                    raw.append(pipeBuf, bytesRead);
                }
                WaitForSingleObject(pi.hProcess, 1500);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);

                if (!raw.empty()) {
                    std::wstring outStr = NbUtf8ToWide(raw);
                    std::wstringstream ss(outStr);
                    std::wstring line;
                    while (std::getline(ss, line)) {
                        auto colIdx = line.find(L":\\");
                        if (colIdx != std::wstring::npos && colIdx > 0) {
                            std::wstring candExe = line.substr(colIdx - 1);
                            while (!candExe.empty() && (candExe.back() == L' ' || candExe.back() == L'\t' || candExe.back() == L'\r' || candExe.back() == L'\n')) {
                                candExe.pop_back();
                            }
                            if (IsValidPythonExe(candExe)) {
                                CloseHandle(hRead);
                                return candExe;
                            }
                        }
                    }
                }
            }
            if (hWrite) CloseHandle(hWrite);
            if (hRead) CloseHandle(hRead);
        }
    }

    return L"";
}

void NotebookView::RunCell(int cellIdx, editor::OpenDocument& doc, HWND hwnd) {
    if (!doc.notebookDoc || cellIdx < 0 || cellIdx >= static_cast<int>(doc.notebookDoc->cells.size())) return;
    auto nb = doc.notebookDoc;
    auto& cell = nb->cells[cellIdx];

    // Markdown cells don't execute; they just remain formatted
    if (cell.type == editor::NotebookCellType::Markdown) {
        cell.executionCount = -1;
        return;
    }

    // If another cell is already running, enqueue this cell into the execution queue
    if (m_isExecuting.load()) {
        std::lock_guard<std::mutex> qLock(m_queueMutex);
        if (m_executingCellIdx.load() == cellIdx) {
            return; // Already actively executing
        }
        bool alreadyInQueue = false;
        for (int q : m_cellQueue) {
            if (q == cellIdx) {
                alreadyInQueue = true;
                break;
            }
        }
        if (!alreadyInQueue) {
            m_cellQueue.push_back(cellIdx);
            cell.isQueued = true;
            cell.isExecuting = false;
            cell.executionCount = -2; // shows [*] while waiting in queue
        }
        nb->isDirty = true;
        if (hwnd) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }

    std::wstring code = cell.inputBuffer.GetText();
    std::wstring lang = cell.language;
    auto nbDocPtr = doc.notebookDoc;

    // Fast-path: empty or whitespace-only code cells complete immediately in 0 ms (no freeze)
    bool isWhitespaceOnly = true;
    for (wchar_t ch : code) {
        if (!iswspace(ch)) {
            isWhitespaceOnly = false;
            break;
        }
    }
    if (isWhitespaceOnly) {
        cell.isExecuting = false;
        cell.isQueued = false;
        {
            std::lock_guard<std::recursive_mutex> viewLock(m_outputsMutex);
            std::unique_lock<std::recursive_mutex> docLock(nb->outputsMutex ? *nb->outputsMutex : m_outputsMutex);
            cell.outputs.clear();
        }
        cell.hasError = false;
        cell.errorLine = -1;
        cell.lastElapsedMs = 0;
        if (nb->nextExecutionCount < 1) {
            nb->nextExecutionCount = 1;
        }
        cell.executionCount = nb->nextExecutionCount++;
        nb->isDirty = true;
        if (hwnd) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        // Trigger next queued cell if any
        int nextCellIdx = -1;
        {
            std::lock_guard<std::mutex> qLock(m_queueMutex);
            while (!m_cellQueue.empty()) {
                int cand = m_cellQueue.front();
                m_cellQueue.erase(m_cellQueue.begin());
                if (cand >= 0 && cand < static_cast<int>(nb->cells.size())) {
                    nextCellIdx = cand;
                    break;
                }
            }
        }
        if (nextCellIdx >= 0) {
            RunCell(nextCellIdx, doc, hwnd);
        }
        return;
    }

    // Join any prior worker BEFORE arming state for the new cell so its teardown
    // cannot overwrite m_isExecuting / m_executingCellIdx.
    if (m_execThread.joinable()) {
        m_execThread.join();
    }

    cell.isExecuting = true;
    cell.isQueued = false;
    {
        std::lock_guard<std::recursive_mutex> viewLock(m_outputsMutex);
        std::unique_lock<std::recursive_mutex> docLock(nb->outputsMutex ? *nb->outputsMutex : m_outputsMutex);
        cell.outputs.clear();
    }
    cell.hasError = false;
    cell.errorLine = -1;
    m_isExecuting.store(true);
    m_executingCellIdx.store(cellIdx);
    m_shouldStop.store(false);

    if (hwnd) {
        SetTimer(hwnd, 1002, 16, nullptr); // Start 60 FPS animation timer immediately
    }

    if (!m_isDestroying.load()) {
        m_activeWorkers.fetch_add(1);
        m_execThread = std::thread([this, cellIdx, code, lang, nbDocPtr, hwnd]() {
            struct WorkerGuard {
                std::atomic<int>& counter;
                ~WorkerGuard() { counter.fetch_sub(1); }
            } guard{ m_activeWorkers };
            ExecuteScriptAsync(cellIdx, code, lang, nbDocPtr, hwnd);
        });
    }
}

void NotebookView::RunAllCells(editor::OpenDocument& doc, HWND hwnd) {
    if (!doc.notebookDoc || doc.notebookDoc->cells.empty()) return;
    m_shouldStop.store(false);
    m_isQueueRunning.store(true);

    // Find first Code cell from the top (cell 0 or lowest index)
    int firstCodeIdx = -1;
    for (size_t i = 0; i < doc.notebookDoc->cells.size(); ++i) {
        if (doc.notebookDoc->cells[i].type == editor::NotebookCellType::Code) {
            firstCodeIdx = static_cast<int>(i);
            break;
        }
    }

    if (firstCodeIdx >= 0) {
        RunCell(firstCodeIdx, doc, hwnd);
    } else {
        m_isQueueRunning.store(false);
    }
}

void NotebookView::StopExecution(editor::OpenDocument& doc) {
    StopExecution_Internal();
    if (doc.notebookDoc) {
        for (auto& c : doc.notebookDoc->cells) {
            c.isExecuting = false;
            c.isQueued = false;
        }
    }
}

void NotebookView::RefreshPythonKernels(editor::OpenDocument& doc) {
    if (!doc.notebookDoc) return;
    auto nb = doc.notebookDoc;
    std::vector<std::wstring> found = DiscoverPythonExecutables(nb->filePath);
    const bool wsChanged = (nb->discoveredWorkspaceVersion != m_workspaceVersion);
    if (wsChanged && !nb->userSelectedKernel && !found.empty()) {
        nb->selectedPythonExe = found.front();
    } else if (!nb->selectedPythonExe.empty() && IsValidPythonExe(nb->selectedPythonExe)) {
        bool already = false;
        for (const auto& f : found) {
            if (_wcsicmp(f.c_str(), nb->selectedPythonExe.c_str()) == 0) {
                already = true;
                break;
            }
        }
        if (!already) {
            found.insert(found.begin(), nb->selectedPythonExe);
        }
    } else if (!found.empty()) {
        nb->selectedPythonExe = found.front();
    }
    nb->discoveredPythonExes = std::move(found);
    nb->discoveredWorkspaceVersion = m_workspaceVersion;
}

void NotebookView::RestartKernel(editor::OpenDocument& doc, HWND hwnd) {
    StopExecution(doc);
    m_kernel.Restart();
    if (doc.notebookDoc) {
        std::lock_guard<std::recursive_mutex> viewLock(m_outputsMutex);
        std::unique_lock<std::recursive_mutex> docLock(doc.notebookDoc->outputsMutex ? *doc.notebookDoc->outputsMutex : m_outputsMutex);
        doc.notebookDoc->ClearAllOutputs();
        doc.notebookDoc->inspectorVariables.clear();
    }
    if (!m_isDestroying.load() && hwnd && IsWindow(hwnd)) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void NotebookView::QueryVariablesAsync(const std::shared_ptr<editor::NotebookDocument>& nbDoc, HWND hwnd) {
    if (!nbDoc || m_isDestroying.load()) return;
    m_activeWorkers.fetch_add(1);
    std::thread([this, nbDoc, hwnd]() {
        struct WorkerGuard {
            std::atomic<int>& counter;
            ~WorkerGuard() { counter.fetch_sub(1); }
        } guard{ m_activeWorkers };
        if (m_isDestroying.load()) return;
        std::string varsJson;
        if (m_kernel.QueryVariablesJson(varsJson) && !m_isDestroying.load()) {
            auto parsed = ParseNotebookVarsJson(varsJson);
            {
                std::lock_guard<std::recursive_mutex> viewLock(m_outputsMutex);
                std::unique_lock<std::recursive_mutex> docLock(nbDoc->outputsMutex ? *nbDoc->outputsMutex : m_outputsMutex);
                nbDoc->inspectorVariables = std::move(parsed);
            }
            if (!m_isDestroying.load() && hwnd && IsWindow(hwnd)) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
    }).detach();
}

void NotebookView::CloseVariableTableModal() noexcept {
    m_varTableModal.isOpen = false;
    m_varTableModal.isLoading = false;
}

bool NotebookView::ParseVariableTableJson(const std::string& jsonStr, VariableTableModalState& outState) {
    outState.columns.clear();
    outState.rows.clear();
    outState.colWidths.clear();
    outState.truncated = false;
    outState.shapeLabel.clear();
    outState.typeName.clear();
    outState.statusMessage.clear();
    if (jsonStr.empty()) return false;

    auto skipWs = [&](size_t& p) {
        while (p < jsonStr.size() && (jsonStr[p] == ' ' || jsonStr[p] == '\t' || jsonStr[p] == '\r' || jsonStr[p] == '\n')) ++p;
    };
    auto readStringField = [&](const char* fieldName, std::wstring& outVal) {
        std::string needle = std::string("\"") + fieldName + "\"";
        size_t pos = jsonStr.find(needle);
        if (pos == std::string::npos) return;
        size_t colon = jsonStr.find(':', pos + needle.size());
        if (colon == std::string::npos) return;
        size_t p = colon + 1;
        skipWs(p);
        if (p < jsonStr.size() && jsonStr[p] == '"') {
            outVal = NbUtf8ToWide(UnescapeJsonField(jsonStr, p));
        }
    };

    std::wstring parsedName;
    readStringField("name", parsedName);
    if (!parsedName.empty()) outState.varName = std::move(parsedName);
    readStringField("type", outState.typeName);
    readStringField("shape", outState.shapeLabel);
    if (jsonStr.find("\"truncated\":true") != std::string::npos ||
        jsonStr.find("\"truncated\": true") != std::string::npos) {
        outState.truncated = true;
    }

    size_t colsKey = jsonStr.find("\"columns\"");
    if (colsKey != std::string::npos) {
        size_t arrStart = jsonStr.find('[', colsKey);
        if (arrStart != std::string::npos) {
            size_t p = arrStart + 1;
            while (p < jsonStr.size()) {
                skipWs(p);
                if (p >= jsonStr.size() || jsonStr[p] == ']') break;
                if (jsonStr[p] == '"') {
                    outState.columns.push_back(NbUtf8ToWide(UnescapeJsonField(jsonStr, p)));
                } else {
                    ++p;
                }
            }
        }
    }

    size_t rowsKey = jsonStr.find("\"rows\"");
    if (rowsKey != std::string::npos) {
        size_t outerStart = jsonStr.find('[', rowsKey);
        if (outerStart != std::string::npos) {
            size_t p = outerStart + 1;
            while (p < jsonStr.size() && outState.rows.size() < 200u) {
                skipWs(p);
                if (p >= jsonStr.size() || jsonStr[p] == ']') break;
                if (jsonStr[p] == '[') {
                    ++p;
                    std::vector<std::wstring> row;
                    while (p < jsonStr.size()) {
                        skipWs(p);
                        if (p >= jsonStr.size() || jsonStr[p] == ']') { ++p; break; }
                        if (jsonStr[p] == '"') {
                            row.push_back(NbUtf8ToWide(UnescapeJsonField(jsonStr, p)));
                        } else if (jsonStr[p] == ',') {
                            ++p;
                        } else {
                            size_t tokEnd = jsonStr.find_first_of(",]", p);
                            if (tokEnd == std::string::npos) break;
                            row.push_back(NbUtf8ToWide(jsonStr.substr(p, tokEnd - p)));
                            p = tokEnd;
                        }
                    }
                    outState.rows.push_back(std::move(row));
                } else {
                    ++p;
                }
            }
        }
    }

    return !outState.columns.empty() || !outState.rows.empty();
}

void NotebookView::InspectVariableTableAsync(const std::wstring& varName, const std::wstring& typeHint, HWND hwnd) {
    if (m_isDestroying.load() || varName.empty()) return;
    {
        std::lock_guard<std::recursive_mutex> lk(m_outputsMutex);
        m_varTableModal.isOpen = true;
        m_varTableModal.isLoading = true;
        m_varTableModal.varName = varName;
        m_varTableModal.typeName = typeHint;
        m_varTableModal.shapeLabel.clear();
        m_varTableModal.statusMessage.clear();
        m_varTableModal.truncated = false;
        m_varTableModal.columns.clear();
        m_varTableModal.rows.clear();
        m_varTableModal.colWidths.clear();
        m_varTableModal.scrollY = 0.0f;
        m_varTableModal.scrollX = 0.0f;
    }
    if (hwnd && IsWindow(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);

    m_activeWorkers.fetch_add(1);
    std::thread([this, varName, hwnd]() {
        struct WorkerGuard {
            std::atomic<int>& counter;
            ~WorkerGuard() { counter.fetch_sub(1); }
        } guard{ m_activeWorkers };
        if (m_isDestroying.load()) return;

        std::string tblJson;
        bool ok = m_kernel.QueryVariableTableJson(varName, tblJson);
        if (!m_isDestroying.load()) {
            std::lock_guard<std::recursive_mutex> lk(m_outputsMutex);
            if (m_varTableModal.isOpen && m_varTableModal.varName == varName) {
                m_varTableModal.isLoading = false;
                if (!ok || !ParseVariableTableJson(tblJson, m_varTableModal)) {
                    m_varTableModal.statusMessage = ok
                        ? L"Variable has no tabular slice available."
                        : L"Kernel is busy or offline; run a cell to inspect live values.";
                }
            }
        }
        if (!m_isDestroying.load() && hwnd && IsWindow(hwnd)) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }).detach();
}

void NotebookView::StopExecution_Internal() {
    m_shouldStop.store(true);
    m_isQueueRunning.store(false);
    m_queueNextCellIdx.store(-1);
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_cellQueue.clear();
    }
    m_kernel.Interrupt();
    m_isExecuting.store(false);
    m_executingCellIdx.store(-1);
    std::lock_guard<std::mutex> lock(m_execMutex);
    if (m_hChildProcess) {
        TerminateProcess(m_hChildProcess, 1);
        CloseHandle(m_hChildProcess);
        m_hChildProcess = NULL;
    }
}

void NotebookView::ExecuteScriptAsync(int cellIdx, const std::wstring& script, const std::wstring& lang,
                                      std::shared_ptr<editor::NotebookDocument> nbDoc, HWND hwnd) {
    if (m_isDestroying.load()) return;
    std::wstring docPath = nbDoc ? nbDoc->filePath : L"";
    std::wstring pyExe;
    if (nbDoc && !nbDoc->userSelectedKernel && nbDoc->discoveredWorkspaceVersion != m_workspaceVersion) {
        pyExe = FindPythonExecutable(docPath);
        if (!pyExe.empty()) {
            nbDoc->selectedPythonExe = pyExe;
            nbDoc->discoveredWorkspaceVersion = m_workspaceVersion;
        }
    }
    if (pyExe.empty()) {
        pyExe = (nbDoc && !nbDoc->selectedPythonExe.empty() && IsValidPythonExe(nbDoc->selectedPythonExe))
            ? nbDoc->selectedPythonExe
            : FindPythonExecutable(docPath);
    }
    std::wstring workingDir;
    if (!docPath.empty()) {
        try {
            workingDir = std::filesystem::path(docPath).parent_path().wstring();
        } catch (...) {}
    }
    if (workingDir.empty()) {
        workingDir = m_workspacePath;
    }

    auto onChunk = [this, nbDoc, cellIdx, hwnd](const std::wstring& chunk, bool isPrompt, const std::wstring& promptText) {
        if (m_isDestroying.load() || !nbDoc || cellIdx < 0 || cellIdx >= static_cast<int>(nbDoc->cells.size())) return;
        std::lock_guard<std::recursive_mutex> viewLock(m_outputsMutex);
        std::unique_lock<std::recursive_mutex> docLock((nbDoc && nbDoc->outputsMutex) ? *nbDoc->outputsMutex : m_outputsMutex);
        auto& cell = nbDoc->cells[cellIdx];
        if (!chunk.empty()) {
            if (cell.outputs.empty()) {
                editor::CellOutput out;
                out.streamName = L"stdout";
                out.text = chunk;
                out.isError = false;
                cell.outputs.push_back(std::move(out));
            } else {
                cell.outputs.back().text += chunk;
            }
        }
        if (isPrompt) {
            cell.isWaitingForInput = true;
            cell.inputPrompt = promptText;
            cell.inputFieldValue = L"";
            cell.isInputFieldFocused = true;
        }
        if (!m_isDestroying.load() && hwnd && IsWindow(hwnd)) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    };

    editor::KernelExecutionResult kernelResult;
    if (pyExe.empty()) {
        kernelResult.exitCode = 1;
        kernelResult.hasError = true;
        kernelResult.elapsedMs = 0;
        kernelResult.output = L"Python is not detected on this system. Please install Python and select a Python interpreter to run notebook cells.\n";
    } else {
        kernelResult = m_kernel.Execute(script, pyExe, workingDir, onChunk);
    }

    bool hasErr = kernelResult.hasError;
    std::wstring capturedOutput = kernelResult.output;
    uint64_t elapsedMs = kernelResult.elapsedMs;

    // 3. Update notebook document cell outputs & error state
    if (nbDoc && cellIdx >= 0 && cellIdx < static_cast<int>(nbDoc->cells.size())) {
        std::lock_guard<std::recursive_mutex> viewLock(m_outputsMutex);
        std::unique_lock<std::recursive_mutex> docLock((nbDoc && nbDoc->outputsMutex) ? *nbDoc->outputsMutex : m_outputsMutex);
        auto& cell = nbDoc->cells[cellIdx];
        cell.isExecuting = false;
        cell.isQueued = false;
        cell.hasError = hasErr;
        cell.lastElapsedMs = elapsedMs;
        cell.outputs.clear();

        int errLine = -1;
        if (hasErr) {
            size_t pos = capturedOutput.find(L"File \"<cell>\", line ");
            if (pos != std::wstring::npos) {
                size_t numStart = pos + wcslen(L"File \"<cell>\", line ");
                size_t numEnd = numStart;
                while (numEnd < capturedOutput.length() && iswdigit(capturedOutput[numEnd])) {
                    numEnd++;
                }
                if (numEnd > numStart) {
                    try {
                        int line1 = std::stoi(capturedOutput.substr(numStart, numEnd - numStart));
                        errLine = line1 - 1;
                    } catch (...) {}
                }
            }
            if (errLine < 0) {
                size_t pos2 = capturedOutput.find(L", line ");
                if (pos2 != std::wstring::npos) {
                    size_t numStart = pos2 + wcslen(L", line ");
                    size_t numEnd = numStart;
                    while (numEnd < capturedOutput.length() && iswdigit(capturedOutput[numEnd])) {
                        numEnd++;
                    }
                    if (numEnd > numStart) {
                        try {
                            int line1 = std::stoi(capturedOutput.substr(numStart, numEnd - numStart));
                            errLine = line1 - 1;
                        } catch (...) {}
                    }
                }
            }
        }
        cell.errorLine = errLine;

        // Preserve exact chronological execution order (print before chart -> chart -> print after chart)
        size_t addedImgs = 0;
        if (!kernelResult.segments.empty()) {
            for (const auto& seg : kernelResult.segments) {
                if (seg.isImage) {
                    if (addedImgs >= editor::kMaxNotebookCellImages) continue;
                    editor::CellOutput imgOut;
                    if (editor::DecodeBase64PngBounded(seg.base64Png, imgOut.imagePngBytes) && !imgOut.imagePngBytes.empty()) {
                        editor::ReadPngDimensions(imgOut.imagePngBytes, imgOut.imagePixelW, imgOut.imagePixelH);
                        imgOut.streamName = L"display_data";
                        imgOut.imageId = editor::NextCellImageId();
                        imgOut.elapsedMs = elapsedMs;
                        cell.outputs.push_back(std::move(imgOut));
                        addedImgs++;
                    }
                } else {
                    std::wstring txtW = NbUtf8ToWide(seg.textUtf8);
                    if (!txtW.empty()) {
                        editor::CellOutput out;
                        out.streamName = hasErr ? L"stderr" : L"stdout";
                        out.text = std::move(txtW);
                        out.isError = hasErr;
                        out.elapsedMs = elapsedMs;
                        cell.outputs.push_back(std::move(out));
                    }
                }
            }
        } else {
            if (!capturedOutput.empty()) {
                editor::CellOutput out;
                out.streamName = hasErr ? L"stderr" : L"stdout";
                out.text = capturedOutput;
                out.isError = hasErr;
                out.elapsedMs = elapsedMs;
                cell.outputs.push_back(std::move(out));
            }
            for (const auto& b64 : kernelResult.base64Pngs) {
                if (addedImgs >= editor::kMaxNotebookCellImages) break;
                editor::CellOutput imgOut;
                if (editor::DecodeBase64PngBounded(b64, imgOut.imagePngBytes) && !imgOut.imagePngBytes.empty()) {
                    editor::ReadPngDimensions(imgOut.imagePngBytes, imgOut.imagePixelW, imgOut.imagePixelH);
                    imgOut.streamName = L"display_data";
                    imgOut.imageId = editor::NextCellImageId();
                    imgOut.elapsedMs = elapsedMs;
                    cell.outputs.push_back(std::move(imgOut));
                    addedImgs++;
                }
            }
        }

        // Monotonic Jupyter execution counter across the notebook ([1], [2], [3], ...)
        if (nbDoc->nextExecutionCount < 1) {
            nbDoc->nextExecutionCount = 1;
        }
        cell.executionCount = nbDoc->nextExecutionCount++;
        nbDoc->isDirty = true;
    }

    if (nbDoc && !hasErr && !m_isDestroying.load() && nbDoc->showVariableInspector) {
        std::string varsJson;
        if (m_kernel.QueryVariablesJson(varsJson) && !m_isDestroying.load()) {
            auto parsed = ParseNotebookVarsJson(varsJson);
            std::lock_guard<std::recursive_mutex> viewLock(m_outputsMutex);
            std::unique_lock<std::recursive_mutex> docLock(nbDoc->outputsMutex ? *nbDoc->outputsMutex : m_outputsMutex);
            nbDoc->inspectorVariables = std::move(parsed);
        }
    }

    m_isExecuting.store(false);
    m_executingCellIdx.store(-1);

    if (!m_isDestroying.load() && hwnd && IsWindow(hwnd)) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    // 4. Check if there are user-queued cells waiting to run (dashed spinner queue)
    int queuedCellIdx = -1;
    {
        std::lock_guard<std::mutex> qLock(m_queueMutex);
        while (!m_cellQueue.empty()) {
            int cand = m_cellQueue.front();
            m_cellQueue.erase(m_cellQueue.begin());
            if (nbDoc && cand >= 0 && cand < static_cast<int>(nbDoc->cells.size())) {
                queuedCellIdx = cand;
                break;
            }
        }
    }

    if (queuedCellIdx >= 0 && !m_shouldStop.load() && !m_isDestroying.load() && nbDoc) {
        std::wstring nextCode;
        std::wstring nextLang;
        {
            std::lock_guard<std::recursive_mutex> viewLock(m_outputsMutex);
            std::unique_lock<std::recursive_mutex> docLock((nbDoc && nbDoc->outputsMutex) ? *nbDoc->outputsMutex : m_outputsMutex);
            auto& nextCell = nbDoc->cells[queuedCellIdx];
            nextCell.isQueued = false;
            nextCell.isExecuting = true;
            nextCell.outputs.clear();
            nextCell.hasError = false;
            nextCell.errorLine = -1;
            nextCode = nextCell.inputBuffer.GetText();
            nextLang = nextCell.language;
        }
        m_isExecuting.store(true);
        m_executingCellIdx.store(queuedCellIdx);

        m_activeWorkers.fetch_add(1);
        std::thread([this, queuedCellIdx, nextCode, nextLang, nbDoc, hwnd]() {
            struct WorkerGuard {
                std::atomic<int>& counter;
                ~WorkerGuard() { counter.fetch_sub(1); }
            } guard{ m_activeWorkers };
            ExecuteScriptAsync(queuedCellIdx, nextCode, nextLang, nbDoc, hwnd);
        }).detach();
        if (!m_isDestroying.load() && hwnd && IsWindow(hwnd)) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }

    // 5. Sequential execution for RunAll: run 1-by-1 in order from top to bottom
    // If execution fails, halt sequential queue immediately (Colab/Jupyter standard)
    if (m_isQueueRunning.load() && !m_shouldStop.load() && !m_isDestroying.load() && !hasErr && nbDoc) {
        int nextIdx = cellIdx + 1;
        while (nextIdx < static_cast<int>(nbDoc->cells.size()) &&
               nbDoc->cells[nextIdx].type != editor::NotebookCellType::Code) {
            nextIdx++;
        }
        if (nextIdx < static_cast<int>(nbDoc->cells.size())) {
            std::wstring nextCode;
            std::wstring nextLang;
            {
                std::lock_guard<std::recursive_mutex> viewLock(m_outputsMutex);
                std::unique_lock<std::recursive_mutex> docLock((nbDoc && nbDoc->outputsMutex) ? *nbDoc->outputsMutex : m_outputsMutex);
                auto& nextCell = nbDoc->cells[nextIdx];
                nextCell.isExecuting = true;
                nextCell.outputs.clear();
                nextCell.hasError = false;
                nextCode = nextCell.inputBuffer.GetText();
                nextLang = nextCell.language;
            }
            m_isExecuting.store(true);
            m_executingCellIdx.store(nextIdx);

            m_activeWorkers.fetch_add(1);
            std::thread([this, nextIdx, nextCode, nextLang, nbDoc, hwnd]() {
                struct WorkerGuard {
                    std::atomic<int>& counter;
                    ~WorkerGuard() { counter.fetch_sub(1); }
                } guard{ m_activeWorkers };
                ExecuteScriptAsync(nextIdx, nextCode, nextLang, nbDoc, hwnd);
            }).detach();
            return;
        } else {
            m_isQueueRunning.store(false);
        }
    } else {
        m_isQueueRunning.store(false);
    }
}

} // namespace phide::ui
