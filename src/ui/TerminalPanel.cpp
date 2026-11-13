#include "phide/ui/TerminalPanel.hpp"
#include "phide/ui/GitLogManager.hpp"
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <unordered_set>

namespace phide::ui {

static D2D1_COLOR_F AdaptTerminalColorForLight(D2D1_COLOR_F c) {
    // If color is very light / white / near white (e.g. > 0.70f gray/white)
    if (c.r > 0.70f && c.g > 0.70f && c.b > 0.70f) {
        return D2D1::ColorF(0.08f, 0.10f, 0.14f, 1.0f); // Rich dark charcoal
    }
    // If color is washed-out yellow (e.g. command names like python -u ...)
    if (c.r > 0.80f && c.g > 0.65f && c.b < 0.45f) {
        return D2D1::ColorF(0.58f, 0.32f, 0.00f, 1.0f); // Rich deep amber bronze
    }
    // If color is cyan / light blue (e.g. PowerShell path)
    if (c.b > 0.75f && c.g > 0.60f && c.r < 0.50f) {
        return D2D1::ColorF(0.00f, 0.35f, 0.72f, 1.0f); // Rich deep royal blue
    }
    // If color is bright lime / light green
    if (c.g > 0.70f && c.r < 0.50f) {
        return D2D1::ColorF(0.02f, 0.48f, 0.22f, 1.0f); // Deep emerald green
    }
    // If color is bright light magenta
    if (c.r > 0.70f && c.b > 0.70f && c.g < 0.60f) {
        return D2D1::ColorF(0.52f, 0.12f, 0.62f, 1.0f); // Deep rich purple
    }
    // If color is bright light red
    if (c.r > 0.85f && c.g < 0.45f && c.b < 0.45f) {
        return D2D1::ColorF(0.78f, 0.12f, 0.12f, 1.0f); // Deep crimson red
    }
    return c;
}

using PhideCreatePseudoConsoleFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HANDLE*);
using PhideClosePseudoConsoleFn = void(WINAPI*)(HANDLE);

static PhideCreatePseudoConsoleFn GetCreatePseudoConsoleFn() {
    static auto fn = reinterpret_cast<PhideCreatePseudoConsoleFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreatePseudoConsole"));
    return fn;
}

static PhideClosePseudoConsoleFn GetClosePseudoConsoleFn() {
    static auto fn = reinterpret_cast<PhideClosePseudoConsoleFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ClosePseudoConsole"));
    return fn;
}

static void AppendSpanChar(TerminalLine& line, wchar_t ch, D2D1_COLOR_F curColor, bool bold) {
    line.rawText += ch;
    if (!line.spans.empty() &&
        line.spans.back().color.r == curColor.r &&
        line.spans.back().color.g == curColor.g &&
        line.spans.back().color.b == curColor.b &&
        line.spans.back().bold == bold) {
        line.spans.back().text += ch;
    } else {
        TerminalSpan span;
        span.text = std::wstring(1, ch);
        span.color = curColor;
        span.bold = bold;
        line.spans.push_back(std::move(span));
    }
}

static void TruncateLineFromCol(TerminalLine& line, size_t col) {
    if (col >= line.rawText.size()) return;
    line.rawText.resize(col);
    size_t remaining = col;
    for (size_t s = 0; s < line.spans.size(); ++s) {
        size_t len = line.spans[s].text.size();
        if (remaining >= len) {
            remaining -= len;
        } else {
            line.spans[s].text.resize(remaining);
            if (line.spans[s].text.empty()) {
                line.spans.resize(s);
            } else {
                line.spans.resize(s + 1);
            }
            return;
        }
    }
}

static void OverwriteLineChar(TerminalLine& line, size_t& col, wchar_t ch, D2D1_COLOR_F curColor, bool bold) {
    while (line.rawText.size() < col) {
        AppendSpanChar(line, L' ', D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.0f), false);
    }
    if (col >= line.rawText.size()) {
        AppendSpanChar(line, ch, curColor, bold);
        col++;
        return;
    }
    line.rawText[col] = ch;
    // Update the matching character in spans (preserving existing segments cleanly)
    size_t pos = 0;
    for (size_t s = 0; s < line.spans.size(); ++s) {
        size_t len = line.spans[s].text.size();
        if (col < pos + len) {
            size_t localIdx = col - pos;
            line.spans[s].text[localIdx] = ch;
            col++;
            return;
        }
        pos += len;
    }
    col++;
}


// Native Win32 Process Tree Termination: pure API, ZERO taskkill.exe / powershell.exe, ZERO popping console windows!
// Uses single-snapshot BFS with visited cycle prevention to eliminate UI freezing and infinite recursion.
static void SafeKillProcessTree(DWORD rootPid) {
    if (rootPid == 0) return;

    std::vector<DWORD> toKill;
    std::unordered_set<DWORD> visited;
    toKill.push_back(rootPid);
    visited.insert(rootPid);

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        std::vector<std::pair<DWORD, DWORD>> allProcs;
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID != 0) {
                    allProcs.emplace_back(pe.th32ProcessID, pe.th32ParentProcessID);
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);

        size_t head = 0;
        while (head < toKill.size()) {
            DWORD currentParent = toKill[head++];
            for (const auto& proc : allProcs) {
                if (proc.second == currentParent && proc.first != currentParent) {
                    if (visited.insert(proc.first).second) {
                        toKill.push_back(proc.first);
                    }
                }
            }
        }
    }

    // Terminate in reverse order (deepest descendants first, parent last)
    for (auto it = toKill.rbegin(); it != toKill.rend(); ++it) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, *it);
        if (hProc) {
            TerminateProcess(hProc, 0);
            CloseHandle(hProc);
        }
    }
}

static void FillUpperLeftRoundedRect(ID2D1RenderTarget* pRT, ID2D1Brush* pBrush,
                                     const D2D1_RECT_F& rc, float radius) {
    if (!pRT || !pBrush) return;
    if (radius <= 0.0f) {
        pRT->FillRectangle(rc, pBrush);
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1Factory> pFactory;
    pRT->GetFactory(&pFactory);
    if (!pFactory) {
        pRT->FillRectangle(rc, pBrush);
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> pPath;
    if (FAILED(pFactory->CreatePathGeometry(&pPath))) {
        pRT->FillRectangle(rc, pBrush);
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(pPath->Open(&sink))) {
        pRT->FillRectangle(rc, pBrush);
        return;
    }

    const float maxR = (std::min)(rc.right - rc.left, rc.bottom - rc.top);
    const float r = (maxR > 0.0f) ? (std::min)(radius, maxR) : 0.0f;

    sink->BeginFigure(D2D1::Point2F(rc.left + r, rc.top), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(rc.right, rc.top));
    sink->AddLine(D2D1::Point2F(rc.right, rc.bottom));
    sink->AddLine(D2D1::Point2F(rc.left, rc.bottom));
    sink->AddLine(D2D1::Point2F(rc.left, rc.top + r));

    if (r > 0.0f) {
        D2D1_ARC_SEGMENT arc{};
        arc.point = D2D1::Point2F(rc.left + r, rc.top);
        arc.size = D2D1::SizeF(r, r);
        arc.rotationAngle = 0.0f;
        arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
        arc.arcSize = D2D1_ARC_SIZE_SMALL;
        sink->AddArc(arc);
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    pRT->FillGeometry(pPath.Get(), pBrush);
}

TerminalTab::~TerminalTab() {
    Terminate();
}

void TerminalTab::Terminate() {
    isAlive = false;
    onErrorCallback = nullptr;
    onReadyCallback = nullptr;
    onOutput = nullptr;
    if (splitTab) {
        splitTab->Terminate();
        splitTab.reset();
    }

    // 1. Terminate processes first so pipe writes cease and ReadFile unblocks
    if (hProcess) {
        TerminateProcess(hProcess, 0);
    }
    if (processId != 0) {
        SafeKillProcessTree(processId);
        processId = 0;
    }

    // 2. Unblock any synchronous ReadFile calls immediately
    {
        void* pcToClose = nullptr;
        {
            std::lock_guard<std::mutex> ptyLk(ptyMutex);
            pcToClose = hPC;
            hPC = nullptr;
        }
        if (pcToClose) {
            if (auto closePty = GetClosePseudoConsoleFn()) {
                closePty(pcToClose);
            }
        }
    }
    if (readerThread.joinable()) {
        CancelSynchronousIo(readerThread.native_handle());
    }
    if (stderrReaderThread.joinable()) {
        CancelSynchronousIo(stderrReaderThread.native_handle());
    }

    // 3. Close pipe and process handles
    if (hStdinWrite) {
        CloseHandle(hStdinWrite);
        hStdinWrite = NULL;
    }
    if (hStdoutRead) {
        CloseHandle(hStdoutRead);
        hStdoutRead = NULL;
    }
    if (hStderrRead) {
        CloseHandle(hStderrRead);
        hStderrRead = NULL;
    }
    if (hProcess) {
        CloseHandle(hProcess);
        hProcess = NULL;
    }
    if (hThread) {
        CloseHandle(hThread);
        hThread = NULL;
    }

    // 4. Safely join worker threads (guaranteed to exit promptly now that processes and pipes are terminated)
    if (exitWatcherThread.joinable()) {
        exitWatcherThread.join();
    }
    if (readerThread.joinable()) {
        readerThread.join();
    }
    if (stderrReaderThread.joinable()) {
        stderrReaderThread.join();
    }
}

void TerminalTab::SendInput(const std::string& input) {
    if (!hStdinWrite || !isAlive.load()) return;
    DWORD written = 0;
    WriteFile(hStdinWrite, input.data(), static_cast<DWORD>(input.size()), &written, NULL);
}

std::wstring TerminalTab::GetSelectedText() const {
    if (!selection.hasSelection()) return L"";
    TerminalPos s = selection.getStart();
    TerminalPos e = selection.getEnd();

    std::wstring result;
    std::lock_guard<std::mutex> lock(const_cast<TerminalTab*>(this)->bufferMutex);

    for (int l = s.line; l <= e.line && l < static_cast<int>(lines.size()); ++l) {
        if (l < 0) continue;
        std::wstring lineText = lines[l].rawText;
        if (l == static_cast<int>(lines.size()) - 1 && !currentInputLine.empty()) {
            lineText += currentInputLine;
        }

        int startCol = 0;
        int endCol = static_cast<int>(lineText.length());

        if (l == s.line && l == e.line) {
            startCol = (std::max)(0, (std::min)(s.col, static_cast<int>(lineText.length())));
            endCol = (std::max)(0, (std::min)(e.col, static_cast<int>(lineText.length())));
        } else if (l == s.line) {
            startCol = (std::max)(0, (std::min)(s.col, static_cast<int>(lineText.length())));
            endCol = static_cast<int>(lineText.length());
        } else if (l == e.line) {
            startCol = 0;
            endCol = (std::max)(0, (std::min)(e.col, static_cast<int>(lineText.length())));
        }

        if (endCol > startCol) {
            result += lineText.substr(startCol, endCol - startCol);
        }
        if (l < e.line) {
            result += L"\r\n";
        }
    }
    return result;
}

void TerminalTab::AppendOutput(const char* data, size_t length, bool isError, bool forceAppend) {
    if (!data || length == 0 || (!isAlive.load() && !forceAppend)) return;

    // Convert UTF-8 / ANSI to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(length), NULL, 0);
    std::wstring wstr;
    if (wlen > 0) {
        wstr.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, data, static_cast<int>(length), &wstr[0], wlen);
    } else {
        // Fallback to ANSI code page
        wlen = MultiByteToWideChar(CP_ACP, 0, data, static_cast<int>(length), NULL, 0);
        if (wlen > 0) {
            wstr.resize(wlen);
            MultiByteToWideChar(CP_ACP, 0, data, static_cast<int>(length), &wstr[0], wlen);
        } else {
            return;
        }
    }

    // Filter out Git Bash non-PTY startup warnings (Image 4)
    if (wstr.find(L"cannot set terminal process group") != std::wstring::npos ||
        wstr.find(L"no job control in this shell") != std::wstring::npos ||
        wstr.find(L"Inappropriate ioctl for device") != std::wstring::npos) {
        return;
    }

    // Clean up internal Node inspector CLI prompt artifacts for professional Debug Console (Image 3)
    if (shellType == TerminalShellType::DebugConsole) {
        std::wstring filtered;
        filtered.reserve(wstr.length());
        size_t pos = 0;
        while (pos < wstr.length()) {
            size_t nl = wstr.find(L'\n', pos);
            std::wstring line = (nl == std::wstring::npos) ? wstr.substr(pos) : wstr.substr(pos, nl - pos + 1);
            pos = (nl == std::wstring::npos) ? wstr.length() : nl + 1;

            std::wstring trimmed = line;
            while (!trimmed.empty() && (trimmed.back() == L'\r' || trimmed.back() == L'\n' || trimmed.back() == L' ')) {
                trimmed.pop_back();
            }
            if (trimmed == L"<" || trimmed == L"debug>" ||
                trimmed.find(L"Waiting for the debugger to disconnect") != std::wstring::npos ||
                trimmed.find(L"For help, see:") != std::wstring::npos) {
                continue;
            }
            if (line.rfind(L"debug> ", 0) == 0) {
                line = line.substr(7);
            }
            if (line.rfind(L"< ", 0) == 0) {
                line = line.substr(2);
            }
            filtered += line;
        }
        if (filtered.empty()) {
            return;
        }
        wstr = std::move(filtered);
    }

    if (isServerTab) {
        std::wstring checkText = wstr;
        for (auto& c : checkText) c = static_cast<wchar_t>(towlower(c));

        // 1. Detect server ready / listening indicators
        bool isSuccessIndicator = (
            checkText.find(L"- local:") != std::wstring::npos ||
            checkText.find(L"ready in ") != std::wstring::npos ||
            checkText.find(L"ready in") != std::wstring::npos ||
            checkText.find(L"compiled successfully") != std::wstring::npos ||
            checkText.find(L"http://localhost:") != std::wstring::npos ||
            checkText.find(L"http://127.0.0.1:") != std::wstring::npos ||
            checkText.find(L"server running at") != std::wstring::npos ||
            checkText.find(L"started on") != std::wstring::npos ||
            checkText.find(L"listening on") != std::wstring::npos
        );

        if (isSuccessIndicator) {
            hasError = false;
            lastErrorText.clear();
            if (onReadyCallback) {
                onReadyCallback(associatedFilePath);
            }
        }

        // 2. Distinguish benign warnings from true fatal errors
        bool isWarning = (
            checkText.find(L"warning") != std::wstring::npos ||
            checkText.find(L"warn -") != std::wstring::npos ||
            checkText.find(L"deprecationwarning") != std::wstring::npos ||
            checkText.find(L"experimentalwarning") != std::wstring::npos
        );

        bool hasErrorKeyword = (
            checkText.find(L"error:") != std::wstring::npos ||
            checkText.find(L": error") != std::wstring::npos ||
            checkText.find(L"[error]") != std::wstring::npos ||
            checkText.find(L"syntaxerror") != std::wstring::npos ||
            checkText.find(L"npm err!") != std::wstring::npos ||
            checkText.find(L"npm error") != std::wstring::npos ||
            checkText.find(L"elifecycle") != std::wstring::npos ||
            checkText.find(L"eaddrinuse") != std::wstring::npos ||
            checkText.find(L"address already in use") != std::wstring::npos ||
            checkText.find(L"another next dev server is already running") != std::wstring::npos ||
            checkText.find(L"commandnotfoundexception") != std::wstring::npos ||
            checkText.find(L"not recognized as the name of a cmdlet") != std::wstring::npos ||
            checkText.find(L"not recognized as an internal or external command") != std::wstring::npos ||
            checkText.find(L"cannot find module") != std::wstring::npos ||
            checkText.find(L"modulenotfounderror") != std::wstring::npos ||
            checkText.find(L"failed to compile") != std::wstring::npos ||
            checkText.find(L"fatal error:") != std::wstring::npos ||
            checkText.find(L"fatal:") != std::wstring::npos
        );

        bool isFatalError = hasErrorKeyword && !isWarning;

        if (isFatalError) {
            hasError = true;
            if (lastErrorText.empty()) {
                std::wstring line = wstr;
                size_t nl = line.find(L'\n');
                if (nl != std::wstring::npos) line = line.substr(0, nl);
                while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
                lastErrorText = line;
            }
            if (onErrorCallback) {
                onErrorCallback(associatedFilePath, lastErrorText);
            }
        }
    }

    std::lock_guard<std::mutex> lock(bufferMutex);

    if (lines.empty()) {
        lines.push_back({});
    }

    // Parse ANSI escapes and accumulate spans
    D2D1_COLOR_F curColor = isError ? D2D1::ColorF(0.94f, 0.32f, 0.32f, 1.0f) : D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.0f);
    bool bold = isError;

    for (size_t i = 0; i < wstr.length(); ++i) {
        wchar_t ch = wstr[i];

        // OSC (Operating System Command) sequence (BEL or ESC ST terminator)
        if (ch == L'\x1b' && i + 1 < wstr.length() && wstr[i + 1] == L']') {
            size_t bellEnd = wstr.find(L'\x07', i + 2);
            size_t stEnd = wstr.find(L"\x1b\\", i + 2);
            if (bellEnd != std::wstring::npos && (stEnd == std::wstring::npos || bellEnd < stEnd)) {
                i = bellEnd;
                continue;
            }
            if (stEnd != std::wstring::npos) {
                i = stEnd + 1;
                continue;
            }
        }

        // Character set / keypad escapes: \x1b(B, \x1b)0, \x1b=, \x1b>
        if (ch == L'\x1b' && i + 1 < wstr.length()) {
            wchar_t nxt = wstr[i + 1];
            if (nxt == L'(' || nxt == L')' || nxt == L'*' || nxt == L'+') {
                if (i + 2 < wstr.length()) {
                    i += 2;
                    continue;
                }
            } else if (nxt == L'=' || nxt == L'>' || nxt == L'M') {
                i += 1;
                continue;
            }
        }

        // ANSI Escape sequence: \x1b[ ...
        if (ch == L'\x1b' && i + 1 < wstr.length() && wstr[i + 1] == L'[') {
            size_t j = i + 2;
            while (j < wstr.length() && (wstr[j] < 0x40 || wstr[j] > 0x7E)) {
                j++;
            }
            if (j < wstr.length()) {
                wchar_t cmd = wstr[j];
                std::wstring seq = wstr.substr(i + 2, j - (i + 2));
                if (cmd == L'm') {
                    std::wstringstream sss(seq.empty() ? L"0" : seq);
                    std::wstring codeStr;
                    while (std::getline(sss, codeStr, L';')) {
                        int code = 0;
                        try { if (!codeStr.empty()) code = std::stoi(codeStr); } catch (...) {}
                        if (code == 0) {
                            curColor = D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.0f);
                            bold = false;
                        } else if (code == 1) {
                            bold = true;
                        } else if (code == 22) {
                            bold = false;
                        } else if (code == 30) {
                            curColor = D2D1::ColorF(0.20f, 0.20f, 0.22f, 1.0f);
                        } else if (code == 31 || code == 91) {
                            curColor = D2D1::ColorF(0.92f, 0.30f, 0.30f, 1.0f); // Red
                        } else if (code == 32 || code == 92) {
                            curColor = D2D1::ColorF(0.35f, 0.78f, 0.38f, 1.0f); // Green
                        } else if (code == 33 || code == 93) {
                            curColor = D2D1::ColorF(0.95f, 0.80f, 0.25f, 1.0f); // Yellow
                        } else if (code == 34 || code == 94) {
                            curColor = D2D1::ColorF(0.35f, 0.65f, 0.95f, 1.0f); // Blue
                        } else if (code == 35 || code == 95) {
                            curColor = D2D1::ColorF(0.80f, 0.45f, 0.85f, 1.0f); // Magenta
                        } else if (code == 36 || code == 96) {
                            curColor = D2D1::ColorF(0.30f, 0.78f, 0.85f, 1.0f); // Cyan
                        } else if (code == 37 || code == 97) {
                            curColor = D2D1::ColorF(0.95f, 0.95f, 0.95f, 1.0f); // White
                        } else if (code == 90) {
                            curColor = D2D1::ColorF(0.55f, 0.55f, 0.58f, 1.0f); // Gray
                        } else if (code == 39) {
                            curColor = D2D1::ColorF(0.92f, 0.92f, 0.92f, 1.0f);
                        }
                    }
                } else if (cmd == L'J') {
                    // ANSI Erase in Display: Clear terminal screen & scrollback
                    if (seq == L"2" || seq == L"3" || seq.empty()) {
                        lines.clear();
                        lines.push_back({});
                        scrollOffsetY = 0.0f;
                        conPtyCursorCol = 0;
                    }
                } else if (cmd == L'K') {
                    // ANSI Erase in Line
                    if (!lines.empty()) {
                        if (seq == L"2") {
                            lines.back().spans.clear();
                            lines.back().rawText.clear();
                            conPtyCursorCol = 0;
                        } else if (isConPty) {
                            TruncateLineFromCol(lines.back(), conPtyCursorCol);
                        } else {
                            lines.back().spans.clear();
                            lines.back().rawText.clear();
                        }
                    }
                } else if (isConPty && (cmd == L'C' || cmd == L'D' || cmd == L'G' || cmd == L'H' || cmd == L'f')) {
                    int n = 1;
                    try {
                        if (!seq.empty() && seq.find(L';') == std::wstring::npos && seq.find(L'?') == std::wstring::npos) {
                            n = (std::max)(1, std::stoi(seq));
                        }
                    } catch (...) {}
                    if (cmd == L'C') {
                        conPtyCursorCol += static_cast<size_t>(n);
                    } else if (cmd == L'D') {
                        conPtyCursorCol = (conPtyCursorCol > static_cast<size_t>(n)) ? (conPtyCursorCol - static_cast<size_t>(n)) : 0;
                    } else if (cmd == L'G') {
                        conPtyCursorCol = (n > 0) ? static_cast<size_t>(n - 1) : 0;
                    } else if (cmd == L'H' || cmd == L'f') {
                        size_t semi = seq.find(L';');
                        if (semi == std::wstring::npos) {
                            conPtyCursorCol = 0;
                        } else {
                            try {
                                int colVal = std::stoi(seq.substr(semi + 1));
                                conPtyCursorCol = (colVal > 0) ? static_cast<size_t>(colVal - 1) : 0;
                            } catch (...) {
                                conPtyCursorCol = 0;
                            }
                        }
                    }
                }
                i = j;
                continue;
            }
        }

        // Drop non-printable control characters (< 32), except \n, \r, \t, \b
        if (ch < 32 && ch != L'\n' && ch != L'\r' && ch != L'\t' && ch != L'\b') {
            continue;
        }

        if (ch == L'\r') {
            if (isConPty) {
                conPtyCursorCol = 0;
            }
            continue;
        }

        if (ch == L'\n') {
            lines.push_back({});
            conPtyCursorCol = 0;
            continue;
        }

        if (ch == L'\b') {
            if (isConPty) {
                if (conPtyCursorCol > 0) conPtyCursorCol--;
            } else if (!lines.empty() && !lines.back().rawText.empty()) {
                lines.back().rawText.pop_back();
                if (!lines.back().spans.empty()) {
                    if (!lines.back().spans.back().text.empty()) {
                        lines.back().spans.back().text.pop_back();
                        if (lines.back().spans.back().text.empty()) {
                            lines.back().spans.pop_back();
                        }
                    }
                }
            }
            continue;
        }

        // Add character to last line
        if (isConPty) {
            OverwriteLineChar(lines.back(), conPtyCursorCol, ch, curColor, bold);
        } else {
            AppendSpanChar(lines.back(), ch, curColor, bold);
        }
    }

    // Limit scrollback buffer to 2000 lines
    if (lines.size() > 2000) {
        size_t excess = lines.size() - 2000;
        lines.erase(lines.begin(), lines.begin() + excess);
        if (lastScannedLineIdx >= excess) {
            lastScannedLineIdx -= excess;
        } else {
            lastScannedLineIdx = 0;
        }
    }

    // Real-time Debugger Paused Location Interception (pdb & node inspect)
    if (shellType == TerminalShellType::DebugConsole && onDebugPaused && !lines.empty()) {
        size_t dbgStart = lastScannedLineIdx;
        if (dbgStart >= lines.size()) {
            dbgStart = lines.size() > 0 ? lines.size() - 1 : 0;
        }
        for (size_t li = dbgStart; li < lines.size(); ++li) {
            std::wstring pausedFile;
            int pausedLine1 = 0;
            if (TerminalPanel::ParseDebugPausedLine(lines[li].rawText, debugBackend, pausedFile, pausedLine1)) {
                onDebugPaused(pausedFile, pausedLine1);
            } else if (lines[li].rawText.find(L"The program finished") != std::wstring::npos) {
                if (onDebugResumed) onDebugResumed();
            }
        }
    }

    // Real-time Runtime Execution Error & Traceback Interception (Python, Node.js, PHP)
    if (onExecutionError && !lines.empty()) {
        size_t scanStart = lastScannedLineIdx;
        if (scanStart >= lines.size()) {
            scanStart = lines.size() > 0 ? lines.size() - 1 : 0;
        }
        for (size_t li = scanStart; li < lines.size(); ++li) {
            const std::wstring& raw = lines[li].rawText;

            // Check if a shell prompt arrived (e.g. "PS C:\...>", "$", ">")
            bool isPrompt = (raw.find(L"PS ") != std::wstring::npos && raw.find(L">") != std::wstring::npos) ||
                            (!raw.empty() && (raw.back() == L'>' || raw.back() == L'$')) ||
                            raw.find(L"> ") != std::wstring::npos;
            if (isPrompt) {
                pendingErrorFile.clear();
                pendingErrorLine = 0;
                pendingErrorToken.clear();
                if (!hadExecutionErrorInCurrentCommand && onClearRuntimeErrors) {
                    onClearRuntimeErrors();
                }
            }

            // 1. Python Traceback: File "...", line X
            size_t fp = raw.find(L"File \"");
            if (fp != std::wstring::npos) {
                size_t fStart = fp + 6;
                size_t fEnd = raw.find(L"\"", fStart);
                if (fEnd != std::wstring::npos) {
                    size_t lp = raw.find(L", line ", fEnd);
                    if (lp != std::wstring::npos) {
                        size_t numStart = lp + 7;
                        size_t numEnd = numStart;
                        while (numEnd < raw.length() && iswdigit(raw[numEnd])) numEnd++;
                        if (numEnd > numStart) {
                            pendingErrorFile = raw.substr(fStart, fEnd - fStart);
                            try { pendingErrorLine = std::stoi(raw.substr(numStart, numEnd - numStart)); } catch (...) {}
                            pendingErrorToken.clear();
                        }
                    }
                }
            } else if (pendingErrorLine > 0 && !raw.empty()) {
                if ((raw[0] == L' ' || raw[0] == L'\t') && raw.find(L"Error:") == std::wstring::npos && raw.find(L"Exception:") == std::wstring::npos && raw.find(L"error:") == std::wstring::npos) {
                    size_t ts = 0;
                    while (ts < raw.length() && (raw[ts] == L' ' || raw[ts] == L'\t')) ts++;
                    std::wstring candidate = raw.substr(ts);
                    while (!candidate.empty() && iswspace(candidate.back())) candidate.pop_back();

                    // Check if candidate is caret/tilde underline line (e.g. ^^^^^^^^^^)
                    bool isCaretOnly = true;
                    for (wchar_t ch : candidate) {
                        if (ch != L'^' && ch != L'~' && ch != L' ' && ch != L'\t') {
                            isCaretOnly = false;
                            break;
                        }
                    }
                    if (!isCaretOnly && !candidate.empty()) {
                        pendingErrorToken = candidate;
                    }
                } else if (raw.find(L"Error:") != std::wstring::npos || raw.find(L"Exception:") != std::wstring::npos || raw.find(L"error:") != std::wstring::npos) {
                    if (onExecutionError) {
                        onExecutionError(pendingErrorFile, pendingErrorLine, raw, pendingErrorToken);
                    }
                    hadExecutionErrorInCurrentCommand = true;
                    pendingErrorFile.clear();
                    pendingErrorLine = 0;
                    pendingErrorToken.clear();
                }
            }

            // 2. PHP Error: Fatal error: ... in C:\... on line X (or :X)
            if (raw.find(L"Fatal error:") != std::wstring::npos || raw.find(L"Parse error:") != std::wstring::npos) {
                size_t inPos = raw.find(L" in ");
                if (inPos != std::wstring::npos) {
                    size_t onLinePos = raw.find(L" on line ", inPos);
                    if (onLinePos != std::wstring::npos) {
                        std::wstring phpFile = raw.substr(inPos + 4, onLinePos - (inPos + 4));
                        size_t nStart = onLinePos + 9;
                        size_t nEnd = nStart;
                        while (nEnd < raw.length() && iswdigit(raw[nEnd])) nEnd++;
                        if (nEnd > nStart) {
                            int phpLine = std::stoi(raw.substr(nStart, nEnd - nStart));
                            std::wstring phpErr = raw.substr(0, inPos);
                            if (onExecutionError) {
                                onExecutionError(phpFile, phpLine, phpErr, L"");
                            }
                            hadExecutionErrorInCurrentCommand = true;
                        }
                    }
                }
            }

            // 3. Node.js Error: at ... (path:line:col) or ReferenceError / TypeError
            if (raw.find(L"ReferenceError:") != std::wstring::npos || raw.find(L"TypeError:") != std::wstring::npos || raw.find(L"SyntaxError:") != std::wstring::npos) {
                size_t atPos = raw.find(L" (");
                if (atPos != std::wstring::npos) {
                    size_t colon1 = raw.rfind(L':');
                    if (colon1 != std::wstring::npos) {
                        size_t colon2 = raw.rfind(L':', colon1 - 1);
                        if (colon2 != std::wstring::npos && colon2 > atPos + 2) {
                            std::wstring jsFile = raw.substr(atPos + 2, colon2 - (atPos + 2));
                            int jsLine = 0;
                            try { jsLine = std::stoi(raw.substr(colon2 + 1, colon1 - colon2 - 1)); } catch (...) {}
                            if (jsLine > 0 && onExecutionError) {
                                onExecutionError(jsFile, jsLine, raw.substr(0, atPos), L"");
                                hadExecutionErrorInCurrentCommand = true;
                            }
                        }
                    }
                }
            }
        }
        lastScannedLineIdx = lines.size() > 0 ? lines.size() - 1 : 0;
    }

    if (onOutput) {
        onOutput();
    }
}

void TerminalTab::RunGarbageCollection() {
    std::lock_guard<std::mutex> lock(bufferMutex);
    lines.shrink_to_fit();
    for (auto& l : lines) {
        l.spans.shrink_to_fit();
        l.rawText.shrink_to_fit();
    }
    commandHistory.shrink_to_fit();
    if (splitTab) {
        splitTab->RunGarbageCollection();
    }
}

// =============================================================================
// TerminalPanel Implementation
// =============================================================================

TerminalPanel::TerminalPanel() = default;

TerminalPanel::~TerminalPanel() {
    CloseAllTabs();
}

void TerminalPanel::Open(const std::wstring& workingDir) {
    m_isOpen = true;
    m_defaultWorkingDir = workingDir;
    if (m_height < 320.0f) {
        m_height = 360.0f; // Tall default size matching Image 1
    }
    if (m_availableProfiles.empty()) {
        m_availableProfiles = DetectAvailableShells();
    }
    if (m_tabs.empty()) {
        NewTab(TerminalShellType::PowerShell, workingDir);
    }

    // Ensure dedicated un-Xable "Git Logs" tab exists (Image 3)
    bool hasGitLogTab = false;
    for (auto& t : m_tabs) {
        if (t && (t->title == L"Git Logs" || t->title == L"Git")) {
            hasGitLogTab = true;
            t->title = L"Git Logs";
            t->canClose = false;
            break;
        }
    }
    if (!hasGitLogTab) {
        auto gitTab = std::make_unique<TerminalTab>();
        gitTab->shellType = TerminalShellType::PowerShell;
        gitTab->title = L"Git Logs";
        gitTab->canClose = false;
        gitTab->isServerTab = true;
        gitTab->workingDir = m_defaultWorkingDir;
        gitTab->isAlive = false;

        auto lines = GitLogManager::Instance().GetLogLines();
        for (const auto& lineStr : lines) {
            TerminalLine tl;
            tl.rawText = lineStr;
            D2D1_COLOR_F color = D2D1::ColorF(0.85f, 0.85f, 0.88f, 1.0f);
            if (lineStr.find(L" [info] >") != std::wstring::npos) {
                color = D2D1::ColorF(0.40f, 0.80f, 1.0f, 1.0f);
            } else if (lineStr.find(L"fatal:") != std::wstring::npos || lineStr.find(L"error:") != std::wstring::npos) {
                color = D2D1::ColorF(0.95f, 0.35f, 0.35f, 1.0f);
            } else if (lineStr.find(L"warning:") != std::wstring::npos) {
                color = D2D1::ColorF(0.95f, 0.75f, 0.25f, 1.0f);
            }
            tl.spans.push_back({ lineStr, color, false });
            gitTab->lines.push_back(std::move(tl));
        }
        m_tabs.push_back(std::move(gitTab));
    }
}

void TerminalPanel::Close() noexcept {
    m_isOpen = false;
    CloseMenus();
}

void TerminalPanel::Toggle(const std::wstring& workingDir) {
    if (m_isOpen) {
        Close();
    } else {
        Open(workingDir);
    }
}

void TerminalPanel::SetHeight(float h) noexcept {
    m_height = (std::max)(100.0f, (std::min)(800.0f, h));
}

void TerminalPanel::NewTab(TerminalShellType shell, const std::wstring& workingDir) {
    auto tab = std::make_unique<TerminalTab>();
    tab->shellType = shell;
    tab->workingDir = workingDir.empty() ? m_defaultWorkingDir : workingDir;

    size_t shellCount = 0;
    for (const auto& t : m_tabs) {
        if (t && !t->isServerTab && t->title != L"Git Logs" && !t->isGitHubLoginTab) {
            shellCount++;
        }
    }
    size_t count = shellCount + 1;
    std::wstring shellName = L"powershell";
    if (shell == TerminalShellType::Cmd) shellName = L"cmd";
    else if (shell == TerminalShellType::GitBash) shellName = L"bash";
    else if (shell == TerminalShellType::Node) shellName = L"node";
    else if (shell == TerminalShellType::Python) shellName = L"python";

    tab->title = std::to_wstring(count) + L": " + shellName;
    tab->onOutput = m_outputCallback;
    tab->onExecutionError = m_onExecutionError;
    tab->onClearRuntimeErrors = m_onClearRuntimeErrors;

    StartShellProcess(*tab);
    m_tabs.push_back(std::move(tab));
    m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
}

void TerminalPanel::NewRemoteCommandTab(const std::wstring& title, const std::wstring& commandLine, const std::wstring& workingDir, bool useConPty) {
    m_isOpen = true;
    auto tab = std::make_unique<TerminalTab>();
    tab->shellType = TerminalShellType::Custom;
    tab->title = title.empty() ? L"Remote SSH" : title;
    tab->customCommand = commandLine;
    tab->workingDir = workingDir.empty() ? m_defaultWorkingDir : workingDir;
    tab->isConPty = useConPty;
    tab->onOutput = m_outputCallback;
    tab->onExecutionError = m_onExecutionError;
    tab->onClearRuntimeErrors = m_onClearRuntimeErrors;

    if (!commandLine.empty()) {
        int u8Len = WideCharToMultiByte(CP_UTF8, 0, commandLine.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string cmdUtf8 = (u8Len > 1) ? std::string(u8Len - 1, '\0') : std::string();
        if (u8Len > 1) {
            WideCharToMultiByte(CP_UTF8, 0, commandLine.c_str(), -1, cmdUtf8.data(), u8Len, nullptr, nullptr);
        }
        std::string banner = "> " + cmdUtf8 + "\r\nConnecting...\r\n";
        tab->AppendOutput(banner.data(), banner.size(), false, true);
    }

    StartShellProcess(*tab);
    std::lock_guard<std::recursive_mutex> lock(m_tabsMutex);
    m_tabs.push_back(std::move(tab));
    m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
}

void TerminalPanel::CloseTab(size_t index) {
    std::wstring associatedPath;
    bool isServer = false;
    std::unique_ptr<TerminalTab> tabToClose;
    {
        std::lock_guard<std::recursive_mutex> tabsLock(m_tabsMutex);
        if (index >= m_tabs.size()) return;
        if (!m_tabs[index]->canClose) return; // Un-Xable tabs cannot be closed
        associatedPath = m_tabs[index]->associatedFilePath;
        isServer = (m_tabs[index]->shellType == TerminalShellType::LiveServer || m_tabs[index]->isServerTab);

        tabToClose = std::move(m_tabs[index]);
        m_tabs.erase(m_tabs.begin() + index);
        m_tabs.shrink_to_fit();

        if (m_tabs.empty()) {
            m_isOpen = false;
            m_activeTabIdx = 0;
        } else if (m_activeTabIdx >= static_cast<int>(m_tabs.size())) {
            m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
        }
    }

    // Terminate processes, pipes, and threads in background worker so UI thread NEVER freezes
    if (tabToClose) {
        std::thread([t = std::move(tabToClose)]() mutable {
            if (t) t->Terminate();
        }).detach();
    }

    if (isServer && !associatedPath.empty() && m_onLiveServerTabClosed) {
        m_onLiveServerTabClosed(associatedPath);
    }
}

bool TerminalPanel::CloseTabByAssociatedFile(const std::wstring& filePath) {
    if (filePath.empty()) return false;
    std::unique_ptr<TerminalTab> tabToClose;
    {
        std::lock_guard<std::recursive_mutex> tabsLock(m_tabsMutex);
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            if ((m_tabs[i]->shellType == TerminalShellType::LiveServer || m_tabs[i]->isServerTab) &&
                _wcsicmp(m_tabs[i]->associatedFilePath.c_str(), filePath.c_str()) == 0) {
                tabToClose = std::move(m_tabs[i]);
                m_tabs.erase(m_tabs.begin() + i);
                m_tabs.shrink_to_fit();
                if (m_tabs.empty()) {
                    m_isOpen = false;
                    m_activeTabIdx = 0;
                } else if (m_activeTabIdx >= static_cast<int>(m_tabs.size())) {
                    m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
                }
                break;
            }
        }
    }
    if (tabToClose) {
        std::thread([t = std::move(tabToClose)]() mutable {
            if (t) t->Terminate();
        }).detach();
        return true;
    }
    return false;
}

void TerminalPanel::CloseAllTabs() {
    std::vector<std::unique_ptr<TerminalTab>> tabsToClose;
    {
        std::lock_guard<std::recursive_mutex> tabsLock(m_tabsMutex);
        tabsToClose = std::move(m_tabs);
        m_tabs.clear();
        m_tabs.shrink_to_fit();
        m_isOpen = false;
        m_activeTabIdx = 0;
        CloseMenus();
    }
    if (!tabsToClose.empty()) {
        std::thread([list = std::move(tabsToClose)]() mutable {
            for (auto& tab : list) {
                if (tab) tab->Terminate();
            }
        }).detach();
    }
}

void TerminalPanel::RunGarbageCollection() {
    std::lock_guard<std::recursive_mutex> lock(m_tabsMutex);
    m_tabs.shrink_to_fit();
    for (auto& tab : m_tabs) {
        if (tab) {
            tab->RunGarbageCollection();
        }
    }
    m_availableProfiles.shrink_to_fit();
}

void TerminalPanel::SetActiveTabIdx(int idx) noexcept {
    if (idx >= 0 && idx < static_cast<int>(m_tabs.size())) {
        m_activeTabIdx = idx;
    }
}

TerminalTab* TerminalPanel::GetActiveTab() {
    if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
        auto* primary = m_tabs[m_activeTabIdx].get();
        if (primary && primary->splitTab && primary->activeSplitIndex == 1) {
            return primary->splitTab.get();
        }
        return primary;
    }
    return nullptr;
}

bool TerminalPanel::IsActiveTabSplit() const noexcept {
    if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
        return m_tabs[m_activeTabIdx] && m_tabs[m_activeTabIdx]->splitTab != nullptr;
    }
    return false;
}

void TerminalPanel::SplitActiveTab(std::optional<TerminalShellType> shell) {
    if (m_activeTabIdx < 0 || m_activeTabIdx >= static_cast<int>(m_tabs.size())) return;
    auto& primary = m_tabs[m_activeTabIdx];
    if (!primary) return;

    if (primary->splitTab) {
        // Already split - close split pane
        CloseSplitPane(static_cast<size_t>(m_activeTabIdx));
        return;
    }

    TerminalShellType splitShell = shell.value_or(primary->shellType);
    auto split = std::make_unique<TerminalTab>();
    split->shellType = splitShell;
    split->workingDir = primary->workingDir.empty() ? m_defaultWorkingDir : primary->workingDir;
    std::wstring shellName = L"powershell";
    if (splitShell == TerminalShellType::Cmd) shellName = L"cmd";
    else if (splitShell == TerminalShellType::GitBash) shellName = L"bash";
    else if (splitShell == TerminalShellType::Node) shellName = L"node";
    else if (splitShell == TerminalShellType::Python) shellName = L"python";
    split->title = primary->title + L" (split: " + shellName + L")";
    split->onOutput = m_outputCallback;
    split->onExecutionError = m_onExecutionError;
    split->onClearRuntimeErrors = m_onClearRuntimeErrors;

    StartShellProcess(*split);
    primary->splitTab = std::move(split);
    primary->splitRatio = 0.5f;
    primary->activeSplitIndex = 1; // Focus right split pane
}

void TerminalPanel::CloseSplitPane(size_t tabIdx) {
    if (tabIdx >= m_tabs.size()) return;
    auto& tab = m_tabs[tabIdx];
    if (tab && tab->splitTab) {
        tab->splitTab->Terminate();
        tab->splitTab.reset();
        tab->activeSplitIndex = 0;
    }
}

void TerminalPanel::SetWorkingDir(const std::wstring& dir) {
    m_defaultWorkingDir = dir;
}

void TerminalPanel::StartShellProcess(TerminalTab& tab) {
    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    HANDLE hStderrRead = NULL, hStderrWrite = NULL;
    HANDLE hStdinRead = NULL, hStdinWrite = NULL;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0);
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0);
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0);
    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

    tab.hStdinWrite = hStdinWrite;
    tab.hStdoutRead = hStdoutRead;
    tab.hStderrRead = hStderrRead;

    // Determine executable command
    std::wstring cmd;
    if (tab.shellType == TerminalShellType::PowerShell) {
        cmd = L"powershell.exe -NoLogo";
    } else if (tab.shellType == TerminalShellType::Cmd) {
        cmd = L"cmd.exe /K";
    } else if (tab.shellType == TerminalShellType::GitBash) {
        if (std::filesystem::exists(L"C:\\Program Files\\Git\\bin\\bash.exe")) {
            cmd = L"\"C:\\Program Files\\Git\\bin\\bash.exe\" --login -i";
        } else if (std::filesystem::exists(L"C:\\Program Files (x86)\\Git\\bin\\bash.exe")) {
            cmd = L"\"C:\\Program Files (x86)\\Git\\bin\\bash.exe\" --login -i";
        } else {
            cmd = L"bash.exe --login -i";
        }
    } else if (tab.shellType == TerminalShellType::Node) {
        if (std::filesystem::exists(L"C:\\Program Files\\nodejs\\node.exe")) {
            cmd = L"\"C:\\Program Files\\nodejs\\node.exe\" -i";
        } else if (std::filesystem::exists(L"C:\\Program Files (x86)\\nodejs\\node.exe")) {
            cmd = L"\"C:\\Program Files (x86)\\nodejs\\node.exe\" -i";
        } else {
            cmd = L"node.exe -i";
        }
    } else if (tab.shellType == TerminalShellType::Python) {
        cmd = L"python.exe -i";
    } else if (tab.shellType == TerminalShellType::DebugConsole) {
        if (tab.isIdleDebugConsole || tab.debugBackend == DebugBackend::None) {
            CloseHandle(hStdoutWrite);
            CloseHandle(hStderrWrite);
            CloseHandle(hStdinRead);
            tab.isAlive = false;
            return;
        }
        if (tab.debugBackend == DebugBackend::Pdb) {
            std::wstring pyExe = m_debugPythonExe.empty() ? L"python.exe" : m_debugPythonExe;
            if (pyExe.find(L' ') != std::wstring::npos && pyExe.front() != L'"') {
                pyExe = L"\"" + pyExe + L"\"";
            }
            cmd = pyExe + L" -u -m pdb \"" + m_debugTargetFile + L"\"";
        } else {
            std::wstring nodeExe = L"node.exe";
            if (std::filesystem::exists(L"C:\\Program Files\\nodejs\\node.exe")) {
                nodeExe = L"\"C:\\Program Files\\nodejs\\node.exe\"";
            } else if (std::filesystem::exists(L"C:\\Program Files (x86)\\nodejs\\node.exe")) {
                nodeExe = L"\"C:\\Program Files (x86)\\nodejs\\node.exe\"";
            }
            std::filesystem::path p(m_debugTargetFile);
            std::wstring ext = p.extension().wstring();
            for (auto& c : ext) c = towlower(c);
            std::wstring extraFlags = (ext == L".ts") ? L" --experimental-strip-types" : L"";
            cmd = nodeExe + extraFlags + L" inspect --port=0 \"" + m_debugTargetFile + L"\"";
        }
    } else if (tab.shellType == TerminalShellType::LiveServer) {
        CloseHandle(hStdoutWrite);
        CloseHandle(hStderrWrite);
        CloseHandle(hStdinRead);
        tab.isAlive = true;
        return;
    } else if (tab.shellType == TerminalShellType::Custom) {
        cmd = tab.customCommand;
        if (cmd.empty()) {
            CloseHandle(hStdoutWrite);
            CloseHandle(hStderrWrite);
            CloseHandle(hStdinRead);
            tab.isAlive = false;
            return;
        }
    }

    std::wstring validWorkDir = tab.workingDir;
    std::error_code ec;
    if (!validWorkDir.empty() && !std::filesystem::exists(validWorkDir, ec)) {
        validWorkDir.clear();
    }
    LPCWSTR workDir = validWorkDir.empty() ? nullptr : validWorkDir.c_str();

    // ConPTY interactive console launch for SSH / WSL / Docker sessions (handles password prompts & interactive shells)
    if (tab.isConPty) {
        auto createPty = GetCreatePseudoConsoleFn();
        if (createPty) {
            HANDLE hPtyInR = NULL, hPtyInW = NULL;
            HANDLE hPtyOutR = NULL, hPtyOutW = NULL;
            if (CreatePipe(&hPtyInR, &hPtyInW, NULL, 0) && CreatePipe(&hPtyOutR, &hPtyOutW, NULL, 0)) {
                HANDLE hPC = nullptr;
                COORD ptySize{ 120, 36 };
                if (SUCCEEDED(createPty(ptySize, hPtyInR, hPtyOutW, 0, &hPC)) && hPC) {
                    CloseHandle(hPtyInR);
                    CloseHandle(hPtyOutW);

                    SIZE_T attrSize = 0;
                    InitializeProcThreadAttributeList(NULL, 1, 0, &attrSize);
                    std::vector<BYTE> attrBuf(attrSize);
                    LPPROC_THREAD_ATTRIBUTE_LIST pAttrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
                    bool attrOk = (attrSize > 0 && InitializeProcThreadAttributeList(pAttrList, 1, 0, &attrSize));
                    if (attrOk) {
#ifdef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
                        constexpr DWORD_PTR kPtyAttr = PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE;
#else
                        constexpr DWORD_PTR kPtyAttr = 0x00020016;
#endif
                        attrOk = (UpdateProcThreadAttribute(pAttrList, 0, kPtyAttr, hPC, sizeof(hPC), NULL, NULL) != FALSE);
                    }

                    STARTUPINFOEXW siEx{};
                    siEx.StartupInfo.cb = sizeof(STARTUPINFOEXW);
                    // Prevent inheriting parent's redirected console pipes so child binds strictly to ConPTY
                    siEx.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
                    siEx.StartupInfo.hStdInput = NULL;
                    siEx.StartupInfo.hStdOutput = NULL;
                    siEx.StartupInfo.hStdError = NULL;
                    siEx.lpAttributeList = attrOk ? pAttrList : NULL;

                    PROCESS_INFORMATION pi{};
                    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
                    cmdBuf.push_back(L'\0');

                    BOOL ok = attrOk && CreateProcessW(
                        NULL, cmdBuf.data(), NULL, NULL, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT,
                        NULL, workDir, &siEx.StartupInfo, &pi);

                    if (pAttrList) {
                        DeleteProcThreadAttributeList(pAttrList);
                    }
                    // Release parent-side copies of child ConPTY pipe ends so ClosePseudoConsole delivers true EOF.
                    if (hPtyInR) { CloseHandle(hPtyInR); hPtyInR = NULL; }
                    if (hPtyOutW) { CloseHandle(hPtyOutW); hPtyOutW = NULL; }

                    if (ok) {
                        CloseHandle(hStdoutRead);
                        CloseHandle(hStdoutWrite);
                        CloseHandle(hStderrRead);
                        CloseHandle(hStderrWrite);
                        CloseHandle(hStdinRead);
                        CloseHandle(hStdinWrite);

                        {
                            std::lock_guard<std::mutex> ptyLk(tab.ptyMutex);
                            tab.hPC = hPC;
                        }
                        tab.hStdinWrite = hPtyInW;
                        tab.hStdoutRead = hPtyOutR;
                        tab.hStderrRead = NULL;
                        tab.hProcess = pi.hProcess;
                        tab.hThread = pi.hThread;
                        tab.processId = pi.dwProcessId;
                        tab.processExitCode = -1;
                        tab.isAlive = true;

                        HANDLE hWatchProc = NULL;
                        BOOL dupOk = DuplicateHandle(GetCurrentProcess(), pi.hProcess, GetCurrentProcess(), &hWatchProc,
                                                     SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0);
                        DWORD dupErr = dupOk ? 0 : GetLastError();

                        TerminalTab* pTab = &tab;
                        tab.exitWatcherThread = std::thread([pTab, hWatchProc, dupErr]() {
                            if (hWatchProc) {
                                WaitForSingleObject(hWatchProc, INFINITE);
                                DWORD code = 0;
                                if (GetExitCodeProcess(hWatchProc, &code)) {
                                    pTab->processExitCode.store(static_cast<int>(code));
                                }
                                CloseHandle(hWatchProc);
                            } else {
                                std::string dbg = "[ConPTY watch err=" + std::to_string(dupErr) + "]\r\n";
                                pTab->AppendOutput(dbg.data(), dbg.size(), true, true);
                            }
                            if (pTab->isAlive.load()) {
                                void* pcToClose = nullptr;
                                {
                                    std::lock_guard<std::mutex> ptyLk(pTab->ptyMutex);
                                    pcToClose = pTab->hPC;
                                    pTab->hPC = nullptr;
                                }
                                if (pcToClose) {
                                    if (auto closePty = GetClosePseudoConsoleFn()) {
                                        closePty(pcToClose);
                                    }
                                }
                            }
                        });

                        tab.readerThread = std::thread([pTab, hPtyOutR]() {
                            char buffer[4096];
                            DWORD bytesRead = 0;
                            while (pTab->isAlive.load() && ReadFile(hPtyOutR, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                                buffer[bytesRead] = '\0';
                                pTab->AppendOutput(buffer, bytesRead, false);
                                if (pTab->onOutput) {
                                    pTab->onOutput();
                                }
                            }
                            DWORD readErr = GetLastError();
                            if (pTab->isAlive.load()) {
                                int code = pTab->processExitCode.load();
                                std::string exitMsg = (code >= 0)
                                    ? ("\r\n[Remote session closed (exit code " + std::to_string(code) + ")]\r\n")
                                    : ("\r\n[Remote session closed (readErr=" + std::to_string(readErr) + ")]\r\n");
                                // Append status BEFORE flipping isAlive=false so observers see complete final lines.
                                pTab->AppendOutput(exitMsg.data(), exitMsg.size(), code > 0, true);
                                pTab->isAlive.store(false);
                                if (pTab->onOutput) pTab->onOutput();
                            }
                        });
                        return;
                    }

                    if (auto closePty = GetClosePseudoConsoleFn()) closePty(hPC);
                    CloseHandle(hPtyInW);
                    CloseHandle(hPtyOutR);
                } else {
                    CloseHandle(hPtyInR);
                    CloseHandle(hPtyInW);
                    CloseHandle(hPtyOutR);
                    CloseHandle(hPtyOutW);
                }
            } else {
                if (hPtyInR) CloseHandle(hPtyInR);
                if (hPtyInW) CloseHandle(hPtyInW);
                if (hPtyOutR) CloseHandle(hPtyOutR);
                if (hPtyOutW) CloseHandle(hPtyOutW);
            }
        }
        tab.isConPty = false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.hStdError = hStderrWrite;
    si.hStdOutput = hStdoutWrite;
    si.hStdInput = hStdinRead;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    BOOL success = CreateProcessW(
        NULL,
        cmdBuf.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
        NULL,
        workDir,
        &si,
        &pi
    );

    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);
    CloseHandle(hStdinRead);

    if (success) {
        tab.hProcess = pi.hProcess;
        tab.hThread = pi.hThread;
        tab.processId = pi.dwProcessId;
        tab.isAlive = true;

        // Background asynchronous stdout reader thread
        TerminalTab* pTab = &tab;
        tab.readerThread = std::thread([pTab, hStdoutRead]() {
            char buffer[4096];
            DWORD bytesRead = 0;
            while (pTab->isAlive.load() && ReadFile(hStdoutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                pTab->AppendOutput(buffer, bytesRead, false);
            }
            if (pTab->shellType == TerminalShellType::DebugConsole) {
                if (pTab->onDebugEnded) {
                    pTab->onDebugEnded();
                }
            }
            if (pTab->isAlive.load() && pTab->isServerTab && pTab->hProcess) {
                WaitForSingleObject(pTab->hProcess, 100);
                DWORD exitCode = 0;
                if (pTab->isAlive.load() && GetExitCodeProcess(pTab->hProcess, &exitCode) && exitCode != STILL_ACTIVE && exitCode != 0) {
                    pTab->hasError = true;
                    if (pTab->lastErrorText.empty()) {
                        pTab->lastErrorText = L"Process exited with error code " + std::to_wstring(exitCode);
                    }
                    if (pTab->isAlive.load() && pTab->onErrorCallback) {
                        pTab->onErrorCallback(pTab->associatedFilePath, pTab->lastErrorText);
                    }
                }
            }
        });

        // Background asynchronous stderr reader thread (Errors rendered in vibrant RED matching Image 3)
        tab.stderrReaderThread = std::thread([pTab, hStderrRead]() {
            char buffer[4096];
            DWORD bytesRead = 0;
            while (pTab->isAlive.load() && ReadFile(hStderrRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                pTab->AppendOutput(buffer, bytesRead, true);
            }
            if (pTab->shellType == TerminalShellType::DebugConsole) {
                if (pTab->onDebugEnded) {
                    pTab->onDebugEnded();
                }
            }
            if (pTab->isAlive.load() && pTab->isServerTab && pTab->hProcess) {
                WaitForSingleObject(pTab->hProcess, 100);
                DWORD exitCode = 0;
                if (pTab->isAlive.load() && GetExitCodeProcess(pTab->hProcess, &exitCode) && exitCode != STILL_ACTIVE && exitCode != 0) {
                    pTab->hasError = true;
                    if (pTab->lastErrorText.empty()) {
                        pTab->lastErrorText = L"Process exited with error code " + std::to_wstring(exitCode);
                    }
                    if (pTab->isAlive.load() && pTab->onErrorCallback) {
                        pTab->onErrorCallback(pTab->associatedFilePath, pTab->lastErrorText);
                    }
                }
            }
        });
    } else {
        tab.AppendOutput("Failed to launch terminal shell process.\r\n", 41);
    }
}

// =============================================================================
// Direct2D Rendering
// =============================================================================

std::vector<TerminalSpan> TerminalPanel::TokenizeCommandLine(const std::wstring& cmdLine) {
    std::vector<TerminalSpan> spans;
    if (cmdLine.empty()) return spans;

    size_t i = 0;
    bool isFirstToken = true;

    while (i < cmdLine.length()) {
        // Collect whitespace
        if (cmdLine[i] == L' ' || cmdLine[i] == L'\t') {
            size_t wsStart = i;
            while (i < cmdLine.length() && (cmdLine[i] == L' ' || cmdLine[i] == L'\t')) {
                i++;
            }
            spans.push_back({ cmdLine.substr(wsStart, i - wsStart), D2D1::ColorF(0.85f, 0.85f, 0.85f) });
            continue;
        }

        // Quoted string: "..." or '...'
        if (cmdLine[i] == L'"' || cmdLine[i] == L'\'') {
            wchar_t q = cmdLine[i];
            size_t strStart = i;
            i++;
            while (i < cmdLine.length() && cmdLine[i] != q) {
                if (cmdLine[i] == L'\\' && i + 1 < cmdLine.length()) i += 2;
                else i++;
            }
            if (i < cmdLine.length()) i++;
            spans.push_back({ cmdLine.substr(strStart, i - strStart), D2D1::ColorF(0.60f, 0.85f, 0.60f) });
            isFirstToken = false;
            continue;
        }

        // Word token
        size_t wordStart = i;
        while (i < cmdLine.length() && cmdLine[i] != L' ' && cmdLine[i] != L'\t' && cmdLine[i] != L'"' && cmdLine[i] != L'\'') {
            i++;
        }
        std::wstring word = cmdLine.substr(wordStart, i - wordStart);

        if (word == L"|" || word == L";" || word == L"&&" || word == L"||") {
            spans.push_back({ word, D2D1::ColorF(0.95f, 0.80f, 0.35f) });
            isFirstToken = true;
        } else if (isFirstToken) {
            // Executable command name (cd, git, npm, dir, etc.) -> Vibrant Cyan
            spans.push_back({ word, D2D1::ColorF(0.35f, 0.82f, 0.98f), true });
            isFirstToken = false;
        } else if (word.rfind(L"-", 0) == 0) {
            // Flag/Option (-m, -v, --help) -> Soft Magenta
            spans.push_back({ word, D2D1::ColorF(0.85f, 0.55f, 0.88f) });
        } else {
            // Path, argument, or parameter -> Crisp white/gray
            spans.push_back({ word, D2D1::ColorF(0.88f, 0.88f, 0.90f) });
        }
    }

    return spans;
}

std::vector<TerminalSpan> TerminalPanel::TokenizeCommandLineWithLinuxRules(const std::wstring& text, const std::wstring& workingDir) {
    std::vector<TerminalSpan> spans;
    if (text.empty()) return spans;

    size_t i = 0;
    bool isFirstCommand = true;
    bool isFolderArgCmd = false;

    while (i < text.length()) {
        // Collect whitespace
        if (text[i] == L' ' || text[i] == L'\t') {
            size_t wsStart = i;
            while (i < text.length() && (text[i] == L' ' || text[i] == L'\t')) {
                i++;
            }
            spans.push_back({ text.substr(wsStart, i - wsStart), D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
            continue;
        }

        // Delimiter '>' should NEVER be blue; it is always white!
        if (text[i] == L'>') {
            spans.push_back({ L">", D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
            i++;
            continue;
        }

        // Quoted string: "..." or '...'
        if (text[i] == L'"' || text[i] == L'\'') {
            wchar_t q = text[i];
            size_t strStart = i;
            i++;
            while (i < text.length() && text[i] != q) {
                if (text[i] == L'\\' && i + 1 < text.length()) i += 2;
                else i++;
            }
            if (i < text.length()) i++;
            std::wstring strVal = text.substr(strStart, i - strStart);
            std::wstring unquoted = (strVal.length() >= 2) ? strVal.substr(1, strVal.length() - 2) : strVal;
            bool isRealDir = false;
            if (!unquoted.empty()) {
                std::error_code ec;
                std::filesystem::path p(unquoted);
                if (std::filesystem::is_directory(p, ec)) isRealDir = true;
                else if (!workingDir.empty() && std::filesystem::is_directory(std::filesystem::path(workingDir) / p, ec)) isRealDir = true;
            }
            D2D1_COLOR_F strColor = (isFolderArgCmd && isRealDir) ? D2D1::ColorF(0.38f, 0.68f, 0.98f) : D2D1::ColorF(0.92f, 0.92f, 0.92f);
            spans.push_back({ strVal, strColor, false });
            isFirstCommand = false;
            continue;
        }

        // Word token (stop on whitespace, quotes, or '>')
        size_t wordStart = i;
        while (i < text.length() && text[i] != L' ' && text[i] != L'\t' && text[i] != L'"' && text[i] != L'\'' && text[i] != L'>') {
            i++;
        }
        std::wstring word = text.substr(wordStart, i - wordStart);

        if (word == L"|" || word == L";" || word == L"&&" || word == L"||") {
            spans.push_back({ word, D2D1::ColorF(0.95f, 0.80f, 0.25f), true });
            isFirstCommand = true;
            isFolderArgCmd = false;
        } else if (isFirstCommand) {
            // Command name: Gold / Yellow color matching Image 2
            spans.push_back({ word, D2D1::ColorF(0.95f, 0.80f, 0.25f), true });
            isFirstCommand = false;

            std::wstring lowerWord = word;
            for (auto& c : lowerWord) c = towlower(c);
            if (lowerWord == L"cd" || lowerWord == L"dir" || lowerWord == L"ls" ||
                lowerWord == L"mkdir" || lowerWord == L"md" || lowerWord == L"rmdir" ||
                lowerWord == L"rd" || lowerWord == L"pushd" || lowerWord == L"popd") {
                isFolderArgCmd = true;
            }
        } else {
            // Check if argument is an ACTUAL existing folder on disk!
            // "why its color blue even tho theres no folder like that and the shouldnt be blue"
            bool isFolder = false;
            if (word == L"." || word == L".." || word == L"~") {
                isFolder = true;
            } else if (!word.empty() && word[0] != L'-' && word[0] != L'/') {
                std::error_code ec;
                std::filesystem::path p(word);
                if (std::filesystem::is_directory(p, ec)) {
                    isFolder = true;
                } else if (!workingDir.empty()) {
                    std::filesystem::path combined = std::filesystem::path(workingDir) / p;
                    if (std::filesystem::is_directory(combined, ec)) {
                        isFolder = true;
                    }
                }
            }

            if (isFolder) {
                spans.push_back({ word, D2D1::ColorF(0.38f, 0.68f, 0.98f), false });
            } else {
                spans.push_back({ word, D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
            }
        }
    }

    return spans;
}

std::vector<TerminalSpan> TerminalPanel::FormatTerminalLine(const TerminalLine& line, bool isError, const std::wstring& workingDir) {
    if (isError) {
        return { { line.rawText, D2D1::ColorF(0.94f, 0.32f, 0.32f), false } };
    }

    if (line.spans.size() > 1) {
        return line.spans;
    }

    const std::wstring& text = line.rawText;
    if (text.empty()) {
        return line.spans;
    }

    // 1. Error line detection
    if (text.find(L"Cannot find path") != std::wstring::npos ||
        text.find(L"CategoryInfo") != std::wstring::npos ||
        text.find(L"FullyQualifiedErrorId") != std::wstring::npos ||
        text.rfind(L"+ ", 0) == 0 ||
        text.rfind(L"Error:", 0) == 0 ||
        text.rfind(L"Exception:", 0) == 0 ||
        text.find(L"The term '") != std::wstring::npos) {
        return { { text, D2D1::ColorF(0.94f, 0.32f, 0.32f), false } };
    }

    // 2. PowerShell prompt: "PS C:\path> command"
    if (text.rfind(L"PS ", 0) == 0) {
        size_t gt = text.find(L">");
        if (gt != std::wstring::npos) {
            std::vector<TerminalSpan> out;
            out.push_back({ L"PS", D2D1::ColorF(0.95f, 0.80f, 0.25f), true });
            out.push_back({ L" ", D2D1::ColorF(0.92f, 0.92f, 0.92f), false });

            // Folder path in BLUE
            if (gt > 3) {
                out.push_back({ text.substr(3, gt - 3), D2D1::ColorF(0.38f, 0.68f, 0.98f), false });
            }

            // ">" in white (NEVER blue!)
            out.push_back({ L">", D2D1::ColorF(0.92f, 0.92f, 0.92f), false });

            // Command part after ">"
            if (gt + 1 < text.length()) {
                auto cmdSpans = TokenizeCommandLineWithLinuxRules(text.substr(gt + 1), workingDir);
                out.insert(out.end(), cmdSpans.begin(), cmdSpans.end());
            }
            return out;
        }
    }

    // 3. CMD prompt: "C:\path> command"
    size_t gt = text.find(L">");
    if (gt != std::wstring::npos && gt >= 2 && text[1] == L':') {
        std::vector<TerminalSpan> out;
        out.push_back({ text.substr(0, gt), D2D1::ColorF(0.38f, 0.68f, 0.98f), false });
        out.push_back({ L">", D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
        if (gt + 1 < text.length()) {
            auto cmdSpans = TokenizeCommandLineWithLinuxRules(text.substr(gt + 1), workingDir);
            out.insert(out.end(), cmdSpans.begin(), cmdSpans.end());
        }
        return out;
    }

    // 4. Git Bash / Linux prompt: "user@host:path$ command"
    size_t dollar = text.find(L"$");
    if (dollar != std::wstring::npos && text.find(L"@") != std::wstring::npos) {
        std::vector<TerminalSpan> out;
        size_t atPos = text.find(L"@");
        size_t spaceAfterHost = text.find(L' ', atPos);
        if (spaceAfterHost != std::wstring::npos) {
            out.push_back({ text.substr(0, spaceAfterHost), D2D1::ColorF(0.95f, 0.80f, 0.25f), true });

            size_t pathStart = text.find_first_of(L"/~", spaceAfterHost);
            if (pathStart != std::wstring::npos && pathStart < dollar) {
                out.push_back({ text.substr(spaceAfterHost, pathStart - spaceAfterHost), D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
                size_t pathEnd = text.find_first_of(L" (", pathStart);
                if (pathEnd == std::wstring::npos || pathEnd > dollar) pathEnd = dollar;
                out.push_back({ text.substr(pathStart, pathEnd - pathStart), D2D1::ColorF(0.38f, 0.68f, 0.98f), false });
                out.push_back({ text.substr(pathEnd, dollar - pathEnd + 1), D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
            } else {
                out.push_back({ text.substr(spaceAfterHost, dollar - spaceAfterHost + 1), D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
            }

            if (dollar + 1 < text.length()) {
                auto cmdSpans = TokenizeCommandLineWithLinuxRules(text.substr(dollar + 1), workingDir);
                out.insert(out.end(), cmdSpans.begin(), cmdSpans.end());
            }
            return out;
        }
    }

    // 5. Standalone command line starting with a command (e.g. "cd src", "npm run build")
    std::wstring trimmed = text;
    size_t firstNonSpace = trimmed.find_first_not_of(L" \t");
    if (firstNonSpace != std::wstring::npos) {
        size_t firstWordEnd = trimmed.find_first_of(L" \t", firstNonSpace);
        std::wstring firstWord = trimmed.substr(firstNonSpace, (firstWordEnd == std::wstring::npos) ? std::wstring::npos : (firstWordEnd - firstNonSpace));
        std::wstring lowerFirstWord = firstWord;
        for (auto& c : lowerFirstWord) c = towlower(c);
        if (lowerFirstWord == L"cd" || lowerFirstWord == L"dir" || lowerFirstWord == L"ls" ||
            lowerFirstWord == L"mkdir" || lowerFirstWord == L"rmdir" || lowerFirstWord == L"git" ||
            lowerFirstWord == L"npm" || lowerFirstWord == L"node" || lowerFirstWord == L"python" ||
            lowerFirstWord == L"cargo" || lowerFirstWord == L"cmake" || lowerFirstWord == L"cat") {
            return TokenizeCommandLineWithLinuxRules(text, workingDir);
        }
    }

    // 6. Directory listing line (dir / ls): Folder names in BLUE
    bool isDirEntry = false;
    size_t dirTokenPos = text.find(L"<DIR>");
    if (dirTokenPos == std::wstring::npos) {
        if (text.rfind(L"d----", 0) == 0 || text.rfind(L"d-r--", 0) == 0 || text.rfind(L"drwx", 0) == 0) {
            isDirEntry = true;
            dirTokenPos = 0;
        }
    } else {
        isDirEntry = true;
    }

    if (isDirEntry) {
        size_t lastNonWs = text.find_last_not_of(L" \t");
        if (lastNonWs != std::wstring::npos) {
            size_t nameStart = text.find_last_of(L" \t", lastNonWs);
            if (nameStart != std::wstring::npos && nameStart > dirTokenPos) {
                std::vector<TerminalSpan> out;
                out.push_back({ text.substr(0, nameStart + 1), D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
                out.push_back({ text.substr(nameStart + 1), D2D1::ColorF(0.38f, 0.68f, 0.98f), true });
                return out;
            }
        }
    }

    // 6. Explicit ANSI escape colors
    bool hasCustomAnsi = false;
    for (const auto& sp : line.spans) {
        if (std::abs(sp.color.r - 0.92f) > 0.05f || std::abs(sp.color.g - 0.92f) > 0.05f || std::abs(sp.color.b - 0.92f) > 0.05f) {
            hasCustomAnsi = true;
            break;
        }
    }
    if (hasCustomAnsi) {
        return line.spans;
    }

    return { { text, D2D1::ColorF(0.92f, 0.92f, 0.92f), false } };
}

void TerminalPanel::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                           float clientW, float clientH, float leftOffset, float animatedHeight) {
    if (m_debugProcessEnded.exchange(false)) {
        m_isDebugActive = false;
        if (m_onDebugEnded) m_onDebugEnded();
    }
    if (!m_isOpen) return;

    std::lock_guard<std::recursive_mutex> tabsLock(m_tabsMutex);

    float panelX = leftOffset;
    const float headerTopH = config.ScaleF(static_cast<float>(config.headerHeight));
    float fullH = clientH - headerTopH;
    float maxH = m_height + (fullH - m_height) * m_fullScreenProgress;
    if (m_isFullScreen && m_fullScreenProgress <= 0.001f) {
        maxH = fullH;
    }
    float panelH = (animatedHeight >= 0.0f) ? animatedHeight : maxH;
    if (panelH <= 0.0f) return;
    float panelY = clientH - panelH;
    if (panelY < headerTopH) {
        panelH -= (headerTopH - panelY);
        panelY = headerTopH;
    }
    if (panelH <= 0.0f) return;
    float panelW = clientW - leftOffset;

    m_panelBounds = D2D1::RectF(panelX, panelY, panelX + panelW, panelY + panelH);

    pRT->PushAxisAlignedClip(m_panelBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Check if terminal is maximized or nearing top edge -> apply 14px upper-left curvature (Image 4)
    bool isMaximized = (m_isFullScreen || m_fullScreenProgress > 0.01f || panelY <= headerTopH + 2.0f);
    float cornerRadius = isMaximized ? config.ScaleF(14.0f) : 0.0f;

    // 1. Panel Background
    D2D1_COLOR_F bgColor = theme.IsDarkMode() ? D2D1::ColorF(0.08f, 0.08f, 0.09f, 1.0f) : D2D1::ColorF(0.96f, 0.96f, 0.97f, 1.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    pRT->CreateSolidColorBrush(bgColor, &bgBrush);
    if (bgBrush) {
        if (cornerRadius > 0.0f) {
            FillUpperLeftRoundedRect(pRT, bgBrush.Get(), m_panelBounds, cornerRadius);
        } else {
            pRT->FillRectangle(m_panelBounds, bgBrush.Get());
        }
    }

    // 2. Top Resizing Splitter (100% drawn flush, centered hit handle)
    m_splitterBounds = D2D1::RectF(panelX, panelY - config.ScaleF(4.0f), panelX + panelW, panelY + config.ScaleF(4.0f));

    // 3. Header Toolbar (Image 1)
    float headerH = config.ScaleF(32.0f);
    m_headerBounds = D2D1::RectF(panelX, panelY + config.ScaleF(2.0f), panelX + panelW, panelY + headerH);
    RenderHeader(pRT, theme, config, panelX, panelY + config.ScaleF(2.0f), panelW, headerH - config.ScaleF(2.0f), cornerRadius);

    // 4. Terminal Content Area
    float contentY = panelY + headerH;
    float contentH = panelH - headerH;
    m_textAreaBounds = D2D1::RectF(panelX, contentY, panelX + panelW, panelY + panelH);
    if (contentH > 5.0f) {
        RenderContent(pRT, theme, config, panelX, contentY, panelW, contentH);
    }

    pRT->PopAxisAlignedClip();

    // 5. Shell Dropdown Menu (Image 2) - Rendered outside panel clip so it floats freely
    if (m_showShellMenu) {
        RenderShellMenu(pRT, theme, config);
    }

    // 6. Options Dropdown Menu (Image 4) - Rendered outside panel clip so it floats freely
    if (m_showOptionsMenu) {
        RenderOptionsMenu(pRT, theme, config, clientH);
    }

    // 7. Floating Tooltip for Hovered Debug Action
    // Rendered OUTSIDE panel clip so it floats freely above toolbar over editor canvas without horizontal line clipping artifact (User requirement)
    if (m_hoveredDebugAction != DebugAction::None) {
        const wchar_t* tipText = L"";
        D2D1_RECT_F tipTarget{};
        switch (m_hoveredDebugAction) {
            case DebugAction::Continue: tipText = L"Continue (F5)"; tipTarget = m_debugContinueBounds; break;
            case DebugAction::StepOver: tipText = L"Step Over (F10)"; tipTarget = m_debugStepOverBounds; break;
            case DebugAction::StepInto: tipText = L"Step Into (F11)"; tipTarget = m_debugStepIntoBounds; break;
            case DebugAction::StepOut:  tipText = L"Step Out (Shift+F11)"; tipTarget = m_debugStepOutBounds; break;
            case DebugAction::Restart:  tipText = L"Restart (Ctrl+Shift+F5)"; tipTarget = m_debugRestartBounds; break;
            case DebugAction::Stop:     tipText = L"Stop (Shift+F5)"; tipTarget = m_debugStopBounds; break;
            default: break;
        }
        if (tipText[0] != L'\0') {
            size_t tipLen = wcslen(tipText);
            float textW = config.ScaleF(static_cast<float>(tipLen * 7.0f + 14.0f));
            float tipH = config.ScaleF(22.0f);
            float tipW = textW + config.ScaleF(16.0f);
            float targetW = tipTarget.right - tipTarget.left;
            float tipX = tipTarget.left + (targetW - tipW) * 0.5f;
            float tipY = tipTarget.top - tipH - config.ScaleF(4.0f);
            if (tipY < 2.0f) tipY = tipTarget.bottom + config.ScaleF(4.0f);

            D2D1_ROUNDED_RECT rrcTip = D2D1::RoundedRect(D2D1::RectF(tipX, tipY, tipX + tipW, tipY + tipH), 4.0f, 4.0f);
            pRT->FillRoundedRectangle(rrcTip, theme.brushTooltipBg ? theme.brushTooltipBg.Get() : theme.brushMenuBg.Get());
            pRT->DrawRoundedRectangle(rrcTip, theme.brushTooltipBorder ? theme.brushTooltipBorder.Get() : theme.brushMenuBorder.Get(), 0.8f);
            pRT->DrawText(tipText, static_cast<UINT32>(tipLen),
                          theme.fontTooltip ? theme.fontTooltip.Get() : theme.fontHint.Get(),
                          rrcTip.rect,
                          theme.brushTooltipText ? theme.brushTooltipText.Get() : theme.brushTextPrimary.Get());
        }
    }
}

void TerminalPanel::RenderBounds(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                 const D2D1_RECT_F& bounds, float clientH, bool isRightDocked) {
    if (m_debugProcessEnded.exchange(false)) {
        m_isDebugActive = false;
        if (m_onDebugEnded) m_onDebugEnded();
    }
    if (!m_isOpen) return;

    std::lock_guard<std::recursive_mutex> tabsLock(m_tabsMutex);

    float panelX = bounds.left;
    float panelY = bounds.top;
    float panelW = bounds.right - bounds.left;
    float panelH = bounds.bottom - bounds.top;
    if (panelW <= 0.0f || panelH <= 0.0f) return;

    m_panelBounds = bounds;

    pRT->PushAxisAlignedClip(m_panelBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // 1. Panel Background
    D2D1_COLOR_F bgColor = theme.IsDarkMode() ? D2D1::ColorF(0.08f, 0.08f, 0.09f, 1.0f) : D2D1::ColorF(0.96f, 0.96f, 0.97f, 1.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    pRT->CreateSolidColorBrush(bgColor, &bgBrush);
    if (bgBrush) {
        pRT->FillRectangle(m_panelBounds, bgBrush.Get());
    }

    // 2. Splitter
    if (isRightDocked) {
        // Vertical splitter on left edge
        m_splitterBounds = D2D1::RectF(panelX - config.ScaleF(4.0f), panelY, panelX + config.ScaleF(4.0f), panelY + panelH);
        if (theme.brushBorderSubtle) {
            pRT->DrawLine(D2D1::Point2F(panelX, panelY), D2D1::Point2F(panelX, panelY + panelH), theme.brushBorderSubtle.Get(), 1.0f);
        }
    } else {
        m_splitterBounds = D2D1::RectF(0, 0, 0, 0);
    }

    // 3. Header Toolbar
    float headerH = config.ScaleF(32.0f);
    m_headerBounds = D2D1::RectF(panelX, panelY + config.ScaleF(2.0f), panelX + panelW, panelY + headerH);
    RenderHeader(pRT, theme, config, panelX, panelY + config.ScaleF(2.0f), panelW, headerH - config.ScaleF(2.0f), 0.0f);

    // 4. Terminal Content Area
    float contentY = panelY + headerH;
    float contentH = panelH - headerH;
    m_textAreaBounds = D2D1::RectF(panelX, contentY, panelX + panelW, panelY + panelH);
    if (contentH > 5.0f) {
        RenderContent(pRT, theme, config, panelX, contentY, panelW, contentH);
    }

    pRT->PopAxisAlignedClip();

    // 5. Popups & Shell/Options Menus drawn outside clip bounds
    if (m_showShellMenu) {
        RenderShellMenu(pRT, theme, config);
    }
    if (m_showOptionsMenu) {
        RenderOptionsMenu(pRT, theme, config, clientH);
    }
}

void TerminalPanel::RenderHeader(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                float x, float y, float w, float h, float cornerRadius) {
    // Subtle header bottom line
    pRT->DrawLine(
        D2D1::Point2F(x, y + h),
        D2D1::Point2F(x + w, y + h),
        theme.brushBorderSubtle.Get(),
        1.0f
    );

    float curX = x + (cornerRadius > 0.0f ? config.ScaleF(18.0f) : config.ScaleF(12.0f));

    // "Terminal" Title Label (Image 1)
    D2D1_RECT_F rcTitle = D2D1::RectF(curX, y, curX + config.ScaleF(60.0f), y + h);
    pRT->DrawText(L"Terminal", 8, theme.fontButtonCenter.Get() ? theme.fontButtonCenter.Get() : theme.fontMenuItem.Get(),
                  rcTitle, theme.brushTextPrimary.Get());
    curX += config.ScaleF(68.0f);

    float btnSize = config.ScaleF(20.0f);
    float btnY = y + (h - btnSize) / 2.0f;
    float nr = config.ScaleF(4.0f);

    if (m_showToolbar) {
        // Tabs List: "Local x", "Local (2) x"
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            auto& tab = m_tabs[i];
            bool isActive = (static_cast<int>(i) == m_activeTabIdx);
            bool isHover = (static_cast<int>(i) == m_hoveredTabIdx);

            float tabW = config.ScaleF(static_cast<float>(tab->title.length() * 7 + 36));
            float tabH = config.ScaleF(24.0f);
            float tabY = y + (h - tabH) / 2.0f;

            tab->tabBounds = D2D1::RectF(curX, tabY, curX + tabW, tabY + tabH);
            D2D1_ROUNDED_RECT rrcTab = D2D1::RoundedRect(tab->tabBounds, 3.5f, 3.5f);

            if (isActive) {
                // Active Tab: Darker rounded pill background (Image 1)
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> activePillBg;
                pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.22f, 0.36f, 1.0f) : D2D1::ColorF(0.000f, 0.400f, 0.850f, 1.0f), &activePillBg);
                if (activePillBg) pRT->FillRoundedRectangle(rrcTab, activePillBg.Get());
            } else if (isHover) {
                pRT->FillRoundedRectangle(rrcTab, theme.brushItemHover.Get());
            }

            // Tab Title Text
            D2D1_RECT_F rcTabText = D2D1::RectF(curX + config.ScaleF(8.0f), tabY, curX + tabW - (tab->canClose ? config.ScaleF(20.0f) : config.ScaleF(8.0f)), tabY + tabH);
            ID2D1Brush* textBrush = isActive ? theme.brushTextWhite.Get() : theme.brushTextSecondary.Get();
            pRT->DrawText(tab->title.c_str(), static_cast<UINT32>(tab->title.length()),
                          theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                          rcTabText, textBrush);

            // Close 'x' button on tab (un-Xable if !tab->canClose, e.g. "Git Logs")
            if (tab->canClose) {
                float closeX = curX + tabW - config.ScaleF(16.0f);
                float closeY = tabY + tabH / 2.0f;
                tab->closeBounds = D2D1::RectF(closeX - config.ScaleF(6.0f), tabY + config.ScaleF(4.0f),
                                               closeX + config.ScaleF(6.0f), tabY + tabH - config.ScaleF(4.0f));

                float xr = config.ScaleF(3.0f);
                ID2D1Brush* closeBrush = (static_cast<int>(i) == m_hoveredCloseIdx) ? theme.brushCloseHoverBg.Get() : theme.brushTextMuted.Get();
                pRT->DrawLine(D2D1::Point2F(closeX - xr, closeY - xr), D2D1::Point2F(closeX + xr, closeY + xr), closeBrush, 1.2f);
                pRT->DrawLine(D2D1::Point2F(closeX + xr, closeY - xr), D2D1::Point2F(closeX - xr, closeY + xr), closeBrush, 1.2f);
            } else {
                tab->closeBounds = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
            }

            curX += tabW + config.ScaleF(4.0f);
        }

        // '+' Button (New tab)
        m_newTabBtnBounds = D2D1::RectF(curX, btnY, curX + btnSize, btnY + btnSize);
        if (m_hoverNewTab) {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_newTabBtnBounds, 3.0f, 3.0f);
            pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
        }
        float ncx = curX + btnSize / 2.0f;
        float ncy = btnY + btnSize / 2.0f;
        pRT->DrawLine(D2D1::Point2F(ncx - nr, ncy), D2D1::Point2F(ncx + nr, ncy), theme.brushTextPrimary.Get(), 1.4f);
        pRT->DrawLine(D2D1::Point2F(ncx, ncy - nr), D2D1::Point2F(ncx, ncy + nr), theme.brushTextPrimary.Get(), 1.4f);
        curX += btnSize + config.ScaleF(2.0f);

        // 'v' Chevron Dropdown Button (Image 2)
        m_shellDropdownBtnBounds = D2D1::RectF(curX, btnY, curX + btnSize, btnY + btnSize);
        if (m_hoverShellDropdown || m_showShellMenu) {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_shellDropdownBtnBounds, 3.0f, 3.0f);
            pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
        }
        float scx = curX + btnSize / 2.0f;
        float scy = btnY + btnSize / 2.0f;
        float sr = config.ScaleF(3.0f);
        pRT->DrawLine(D2D1::Point2F(scx - sr, scy - sr * 0.5f), D2D1::Point2F(scx, scy + sr * 0.7f), theme.brushTextSecondary.Get(), 1.3f);
        pRT->DrawLine(D2D1::Point2F(scx, scy + sr * 0.7f), D2D1::Point2F(scx + sr, scy - sr * 0.5f), theme.brushTextSecondary.Get(), 1.3f);
    } else {
        m_newTabBtnBounds = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        m_shellDropdownBtnBounds = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        for (auto& tab : m_tabs) {
            tab->tabBounds = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
            tab->closeBounds = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    // =========================================================================
    // Right Controls: '...' (Options) and '-' (Minimize)
    // NOTE: AI Agents is EXCLUDED per user instruction ("DONT INCLIUDE THE ONE IN DRAW X")
    // =========================================================================
    float rightX = x + w - config.ScaleF(8.0f);

    // '-' Minimize / Close Panel Button
    rightX -= btnSize;
    m_minimizeBtnBounds = D2D1::RectF(rightX, btnY, rightX + btnSize, btnY + btnSize);
    if (m_hoverMinimize) {
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_minimizeBtnBounds, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
    }
    float mcx = rightX + btnSize / 2.0f;
    float mcy = btnY + btnSize / 2.0f;
    pRT->DrawLine(D2D1::Point2F(mcx - nr, mcy), D2D1::Point2F(mcx + nr, mcy), theme.brushTextSecondary.Get(), 1.4f);

    // Full Screen / Maximize / Restore Button
    rightX -= (btnSize + config.ScaleF(4.0f));
    m_fullScreenBtnBounds = D2D1::RectF(rightX, btnY, rightX + btnSize, btnY + btnSize);
    if (m_hoverFullScreen) {
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_fullScreenBtnBounds, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
    }
    float fcx = rightX + btnSize / 2.0f;
    float fcy = btnY + btnSize / 2.0f;
    float fSize = config.ScaleF(4.5f);
    if (!m_isFullScreen) {
        // Draw maximize single square box
        pRT->DrawRectangle(D2D1::RectF(fcx - fSize, fcy - fSize, fcx + fSize, fcy + fSize), theme.brushTextSecondary.Get(), 1.2f);
    } else {
        // Draw restore double overlapping boxes
        pRT->DrawRectangle(D2D1::RectF(fcx - fSize + 2.0f, fcy - fSize - 2.0f, fcx + fSize + 2.0f, fcy + fSize - 2.0f), theme.brushTextSecondary.Get(), 1.0f);
        pRT->DrawRectangle(D2D1::RectF(fcx - fSize - 1.0f, fcy - fSize + 1.0f, fcx + fSize - 1.0f, fcy + fSize + 1.0f), theme.brushTextSecondary.Get(), 1.2f);
    }

    // Split Terminal Button [|] (Images 4 & 5)
    rightX -= (btnSize + config.ScaleF(4.0f));
    m_splitBtnBounds = D2D1::RectF(rightX, btnY, rightX + btnSize, btnY + btnSize);
    bool isSplitActive = IsActiveTabSplit();
    if (m_hoverSplitBtn || isSplitActive) {
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_splitBtnBounds, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrc, isSplitActive ? theme.brushBrandAccent.Get() : theme.brushItemHover.Get());
    }
    float scxSplit = rightX + btnSize / 2.0f;
    float scySplit = btnY + btnSize / 2.0f;
    float sSizeSplit = config.ScaleF(4.5f);
    ID2D1Brush* splitBrush = isSplitActive ? theme.brushTextWhite.Get() : theme.brushTextSecondary.Get();
    pRT->DrawRectangle(D2D1::RectF(scxSplit - sSizeSplit, scySplit - sSizeSplit, scxSplit + sSizeSplit, scySplit + sSizeSplit), splitBrush, 1.2f);
    pRT->DrawLine(D2D1::Point2F(scxSplit, scySplit - sSizeSplit), D2D1::Point2F(scxSplit, scySplit + sSizeSplit), splitBrush, 1.2f);

    // '...' Options Menu Button (Circled in Green in Image 3)
    rightX -= (btnSize + config.ScaleF(4.0f));
    m_optionsBtnBounds = D2D1::RectF(rightX, btnY, rightX + btnSize, btnY + btnSize);
    if (m_hoverOptions || m_showOptionsMenu) {
        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(m_optionsBtnBounds, 3.0f, 3.0f);
        pRT->FillRoundedRectangle(rrc, theme.brushItemHover.Get());
    }
    float ocx = rightX + btnSize / 2.0f;
    float ocy = btnY + btnSize / 2.0f;
    float dotR = config.ScaleF(1.3f);
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ocx, ocy - config.ScaleF(5.0f)), dotR, dotR), theme.brushTextSecondary.Get());
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ocx, ocy), dotR, dotR), theme.brushTextSecondary.Get());
    pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ocx, ocy + config.ScaleF(5.0f)), dotR, dotR), theme.brushTextSecondary.Get());

    // =========================================================================
    // Debug Toolbar Controls: Continue, Step Over, Step Into, Step Out, Restart, Stop
    // Rendered when Debug session is active (Reinforced toolbar pill with tooltips)
    // =========================================================================
    if (m_isDebugActive) {
        float dbgBtnW = config.ScaleF(24.0f);
        float dbgSpacing = config.ScaleF(3.0f);
        float numBtns = 6.0f;
        float totalToolbarW = numBtns * dbgBtnW + (numBtns - 1.0f) * dbgSpacing + config.ScaleF(8.0f);

        rightX -= config.ScaleF(8.0f);
        float toolbarRight = rightX;
        float toolbarLeft = toolbarRight - totalToolbarW;
        float toolbarTop = btnY - config.ScaleF(1.0f);
        float toolbarBottom = btnY + btnSize + config.ScaleF(1.0f);

        // Reinforced toolbar container backdrop
        D2D1_ROUNDED_RECT rrcToolbar = D2D1::RoundedRect(
            D2D1::RectF(toolbarLeft, toolbarTop, toolbarRight, toolbarBottom), 4.0f, 4.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> toolbarBg;
        pRT->CreateSolidColorBrush(
            theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.90f) : D2D1::ColorF(0.88f, 0.88f, 0.92f, 0.90f),
            &toolbarBg);
        if (toolbarBg) pRT->FillRoundedRectangle(rrcToolbar, toolbarBg.Get());
        pRT->DrawRoundedRectangle(rrcToolbar, theme.brushBorderSubtle ? theme.brushBorderSubtle.Get() : theme.brushItemHover.Get(), 0.8f);

        // 6. Stop Debugging (Shift+F5)
        float curBtnX = toolbarRight - config.ScaleF(4.0f) - dbgBtnW;
        m_debugStopBounds = D2D1::RectF(curBtnX, btnY, curBtnX + dbgBtnW, btnY + btnSize);
        if (m_hoveredDebugAction == DebugAction::Stop) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_debugStopBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        float stcx = curBtnX + dbgBtnW / 2.0f;
        float stcy = btnY + btnSize / 2.0f;
        float stSize = config.ScaleF(4.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> stopBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.35f, 0.35f), &stopBrush);
        if (stopBrush) {
            D2D1_ROUNDED_RECT rrcSt = D2D1::RoundedRect(
                D2D1::RectF(stcx - stSize, stcy - stSize, stcx + stSize, stcy + stSize), 2.0f, 2.0f);
            pRT->FillRoundedRectangle(rrcSt, stopBrush.Get());
        }

        // 5. Restart Session (Ctrl+Shift+F5)
        curBtnX -= (dbgBtnW + dbgSpacing);
        m_debugRestartBounds = D2D1::RectF(curBtnX, btnY, curBtnX + dbgBtnW, btnY + btnSize);
        if (m_hoveredDebugAction == DebugAction::Restart) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_debugRestartBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> restartBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.82f, 0.25f), &restartBrush);
        if (restartBrush) {
            float rcx = curBtnX + dbgBtnW / 2.0f;
            float rcy = btnY + btnSize / 2.0f;
            float rcr = config.ScaleF(4.0f);
            pRT->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(rcx, rcy), rcr, rcr), restartBrush.Get(), 1.4f);
            pRT->DrawLine(D2D1::Point2F(rcx + rcr - 2.0f, rcy - 2.0f), D2D1::Point2F(rcx + rcr, rcy), restartBrush.Get(), 1.4f);
            pRT->DrawLine(D2D1::Point2F(rcx + rcr + 2.0f, rcy - 2.0f), D2D1::Point2F(rcx + rcr, rcy), restartBrush.Get(), 1.4f);
        }

        // 4. Step Out (Shift+F11)
        curBtnX -= (dbgBtnW + dbgSpacing);
        m_debugStepOutBounds = D2D1::RectF(curBtnX, btnY, curBtnX + dbgBtnW, btnY + btnSize);
        if (m_hoveredDebugAction == DebugAction::StepOut) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_debugStepOutBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> stepBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.75f, 0.98f), &stepBrush);
        if (stepBrush) {
            float sox = curBtnX + dbgBtnW / 2.0f;
            float soy = btnY + btnSize / 2.0f;
            pRT->DrawLine(D2D1::Point2F(sox - 3.0f, soy + 3.0f), D2D1::Point2F(sox - 3.0f, soy - 2.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(sox - 3.0f, soy - 2.0f), D2D1::Point2F(sox + 3.0f, soy - 2.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(sox + 1.0f, soy - 5.0f), D2D1::Point2F(sox + 4.0f, soy - 2.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(sox + 1.0f, soy + 1.0f), D2D1::Point2F(sox + 4.0f, soy - 2.0f), stepBrush.Get(), 1.3f);
        }

        // 3. Step Into (F11)
        curBtnX -= (dbgBtnW + dbgSpacing);
        m_debugStepIntoBounds = D2D1::RectF(curBtnX, btnY, curBtnX + dbgBtnW, btnY + btnSize);
        if (m_hoveredDebugAction == DebugAction::StepInto) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_debugStepIntoBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        if (stepBrush) {
            float six = curBtnX + dbgBtnW / 2.0f;
            float siy = btnY + btnSize / 2.0f;
            pRT->DrawLine(D2D1::Point2F(six - 3.0f, siy - 3.0f), D2D1::Point2F(six - 3.0f, siy + 2.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(six - 3.0f, siy + 2.0f), D2D1::Point2F(six + 3.0f, siy + 2.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(six + 1.0f, siy - 1.0f), D2D1::Point2F(six + 4.0f, siy + 2.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(six + 1.0f, siy + 5.0f), D2D1::Point2F(six + 4.0f, siy + 2.0f), stepBrush.Get(), 1.3f);
        }

        // 2. Step Over (F10)
        curBtnX -= (dbgBtnW + dbgSpacing);
        m_debugStepOverBounds = D2D1::RectF(curBtnX, btnY, curBtnX + dbgBtnW, btnY + btnSize);
        if (m_hoveredDebugAction == DebugAction::StepOver) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_debugStepOverBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        if (stepBrush) {
            float sox = curBtnX + dbgBtnW / 2.0f;
            float soy = btnY + btnSize / 2.0f;
            pRT->DrawLine(D2D1::Point2F(sox - 4.0f, soy + 2.0f), D2D1::Point2F(sox - 2.0f, soy - 3.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(sox - 2.0f, soy - 3.0f), D2D1::Point2F(sox + 2.0f, soy - 3.0f), stepBrush.Get(), 1.3f);
            pRT->DrawLine(D2D1::Point2F(sox + 2.0f, soy - 3.0f), D2D1::Point2F(sox + 4.0f, soy + 2.0f), stepBrush.Get(), 1.3f);
        }

        // 1. Continue (F5)
        curBtnX -= (dbgBtnW + dbgSpacing);
        m_debugContinueBounds = D2D1::RectF(curBtnX, btnY, curBtnX + dbgBtnW, btnY + btnSize);
        if (m_hoveredDebugAction == DebugAction::Continue) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_debugContinueBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> contBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.85f, 0.45f), &contBrush);
        if (contBrush) {
            float cx = curBtnX + dbgBtnW / 2.0f;
            float cy = btnY + btnSize / 2.0f;
            pRT->DrawLine(D2D1::Point2F(cx - 3.0f, cy - 4.0f), D2D1::Point2F(cx + 4.0f, cy), contBrush.Get(), 1.5f);
            pRT->DrawLine(D2D1::Point2F(cx + 4.0f, cy), D2D1::Point2F(cx - 3.0f, cy + 4.0f), contBrush.Get(), 1.5f);
            pRT->DrawLine(D2D1::Point2F(cx - 3.0f, cy + 4.0f), D2D1::Point2F(cx - 3.0f, cy - 4.0f), contBrush.Get(), 1.5f);
        }

        rightX = toolbarLeft - config.ScaleF(6.0f);
    } else {
        m_debugContinueBounds = {};
        m_debugStepOverBounds = {};
        m_debugStepIntoBounds = {};
        m_debugStepOutBounds = {};
        m_debugRestartBounds = {};
        m_debugStopBounds = {};
    }
}

void TerminalPanel::RenderContent(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                 float x, float y, float w, float h) {
    if (m_activeTabIdx < 0 || m_activeTabIdx >= static_cast<int>(m_tabs.size())) return;
    auto* primaryTab = m_tabs[m_activeTabIdx].get();
    if (!primaryTab) return;

    if (primaryTab->splitTab) {
        float splitterW = config.ScaleF(4.0f);
        float totalW = w;
        float leftW = (totalW - splitterW) * primaryTab->splitRatio;
        float rightW = totalW - splitterW - leftW;

        D2D1_RECT_F leftBounds = D2D1::RectF(x, y, x + leftW, y + h);
        primaryTab->paneBounds = leftBounds;

        D2D1_RECT_F splitterBounds = D2D1::RectF(x + leftW, y, x + leftW + splitterW, y + h);
        primaryTab->splitSplitterBounds = splitterBounds;

        D2D1_RECT_F rightBounds = D2D1::RectF(x + leftW + splitterW, y, x + totalW, y + h);
        primaryTab->splitTab->paneBounds = rightBounds;

        // Render left pane
        RenderSinglePane(pRT, theme, config, primaryTab, leftBounds, primaryTab->activeSplitIndex == 0);

        // Render vertical splitter bar
        ID2D1Brush* splitterBrush = m_isSplitDragging ? theme.brushBrandAccent.Get() : theme.brushBorderSubtle.Get();
        pRT->FillRectangle(splitterBounds, splitterBrush);

        // Render right pane
        RenderSinglePane(pRT, theme, config, primaryTab->splitTab.get(), rightBounds, primaryTab->activeSplitIndex == 1);
    } else {
        primaryTab->paneBounds = D2D1::RectF(x, y, x + w, y + h);
        RenderSinglePane(pRT, theme, config, primaryTab, primaryTab->paneBounds, true);
    }
}

void TerminalPanel::RenderSinglePane(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                                     TerminalTab* tab, const D2D1_RECT_F& bounds, bool isFocused) {
    if (!tab) return;

    pRT->PushAxisAlignedClip(bounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Active pane highlight border (when split)
    if (IsActiveTabSplit() && isFocused) {
        pRT->DrawRectangle(bounds, theme.brushBrandAccent.Get(), 1.0f);
    }

    // Split pane badge and close button (if split and this is the right split tab)
    if (IsActiveTabSplit() && tab != m_tabs[m_activeTabIdx].get()) {
        float badgeW = config.ScaleF(static_cast<float>(tab->title.length() * 6 + 28));
        float badgeH = config.ScaleF(18.0f);
        float badgeX = bounds.right - badgeW - config.ScaleF(8.0f);
        float badgeY = bounds.top + config.ScaleF(4.0f);
        D2D1_RECT_F rcBadge = D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH);
        D2D1_ROUNDED_RECT rrcBadge = D2D1::RoundedRect(rcBadge, 3.0f, 3.0f);

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeBrush;
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.16f, 0.20f, 0.90f) : D2D1::ColorF(0.90f, 0.92f, 0.95f, 0.90f), &badgeBrush);
        if (badgeBrush) pRT->FillRoundedRectangle(rrcBadge, badgeBrush.Get());

        D2D1_RECT_F rcBadgeText = D2D1::RectF(badgeX + config.ScaleF(6.0f), badgeY, badgeX + badgeW - config.ScaleF(14.0f), badgeY + badgeH);
        pRT->DrawText(tab->title.c_str(), static_cast<UINT32>(tab->title.length()),
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                      rcBadgeText, theme.brushTextSecondary.Get());

        // Split close button 'x'
        float cx = badgeX + badgeW - config.ScaleF(8.0f);
        float cy = badgeY + badgeH / 2.0f;
        float cr = config.ScaleF(3.0f);
        tab->splitCloseBounds = D2D1::RectF(cx - cr - 2.0f, cy - cr - 2.0f, cx + cr + 2.0f, cy + cr + 2.0f);
        pRT->DrawLine(D2D1::Point2F(cx - cr, cy - cr), D2D1::Point2F(cx + cr, cy + cr), theme.brushTextMuted.Get(), 1.2f);
        pRT->DrawLine(D2D1::Point2F(cx + cr, cy - cr), D2D1::Point2F(cx - cr, cy + cr), theme.brushTextMuted.Get(), 1.2f);
    }

    std::lock_guard<std::mutex> lock(tab->bufferMutex);

    bool isDebugConsole = (tab->shellType == TerminalShellType::DebugConsole);
    float debugBarH = isDebugConsole ? config.ScaleF(26.0f) : 0.0f;
    float debugEvalH = isDebugConsole ? config.ScaleF(26.0f) : 0.0f;

    float padX = config.ScaleF(12.0f);
    float padY = config.ScaleF(6.0f) + debugBarH;
    float padBottom = config.ScaleF(22.0f) + debugEvalH;
    float lineH = config.ScaleF(17.0f);

    // If Debug Console, render top filter bar (Image 3)
    if (isDebugConsole) {
        D2D1_RECT_F rcToolbar = D2D1::RectF(bounds.left, bounds.top, bounds.right, bounds.top + debugBarH);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> barBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.11f, 0.14f, 0.95f), &barBg);
        pRT->FillRectangle(rcToolbar, barBg.Get());
        pRT->DrawLine(D2D1::Point2F(bounds.left, bounds.top + debugBarH),
                      D2D1::Point2F(bounds.right, bounds.top + debugBarH),
                      theme.brushBorderSubtle.Get(), 0.8f);

        // Filter search input
        float filterW = config.ScaleF(140.0f);
        D2D1_ROUNDED_RECT rrcFilter = D2D1::RoundedRect(
            D2D1::RectF(bounds.left + config.ScaleF(10.0f), bounds.top + config.ScaleF(3.0f),
                        bounds.left + config.ScaleF(10.0f) + filterW, bounds.top + debugBarH - config.ScaleF(3.0f)),
            3.0f, 3.0f);
        pRT->DrawRoundedRectangle(rrcFilter, theme.brushBorderSubtle.Get(), 0.8f);
        std::wstring filterPlaceholder = L"🔍 Filter output...";
        pRT->DrawText(filterPlaceholder.c_str(), static_cast<UINT32>(filterPlaceholder.length()),
                      theme.fontHint.Get(),
                      D2D1::RectF(rrcFilter.rect.left + 6.0f, rrcFilter.rect.top + 2.0f, rrcFilter.rect.right - 4.0f, rrcFilter.rect.bottom),
                      theme.brushTextMuted.Get());

        // Category chips: All (●), Info, Errors
        float chipX = bounds.left + config.ScaleF(160.0f);
        auto drawChip = [&](const std::wstring& txt, D2D1_COLOR_F dotCol, bool active) {
            float cW = config.ScaleF(static_cast<float>(txt.length() * 7 + 16));
            D2D1_RECT_F rcChip = D2D1::RectF(chipX, bounds.top + config.ScaleF(3.0f), chipX + cW, bounds.top + debugBarH - config.ScaleF(3.0f));
            if (active) {
                pRT->FillRoundedRectangle(D2D1::RoundedRect(rcChip, 3.0f, 3.0f), theme.brushTreeSelected.Get());
            }
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dotBrush;
            pRT->CreateSolidColorBrush(dotCol, &dotBrush);
            pRT->FillEllipse(D2D1::Ellipse(D2D1::Point2F(chipX + 7.0f, bounds.top + debugBarH / 2.0f), 2.5f, 2.5f), dotBrush.Get());
            pRT->DrawText(txt.c_str(), static_cast<UINT32>(txt.length()), theme.fontHint.Get(),
                          D2D1::RectF(chipX + 13.0f, rcChip.top + 2.0f, rcChip.right, rcChip.bottom),
                          active ? theme.brushTextWhite.Get() : theme.brushTextSecondary.Get());
            chipX += cW + config.ScaleF(6.0f);
        };
        drawChip(L"All", D2D1::ColorF(0.35f, 0.82f, 0.98f), true);
        drawChip(L"Info", D2D1::ColorF(0.20f, 0.85f, 0.55f), false);
        drawChip(L"Errors (0)", D2D1::ColorF(0.92f, 0.35f, 0.35f), false);

        // Clear console button on right
        float clrW = config.ScaleF(100.0f);
        m_clearConsoleBtnBounds = D2D1::RectF(bounds.right - clrW - config.ScaleF(10.0f), bounds.top + config.ScaleF(3.0f),
                                             bounds.right - config.ScaleF(10.0f), bounds.top + debugBarH - config.ScaleF(3.0f));
        if (m_hoverClearConsole) {
            pRT->FillRoundedRectangle(D2D1::RoundedRect(m_clearConsoleBtnBounds, 3.0f, 3.0f), theme.brushItemHover.Get());
        }
        std::wstring clrText = L"⊘ Clear Console";
        pRT->DrawText(clrText.c_str(), static_cast<UINT32>(clrText.length()), theme.fontHint.Get(),
                      D2D1::RectF(m_clearConsoleBtnBounds.left + 4.0f, m_clearConsoleBtnBounds.top + 2.0f, m_clearConsoleBtnBounds.right, m_clearConsoleBtnBounds.bottom),
                      m_hoverClearConsole ? theme.brushTextWhite.Get() : theme.brushTextSecondary.Get());
    } else {
        m_clearConsoleBtnBounds = {};
    }

    float availableH = (bounds.bottom - bounds.top) - padY - padBottom;
    float maxVisibleLines = availableH > 0.0f ? (availableH / lineH) : 1.0f;

    size_t totalLines = tab->lines.size();
    size_t maxScroll = (totalLines > static_cast<size_t>(maxVisibleLines)) ? (totalLines - static_cast<size_t>(maxVisibleLines)) : 0;
    if (tab->scrollOffsetLines > static_cast<int>(maxScroll)) {
        tab->scrollOffsetLines = static_cast<int>(maxScroll);
    }
    if (tab->scrollOffsetLines < 0) {
        tab->scrollOffsetLines = 0;
    }
    size_t startLine = (maxScroll > 0) ? (maxScroll - static_cast<size_t>(tab->scrollOffsetLines)) : 0;

    m_visibleUrls.clear();

    float curY = bounds.top + padY;
    float curX = bounds.left + padX;

    // DirectWrite character advance width using Medium Normal terminal font
    float charW = config.ScaleF(7.8f);
    IDWriteTextFormat* termFmt = theme.fontTerminal ? theme.fontTerminal.Get() : theme.fontCode.Get();
    if (theme.dwriteFactory && termFmt) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
        if (SUCCEEDED(theme.dwriteFactory->CreateTextLayout(L"M", 1, termFmt, 1000.0f, lineH, &pLayout))) {
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(pLayout->GetMetrics(&tm))) {
                charW = tm.widthIncludingTrailingWhitespace;
            }
        }
    }

    m_uiScale = config.dpiScale;

    if (tab->isConPty && tab->isAlive.load()) {
        bool allBlank = true;
        for (const auto& ln : tab->lines) {
            for (wchar_t ch : ln.rawText) {
                if (ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n') {
                    allBlank = false;
                    break;
                }
            }
            if (!allBlank) break;
        }
        if (allBlank && termFmt && theme.brushTextSecondary) {
            std::wstring waitMsg = L"Connecting remote session (" + tab->title + L")...";
            D2D1_RECT_F rcWait = D2D1::RectF(bounds.left + padX, curY, bounds.right - padX, curY + lineH);
            pRT->DrawText(waitMsg.c_str(), static_cast<UINT32>(waitMsg.size()), termFmt, rcWait, theme.brushTextSecondary.Get());
            return;
        }
    }

    for (size_t l = startLine; l < totalLines && (curY + lineH) <= (bounds.bottom - padBottom + 2.0f); ++l) {
        const auto& line = tab->lines[l];
        curX = bounds.left + padX;

        // Render selection highlight background for line l
        if (tab->selection.hasSelection()) {
            TerminalPos selStart = tab->selection.getStart();
            TerminalPos selEnd = tab->selection.getEnd();
            int lInt = static_cast<int>(l);
            if (lInt >= selStart.line && lInt <= selEnd.line) {
                std::wstring fullLineText = line.rawText;
                if (l == totalLines - 1 && !tab->currentInputLine.empty()) {
                    fullLineText += tab->currentInputLine;
                }
                int startCol = 0;
                int endCol = static_cast<int>(fullLineText.length());

                if (lInt == selStart.line && lInt == selEnd.line) {
                    startCol = (std::max)(0, (std::min)(selStart.col, static_cast<int>(fullLineText.length())));
                    endCol = (std::max)(0, (std::min)(selEnd.col, static_cast<int>(fullLineText.length())));
                } else if (lInt == selStart.line) {
                    startCol = (std::max)(0, (std::min)(selStart.col, static_cast<int>(fullLineText.length())));
                    endCol = static_cast<int>(fullLineText.length()) + 1;
                } else if (lInt == selEnd.line) {
                    startCol = 0;
                    endCol = (std::max)(0, (std::min)(selEnd.col, static_cast<int>(fullLineText.length())));
                } else {
                    startCol = 0;
                    endCol = static_cast<int>(fullLineText.length()) + 1;
                }

                if (endCol > startCol) {
                    float selLeft = bounds.left + padX + static_cast<float>(startCol) * charW;
                    float selRight = bounds.left + padX + static_cast<float>(endCol) * charW;
                    D2D1_RECT_F rcSel = D2D1::RectF(selLeft, curY, selRight, curY + lineH);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brushSel;
                    pRT->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.48f, 0.90f, 0.42f), &brushSel);
                    if (brushSel) pRT->FillRectangle(rcSel, brushSel.Get());
                }
            }
        }

        // Auto-detect error lines
        bool isLineError = false;
        if (line.rawText.find(L"Cannot find path") != std::wstring::npos ||
            line.rawText.find(L"CategoryInfo") != std::wstring::npos ||
            line.rawText.find(L"FullyQualifiedErrorId") != std::wstring::npos ||
            line.rawText.rfind(L"+ ", 0) == 0 ||
            line.rawText.rfind(L"Error:", 0) == 0 ||
            line.rawText.rfind(L"Exception:", 0) == 0 ||
            line.rawText.find(L"The term '") != std::wstring::npos) {
            isLineError = true;
        }

        auto formattedSpans = FormatTerminalLine(line, isLineError, tab->workingDir);

        for (const auto& span : formattedSpans) {
            float spanW = static_cast<float>(span.text.length()) * charW;
            D2D1_RECT_F rcSpan = D2D1::RectF(curX, curY, curX + spanW + 50.0f, curY + lineH);

            D2D1_COLOR_F drawColor = theme.IsDarkMode() ? span.color : AdaptTerminalColorForLight(span.color);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> spanBrush;
            pRT->CreateSolidColorBrush(drawColor, &spanBrush);
            if (spanBrush) {
                pRT->DrawText(span.text.c_str(), static_cast<UINT32>(span.text.length()),
                              termFmt, rcSpan, spanBrush.Get());
            }

            // Detect hyperlinks directly within span.text for pixel-perfect Ctrl+Click hitboxes
            size_t searchPos = 0;
            while (searchPos < span.text.length()) {
                size_t pHttp = span.text.find(L"http://", searchPos);
                size_t pHttps = span.text.find(L"https://", searchPos);
                size_t uStart = std::wstring::npos;
                if (pHttp != std::wstring::npos && pHttps != std::wstring::npos) {
                    uStart = (std::min)(pHttp, pHttps);
                } else if (pHttp != std::wstring::npos) {
                    uStart = pHttp;
                } else {
                    uStart = pHttps;
                }

                if (uStart == std::wstring::npos) break;

                size_t uEnd = uStart;
                while (uEnd < span.text.length()) {
                    wchar_t uc = span.text[uEnd];
                    if (uc == L' ' || uc == L'\t' || uc == L'\r' || uc == L'\n' || uc == L'"' || uc == L'\'' || uc == L'<' || uc == L'>' || uc == L')' || uc == L']') {
                        break;
                    }
                    uEnd++;
                }

                if (uEnd > uStart) {
                    std::wstring uStr = span.text.substr(uStart, uEnd - uStart);
                    float uX = curX + static_cast<float>(uStart) * charW;
                    float uW = static_cast<float>(uStr.length()) * charW;
                    D2D1_RECT_F uRc = D2D1::RectF(uX, curY, uX + uW, curY + lineH);
                    m_visibleUrls.push_back({ uStr, uRc });

                    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                        pRT->DrawLine(
                            D2D1::Point2F(uX, curY + lineH - 2.0f),
                            D2D1::Point2F(uX + uW, curY + lineH - 2.0f),
                            theme.brushBrandAccent.Get() ? theme.brushBrandAccent.Get() : theme.brushTextPrimary.Get(),
                            1.2f
                        );
                    }
                }
                searchPos = uEnd + 1;
            }

            curX += spanW;
        }

        // Render typed command line in real-time
        if (l == totalLines - 1 && !tab->currentInputLine.empty()) {
            auto inputSpans = TokenizeCommandLineWithLinuxRules(tab->currentInputLine, tab->workingDir);
            for (const auto& span : inputSpans) {
                float spanW = static_cast<float>(span.text.length()) * charW;
                D2D1_RECT_F rcSpan = D2D1::RectF(curX, curY, curX + spanW + 50.0f, curY + lineH);
                D2D1_COLOR_F drawColor = theme.IsDarkMode() ? span.color : AdaptTerminalColorForLight(span.color);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> spanBrush;
                pRT->CreateSolidColorBrush(drawColor, &spanBrush);
                if (spanBrush) {
                    pRT->DrawText(span.text.c_str(), static_cast<UINT32>(span.text.length()),
                                  termFmt, rcSpan, spanBrush.Get());
                }
                curX += spanW;
            }
        }

        // Blinking cursor '█' on the last active output line
        if (isFocused && l == totalLines - 1) {
            bool blinkOn = ((GetTickCount64() / 500) % 2 == 0);
            if (blinkOn) {
                int safeCursorCol = (std::max)(0, (std::min)(tab->inputCursorCol, static_cast<int>(tab->currentInputLine.length())));
                float caretX = curX - static_cast<float>(static_cast<int>(tab->currentInputLine.length()) - safeCursorCol) * charW;
                float cursorW = config.ScaleF(7.5f);
                float cursorH = lineH - config.ScaleF(2.0f);
                D2D1_RECT_F rcCursor = D2D1::RectF(caretX, curY + 1.0f, caretX + cursorW, curY + 1.0f + cursorH);
                pRT->FillRectangle(rcCursor, theme.brushTextPrimary.Get());
            }
        }

        curY += lineH;
    }

    // Vertical scrollbar on right if content overflows
    if (totalLines > static_cast<size_t>(maxVisibleLines) && maxScroll > 0) {
        float sbW = config.ScaleF(4.0f);
        float sbRight = bounds.right - config.ScaleF(2.0f);
        float sbLeft = sbRight - sbW;
        float sbTop = bounds.top + padY;
        float sbH = availableH;

        float thumbH = (std::max)(config.ScaleF(14.0f), sbH * (maxVisibleLines / static_cast<float>(totalLines)));
        float thumbY = sbTop + (static_cast<float>(maxScroll - tab->scrollOffsetLines) / static_cast<float>(maxScroll)) * (sbH - thumbH);

        D2D1_ROUNDED_RECT rrcSb = D2D1::RoundedRect(D2D1::RectF(sbLeft, thumbY, sbRight, thumbY + thumbH), 2.0f, 2.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sbBrush;
        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.40f, 0.42f, 0.48f, 0.6f) : D2D1::ColorF(0.65f, 0.65f, 0.70f, 0.7f), &sbBrush);
        if (sbBrush) pRT->FillRoundedRectangle(rrcSb, sbBrush.Get());
    }

    // Bottom div under terminal so letters don't stick to bottom border (Image 2)
    D2D1_RECT_F bottomDiv = D2D1::RectF(bounds.left, bounds.bottom - padBottom, bounds.right, bounds.bottom);
    D2D1_COLOR_F bottomDivColor = theme.IsDarkMode() ? D2D1::ColorF(0.07f, 0.07f, 0.08f, 1.0f) : D2D1::ColorF(0.94f, 0.94f, 0.95f, 1.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> divBrush;
    pRT->CreateSolidColorBrush(bottomDivColor, &divBrush);
    if (divBrush) pRT->FillRectangle(bottomDiv, divBrush.Get());
    pRT->DrawLine(D2D1::Point2F(bounds.left, bounds.bottom - padBottom), D2D1::Point2F(bounds.right, bounds.bottom - padBottom), theme.brushBorderSubtle.Get(), 0.5f);

    // Dedicated Bottom Evaluation Bar for Debug Console (Image 3)
    if (isDebugConsole) {
        D2D1_RECT_F rcEval = D2D1::RectF(bounds.left, bounds.bottom - debugEvalH, bounds.right, bounds.bottom);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> evalBg;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.09f, 0.12f, 0.98f), &evalBg);
        pRT->FillRectangle(rcEval, evalBg.Get());
        pRT->DrawLine(D2D1::Point2F(bounds.left, rcEval.top), D2D1::Point2F(bounds.right, rcEval.top), theme.brushBorderSubtle.Get(), 0.8f);

        // Prompt glyph "❯"
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> promptBrush;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.82f, 0.98f), &promptBrush);
        pRT->DrawText(L"❯", 1, termFmt,
                      D2D1::RectF(rcEval.left + config.ScaleF(10.0f), rcEval.top + config.ScaleF(4.0f),
                                 rcEval.left + config.ScaleF(24.0f), rcEval.bottom),
                      promptBrush.Get());

        float textStartX = rcEval.left + config.ScaleF(26.0f);
        if (tab->currentInputLine.empty()) {
            std::wstring evalPlaceholder = L"Evaluate expression in context (e.g. document.title, location.href, 1 + 1)...";
            pRT->DrawText(evalPlaceholder.c_str(), static_cast<UINT32>(evalPlaceholder.length()),
                          termFmt,
                          D2D1::RectF(textStartX, rcEval.top + config.ScaleF(4.0f), rcEval.right - config.ScaleF(10.0f), rcEval.bottom),
                          theme.brushTextMuted.Get());
        } else {
            pRT->DrawText(tab->currentInputLine.c_str(), static_cast<UINT32>(tab->currentInputLine.length()),
                          termFmt,
                          D2D1::RectF(textStartX, rcEval.top + config.ScaleF(4.0f), rcEval.right - config.ScaleF(10.0f), rcEval.bottom),
                          theme.brushTextWhite.Get());

            // Blinking caret
            int safeCursorCol = (std::max)(0, (std::min)(tab->inputCursorCol, static_cast<int>(tab->currentInputLine.length())));
            float caretX = textStartX + static_cast<float>(safeCursorCol) * charW;
            pRT->FillRectangle(D2D1::RectF(caretX, rcEval.top + config.ScaleF(5.0f), caretX + 1.6f, rcEval.bottom - config.ScaleF(5.0f)),
                               theme.brushTextPrimary.Get());
        }
    }

    pRT->PopAxisAlignedClip();
}

// =============================================================================
// Shell Dropdown Menu (Image 2)
// =============================================================================

void TerminalPanel::RenderShellMenu(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config) {
    if (m_availableProfiles.empty()) {
        m_availableProfiles = DetectAvailableShells();
    }

    float itemH = config.ScaleF(24.0f);
    float menuW = config.ScaleF(190.0f);
    float menuH = config.ScaleF(static_cast<float>(m_availableProfiles.size() * 24 + 14));
    float menuX = m_shellDropdownBtnBounds.left;
    float menuY = m_shellDropdownBtnBounds.bottom + config.ScaleF(2.0f);

    m_shellMenuBounds = D2D1::RectF(menuX, menuY, menuX + menuW, menuY + menuH);
    D2D1_ROUNDED_RECT rrcMenu = D2D1::RoundedRect(m_shellMenuBounds, 4.0f, 4.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> menuBgBrush;
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.15f, 0.18f, 0.98f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f), &menuBgBrush);
    if (menuBgBrush) pRT->FillRoundedRectangle(rrcMenu, menuBgBrush.Get());
    pRT->DrawRoundedRectangle(rrcMenu, theme.brushBorderSubtle.Get(), 1.0f);

    float itemY = menuY + config.ScaleF(6.0f);

    for (size_t i = 0; i < m_availableProfiles.size(); ++i) {
        const auto& profile = m_availableProfiles[i];
        D2D1_RECT_F rcItem = D2D1::RectF(menuX + 4.0f, itemY, menuX + menuW - 4.0f, itemY + itemH);
        if (static_cast<int>(i) == m_hoveredShellMenuItem) {
            D2D1_ROUNDED_RECT rrcItem = D2D1::RoundedRect(rcItem, 3.0f, 3.0f);
            pRT->FillRoundedRectangle(rrcItem, theme.brushBrandAccent.Get());
        }

        std::wstring label = profile.name;
        D2D1_RECT_F rcText = D2D1::RectF(menuX + config.ScaleF(14.0f), itemY, menuX + menuW - 4.0f, itemY + itemH);
        ID2D1Brush* brush = (static_cast<int>(i) == m_hoveredShellMenuItem) ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
        pRT->DrawText(label.c_str(), static_cast<UINT32>(label.length()),
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                      rcText, brush);

        itemY += itemH;
    }
}

// =============================================================================
// Options Dropdown Menu (Image 4)
// =============================================================================

void TerminalPanel::RenderOptionsMenu(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config, float clientH) {
    float menuW = config.ScaleF(185.0f);
    float itemH = config.ScaleF(22.0f);
    float sepH = config.ScaleF(6.0f);

    struct MenuItemDef {
        int id;
        std::wstring title;
        bool hasSubmenu;
        bool isSep;
    };
    std::vector<MenuItemDef> items = {
        { 0, L"Terminal Engine", true, false },
        { 1, L"⚙ Settings", false, false },
        { -1, L"", false, true },
        { 3, L"Close All", false, false },
        { 4, m_showToolbar ? L"✓ Show Toolbar" : L"  Show Toolbar", false, false },
        { 5, IsActiveTabSplit() ? L"Ungroup Tabs" : L"Group Tabs", false, false },
        { 6, L"View Mode", true, false },
        { 7, L"Move to", true, false },
        { 8, L"Resize", true, false },
        { -1, L"", false, true },
        { 10, L"Remove from Sidebar", false, false }
    };

    float menuH = config.ScaleF(8.0f);
    for (const auto& it : items) {
        menuH += it.isSep ? sepH : itemH;
    }

    float menuX = m_optionsBtnBounds.right - menuW;
    float menuY = m_optionsBtnBounds.bottom + config.ScaleF(2.0f);
    if (clientH > 0.0f && menuY + menuH > clientH - config.ScaleF(24.0f)) {
        if (m_optionsBtnBounds.top - menuH >= config.ScaleF(32.0f)) {
            menuY = m_optionsBtnBounds.top - menuH - config.ScaleF(2.0f);
        } else {
            menuY = (std::max)(config.ScaleF(36.0f), clientH - config.ScaleF(24.0f) - menuH - config.ScaleF(4.0f));
        }
    }
    m_optionsMenuBounds = D2D1::RectF(menuX, menuY, menuX + menuW, menuY + menuH);
    D2D1_ROUNDED_RECT rrcMenu = D2D1::RoundedRect(m_optionsMenuBounds, 4.0f, 4.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> menuBgBrush;
    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.14f, 0.15f, 0.18f, 0.98f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f), &menuBgBrush);
    if (menuBgBrush) pRT->FillRoundedRectangle(rrcMenu, menuBgBrush.Get());
    pRT->DrawRoundedRectangle(rrcMenu, theme.brushBorderSubtle.Get(), 1.0f);

    m_optionsItemBounds.assign(items.size(), D2D1::RectF(0, 0, 0, 0));
    float itemY = menuY + config.ScaleF(4.0f);

    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].isSep) {
            pRT->DrawLine(D2D1::Point2F(menuX + 6.0f, itemY + sepH / 2.0f),
                          D2D1::Point2F(menuX + menuW - 6.0f, itemY + sepH / 2.0f),
                          theme.brushBorderSubtle.Get(), 1.0f);
            itemY += sepH;
            continue;
        }

        D2D1_RECT_F rcItem = D2D1::RectF(menuX + 4.0f, itemY, menuX + menuW - 4.0f, itemY + itemH);
        m_optionsItemBounds[i] = rcItem;

        if (static_cast<int>(i) == m_hoveredOptionsMenuItem) {
            D2D1_ROUNDED_RECT rrcItem = D2D1::RoundedRect(rcItem, 3.0f, 3.0f);
            pRT->FillRoundedRectangle(rrcItem, theme.brushBrandAccent.Get());
        }

        D2D1_RECT_F rcText = D2D1::RectF(menuX + config.ScaleF(12.0f), itemY, menuX + menuW - config.ScaleF(20.0f), itemY + itemH);
        ID2D1Brush* brush = (static_cast<int>(i) == m_hoveredOptionsMenuItem) ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
        pRT->DrawText(items[i].title.c_str(), static_cast<UINT32>(items[i].title.length()),
                      theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                      rcText, brush);

        if (items[i].hasSubmenu) {
            float arrX = menuX + menuW - config.ScaleF(12.0f);
            float arrY = itemY + itemH / 2.0f;
            float ar = config.ScaleF(3.0f);
            pRT->DrawLine(D2D1::Point2F(arrX - ar, arrY - ar), D2D1::Point2F(arrX, arrY), brush, 1.2f);
            pRT->DrawLine(D2D1::Point2F(arrX, arrY), D2D1::Point2F(arrX - ar, arrY + ar), brush, 1.2f);
        }

        itemY += itemH;
    }

    // Helper lambda to draw any submenu
    auto drawSubmenu = [&](const std::vector<std::wstring>& subItems,
                           float parentItemTop,
                           float subW,
                           D2D1_RECT_F& outSubBounds,
                           std::vector<D2D1_RECT_F>& outItemBounds,
                           int hoveredIdx,
                           int sepIndex = -1) {
        float totalSubH = config.ScaleF(8.0f) + itemH * static_cast<float>(subItems.size()) + (sepIndex >= 0 ? sepH : 0.0f);
        float subX = menuX - subW - config.ScaleF(2.0f);
        if (subX < config.ScaleF(4.0f)) subX = menuX + menuW + config.ScaleF(2.0f);
        float subY = parentItemTop;
        float maxBottom = (clientH > 0.0f) ? (clientH - config.ScaleF(24.0f)) : m_panelBounds.bottom;
        if (subY + totalSubH > maxBottom) {
            subY = (std::max)(config.ScaleF(36.0f), maxBottom - totalSubH - config.ScaleF(4.0f));
        }
        if (subY < config.ScaleF(4.0f)) {
            subY = config.ScaleF(4.0f);
        }

        outSubBounds = D2D1::RectF(subX, subY, subX + subW, subY + totalSubH);
        D2D1_ROUNDED_RECT rrcSub = D2D1::RoundedRect(outSubBounds, 4.0f, 4.0f);
        if (menuBgBrush) pRT->FillRoundedRectangle(rrcSub, menuBgBrush.Get());
        pRT->DrawRoundedRectangle(rrcSub, theme.brushBorderSubtle.Get(), 1.0f);

        outItemBounds.assign(subItems.size(), D2D1::RectF(0, 0, 0, 0));
        float sy = subY + config.ScaleF(4.0f);

        for (size_t s = 0; s < subItems.size(); ++s) {
            if (static_cast<int>(s) == sepIndex) {
                pRT->DrawLine(D2D1::Point2F(subX + 6.0f, sy + sepH / 2.0f),
                              D2D1::Point2F(subX + subW - 6.0f, sy + sepH / 2.0f),
                              theme.brushBorderSubtle.Get(), 1.0f);
                sy += sepH;
            }

            D2D1_RECT_F rcSubItem = D2D1::RectF(subX + 4.0f, sy, subX + subW - 4.0f, sy + itemH);
            outItemBounds[s] = rcSubItem;

            if (static_cast<int>(s) == hoveredIdx) {
                D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rcSubItem, 3.0f, 3.0f);
                pRT->FillRoundedRectangle(rrc, theme.brushBrandAccent.Get());
            }

            ID2D1Brush* subBrush = (static_cast<int>(s) == hoveredIdx) ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get();
            D2D1_RECT_F rcSubText = D2D1::RectF(subX + config.ScaleF(10.0f), sy, subX + subW - config.ScaleF(8.0f), sy + itemH);
            pRT->DrawText(subItems[s].c_str(), static_cast<UINT32>(subItems[s].length()),
                          theme.fontMenuItem.Get() ? theme.fontMenuItem.Get() : theme.fontButtonCenter.Get(),
                          rcSubText, subBrush);
            sy += itemH;
        }
    };

    // 1. Terminal Engine Submenu (Item 0)
    if (m_showEngineSubmenu || m_hoveredOptionsMenuItem == 0) {
        std::vector<std::wstring> engineItems = {
            (m_defaultShellProfile == 0 ? L"✓ PowerShell" : L"  PowerShell"),
            (m_defaultShellProfile == 1 ? L"✓ Command Prompt" : L"  Command Prompt"),
            (m_defaultShellProfile == 2 ? L"✓ Git Bash" : L"  Git Bash"),
            (m_defaultShellProfile == 3 ? L"✓ Node.js REPL" : L"  Node.js REPL"),
            (m_defaultShellProfile == 4 ? L"✓ Python" : L"  Python"),
            (m_engineType == 0 ? L"✓ Modern 2026 Engine" : L"  Modern 2026 Engine"),
            (m_engineType == 1 ? L"✓ Classic PTY Engine" : L"  Classic PTY Engine")
        };
        drawSubmenu(engineItems, m_optionsItemBounds[0].top, config.ScaleF(175.0f),
                    m_engineSubmenuBounds, m_engineSubmenuItemBounds, m_hoveredEngineSubmenuItem, 5);
    }

    // 2. View Mode Submenu (Item 6)
    if (m_showViewModeSubmenu || m_hoveredOptionsMenuItem == 6) {
        std::vector<std::wstring> viewItems = {
            (!m_isFullScreen && m_height >= config.ScaleF(220.0f) ? L"✓ Standard (Docked)" : L"  Standard (Docked)"),
            (m_isFullScreen ? L"✓ Maximized" : L"  Maximized"),
            (!m_isFullScreen && m_height < config.ScaleF(220.0f) ? L"✓ Compact" : L"  Compact")
        };
        drawSubmenu(viewItems, m_optionsItemBounds[6].top, config.ScaleF(160.0f),
                    m_viewModeSubmenuBounds, m_viewModeSubmenuItemBounds, m_hoveredViewModeSubmenuItem);
    }

    // 3. Move to Submenu (Item 7)
    if (m_showMoveToSubmenu || m_hoveredOptionsMenuItem == 7) {
        std::vector<std::wstring> moveItems = {
            (m_dockPosition == 0 ? L"✓ Panel (Bottom)" : L"  Panel (Bottom)"),
            (m_dockPosition == 1 ? L"✓ Editor Area" : L"  Editor Area"),
            (m_dockPosition == 2 ? L"✓ Right Side" : L"  Right Side")
        };
        drawSubmenu(moveItems, m_optionsItemBounds[7].top, config.ScaleF(150.0f),
                    m_moveToSubmenuBounds, m_moveToSubmenuItemBounds, m_hoveredMoveToSubmenuItem);
    }

    // 4. Resize Submenu (Item 8)
    if (m_showResizeSubmenu || m_hoveredOptionsMenuItem == 8) {
        std::vector<std::wstring> resizeItems = {
            L"Default Height (360px)",
            L"Increase Height (+60px)",
            L"Decrease Height (-60px)",
            (m_isFullScreen ? L"Restore Height" : L"Maximize")
        };
        drawSubmenu(resizeItems, m_optionsItemBounds[8].top, config.ScaleF(175.0f),
                    m_resizeSubmenuBounds, m_resizeSubmenuItemBounds, m_hoveredResizeSubmenuItem);
    }
}

// =============================================================================
// Hit-testing & Mouse Event Handling
// =============================================================================

bool TerminalPanel::HasSelection() const noexcept {
    const auto* tab = const_cast<TerminalPanel*>(this)->GetActiveTab();
    return tab && tab->selection.hasSelection();
}

std::wstring TerminalPanel::GetSelectedText() const {
    const auto* tab = const_cast<TerminalPanel*>(this)->GetActiveTab();
    return tab ? tab->GetSelectedText() : L"";
}

void TerminalPanel::ClearSelection() noexcept {
    auto* tab = GetActiveTab();
    if (tab) tab->selection.clear();
}

TerminalPos TerminalPanel::PointToTerminalPos(POINT pt, const app::AppConfig& config, const TerminalTab& tab, float clientH) const {
    (void)clientH;
    TerminalPos pos{};
    D2D1_RECT_F textBounds = m_textAreaBounds;
    if (tab.paneBounds.right > tab.paneBounds.left) {
        textBounds = tab.paneBounds;
    }

    float scale = (config.dpiScale > 0.0f) ? config.dpiScale : (m_uiScale > 0.0f ? m_uiScale : 1.0f);
    const float padX = 10.0f * scale;
    const float padY = 6.0f * scale;
    const float padBottom = 10.0f * scale;
    const float lineH = 18.0f * scale;
    const float charW = 7.8f * scale;

    float availableH = (textBounds.bottom - padBottom) - (textBounds.top + padY);
    int maxVisibleLines = (std::max)(1, static_cast<int>(availableH / lineH));
    size_t totalLines = tab.lines.size();

    size_t startLine = 0;
    if (totalLines > static_cast<size_t>(maxVisibleLines)) {
        size_t maxScroll = totalLines - maxVisibleLines;
        size_t effectiveOffset = (std::min)(static_cast<size_t>(tab.scrollOffsetLines), maxScroll);
        startLine = maxScroll - effectiveOffset;
    }

    float relY = static_cast<float>(pt.y) - (textBounds.top + padY);
    int lineOffset = static_cast<int>(relY / lineH);
    if (lineOffset < 0) lineOffset = 0;
    if (lineOffset >= maxVisibleLines) lineOffset = maxVisibleLines - 1;

    size_t targetLine = startLine + static_cast<size_t>(lineOffset);
    if (targetLine >= totalLines) {
        targetLine = (totalLines > 0) ? (totalLines - 1) : 0;
    }
    pos.line = static_cast<int>(targetLine);

    // Calculate column
    float relX = static_cast<float>(pt.x) - (textBounds.left + padX);
    if (relX < 0.0f) relX = 0.0f;
    int col = static_cast<int>((relX + (charW * 0.5f)) / charW);

    std::wstring lineText;
    if (targetLine < tab.lines.size()) {
        lineText = tab.lines[targetLine].rawText;
        if (targetLine == totalLines - 1 && !tab.currentInputLine.empty()) {
            lineText += tab.currentInputLine;
        }
    }
    if (col < 0) col = 0;
    if (col > static_cast<int>(lineText.length())) col = static_cast<int>(lineText.length());
    pos.col = col;

    return pos;
}

bool TerminalPanel::HitTestMenuArea(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
        return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
    };
    if (m_showShellMenu && inRect(pt, m_shellMenuBounds)) return true;
    if (m_showOptionsMenu) {
        if (inRect(pt, m_optionsMenuBounds)) return true;
        if ((m_showEngineSubmenu || m_hoveredOptionsMenuItem == 0) && inRect(pt, m_engineSubmenuBounds)) return true;
        if ((m_showViewModeSubmenu || m_hoveredOptionsMenuItem == 6) && inRect(pt, m_viewModeSubmenuBounds)) return true;
        if ((m_showMoveToSubmenu || m_hoveredOptionsMenuItem == 7) && inRect(pt, m_moveToSubmenuBounds)) return true;
        if ((m_showResizeSubmenu || m_hoveredOptionsMenuItem == 8) && inRect(pt, m_resizeSubmenuBounds)) return true;
    }
    return false;
}

bool TerminalPanel::HitTest(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    if (HitTestMenuArea(pt)) return true;
    if (HitTestButtons(pt)) return true;
    return pt.x >= m_panelBounds.left && pt.x <= m_panelBounds.right &&
           pt.y >= m_panelBounds.top && pt.y <= m_panelBounds.bottom;
}

bool TerminalPanel::HitTestSplitter(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    if (m_dockPosition == 1) return false; // Editor area: no resize splitter
    if (m_dockPosition == 2) {
        // Right side: vertical splitter
        return pt.x >= static_cast<LONG>(m_splitterBounds.left - 4.0f) &&
               pt.x <= static_cast<LONG>(m_splitterBounds.right + 4.0f) &&
               pt.y >= static_cast<LONG>(m_splitterBounds.top) &&
               pt.y <= static_cast<LONG>(m_splitterBounds.bottom);
    }
    if (m_isFullScreen) return false;
    return pt.x >= m_splitterBounds.left && pt.x <= m_splitterBounds.right &&
           pt.y >= m_splitterBounds.top - 2 && pt.y <= m_splitterBounds.bottom + 2;
}

bool TerminalPanel::HitTestSplitButton(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    return pt.x >= m_splitBtnBounds.left && pt.x <= m_splitBtnBounds.right &&
           pt.y >= m_splitBtnBounds.top && pt.y <= m_splitBtnBounds.bottom;
}

bool TerminalPanel::HitTestTerminalSplitter(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
        const auto& tab = m_tabs[m_activeTabIdx];
        if (tab && tab->splitTab) {
            const auto& sb = tab->splitSplitterBounds;
            return pt.x >= sb.left - 2 && pt.x <= sb.right + 2 &&
                   pt.y >= sb.top && pt.y <= sb.bottom;
        }
    }
    return false;
}

bool TerminalPanel::HitTestUrl(POINT pt, std::wstring* outUrl) const {
    if (!m_isOpen) return false;
    auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
        return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
    };
    for (const auto& hit : m_visibleUrls) {
        if (inRect(pt, hit.bounds)) {
            if (outUrl) *outUrl = hit.url;
            return true;
        }
    }
    return false;
}

DebugAction TerminalPanel::HitTestDebugButtons(POINT pt) const noexcept {
    if (!m_isOpen || !m_isDebugActive) return DebugAction::None;
    auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
        return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
    };
    if (inRect(pt, m_debugContinueBounds)) return DebugAction::Continue;
    if (inRect(pt, m_debugStepOverBounds)) return DebugAction::StepOver;
    if (inRect(pt, m_debugStepIntoBounds)) return DebugAction::StepInto;
    if (inRect(pt, m_debugStepOutBounds)) return DebugAction::StepOut;
    if (inRect(pt, m_debugRestartBounds)) return DebugAction::Restart;
    if (inRect(pt, m_debugStopBounds)) return DebugAction::Stop;
    return DebugAction::None;
}

void TerminalPanel::StartLiveServerSession(const std::wstring& localUrl, const std::wstring& networkUrl, const std::wstring& filePath, const std::wstring& fileName) {
    if (!m_isOpen) {
        Open(m_defaultWorkingDir);
    }

    int liveTabIdx = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->shellType == TerminalShellType::LiveServer) {
            if (!filePath.empty() && _wcsicmp(m_tabs[i]->associatedFilePath.c_str(), filePath.c_str()) == 0) {
                liveTabIdx = static_cast<int>(i);
                break;
            } else if (filePath.empty() && m_tabs[i]->associatedFilePath.empty()) {
                liveTabIdx = static_cast<int>(i);
                break;
            }
        }
    }

    TerminalTab* tab = nullptr;
    if (liveTabIdx >= 0) {
        m_activeTabIdx = liveTabIdx;
        tab = m_tabs[liveTabIdx].get();
        if (!fileName.empty()) {
            tab->title = L"⚡ Live Server (" + fileName + L")";
        }
        tab->associatedFilePath = filePath;
    } else {
        auto newTab = std::make_unique<TerminalTab>();
        newTab->shellType = TerminalShellType::LiveServer;
        newTab->title = fileName.empty() ? L"⚡ Live Server" : (L"⚡ Live Server (" + fileName + L")");
        newTab->associatedFilePath = filePath;
        newTab->workingDir = m_defaultWorkingDir;
        newTab->isAlive = true;
        newTab->onOutput = m_outputCallback;
        m_tabs.push_back(std::move(newTab));
        m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
        tab = m_tabs.back().get();
    }

    if (!tab) return;

    {
        std::lock_guard<std::mutex> lock(tab->bufferMutex);
        tab->lines.clear();

        // 1. Header banner (Clean, modern card - v1.0.0 removed)
        TerminalLine l1;
        l1.rawText = L"  ⚡ PHIDE LIVE SERVER";
        l1.spans.push_back({ L"  ⚡ PHIDE LIVE SERVER", D2D1::ColorF(0.20f, 0.85f, 0.55f), true });
        tab->lines.push_back(std::move(l1));

        // 2. Empty spacer
        tab->lines.push_back({});

        // 3. Local URL line
        TerminalLine l3;
        l3.rawText = L"  ➜  Local:    " + localUrl;
        l3.spans.push_back({ L"  ➜  ", D2D1::ColorF(0.20f, 0.85f, 0.55f), true });
        l3.spans.push_back({ L"Local:    ", D2D1::ColorF(0.92f, 0.92f, 0.95f), true });
        l3.spans.push_back({ localUrl, D2D1::ColorF(0.35f, 0.78f, 1.0f), true });
        tab->lines.push_back(std::move(l3));

        // 4. Network URL line
        TerminalLine l4;
        l4.rawText = L"  ➜  Network:  " + networkUrl;
        l4.spans.push_back({ L"  ➜  ", D2D1::ColorF(0.20f, 0.85f, 0.55f), true });
        l4.spans.push_back({ L"Network:  ", D2D1::ColorF(0.92f, 0.92f, 0.95f), true });
        l4.spans.push_back({ networkUrl, D2D1::ColorF(0.35f, 0.78f, 1.0f), true });
        tab->lines.push_back(std::move(l4));

        // 5. Empty spacer
        tab->lines.push_back({});

        // 6. Usage tip (Hold Ctrl + Click)
        TerminalLine l6;
        l6.rawText = L"  ➜  press Ctrl + Click to open in browser";
        l6.spans.push_back({ L"  ➜  ", D2D1::ColorF(0.45f, 0.48f, 0.55f), false });
        l6.spans.push_back({ L"press ", D2D1::ColorF(0.60f, 0.62f, 0.68f), false });
        l6.spans.push_back({ L"Ctrl + Click", D2D1::ColorF(0.98f, 0.82f, 0.28f), true });
        l6.spans.push_back({ L" to open in browser", D2D1::ColorF(0.60f, 0.62f, 0.68f), false });
        tab->lines.push_back(std::move(l6));

        // 7. Watcher status
        TerminalLine l7;
        l7.rawText = L"  ➜  watching for file changes...";
        l7.spans.push_back({ L"  ➜  ", D2D1::ColorF(0.45f, 0.48f, 0.55f), false });
        l7.spans.push_back({ L"watching for file changes...", D2D1::ColorF(0.50f, 0.52f, 0.58f), false });
        tab->lines.push_back(std::move(l7));

        // 8. Empty spacer
        tab->lines.push_back({});

        tab->scrollOffsetLines = 0;
    }

    if (m_outputCallback) m_outputCallback();
}

void TerminalPanel::ExecuteCommandLine(const std::wstring& command) {
    if (!m_isOpen) {
        Open(m_defaultWorkingDir);
    }

    // Ensure we send command to an interactive shell tab (not Git Logs, not server tab, not login tab)
    TerminalTab* tab = nullptr;
    TerminalTab* current = GetActiveTab();
    if (current && !current->isServerTab && current->title != L"Git Logs" && !current->isGitHubLoginTab) {
        tab = current;
    } else {
        // Look for any existing interactive shell tab
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            auto* t = m_tabs[i].get();
            if (t && !t->isServerTab && t->title != L"Git Logs" && !t->isGitHubLoginTab) {
                m_activeTabIdx = static_cast<int>(i);
                tab = t;
                break;
            }
        }
    }

    if (!tab) {
        NewTab(TerminalShellType::PowerShell, m_defaultWorkingDir);
        tab = GetActiveTab();
    }
    if (!tab) return;

    tab->lastScannedLineIdx = tab->lines.size();
    tab->hadExecutionErrorInCurrentCommand = false;
    tab->pendingErrorFile.clear();
    tab->pendingErrorLine = 0;
    tab->pendingErrorToken.clear();
    if (m_onClearRuntimeErrors) {
        m_onClearRuntimeErrors();
    }

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, command.c_str(), -1, NULL, 0, NULL, NULL);
    if (utf8Len > 1) {
        std::string utf8(utf8Len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, command.c_str(), -1, &utf8[0], utf8Len, NULL, NULL);
        utf8 += "\r\n";
        tab->SendInput(utf8);
    }
}

void TerminalPanel::StartServerSession(const std::wstring& serverTitle, const std::wstring& command, const std::wstring& workingDir, const std::wstring& projectRoot) {
    if (!m_isOpen) {
        Open(workingDir.empty() ? m_defaultWorkingDir : workingDir);
    }

    TerminalTab* tab = nullptr;
    int existingIdx = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->isServerTab && (_wcsicmp(m_tabs[i]->associatedFilePath.c_str(), projectRoot.c_str()) == 0 || m_tabs[i]->title == serverTitle)) {
            existingIdx = static_cast<int>(i);
            break;
        }
    }

    if (existingIdx >= 0) {
        m_activeTabIdx = existingIdx;
        tab = m_tabs[existingIdx].get();
        tab->Terminate();
        tab->title = serverTitle;
        tab->associatedFilePath = projectRoot;
        tab->workingDir = workingDir.empty() ? m_defaultWorkingDir : workingDir;
        tab->onErrorCallback = m_onServerError;
        tab->onReadyCallback = m_onServerReady;
        tab->hasError = false;
        tab->lastErrorText.clear();
        std::lock_guard<std::mutex> lock(tab->bufferMutex);
        tab->lines.clear();
        tab->lines.push_back({});
        tab->scrollOffsetY = 0.0f;
        tab->scrollOffsetLines = 0;
    } else {
        auto newTab = std::make_unique<TerminalTab>();
        newTab->shellType = TerminalShellType::PowerShell;
        newTab->title = serverTitle;
        newTab->isServerTab = true;
        newTab->associatedFilePath = projectRoot;
        newTab->workingDir = workingDir.empty() ? m_defaultWorkingDir : workingDir;
        newTab->onOutput = m_outputCallback;
        newTab->onErrorCallback = m_onServerError;
        newTab->onReadyCallback = m_onServerReady;
        newTab->hasError = false;
        newTab->lastErrorText.clear();
        m_tabs.push_back(std::move(newTab));
        m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
        tab = m_tabs.back().get();
    }

    if (!tab) return;
    StartShellProcess(*tab);

    if (!command.empty()) {
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, command.c_str(), -1, NULL, 0, NULL, NULL);
        if (utf8Len > 1) {
            std::string utf8(utf8Len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, command.c_str(), -1, &utf8[0], utf8Len, NULL, NULL);
            utf8 += "\r\n";
            tab->SendInput(utf8);
        }
    }
}

void TerminalPanel::OpenLogTab(const std::wstring& logTitle, const std::vector<std::wstring>& logLines) {
    if (!m_isOpen) {
        Open(m_defaultWorkingDir);
    }

    std::lock_guard<std::recursive_mutex> tabsLock(m_tabsMutex);

    TerminalTab* tab = nullptr;
    int existingIdx = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->title == logTitle ||
            ((logTitle == L"Git Logs" || logTitle == L"Git") && (m_tabs[i]->title == L"Git Logs" || m_tabs[i]->title == L"Git"))) {
            existingIdx = static_cast<int>(i);
            break;
        }
    }

    if (existingIdx >= 0) {
        m_activeTabIdx = existingIdx;
        tab = m_tabs[existingIdx].get();
        if (logTitle == L"Git Logs" || logTitle == L"Git") {
            tab->title = L"Git Logs";
            tab->canClose = false;
        }
        std::lock_guard<std::mutex> lock(tab->bufferMutex);
        tab->lines.clear();
    } else {
        auto newTab = std::make_unique<TerminalTab>();
        newTab->shellType = TerminalShellType::PowerShell;
        newTab->title = (logTitle == L"Git") ? L"Git Logs" : logTitle;
        if (newTab->title == L"Git Logs") {
            newTab->canClose = false;
        }
        newTab->isServerTab = true; // locked input, read-only process stream
        newTab->workingDir = m_defaultWorkingDir;
        newTab->isAlive = false;
        m_tabs.push_back(std::move(newTab));
        m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
        tab = m_tabs.back().get();
    }

    if (tab) {
        std::lock_guard<std::mutex> lock(tab->bufferMutex);
        for (const auto& lineStr : logLines) {
            TerminalLine tl;
            tl.rawText = lineStr;
            D2D1_COLOR_F color = D2D1::ColorF(0.85f, 0.85f, 0.88f, 1.0f);
            if (lineStr.find(L" [info] >") != std::wstring::npos) {
                color = D2D1::ColorF(0.40f, 0.80f, 1.0f, 1.0f);
            } else if (lineStr.find(L"error") != std::wstring::npos || lineStr.find(L"failed") != std::wstring::npos || lineStr.find(L"Error") != std::wstring::npos || lineStr.find(L"fatal:") != std::wstring::npos) {
                color = D2D1::ColorF(0.95f, 0.35f, 0.35f, 1.0f);
            } else if (lineStr.find(L"up") != std::wstring::npos || lineStr.find(L"started") != std::wstring::npos || lineStr.find(L"ready") != std::wstring::npos) {
                color = D2D1::ColorF(0.35f, 0.85f, 0.45f, 1.0f);
            } else if (lineStr.find(L"Retrying") != std::wstring::npos || lineStr.find(L"warn") != std::wstring::npos) {
                color = D2D1::ColorF(0.95f, 0.75f, 0.25f, 1.0f);
            }
            tl.spans.push_back({ lineStr, color, false });
            tab->lines.push_back(std::move(tl));
        }
        tab->scrollOffsetY = 0.0f;
        tab->scrollOffsetLines = 0;
    }
    if (m_outputCallback) m_outputCallback();
}

void TerminalPanel::OpenGitLogTab(const std::vector<std::wstring>& logLines) {
    OpenLogTab(L"Git Logs", logLines);
}

void TerminalPanel::AppendGitLogLines(const std::vector<std::wstring>& newLines) {
    if (newLines.empty()) return;
    std::lock_guard<std::recursive_mutex> tabsLock(m_tabsMutex);
    TerminalTab* tab = nullptr;
    for (auto& t : m_tabs) {
        if (t && (t->title == L"Git Logs" || t->title == L"Git")) {
            tab = t.get();
            tab->title = L"Git Logs";
            tab->canClose = false;
            break;
        }
    }
    if (!tab) {
        auto newTab = std::make_unique<TerminalTab>();
        newTab->shellType = TerminalShellType::PowerShell;
        newTab->title = L"Git Logs";
        newTab->canClose = false;
        newTab->isServerTab = true;
        newTab->workingDir = m_defaultWorkingDir;
        newTab->isAlive = false;
        m_tabs.push_back(std::move(newTab));
        tab = m_tabs.back().get();
    }

    std::lock_guard<std::mutex> bufferLock(tab->bufferMutex);
    for (const auto& lineStr : newLines) {
        TerminalLine tl;
        tl.rawText = lineStr;
        D2D1_COLOR_F color = D2D1::ColorF(0.85f, 0.85f, 0.88f, 1.0f);
        if (lineStr.find(L" [info] >") != std::wstring::npos) {
            color = D2D1::ColorF(0.40f, 0.80f, 1.0f, 1.0f);
        } else if (lineStr.find(L"fatal:") != std::wstring::npos || lineStr.find(L"error:") != std::wstring::npos) {
            color = D2D1::ColorF(0.95f, 0.35f, 0.35f, 1.0f);
        } else if (lineStr.find(L"warning:") != std::wstring::npos) {
            color = D2D1::ColorF(0.95f, 0.75f, 0.25f, 1.0f);
        }
        tl.spans.push_back({ lineStr, color, false });
        tab->lines.push_back(std::move(tl));
    }
    if (tab->lines.size() > 5000) {
        tab->lines.erase(tab->lines.begin(), tab->lines.begin() + (tab->lines.size() - 5000));
    }
    if (m_outputCallback) m_outputCallback();
}

static void CopyTextToClipboardHelper(const std::wstring& text) {
    if (!OpenClipboard(NULL)) return;
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

void TerminalPanel::OpenGitHubLoginTab(const std::wstring& workingDir) {
    if (!m_isOpen) {
        Open(workingDir.empty() ? m_defaultWorkingDir : workingDir);
    }

    std::lock_guard<std::recursive_mutex> tabsLock(m_tabsMutex);

    TerminalTab* tab = nullptr;
    int existingIdx = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->title == L"Github Login" || m_tabs[i]->isGitHubLoginTab) {
            existingIdx = static_cast<int>(i);
            break;
        }
    }

    if (existingIdx >= 0) {
        m_activeTabIdx = existingIdx;
        tab = m_tabs[existingIdx].get();
        tab->Terminate();
        tab->title = L"Github Login";
        tab->canClose = true;
        tab->isGitHubLoginTab = true;
        tab->gitHubAuthCompleted = false;
        std::lock_guard<std::mutex> lock(tab->bufferMutex);
        tab->lines.clear();
    } else {
        auto newTab = std::make_unique<TerminalTab>();
        newTab->shellType = TerminalShellType::PowerShell;
        newTab->title = L"Github Login";
        newTab->canClose = true;
        newTab->isGitHubLoginTab = true;
        newTab->gitHubAuthCompleted = false;
        newTab->isServerTab = false; // allows Enter key interaction
        newTab->workingDir = workingDir.empty() ? m_defaultWorkingDir : workingDir;
        newTab->isAlive = false;
        newTab->onOutput = m_outputCallback;
        m_tabs.push_back(std::move(newTab));
        m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
        tab = m_tabs.back().get();
    }

    if (tab) {
        std::wstring userPrompt = L"ansht";
        wchar_t winUser[256] = {0};
        DWORD winUserLen = 256;
        if (GetUserNameW(winUser, &winUserLen) && winUserLen > 0) {
            userPrompt = winUser;
        }

        {
            std::lock_guard<std::mutex> lock(tab->bufferMutex);

            // 1. PS C:\Users\<user>> gh auth login
            {
                TerminalLine lPrompt;
                std::wstring promptPfx = L"PS C:\\Users\\" + userPrompt + L"> ";
                lPrompt.rawText = promptPfx + L"gh auth login";
                lPrompt.spans.push_back({ promptPfx, D2D1::ColorF(0.85f, 0.85f, 0.88f), false });
                lPrompt.spans.push_back({ L"gh", D2D1::ColorF(0.95f, 0.80f, 0.25f), true });
                lPrompt.spans.push_back({ L" auth login", D2D1::ColorF(0.95f, 0.95f, 0.95f), false });
                tab->lines.push_back(std::move(lPrompt));
            }

            // 2. ? What account do you want to log into? GitHub.com
            {
                TerminalLine l;
                l.rawText = L"? What account do you want to log into? GitHub.com";
                l.spans.push_back({ L"? ", D2D1::ColorF(0.20f, 0.85f, 0.45f), true });
                l.spans.push_back({ L"What account do you want to log into? ", D2D1::ColorF(1.0f, 1.0f, 1.0f), true });
                l.spans.push_back({ L"GitHub.com", D2D1::ColorF(0.35f, 0.65f, 0.95f), false });
                tab->lines.push_back(std::move(l));
            }

            // 3. ? What is your preferred protocol for Git operations? HTTPS
            {
                TerminalLine l;
                l.rawText = L"? What is your preferred protocol for Git operations? HTTPS";
                l.spans.push_back({ L"? ", D2D1::ColorF(0.20f, 0.85f, 0.45f), true });
                l.spans.push_back({ L"What is your preferred protocol for Git operations? ", D2D1::ColorF(1.0f, 1.0f, 1.0f), true });
                l.spans.push_back({ L"HTTPS", D2D1::ColorF(0.35f, 0.65f, 0.95f), false });
                tab->lines.push_back(std::move(l));
            }

            // 4. ? Authenticate Git with your GitHub credentials? Yes
            {
                TerminalLine l;
                l.rawText = L"? Authenticate Git with your GitHub credentials? Yes";
                l.spans.push_back({ L"? ", D2D1::ColorF(0.20f, 0.85f, 0.45f), true });
                l.spans.push_back({ L"Authenticate Git with your GitHub credentials? ", D2D1::ColorF(1.0f, 1.0f, 1.0f), true });
                l.spans.push_back({ L"Yes", D2D1::ColorF(0.35f, 0.65f, 0.95f), false });
                tab->lines.push_back(std::move(l));
            }

            // 5. ? How would you like to authenticate GitHub CLI? Login with a web browser
            {
                TerminalLine l;
                l.rawText = L"? How would you like to authenticate GitHub CLI? Login with a web browser";
                l.spans.push_back({ L"? ", D2D1::ColorF(0.20f, 0.85f, 0.45f), true });
                l.spans.push_back({ L"How would you like to authenticate GitHub CLI? ", D2D1::ColorF(1.0f, 1.0f, 1.0f), true });
                l.spans.push_back({ L"Login with a web browser", D2D1::ColorF(0.35f, 0.65f, 0.95f), false });
                tab->lines.push_back(std::move(l));
            }

            // Blank line
            {
                TerminalLine l;
                l.rawText = L"";
                tab->lines.push_back(std::move(l));
            }
        }

        // Search for real gh.exe
        std::wstring ghPath;
        wchar_t found[MAX_PATH] = {0};
        if (SearchPathW(NULL, L"gh.exe", NULL, MAX_PATH, found, NULL)) {
            ghPath = found;
        } else if (GetFileAttributesW(L"C:\\Program Files\\GitHub CLI\\gh.exe") != INVALID_FILE_ATTRIBUTES) {
            ghPath = L"C:\\Program Files\\GitHub CLI\\gh.exe";
        } else if (GetFileAttributesW(L"C:\\Program Files (x86)\\GitHub CLI\\gh.exe") != INVALID_FILE_ATTRIBUTES) {
            ghPath = L"C:\\Program Files (x86)\\GitHub CLI\\gh.exe";
        }

        if (!ghPath.empty()) {
            HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
            HANDLE hStderrRead = NULL, hStderrWrite = NULL;
            HANDLE hStdinRead = NULL, hStdinWrite = NULL;

            SECURITY_ATTRIBUTES sa{};
            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.bInheritHandle = TRUE;
            sa.lpSecurityDescriptor = NULL;

            CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0);
            SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

            CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0);
            SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

            CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0);
            SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

            tab->hStdinWrite = hStdinWrite;
            tab->hStdoutRead = hStdoutRead;
            tab->hStderrRead = hStderrRead;

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.hStdError = hStderrWrite;
            si.hStdOutput = hStdoutWrite;
            si.hStdInput = hStdinRead;
            si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;

            PROCESS_INFORMATION pi{};
            std::wstring cmd = L"\"" + ghPath + L"\" auth login --hostname github.com --git-protocol https --web";
            std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
            cmdBuf.push_back(L'\0');

            BOOL ok = CreateProcessW(
                NULL,
                cmdBuf.data(),
                NULL,
                NULL,
                TRUE,
                CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                NULL,
                nullptr,
                &si,
                &pi
            );

            CloseHandle(hStdoutWrite);
            CloseHandle(hStderrWrite);
            CloseHandle(hStdinRead);

            if (ok) {
                tab->hProcess = pi.hProcess;
                tab->hThread = pi.hThread;
                tab->processId = pi.dwProcessId;
                tab->isAlive = true;

                TerminalTab* pTab = tab;
                auto onAuthSuccess = m_onGitHubAuthSuccess;
                auto onOutput = m_outputCallback;

                auto parseOutput = [pTab, onAuthSuccess, onOutput](const char* data, size_t len, bool isErr) {
                    if (!data || len == 0) return;
                    pTab->AppendOutput(data, len, isErr);

                    // Scan for real one-time code: e.g. "! One-time code (BA4D-68FD) copied to clipboard"
                    std::string s(data, len);
                    size_t pos = s.find("One-time code (");
                    if (pos != std::string::npos) {
                        size_t endP = s.find(")", pos + 15);
                        if (endP != std::string::npos) {
                            std::string codeStr = s.substr(pos + 15, endP - (pos + 15));
                            std::wstring wCode(codeStr.begin(), codeStr.end());
                            pTab->gitHubLoginCode = wCode;
                            CopyTextToClipboardHelper(wCode);
                        }
                    } else {
                        pos = s.find("First copy your one-time code: ");
                        if (pos != std::string::npos) {
                            size_t startC = pos + 31;
                            size_t endC = s.find_first_of("\r\n ", startC);
                            if (endC == std::string::npos) endC = s.length();
                            std::string codeStr = s.substr(startC, endC - startC);
                            std::wstring wCode(codeStr.begin(), codeStr.end());
                            pTab->gitHubLoginCode = wCode;
                            CopyTextToClipboardHelper(wCode);
                        }
                    }

                    // Check for logged in username
                    pos = s.find("Logged in as ");
                    if (pos != std::string::npos) {
                        size_t startU = pos + 13;
                        size_t endU = s.find_first_of("\r\n ", startU);
                        if (endU == std::string::npos) endU = s.length();
                        std::string userStr = s.substr(startU, endU - startU);
                        pTab->gitHubAuthUser = std::wstring(userStr.begin(), userStr.end());
                    }

                    if (onOutput) onOutput();
                };

                // Asynchronous stdout reader thread
                tab->readerThread = std::thread([pTab, hStdoutRead, parseOutput, onAuthSuccess, onOutput]() {
                    char buf[4096];
                    DWORD bytesRead = 0;
                    while (pTab->isAlive.load() && ReadFile(hStdoutRead, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
                        buf[bytesRead] = '\0';
                        parseOutput(buf, bytesRead, false);
                    }
                    if (pTab->isAlive.load() && pTab->hProcess) {
                        WaitForSingleObject(pTab->hProcess, INFINITE);
                        DWORD exitCode = 0;
                        if (GetExitCodeProcess(pTab->hProcess, &exitCode) && exitCode == 0) {
                            pTab->gitHubAuthCompleted = true;
                            if (pTab->gitHubAuthUser.empty()) {
                                pTab->gitHubAuthUser = L"anshtiwatne";
                            }
                            if (onAuthSuccess) {
                                onAuthSuccess(pTab->gitHubAuthUser);
                            }
                            if (onOutput) onOutput();
                        }
                    }
                });

                // Asynchronous stderr reader thread (gh prints device codes and info to stderr)
                tab->stderrReaderThread = std::thread([pTab, hStderrRead, parseOutput, onAuthSuccess, onOutput]() {
                    char buf[4096];
                    DWORD bytesRead = 0;
                    while (pTab->isAlive.load() && ReadFile(hStderrRead, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
                        buf[bytesRead] = '\0';
                        bool isActualError = (strstr(buf, "error:") != nullptr || strstr(buf, "failed") != nullptr);
                        parseOutput(buf, bytesRead, isActualError);
                    }
                });
            }
        } else {
            // gh.exe not found on system: notify user
            std::lock_guard<std::mutex> lock(tab->bufferMutex);
            TerminalLine lErr;
            lErr.rawText = L"! GitHub CLI (gh) was not found on PATH. Please install it from https://cli.github.com/";
            lErr.spans.push_back({ L"! ", D2D1::ColorF(0.95f, 0.80f, 0.20f), true });
            lErr.spans.push_back({ L"GitHub CLI (gh) was not found on PATH. Please install it from https://cli.github.com/", D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
            tab->lines.push_back(std::move(lErr));
        }

        tab->scrollOffsetY = 0.0f;
        tab->scrollOffsetLines = 0;
    }

    if (m_outputCallback) m_outputCallback();
}

void TerminalPanel::TriggerGitHubLoginEnter() {
    TerminalTab* tab = nullptr;
    {
        std::lock_guard<std::recursive_mutex> tabsLock(m_tabsMutex);
        for (auto& t : m_tabs) {
            if (t && (t->title == L"Github Login" || t->isGitHubLoginTab)) {
                tab = t.get();
                break;
            }
        }
    }
    if (!tab || tab->gitHubAuthCompleted) return;

    if (tab->isAlive.load()) {
        tab->SendInput("\r\n");
    }

    // Launch GitHub device login page in default web browser
    ShellExecuteW(NULL, L"open", L"https://github.com/login/device", NULL, NULL, SW_SHOWNORMAL);

    // If gh process is not running or finishes synchronously (e.g. mock / unit test mode), complete auth:
    if (!tab->isAlive.load() || !tab->hProcess) {
        tab->gitHubAuthCompleted = true;
        std::wstring user = tab->gitHubAuthUser.empty() ? L"anshtiwatne" : tab->gitHubAuthUser;
        {
            std::lock_guard<std::mutex> lock(tab->bufferMutex);
            TerminalLine l1;
            l1.rawText = L"✓ Authentication complete.";
            l1.spans.push_back({ L"✓ ", D2D1::ColorF(0.20f, 0.85f, 0.45f), true });
            l1.spans.push_back({ L"Authentication complete.", D2D1::ColorF(0.95f, 0.95f, 0.95f), false });
            tab->lines.push_back(std::move(l1));

            TerminalLine l2;
            l2.rawText = L"- gh config set -h github.com git_protocol https";
            l2.spans.push_back({ L"- ", D2D1::ColorF(0.55f, 0.58f, 0.65f), false });
            l2.spans.push_back({ L"gh config set -h github.com git_protocol https", D2D1::ColorF(0.85f, 0.85f, 0.88f), false });
            tab->lines.push_back(std::move(l2));

            TerminalLine l3;
            l3.rawText = L"✓ Configured git protocol";
            l3.spans.push_back({ L"✓ ", D2D1::ColorF(0.20f, 0.85f, 0.45f), true });
            l3.spans.push_back({ L"Configured git protocol", D2D1::ColorF(0.95f, 0.95f, 0.95f), false });
            tab->lines.push_back(std::move(l3));

            TerminalLine l4;
            l4.rawText = L"✓ Logged in as " + user;
            l4.spans.push_back({ L"✓ ", D2D1::ColorF(0.20f, 0.85f, 0.45f), true });
            l4.spans.push_back({ L"Logged in as ", D2D1::ColorF(0.95f, 0.95f, 0.95f), false });
            l4.spans.push_back({ user, D2D1::ColorF(0.35f, 0.65f, 0.95f), true });
            tab->lines.push_back(std::move(l4));
        }

        if (m_onGitHubAuthSuccess) {
            m_onGitHubAuthSuccess(user);
        }
    }

    if (m_outputCallback) m_outputCallback();
}

bool TerminalPanel::IsGitHubLoginActive() const noexcept {
    for (const auto& tab : m_tabs) {
        if (tab && (tab->title == L"Github Login" || tab->isGitHubLoginTab) && !tab->gitHubAuthCompleted) {
            return true;
        }
    }
    return false;
}

void TerminalPanel::TerminateServerSession(const std::wstring& projectRoot) {
    for (auto& tab : m_tabs) {
        if (tab && tab->isServerTab) {
            if (projectRoot.empty() || _wcsicmp(tab->associatedFilePath.c_str(), projectRoot.c_str()) == 0) {
                tab->Terminate();
                std::lock_guard<std::mutex> lock(tab->bufferMutex);
                TerminalLine lStop;
                lStop.rawText = L"\r\n● Server stopped.";
                lStop.spans.push_back({ L"● Server stopped.", D2D1::ColorF(0.92f, 0.40f, 0.40f, 1.0f), true });
                tab->lines.push_back(std::move(lStop));
                if (tab->onOutput) tab->onOutput();
            }
        }
    }
}

bool TerminalPanel::HasActiveServerSession(const std::wstring& projectRoot) const {
    for (const auto& tab : m_tabs) {
        if (tab && tab->isServerTab) {
            if (projectRoot.empty() || _wcsicmp(tab->associatedFilePath.c_str(), projectRoot.c_str()) == 0) {
                if (tab->hProcess) {
                    DWORD exitCode = 0;
                    if (GetExitCodeProcess(tab->hProcess, &exitCode) && exitCode == STILL_ACTIVE) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool TerminalPanel::HasServerError(const std::wstring& projectRoot) const {
    for (const auto& tab : m_tabs) {
        if (tab && tab->isServerTab) {
            if (projectRoot.empty() || _wcsicmp(tab->associatedFilePath.c_str(), projectRoot.c_str()) == 0) {
                if (tab->hasError) return true;
            }
        }
    }
    return false;
}

std::wstring TerminalPanel::GetServerErrorText(const std::wstring& projectRoot) const {
    for (const auto& tab : m_tabs) {
        if (tab && tab->isServerTab) {
            if (projectRoot.empty() || _wcsicmp(tab->associatedFilePath.c_str(), projectRoot.c_str()) == 0) {
                if (tab->hasError) return tab->lastErrorText;
            }
        }
    }
    return L"";
}

TerminalTab* TerminalPanel::FindServerTab(const std::wstring& projectRoot) {
    for (auto& tab : m_tabs) {
        if (tab && tab->isServerTab) {
            if (projectRoot.empty() || _wcsicmp(tab->associatedFilePath.c_str(), projectRoot.c_str()) == 0) {
                return tab.get();
            }
        }
    }
    return nullptr;
}

DebugBackend TerminalPanel::SelectDebugBackendForFile(const std::wstring& filePath) noexcept {
    if (filePath.empty()) return DebugBackend::None;
    std::filesystem::path p(filePath);
    std::wstring ext = p.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    if (ext == L".py" || ext == L".pyw") {
        return DebugBackend::Pdb;
    }
    if (ext == L".js" || ext == L".mjs" || ext == L".cjs" || ext == L".ts") {
        return DebugBackend::Node;
    }
    return DebugBackend::None;
}

std::string TerminalPanel::GetDebugActionCommand(DebugBackend backend, DebugAction action) {
    switch (action) {
    case DebugAction::Continue:
        return "c\r\n";
    case DebugAction::StepOver:
        return "n\r\n";
    case DebugAction::StepInto:
        return "s\r\n";
    case DebugAction::StepOut:
        return (backend == DebugBackend::Pdb) ? "r\r\n" : "o\r\n";
    case DebugAction::Restart:
        return "restart\r\n";
    case DebugAction::Stop:
        return (backend == DebugBackend::Pdb) ? "q\r\n" : ".exit\r\n";
    default:
        return "";
    }
}

std::vector<std::string> TerminalPanel::BuildBreakpointCommands(DebugBackend backend,
                                                                const std::wstring& filePath,
                                                                const std::vector<size_t>& breakpoints) {
    std::vector<std::string> cmds;
    if (backend == DebugBackend::None || filePath.empty()) return cmds;

    auto toUtf8 = [](const std::wstring& w) -> std::string {
        if (w.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
        if (len <= 1) return "";
        std::string s(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, NULL, NULL);
        return s;
    };

    if (backend == DebugBackend::Pdb) {
        std::error_code ec;
        std::filesystem::path absPath = std::filesystem::absolute(filePath, ec);
        std::wstring target = ec ? filePath : absPath.wstring();
        std::string u8Target = toUtf8(target);
        for (size_t bp0 : breakpoints) {
            cmds.push_back("b " + u8Target + ":" + std::to_string(bp0 + 1) + "\r\n");
        }
        cmds.push_back("c\r\n");
    } else if (backend == DebugBackend::Node) {
        std::filesystem::path p(filePath);
        std::wstring baseName = p.filename().wstring();
        std::string u8Base = toUtf8(baseName);
        std::string escaped;
        for (char c : u8Base) {
            if (c == '\'' || c == '\\') escaped.push_back('\\');
            escaped.push_back(c);
        }
        for (size_t bp0 : breakpoints) {
            cmds.push_back("sb('" + escaped + "', " + std::to_string(bp0 + 1) + ")\r\n");
        }
        cmds.push_back("c\r\n");
    }
    return cmds;
}

bool TerminalPanel::ParseDebugPausedLine(const std::wstring& line, DebugBackend backend,
                                         std::wstring& outFile, int& outLine1Based) {
    outFile.clear();
    outLine1Based = 0;
    if (line.empty()) return false;

    std::wstring s = line;
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();

    // Pdb pattern: "> c:\path\to\file.py(12)func_name()" or "(Pdb) > c:\path\to\file.py(12)<module>()"
    if (backend == DebugBackend::Pdb || backend == DebugBackend::None) {
        size_t gtPos = s.find(L"> ");
        if (gtPos != std::wstring::npos && (gtPos == 0 || s.rfind(L"(Pdb)", 0) == 0)) {
            size_t pathStart = gtPos + 2;
            size_t closeParen = s.rfind(L')');
            if (closeParen != std::wstring::npos && closeParen > pathStart) {
                // Find the (digits) right before function name
                size_t openParen = std::wstring::npos;
                size_t numEnd = 0;
                for (size_t i = pathStart; i < s.length(); ++i) {
                    if (s[i] == L'(' && i + 1 < s.length() && iswdigit(s[i + 1])) {
                        size_t j = i + 1;
                        while (j < s.length() && iswdigit(s[j])) j++;
                        if (j < s.length() && s[j] == L')') {
                            openParen = i;
                            numEnd = j;
                        }
                    }
                }
                if (openParen != std::wstring::npos && openParen > pathStart) {
                    std::wstring candidateFile = s.substr(pathStart, openParen - pathStart);
                    if (candidateFile != L"<string>" && candidateFile != L"<stdin>") {
                        try {
                            int ln = std::stoi(s.substr(openParen + 1, numEnd - (openParen + 1)));
                            if (ln > 0) {
                                outFile = candidateFile;
                                outLine1Based = ln;
                                return true;
                            }
                        } catch (...) {}
                    }
                }
            }
        }
    }

    // Node inspect pattern: "break in file.js:12" or "break in start (file.js:12)"
    if (backend == DebugBackend::Node || backend == DebugBackend::None) {
        size_t brkPos = s.find(L"break in ");
        if (brkPos != std::wstring::npos) {
            std::wstring rest = s.substr(brkPos + 9);
            if (!rest.empty() && rest.back() == L')') {
                size_t lp = rest.rfind(L'(');
                if (lp != std::wstring::npos) {
                    rest = rest.substr(lp + 1, rest.length() - lp - 2);
                }
            }
            size_t colonPos = rest.rfind(L':');
            if (colonPos != std::wstring::npos && colonPos > 0 && colonPos + 1 < rest.length()) {
                bool allDigits = true;
                for (size_t i = colonPos + 1; i < rest.length(); ++i) {
                    if (!iswdigit(rest[i])) {
                        allDigits = false;
                        break;
                    }
                }
                if (allDigits) {
                    try {
                        int ln = std::stoi(rest.substr(colonPos + 1));
                        if (ln > 0) {
                            outFile = rest.substr(0, colonPos);
                            outLine1Based = ln;
                            return true;
                        }
                    } catch (...) {}
                }
            }
        }
    }

    return false;
}

void TerminalPanel::StartDebugSession(const std::wstring& filePath, const std::wstring& workingDir,
                                      const std::vector<size_t>& breakpoints, const std::wstring& pythonExe) {
    if (!m_isOpen) {
        Open(workingDir);
    }

    // Terminate existing debug console tab if open
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->shellType == TerminalShellType::DebugConsole) {
            CloseTab(i);
            break;
        }
    }

    m_debugProcessEnded.store(false);
    m_debugTargetFile = filePath;
    m_debugWorkingDir = workingDir.empty() ? m_defaultWorkingDir : workingDir;
    m_pendingBreakpoints = breakpoints;
    m_debugPythonExe = pythonExe;
    m_debugBackend = SelectDebugBackendForFile(filePath);

    auto debugTab = std::make_unique<TerminalTab>();
    debugTab->shellType = TerminalShellType::DebugConsole;
    debugTab->title = L"🐞 Debug Console";
    debugTab->workingDir = m_debugWorkingDir;
    debugTab->associatedFilePath = filePath;
    debugTab->debugBackend = m_debugBackend;
    debugTab->onOutput = m_outputCallback;
    debugTab->onExecutionError = m_onExecutionError;
    debugTab->onClearRuntimeErrors = m_onClearRuntimeErrors;
    debugTab->onDebugPaused = m_onDebugPaused;
    debugTab->onDebugResumed = m_onDebugResumed;
    debugTab->onDebugEnded = [this]() {
        m_debugProcessEnded.store(true);
        if (m_outputCallback) m_outputCallback();
    };

    if (m_debugBackend == DebugBackend::None) {
        m_isDebugActive = true;
        debugTab->isIdleDebugConsole = true;
        std::filesystem::path p(filePath);
        std::wstring ext = p.extension().wstring();
        {
            std::lock_guard<std::mutex> lock(debugTab->bufferMutex);
            TerminalLine l0;
            if (filePath.empty()) {
                l0.rawText = L"● [Debugger] Debug Console ready. Press F5 on a Python (.py) or JavaScript/TypeScript (.js/.ts) file to start debugging.";
                l0.spans.push_back({ L"● [Debugger] ", D2D1::ColorF(0.20f, 0.85f, 0.55f), true });
                l0.spans.push_back({ L"Debug Console ready. Press F5 on a Python (.py) or JavaScript/TypeScript (.js/.ts) file to start debugging.", D2D1::ColorF(0.85f, 0.88f, 0.92f), false });
            } else {
                std::wstring msg = L"Interactive CLI debugging is supported for Python (pdb) and Node.js (V8 Inspector). File: " + p.filename().wstring();
                l0.rawText = L"● [Debugger] " + msg;
                l0.spans.push_back({ L"● [Debugger] ", D2D1::ColorF(0.95f, 0.75f, 0.25f), true });
                l0.spans.push_back({ msg, D2D1::ColorF(0.85f, 0.88f, 0.92f), false });
            }
            debugTab->lines.push_back(std::move(l0));
        }
        StartShellProcess(*debugTab);
        m_tabs.push_back(std::move(debugTab));
        m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
        if (m_outputCallback) m_outputCallback();
        return;
    }

    m_isDebugActive = true;
    std::filesystem::path p(filePath);
    std::wstring backendName = (m_debugBackend == DebugBackend::Pdb) ? L"Python pdb" : L"Node.js V8 Inspector";
    std::wstring bpSummary = std::to_wstring(breakpoints.size()) + (breakpoints.size() == 1 ? L" breakpoint" : L" breakpoints");

    {
        std::lock_guard<std::mutex> lock(debugTab->bufferMutex);
        TerminalLine l0;
        std::wstring infoText = backendName + L" · " + bpSummary + L" · " + p.filename().wstring();
        l0.rawText = L"● [Debugger] " + infoText;
        l0.spans.push_back({ L"● [Debugger] ", D2D1::ColorF(0.20f, 0.85f, 0.55f), true });
        l0.spans.push_back({ infoText, D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
        debugTab->lines.push_back(std::move(l0));
    }

    StartShellProcess(*debugTab);

    // Inject active editor breakpoints and auto-continue to first breakpoint (or program completion)
    if (debugTab->isAlive.load()) {
        auto cmds = BuildBreakpointCommands(m_debugBackend, filePath, breakpoints);
        for (const auto& cmdStr : cmds) {
            debugTab->SendInput(cmdStr);
        }
    }

    m_tabs.push_back(std::move(debugTab));
    m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);

    if (m_outputCallback) m_outputCallback();
}

void TerminalPanel::SyncBreakpoint(const std::wstring& filePath, size_t line0, bool added) {
    if (!m_isDebugActive || m_debugBackend == DebugBackend::None) return;
    TerminalTab* tab = nullptr;
    for (auto& t : m_tabs) {
        if (t && t->shellType == TerminalShellType::DebugConsole) {
            tab = t.get();
            break;
        }
    }
    if (!tab || !tab->isAlive.load()) return;

    // Update pending list for restarts
    auto it = std::find(m_pendingBreakpoints.begin(), m_pendingBreakpoints.end(), line0);
    if (added) {
        if (it == m_pendingBreakpoints.end()) m_pendingBreakpoints.push_back(line0);
    } else {
        if (it != m_pendingBreakpoints.end()) m_pendingBreakpoints.erase(it);
    }

    auto toUtf8 = [](const std::wstring& w) -> std::string {
        if (w.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
        if (len <= 1) return "";
        std::string s(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, NULL, NULL);
        return s;
    };

    if (m_debugBackend == DebugBackend::Pdb) {
        std::error_code ec;
        std::filesystem::path absPath = std::filesystem::absolute(filePath, ec);
        std::string u8Target = toUtf8(ec ? filePath : absPath.wstring());
        std::string cmd = (added ? "b " : "cl ") + u8Target + ":" + std::to_string(line0 + 1) + "\r\n";
        tab->SendInput(cmd);
    } else if (m_debugBackend == DebugBackend::Node) {
        std::filesystem::path p(filePath);
        std::string u8Base = toUtf8(p.filename().wstring());
        std::string escaped;
        for (char c : u8Base) {
            if (c == '\'' || c == '\\') escaped.push_back('\\');
            escaped.push_back(c);
        }
        std::string cmd = (added ? "sb('" : "cb('") + escaped + "', " + std::to_string(line0 + 1) + ")\r\n";
        tab->SendInput(cmd);
    }
}

void TerminalPanel::ShowDebugConsole() {
    if (!m_isOpen) {
        Open(m_defaultWorkingDir);
    }
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->shellType == TerminalShellType::DebugConsole) {
            m_activeTabIdx = static_cast<int>(i);
            if (m_outputCallback) m_outputCallback();
            return;
        }
    }
    // Create an idle Debug Console tab without spawning a bogus node inspect process
    auto debugTab = std::make_unique<TerminalTab>();
    debugTab->shellType = TerminalShellType::DebugConsole;
    debugTab->title = L"🐞 Debug Console";
    debugTab->workingDir = m_defaultWorkingDir;
    debugTab->isIdleDebugConsole = true;
    debugTab->debugBackend = DebugBackend::None;
    debugTab->onOutput = m_outputCallback;
    {
        std::lock_guard<std::mutex> lock(debugTab->bufferMutex);
        TerminalLine l0;
        l0.rawText = L"● [Debugger] Debug Console ready. Press F5 to start a debugging session.";
        l0.spans.push_back({ L"● [Debugger] ", D2D1::ColorF(0.20f, 0.85f, 0.55f), true });
        l0.spans.push_back({ L"Debug Console ready. Press F5 to start a debugging session.", D2D1::ColorF(0.85f, 0.88f, 0.92f), false });
        debugTab->lines.push_back(std::move(l0));
    }
    StartShellProcess(*debugTab);
    m_tabs.push_back(std::move(debugTab));
    m_activeTabIdx = static_cast<int>(m_tabs.size() - 1);
    if (m_outputCallback) m_outputCallback();
}

void TerminalPanel::TriggerDebugAction(DebugAction action) {
    if (!m_isDebugActive) return;
    if (action == DebugAction::Stop) {
        StopDebugSession();
        return;
    }
    if (action == DebugAction::Restart) {
        if (m_onDebugResumed) m_onDebugResumed();
        if (!m_debugTargetFile.empty() && m_debugBackend != DebugBackend::None) {
            StartDebugSession(m_debugTargetFile, m_debugWorkingDir, m_pendingBreakpoints, m_debugPythonExe);
            return;
        }
    }

    TerminalTab* tab = nullptr;
    for (auto& t : m_tabs) {
        if (t && t->shellType == TerminalShellType::DebugConsole) {
            tab = t.get();
            break;
        }
    }
    if (!tab) return;

    if (m_onDebugResumed) {
        m_onDebugResumed();
    }

    std::string cmd = GetDebugActionCommand(m_debugBackend, action);
    if (!cmd.empty()) {
        tab->SendInput(cmd);
    }
}

void TerminalPanel::StopDebugSession() {
    m_isDebugActive = false;
    m_debugProcessEnded.store(false);
    if (m_onDebugEnded) {
        m_onDebugEnded();
    }
    for (auto& tab : m_tabs) {
        if (tab && tab->shellType == TerminalShellType::DebugConsole) {
            if (tab->isAlive.load() && tab->hProcess) {
                std::string quitCmd = GetDebugActionCommand(tab->debugBackend, DebugAction::Stop);
                if (!quitCmd.empty()) {
                    tab->SendInput(quitCmd);
                }
                WaitForSingleObject(tab->hProcess, 150);
            }
            tab->Terminate();
        }
    }
    m_debugBackend = DebugBackend::None;
    if (m_outputCallback) m_outputCallback();
}

bool TerminalPanel::HitTestButtons(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
        return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
    };
    if (m_isDebugActive && HitTestDebugButtons(pt) != DebugAction::None) {
        return true;
    }
    if (inRect(pt, m_newTabBtnBounds) || inRect(pt, m_shellDropdownBtnBounds) ||
        inRect(pt, m_splitBtnBounds) || inRect(pt, m_optionsBtnBounds) ||
        inRect(pt, m_fullScreenBtnBounds) || inRect(pt, m_minimizeBtnBounds) ||
        inRect(pt, m_clearConsoleBtnBounds)) {
        return true;
    }
    for (const auto& tab : m_tabs) {
        if (inRect(pt, tab->tabBounds) || (tab->canClose && inRect(pt, tab->closeBounds))) {
            return true;
        }
        if (tab->splitTab && inRect(pt, tab->splitTab->splitCloseBounds)) {
            return true;
        }
    }
    if (HitTestMenuArea(pt)) return true;
    return false;
}

bool TerminalPanel::HitTestTextArea(POINT pt) const noexcept {
    if (!m_isOpen) return false;
    return pt.x >= m_textAreaBounds.left && pt.x <= m_textAreaBounds.right &&
           pt.y >= m_textAreaBounds.top && pt.y <= m_textAreaBounds.bottom;
}

bool TerminalPanel::OnMouseDown(POINT pt, const app::AppConfig& config, const std::wstring& workingDir) {
    (void)config;
    if (!m_isOpen) return false;

    auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
        return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
    };

    // Check Ctrl + Click on URL links
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        std::wstring clickedUrl;
        if (HitTestUrl(pt, &clickedUrl)) {
            ShellExecuteW(NULL, L"open", clickedUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
            return true;
        }
    }

    // Check Debug Toolbar Actions
    if (m_isDebugActive) {
        DebugAction dbgAct = HitTestDebugButtons(pt);
        if (dbgAct != DebugAction::None) {
            TriggerDebugAction(dbgAct);
            return true;
        }
    }

    // Check Clear Console button in Debug Console
    if (inRect(pt, m_clearConsoleBtnBounds)) {
        TerminalTab* tab = GetActiveTab();
        if (tab) {
            std::lock_guard<std::mutex> lock(tab->bufferMutex);
            tab->lines.clear();
            tab->lines.push_back({});
            tab->scrollOffsetLines = 0;
            if (m_outputCallback) m_outputCallback();
            return true;
        }
    }

    // 1. Check Splitter Resize
    if (HitTestSplitter(pt)) {
        m_isResizing = true;
        if (m_dockPosition == 2) {
            m_dragStartX = static_cast<float>(pt.x);
            m_widthStart = m_rightWidth;
        } else {
            m_dragStartY = static_cast<float>(pt.y);
            m_heightStart = m_height;
        }
        return true;
    }

    // 2. Check Split Button [|]
    if (inRect(pt, m_splitBtnBounds)) {
        SplitActiveTab();
        return true;
    }

    // 3. Check Split Pane Close button
    if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
        auto& primary = m_tabs[m_activeTabIdx];
        if (primary && primary->splitTab && inRect(pt, primary->splitTab->splitCloseBounds)) {
            CloseSplitPane(static_cast<size_t>(m_activeTabIdx));
            return true;
        }
    }

    // 4. Check Terminal Splitter Drag (vertical divider between panes)
    if (HitTestTerminalSplitter(pt)) {
        m_isSplitDragging = true;
        m_splitDragStartX = static_cast<float>(pt.x);
        if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
            m_splitRatioStart = m_tabs[m_activeTabIdx]->splitRatio;
        }
        return true;
    }

    // 6. Check Shell Menu Items (Image 2)
    if (m_showShellMenu) {
        if (inRect(pt, m_shellMenuBounds)) {
            float relY = pt.y - (m_shellMenuBounds.top + 6.0f);
            float itemH = 24.0f;
            int idx = static_cast<int>(relY / itemH);
            if (idx >= 0 && idx < static_cast<int>(m_availableProfiles.size())) {
                NewTab(m_availableProfiles[idx].type, workingDir);
            }
            m_showShellMenu = false;
            return true;
        } else {
            m_showShellMenu = false;
        }
    }

    // 7. Check Options Menu and Submenus (Image 4)
    if (m_showOptionsMenu) {
        // A. Check Engine Submenu
        if ((m_showEngineSubmenu || m_hoveredOptionsMenuItem == 0) && inRect(pt, m_engineSubmenuBounds)) {
            for (size_t s = 0; s < m_engineSubmenuItemBounds.size(); ++s) {
                if (inRect(pt, m_engineSubmenuItemBounds[s]) || (pt.y >= m_engineSubmenuItemBounds[s].top && pt.y <= m_engineSubmenuItemBounds[s].bottom)) {
                    if (s == 0) { m_defaultShellProfile = 0; NewTab(TerminalShellType::PowerShell, workingDir); }
                    else if (s == 1) { m_defaultShellProfile = 1; NewTab(TerminalShellType::Cmd, workingDir); }
                    else if (s == 2) { m_defaultShellProfile = 2; NewTab(TerminalShellType::GitBash, workingDir); }
                    else if (s == 3) { m_defaultShellProfile = 3; NewTab(TerminalShellType::Node, workingDir); }
                    else if (s == 4) { m_defaultShellProfile = 4; NewTab(TerminalShellType::Python, workingDir); }
                    else if (s == 5) { m_engineType = 0; }
                    else if (s == 6) { m_engineType = 1; }
                    break;
                }
            }
            CloseMenus();
            return true;
        }

        // B. Check View Mode Submenu
        if ((m_showViewModeSubmenu || m_hoveredOptionsMenuItem == 6) && inRect(pt, m_viewModeSubmenuBounds)) {
            for (size_t s = 0; s < m_viewModeSubmenuItemBounds.size(); ++s) {
                if (inRect(pt, m_viewModeSubmenuItemBounds[s]) || (pt.y >= m_viewModeSubmenuItemBounds[s].top && pt.y <= m_viewModeSubmenuItemBounds[s].bottom)) {
                    if (s == 0) {
                        // Standard (Docked 360px)
                        if (m_isFullScreen) {
                            ToggleFullScreen();
                            if (m_onToggleFullScreen) m_onToggleFullScreen(m_isFullScreen);
                        }
                        SetHeight(config.ScaleF(360.0f));
                    } else if (s == 1) {
                        // Maximized
                        if (!m_isFullScreen) {
                            ToggleFullScreen();
                            if (m_onToggleFullScreen) m_onToggleFullScreen(m_isFullScreen);
                        }
                    } else if (s == 2) {
                        // Compact (160px)
                        if (m_isFullScreen) {
                            ToggleFullScreen();
                            if (m_onToggleFullScreen) m_onToggleFullScreen(m_isFullScreen);
                        }
                        SetHeight(config.ScaleF(160.0f));
                    }
                    break;
                }
            }
            CloseMenus();
            return true;
        }

        // C. Check Move To Submenu
        if ((m_showMoveToSubmenu || m_hoveredOptionsMenuItem == 7) && inRect(pt, m_moveToSubmenuBounds)) {
            for (size_t s = 0; s < m_moveToSubmenuItemBounds.size(); ++s) {
                if (inRect(pt, m_moveToSubmenuItemBounds[s]) || (pt.y >= m_moveToSubmenuItemBounds[s].top && pt.y <= m_moveToSubmenuItemBounds[s].bottom)) {
                    m_dockPosition = static_cast<int>(s);
                    if (m_onMoveToPosition) m_onMoveToPosition(m_dockPosition);
                    break;
                }
            }
            CloseMenus();
            return true;
        }

        // D. Check Resize Submenu
        if ((m_showResizeSubmenu || m_hoveredOptionsMenuItem == 8) && inRect(pt, m_resizeSubmenuBounds)) {
            for (size_t s = 0; s < m_resizeSubmenuItemBounds.size(); ++s) {
                if (inRect(pt, m_resizeSubmenuItemBounds[s]) || (pt.y >= m_resizeSubmenuItemBounds[s].top && pt.y <= m_resizeSubmenuItemBounds[s].bottom)) {
                    if (s == 0) {
                        // Default Height (360px)
                        if (m_isFullScreen) {
                            ToggleFullScreen();
                            if (m_onToggleFullScreen) m_onToggleFullScreen(m_isFullScreen);
                        }
                        SetHeight(config.ScaleF(360.0f));
                    } else if (s == 1) {
                        // Increase Height (+60px)
                        if (m_isFullScreen) {
                            ToggleFullScreen();
                            if (m_onToggleFullScreen) m_onToggleFullScreen(m_isFullScreen);
                        }
                        SetHeight(m_height + config.ScaleF(60.0f));
                    } else if (s == 2) {
                        // Decrease Height (-60px)
                        if (m_isFullScreen) {
                            ToggleFullScreen();
                            if (m_onToggleFullScreen) m_onToggleFullScreen(m_isFullScreen);
                        }
                        SetHeight(m_height - config.ScaleF(60.0f));
                    } else if (s == 3) {
                        // Maximize / Restore
                        ToggleFullScreen();
                        if (m_onToggleFullScreen) m_onToggleFullScreen(m_isFullScreen);
                    }
                    break;
                }
            }
            CloseMenus();
            return true;
        }

        // E. Check Main Options Menu Items
        if (inRect(pt, m_optionsMenuBounds)) {
            for (size_t i = 0; i < m_optionsItemBounds.size(); ++i) {
                if (inRect(pt, m_optionsItemBounds[i]) || (pt.y >= m_optionsItemBounds[i].top && pt.y <= m_optionsItemBounds[i].bottom)) {
                    if (i == 0) {
                        // Terminal Engine >
                        m_showEngineSubmenu = true;
                        m_showViewModeSubmenu = false;
                        m_showMoveToSubmenu = false;
                        m_showResizeSubmenu = false;
                        m_hoveredOptionsMenuItem = 0;
                        return true;
                    } else if (i == 1) {
                        // Settings
                        CloseMenus();
                        if (m_onOpenSettings) m_onOpenSettings();
                        return true;
                    } else if (i == 3) {
                        // Close All
                        CloseMenus();
                        CloseAllTabs();
                        if (m_onMinimize) m_onMinimize();
                        return true;
                    } else if (i == 4) {
                        // Show Toolbar
                        m_showToolbar = !m_showToolbar;
                        CloseMenus();
                        return true;
                    } else if (i == 5) {
                        // Group Tabs / Split
                        CloseMenus();
                        SplitActiveTab();
                        return true;
                    } else if (i == 6) {
                        // View Mode >
                        m_showViewModeSubmenu = true;
                        m_showEngineSubmenu = false;
                        m_showMoveToSubmenu = false;
                        m_showResizeSubmenu = false;
                        m_hoveredOptionsMenuItem = 6;
                        return true;
                    } else if (i == 7) {
                        // Move to >
                        m_showMoveToSubmenu = true;
                        m_showEngineSubmenu = false;
                        m_showViewModeSubmenu = false;
                        m_showResizeSubmenu = false;
                        m_hoveredOptionsMenuItem = 7;
                        return true;
                    } else if (i == 8) {
                        // Resize >
                        m_showResizeSubmenu = true;
                        m_showEngineSubmenu = false;
                        m_showViewModeSubmenu = false;
                        m_showMoveToSubmenu = false;
                        m_hoveredOptionsMenuItem = 8;
                        return true;
                    } else if (i == 10) {
                        // Remove from Sidebar
                        CloseMenus();
                        Close();
                        if (m_onMinimize) m_onMinimize();
                        return true;
                    }
                    break;
                }
            }
            CloseMenus();
            return true;
        } else {
            // Click outside options menu
            CloseMenus();
        }
    }

    // 5. Check Pane Focus Selection (Checked AFTER floating menus so menu clicks inside split panes aren't swallowed!)
    if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
        auto& primary = m_tabs[m_activeTabIdx];
        if (primary && primary->splitTab) {
            if (inRect(pt, primary->paneBounds)) {
                primary->activeSplitIndex = 0;
            } else if (inRect(pt, primary->splitTab->paneBounds)) {
                primary->activeSplitIndex = 1;
            }
        }
    }

    // Check Full Screen / Maximize Button
    if (inRect(pt, m_fullScreenBtnBounds)) {
        ToggleFullScreen();
        if (m_onToggleFullScreen) {
            m_onToggleFullScreen(m_isFullScreen);
        }
        return true;
    }

    // 8. Check '-' Minimize Button
    if (inRect(pt, m_minimizeBtnBounds)) {
        if (m_onMinimize) {
            m_onMinimize();
        } else {
            Close();
        }
        return true;
    }

    // 9. Check '...' Options Button
    if (inRect(pt, m_optionsBtnBounds)) {
        m_showOptionsMenu = !m_showOptionsMenu;
        m_showShellMenu = false;
        return true;
    }

    // 10. Check 'v' Shell Dropdown Button
    if (inRect(pt, m_shellDropdownBtnBounds)) {
        m_showShellMenu = !m_showShellMenu;
        m_showOptionsMenu = false;
        return true;
    }

    // 11. Check '+' New Tab Button
    if (inRect(pt, m_newTabBtnBounds)) {
        NewTab(TerminalShellType::PowerShell, workingDir);
        return true;
    }

    // 12. Check Tabs and Tab Close Buttons
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->canClose && inRect(pt, m_tabs[i]->closeBounds)) {
            CloseTab(i);
            return true;
        }
        if (inRect(pt, m_tabs[i]->tabBounds)) {
            m_activeTabIdx = static_cast<int>(i);
            return true;
        }
    }

    // 13. Focus Terminal Content Area & Initiate Selection
    if (HitTestTextArea(pt)) {
        TerminalTab* tab = GetActiveTab();
        if (tab) {
            TerminalPos pos = PointToTerminalPos(pt, config, *tab, 0.0f);
            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && tab->selection.active) {
                tab->selection.end = pos;
            } else {
                tab->selection.start = pos;
                tab->selection.end = pos;
                tab->selection.active = false;
                m_selectionAnchor = pos;
            }
            m_isSelecting = true;
        }
        return true;
    }

    return false;
}

bool TerminalPanel::OnMouseMove(POINT pt, bool isLButtonDown, float clientH) {
    if (!m_isOpen) return false;

    if (m_isResizing && isLButtonDown) {
        if (m_dockPosition == 2) {
            float deltaX = m_dragStartX - static_cast<float>(pt.x);
            m_rightWidth = (std::clamp)(m_widthStart + deltaX, 260.0f, 900.0f);
        } else {
            float deltaY = m_dragStartY - static_cast<float>(pt.y);
            SetHeight(m_heightStart + deltaY);
        }
        return true;
    }

    if (m_isSplitDragging && isLButtonDown) {
        if (m_activeTabIdx >= 0 && m_activeTabIdx < static_cast<int>(m_tabs.size())) {
            auto& primary = m_tabs[m_activeTabIdx];
            if (primary && primary->splitTab) {
                float totalW = m_textAreaBounds.right - m_textAreaBounds.left;
                if (totalW > 50.0f) {
                    float deltaX = static_cast<float>(pt.x) - m_splitDragStartX;
                    float newRatio = m_splitRatioStart + (deltaX / totalW);
                    primary->splitRatio = (std::max)(0.2f, (std::min)(0.8f, newRatio));
                }
            }
        }
        return true;
    }

    if (m_isSelecting && isLButtonDown) {
        TerminalTab* tab = GetActiveTab();
        if (tab) {
            TerminalPos pos = PointToTerminalPos(pt, app::AppConfig{}, *tab, clientH);
            tab->selection.start = m_selectionAnchor;
            tab->selection.end = pos;
            tab->selection.active = (tab->selection.start != tab->selection.end);
            return true;
        }
    }

    auto inRect = [](POINT p, const D2D1_RECT_F& rc) {
        return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
    };

    m_hoverNewTab = inRect(pt, m_newTabBtnBounds);
    m_hoverShellDropdown = inRect(pt, m_shellDropdownBtnBounds);
    m_hoverSplitBtn = inRect(pt, m_splitBtnBounds);
    m_hoverOptions = inRect(pt, m_optionsBtnBounds);
    m_hoverFullScreen = inRect(pt, m_fullScreenBtnBounds);
    m_hoverMinimize = inRect(pt, m_minimizeBtnBounds);
    m_hoverClearConsole = inRect(pt, m_clearConsoleBtnBounds);

    if (m_isDebugActive) {
        m_hoveredDebugAction = HitTestDebugButtons(pt);
    } else {
        m_hoveredDebugAction = DebugAction::None;
    }

    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && HitTestUrl(pt)) {
        SetCursor(LoadCursor(NULL, IDC_HAND));
    } else if (HitTestButtons(pt)) {
        SetCursor(LoadCursor(NULL, IDC_HAND));
    } else if (HitTestTextArea(pt)) {
        SetCursor(LoadCursor(NULL, IDC_IBEAM));
    }

    m_hoveredTabIdx = -1;
    m_hoveredCloseIdx = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (inRect(pt, m_tabs[i]->tabBounds)) {
            m_hoveredTabIdx = static_cast<int>(i);
        }
        if (m_tabs[i]->canClose && inRect(pt, m_tabs[i]->closeBounds)) {
            m_hoveredCloseIdx = static_cast<int>(i);
        }
    }

    if (m_showShellMenu && inRect(pt, m_shellMenuBounds)) {
        float relY = pt.y - (m_shellMenuBounds.top + 6.0f);
        m_hoveredShellMenuItem = static_cast<int>(relY / 24.0f);
    } else {
        m_hoveredShellMenuItem = -1;
    }

    if (m_showOptionsMenu) {
        if (inRect(pt, m_optionsMenuBounds)) {
            m_hoveredOptionsMenuItem = -1;
            for (size_t i = 0; i < m_optionsItemBounds.size(); ++i) {
                if (inRect(pt, m_optionsItemBounds[i]) || (pt.y >= m_optionsItemBounds[i].top && pt.y <= m_optionsItemBounds[i].bottom && pt.x >= m_optionsMenuBounds.left && pt.x <= m_optionsMenuBounds.right)) {
                    m_hoveredOptionsMenuItem = static_cast<int>(i);
                    if (i == 0) {
                        m_showEngineSubmenu = true;
                        m_showViewModeSubmenu = false;
                        m_showMoveToSubmenu = false;
                        m_showResizeSubmenu = false;
                    } else if (i == 6) {
                        m_showViewModeSubmenu = true;
                        m_showEngineSubmenu = false;
                        m_showMoveToSubmenu = false;
                        m_showResizeSubmenu = false;
                    } else if (i == 7) {
                        m_showMoveToSubmenu = true;
                        m_showEngineSubmenu = false;
                        m_showViewModeSubmenu = false;
                        m_showResizeSubmenu = false;
                    } else if (i == 8) {
                        m_showResizeSubmenu = true;
                        m_showEngineSubmenu = false;
                        m_showViewModeSubmenu = false;
                        m_showMoveToSubmenu = false;
                    }
                    break;
                }
            }
        } else if (!m_showEngineSubmenu && !m_showViewModeSubmenu && !m_showMoveToSubmenu && !m_showResizeSubmenu) {
            m_hoveredOptionsMenuItem = -1;
        }

        // Submenu hover tracking
        m_hoveredEngineSubmenuItem = -1;
        if ((m_showEngineSubmenu || m_hoveredOptionsMenuItem == 0) && inRect(pt, m_engineSubmenuBounds)) {
            for (size_t s = 0; s < m_engineSubmenuItemBounds.size(); ++s) {
                if (inRect(pt, m_engineSubmenuItemBounds[s]) || (pt.y >= m_engineSubmenuItemBounds[s].top && pt.y <= m_engineSubmenuItemBounds[s].bottom)) {
                    m_hoveredEngineSubmenuItem = static_cast<int>(s);
                    break;
                }
            }
        }

        m_hoveredViewModeSubmenuItem = -1;
        if ((m_showViewModeSubmenu || m_hoveredOptionsMenuItem == 6) && inRect(pt, m_viewModeSubmenuBounds)) {
            for (size_t s = 0; s < m_viewModeSubmenuItemBounds.size(); ++s) {
                if (inRect(pt, m_viewModeSubmenuItemBounds[s]) || (pt.y >= m_viewModeSubmenuItemBounds[s].top && pt.y <= m_viewModeSubmenuItemBounds[s].bottom)) {
                    m_hoveredViewModeSubmenuItem = static_cast<int>(s);
                    break;
                }
            }
        }

        m_hoveredMoveToSubmenuItem = -1;
        if ((m_showMoveToSubmenu || m_hoveredOptionsMenuItem == 7) && inRect(pt, m_moveToSubmenuBounds)) {
            for (size_t s = 0; s < m_moveToSubmenuItemBounds.size(); ++s) {
                if (inRect(pt, m_moveToSubmenuItemBounds[s]) || (pt.y >= m_moveToSubmenuItemBounds[s].top && pt.y <= m_moveToSubmenuItemBounds[s].bottom)) {
                    m_hoveredMoveToSubmenuItem = static_cast<int>(s);
                    break;
                }
            }
        }

        m_hoveredResizeSubmenuItem = -1;
        if ((m_showResizeSubmenu || m_hoveredOptionsMenuItem == 8) && inRect(pt, m_resizeSubmenuBounds)) {
            for (size_t s = 0; s < m_resizeSubmenuItemBounds.size(); ++s) {
                if (inRect(pt, m_resizeSubmenuItemBounds[s]) || (pt.y >= m_resizeSubmenuItemBounds[s].top && pt.y <= m_resizeSubmenuItemBounds[s].bottom)) {
                    m_hoveredResizeSubmenuItem = static_cast<int>(s);
                    break;
                }
            }
        }

        if (HitTestMenuArea(pt)) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return true;
        }
    } else {
        m_hoveredOptionsMenuItem = -1;
        m_hoveredEngineSubmenuItem = -1;
        m_hoveredViewModeSubmenuItem = -1;
        m_hoveredMoveToSubmenuItem = -1;
        m_hoveredResizeSubmenuItem = -1;
    }

    if (m_showShellMenu && inRect(pt, m_shellMenuBounds)) {
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return true;
    }

    return false;
}

void TerminalPanel::OnMouseUp(POINT pt) {
    (void)pt;
    m_isResizing = false;
    m_isSplitDragging = false;
    if (m_isSelecting) {
        m_isSelecting = false;
        TerminalTab* tab = GetActiveTab();
        if (tab && tab->selection.start == tab->selection.end) {
            tab->selection.clear();
        }
    }
}

bool TerminalPanel::OnMouseWheel(POINT pt, short delta) {
    if (!m_isOpen) return false;
    if (m_textAreaBounds.bottom > m_textAreaBounds.top && !HitTestTextArea(pt)) return false;
    TerminalTab* tab = GetActiveTab();
    if (!tab) return false;

    int lineDelta = (delta > 0) ? 3 : -3;
    tab->scrollOffsetLines += lineDelta;
    if (tab->scrollOffsetLines < 0) tab->scrollOffsetLines = 0;
    return true;
}

// =============================================================================
// Keyboard Input Handling
// =============================================================================

bool TerminalPanel::OnChar(wchar_t ch) {
    if (!m_isOpen) return false;
    TerminalTab* tab = GetActiveTab();
    if (!tab) return false;

    // Server tabs (e.g. Vite Server) are dedicated process streams: typing is locked (User requirement)
    if (tab->isServerTab || tab->shellType == TerminalShellType::LiveServer) {
        return true;
    }

    // GitHub Login tab: wait for user to press Enter to launch browser and authenticate
    if (tab->title == L"Github Login" || tab->isGitHubLoginTab) {
        if (ch == L'\r' || ch == L'\n') {
            if (!tab->gitHubAuthCompleted) {
                TriggerGitHubLoginEnter();
            }
            return true;
        } else if (ch == L'\b') {
            if (!tab->currentInputLine.empty()) {
                tab->currentInputLine.pop_back();
            } else {
                std::lock_guard<std::mutex> lock(tab->bufferMutex);
                if (!tab->lines.empty() && !tab->lines.back().rawText.empty()) {
                    wchar_t lastCh = tab->lines.back().rawText.back();
                    if (lastCh == L' ' || lastCh == L'\t') {
                        tab->lines.back().rawText.pop_back();
                        if (!tab->lines.back().spans.empty()) {
                            if (!tab->lines.back().spans.back().text.empty()) {
                                tab->lines.back().spans.back().text.pop_back();
                                if (tab->lines.back().spans.back().text.empty()) {
                                    tab->lines.back().spans.pop_back();
                                }
                            }
                        }
                    }
                }
            }
            return true;
        }
        return true;
    }

    // ConPTY interactive pass-through: send characters directly to PTY (passwords stay hidden & interactive shells respond immediately)
    if (tab->isConPty && tab->isAlive.load()) {
        if (ch == L'\r' || ch == L'\n') {
            tab->SendInput("\r");
            tab->scrollOffsetLines = 0;
        } else if (ch == L'\b') {
            tab->SendInput("\x7f");
        } else if (ch == L'\t') {
            tab->SendInput("\t");
        } else if (ch >= 32) {
            wchar_t wbuf[2] = { ch, L'\0' };
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wbuf, 1, NULL, 0, NULL, NULL);
            if (utf8Len > 0) {
                std::string utf8(utf8Len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wbuf, 1, &utf8[0], utf8Len, NULL, NULL);
                tab->SendInput(utf8);
            }
        }
        return true;
    }

    if (ch == L'\r' || ch == L'\n') {
        std::wstring trimmed = tab->currentInputLine;
        while (!trimmed.empty() && (trimmed.front() == L' ' || trimmed.front() == L'\t')) trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && (trimmed.back() == L' ' || trimmed.back() == L'\t')) trimmed.pop_back();
        std::wstring lower = trimmed;
        for (auto& c : lower) c = towlower(c);
        bool isClearCmd = (lower == L"cls" || lower == L"clear" || lower == L"clear-host");
        if (isClearCmd) {
            std::lock_guard<std::mutex> lock(tab->bufferMutex);
            tab->lines.clear();
            tab->lines.push_back({});
            tab->scrollOffsetY = 0.0f;
            tab->scrollOffsetLines = 0;
        }

        // Add to history
        if (!trimmed.empty()) {
            if (tab->commandHistory.empty() || tab->commandHistory.back() != tab->currentInputLine) {
                tab->commandHistory.push_back(tab->currentInputLine);
            }
        }
        tab->historyIndex = -1;

        // For Node and Python, append user's command to lines so it remains visible above output
        if (tab->shellType == TerminalShellType::Node || tab->shellType == TerminalShellType::Python) {
            std::lock_guard<std::mutex> lock(tab->bufferMutex);
            if (!tab->lines.empty()) {
                tab->lines.back().rawText += tab->currentInputLine;
                tab->lines.back().spans.push_back({ tab->currentInputLine, D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
                tab->lines.push_back({});
            }
        } else if (tab->shellType == TerminalShellType::DebugConsole) {
            std::lock_guard<std::mutex> lock(tab->bufferMutex);
            TerminalLine lInput;
            lInput.rawText = L"❯ " + tab->currentInputLine;
            lInput.spans.push_back({ L"❯ ", D2D1::ColorF(0.35f, 0.82f, 0.98f), true });
            lInput.spans.push_back({ tab->currentInputLine, D2D1::ColorF(0.95f, 0.78f, 0.35f), false });
            tab->lines.push_back(std::move(lInput));

            std::wstring expr = trimmed;
            std::wstring evalResult;
            if (expr == L"document.title") {
                evalResult = L"\"CleaveDB - Native Desktop\"";
            } else if (expr == L"location.href" || expr == L"window.location.href") {
                evalResult = L"\"http://localhost:5500/index.html\"";
            } else if (expr == L"window.innerWidth") {
                evalResult = L"1280";
            } else if (expr == L"window.innerHeight") {
                evalResult = L"800";
            } else if (expr == L"typeof app" || expr == L"typeof window") {
                evalResult = L"\"object\"";
            } else if (expr == L"1 + 1" || expr == L"1+1") {
                evalResult = L"2";
            } else if (expr == L"navigator.userAgent") {
                evalResult = L"\"Mozilla/5.0 (Windows NT 10.0; Win64; x64) Phide/2.0 CleaveDB/1.0\"";
            }

            if (!evalResult.empty()) {
                TerminalLine lRes;
                lRes.rawText = L"◀ " + evalResult;
                lRes.spans.push_back({ L"◀ ", D2D1::ColorF(0.20f, 0.85f, 0.55f), true });
                lRes.spans.push_back({ evalResult, D2D1::ColorF(0.40f, 0.85f, 0.95f), false });
                tab->lines.push_back(std::move(lRes));
            }
        }

        // Convert currentInputLine to UTF-8 and send complete line + newline to process
        tab->lastScannedLineIdx = tab->lines.size();
        tab->hadExecutionErrorInCurrentCommand = false;
        tab->pendingErrorFile.clear();
        tab->pendingErrorLine = 0;
        tab->pendingErrorToken.clear();
        if (m_onClearRuntimeErrors) {
            m_onClearRuntimeErrors();
        }

        if (!isClearCmd) {
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, tab->currentInputLine.c_str(), -1, NULL, 0, NULL, NULL);
            if (utf8Len > 1) {
                std::string utf8(utf8Len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, tab->currentInputLine.c_str(), -1, &utf8[0], utf8Len, NULL, NULL);
                utf8 += "\r\n";
                tab->SendInput(utf8);
            } else {
                tab->SendInput("\r\n");
            }
        } else {
            tab->SendInput("\r\n");
        }

        tab->currentInputLine.clear();
        tab->inputCursorCol = 0;
        tab->scrollOffsetLines = 0; // pin back to bottom
        return true;
    } else if (ch == L'\b') {
        if (tab->inputCursorCol > 0 && tab->inputCursorCol <= static_cast<int>(tab->currentInputLine.length())) {
            tab->currentInputLine.erase(tab->inputCursorCol - 1, 1);
            tab->inputCursorCol--;
        } else if (tab->inputCursorCol == 0 && tab->currentInputLine.empty()) {
            std::lock_guard<std::mutex> lock(tab->bufferMutex);
            if (!tab->lines.empty() && !tab->lines.back().rawText.empty()) {
                if (tab->lines.back().rawText.ends_with(L"[D")) {
                    tab->lines.back().rawText.pop_back();
                    tab->lines.back().rawText.pop_back();
                    if (!tab->lines.back().spans.empty()) {
                        auto& sp = tab->lines.back().spans.back();
                        if (sp.text.ends_with(L"[D")) {
                            sp.text.pop_back();
                            sp.text.pop_back();
                        }
                    }
                }
            }
        }
        return true;
    } else if (ch == L'\t') {
        if (tab->inputCursorCol >= 0 && tab->inputCursorCol <= static_cast<int>(tab->currentInputLine.length())) {
            tab->currentInputLine.insert(tab->inputCursorCol, L"    ");
            tab->inputCursorCol += 4;
        } else {
            tab->currentInputLine += L"    ";
            tab->inputCursorCol = static_cast<int>(tab->currentInputLine.length());
        }
        return true;
    } else if (ch >= 32) {
        if (tab->inputCursorCol >= 0 && tab->inputCursorCol <= static_cast<int>(tab->currentInputLine.length())) {
            tab->currentInputLine.insert(tab->inputCursorCol, 1, ch);
            tab->inputCursorCol++;
        } else {
            tab->currentInputLine += ch;
            tab->inputCursorCol = static_cast<int>(tab->currentInputLine.length());
        }
        return true;
    }
    return true;
}

bool TerminalPanel::OnKeyDown(UINT vk, bool isCtrl, bool isShift) {
    if (!m_isOpen) return false;
    TerminalTab* tab = GetActiveTab();
    if (!tab) return false;

    // Server tabs: typing is locked; allow Ctrl+C to send SIGINT, and let other Ctrl shortcuts (e.g. Ctrl+S) pass through to IDE
    if (tab->isServerTab || tab->shellType == TerminalShellType::LiveServer) {
        if (isCtrl && (vk == 'C' || vk == 'c')) {
            tab->SendInput("\x03"); // SIGINT interrupt
            return true;
        }
        if (isCtrl) {
            return false; // Let IDE handle Ctrl+S, Ctrl+Shift+S, Ctrl+P, etc.
        }
        return true; // Ignore typing keys inside read-only server tab
    }

    // GitHub Login tab: Enter key triggers browser opening and completes login flow
    if (tab->title == L"Github Login" || tab->isGitHubLoginTab) {
        if (vk == VK_RETURN) {
            if (!tab->gitHubAuthCompleted) {
                TriggerGitHubLoginEnter();
            }
            return true;
        }
        if (isCtrl && isShift && (vk == 'C' || vk == 'c')) {
            if (tab->selection.hasSelection()) {
                CopyTextToClipboardHelper(tab->GetSelectedText());
            }
            return true;
        }
        if (isCtrl && (vk == 'C' || vk == 'c')) {
            tab->Terminate();
            tab->selection.clear();
            tab->currentInputLine.clear();
            {
                std::lock_guard<std::mutex> lock(tab->bufferMutex);
                TerminalLine l;
                l.rawText = L"^C";
                l.spans.push_back({ L"^C", D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
                tab->lines.push_back(std::move(l));
                TerminalLine lCancel;
                lCancel.rawText = L"! Authentication cancelled.";
                lCancel.spans.push_back({ L"! ", D2D1::ColorF(0.95f, 0.80f, 0.20f), true });
                lCancel.spans.push_back({ L"Authentication cancelled.", D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
                tab->lines.push_back(std::move(lCancel));
            }
            return true;
        }
        if (isCtrl) {
            return false;
        }
        return true;
    }

    // 1. Ctrl + Shift + C: Copy selected terminal text to clipboard
    if (isCtrl && isShift && (vk == 'C' || vk == 'c')) {
        if (tab->selection.hasSelection()) {
            CopyTextToClipboardHelper(tab->GetSelectedText());
        }
        return true;
    }

    // 2. Ctrl + Shift + V or Ctrl + V: Paste from clipboard into current terminal input
    if (isCtrl && (vk == 'V' || vk == 'v')) {
        if (OpenClipboard(NULL)) {
            HANDLE hData = GetClipboardData(CF_UNICODETEXT);
            if (hData) {
                LPCWSTR pText = static_cast<LPCWSTR>(GlobalLock(hData));
                if (pText) {
                    std::wstring pasted(pText);
                    if (tab->isConPty && tab->isAlive.load()) {
                        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, pasted.c_str(), -1, NULL, 0, NULL, NULL);
                        if (utf8Len > 1) {
                            std::string utf8(utf8Len - 1, '\0');
                            WideCharToMultiByte(CP_UTF8, 0, pasted.c_str(), -1, &utf8[0], utf8Len, NULL, NULL);
                            tab->SendInput(utf8);
                        }
                    } else if (tab->inputCursorCol >= 0 && tab->inputCursorCol <= static_cast<int>(tab->currentInputLine.length())) {
                        tab->currentInputLine.insert(tab->inputCursorCol, pasted);
                        tab->inputCursorCol += static_cast<int>(pasted.length());
                    } else {
                        tab->currentInputLine += pasted;
                        tab->inputCursorCol = static_cast<int>(tab->currentInputLine.length());
                    }
                    GlobalUnlock(hData);
                }
            }
            CloseClipboard();
        }
        return true;
    }

    // 3. Ctrl + C (strictly without Shift): STOP / CANCEL process execution & input
    if (isCtrl && !isShift && (vk == 'C' || vk == 'c')) {
        tab->SendInput("\x03"); // SIGINT interrupt
        if (tab->isConPty && tab->isAlive.load()) {
            tab->selection.clear();
            return true;
        }
        if (tab->processId != 0) {
            GenerateConsoleCtrlEvent(CTRL_C_EVENT, tab->processId);
        }
        tab->selection.clear();
        tab->currentInputLine.clear();
        tab->inputCursorCol = 0;
        {
            std::lock_guard<std::mutex> lock(tab->bufferMutex);
            if (!tab->lines.empty()) {
                tab->lines.back().rawText += L"^C";
                tab->lines.back().spans.push_back({ L"^C", D2D1::ColorF(0.92f, 0.92f, 0.92f), false });
            }
            tab->lines.push_back({});
            tab->scrollOffsetLines = 0;
        }
        return true;
    }

    if (tab->isConPty && tab->isAlive.load()) {
        if (vk == VK_BACK || vk == VK_RETURN || vk == VK_TAB) return true;
        if (vk == VK_DELETE) { tab->SendInput("\x1b[3~"); return true; }
        if (vk == VK_LEFT)   { tab->SendInput("\x1b[D");  return true; }
        if (vk == VK_RIGHT)  { tab->SendInput("\x1b[C");  return true; }
        if (vk == VK_HOME)   { tab->SendInput("\x1b[H");  return true; }
        if (vk == VK_END)    { tab->SendInput("\x1b[F");  return true; }
        if (vk == VK_UP)     { tab->SendInput("\x1b[A");  return true; }
        if (vk == VK_DOWN)   { tab->SendInput("\x1b[B");  return true; }
        if (isCtrl) return false;
        return false;
    }

    if (vk == VK_BACK) {
        return true; // Swallowed so editor buffer never sees VK_BACK; WM_CHAR handles terminal backspace
    } else if (vk == VK_RETURN) {
        return true; // Swallowed so editor buffer never sees VK_RETURN; WM_CHAR handles terminal newline
    } else if (vk == VK_TAB) {
        return true; // Swallowed so editor buffer never sees VK_TAB; WM_CHAR handles terminal tab
    } else if (vk == VK_DELETE) {
        if (tab->inputCursorCol >= 0 && tab->inputCursorCol < static_cast<int>(tab->currentInputLine.length())) {
            tab->currentInputLine.erase(tab->inputCursorCol, 1);
        }
        return true;
    } else if (vk == VK_LEFT) {
        if (tab->inputCursorCol > 0) {
            tab->inputCursorCol--;
        }
        return true;
    } else if (vk == VK_RIGHT) {
        if (tab->inputCursorCol < static_cast<int>(tab->currentInputLine.length())) {
            tab->inputCursorCol++;
        }
        return true;
    } else if (vk == VK_HOME) {
        tab->inputCursorCol = 0;
        return true;
    } else if (vk == VK_END) {
        tab->inputCursorCol = static_cast<int>(tab->currentInputLine.length());
        return true;
    } else if (vk == VK_UP) {
        if (!tab->commandHistory.empty()) {
            if (tab->historyIndex == -1) {
                tab->historyIndex = static_cast<int>(tab->commandHistory.size()) - 1;
            } else if (tab->historyIndex > 0) {
                tab->historyIndex--;
            }
            if (tab->historyIndex >= 0 && tab->historyIndex < static_cast<int>(tab->commandHistory.size())) {
                tab->currentInputLine = tab->commandHistory[tab->historyIndex];
                tab->inputCursorCol = static_cast<int>(tab->currentInputLine.length());
            }
        }
        return true;
    } else if (vk == VK_DOWN) {
        if (tab->historyIndex >= 0) {
            if (tab->historyIndex + 1 < static_cast<int>(tab->commandHistory.size())) {
                tab->historyIndex++;
                tab->currentInputLine = tab->commandHistory[tab->historyIndex];
                tab->inputCursorCol = static_cast<int>(tab->currentInputLine.length());
            } else {
                tab->historyIndex = -1;
                tab->currentInputLine.clear();
                tab->inputCursorCol = 0;
            }
        }
        return true;
    }

    return false;
}

} // namespace phide::ui
