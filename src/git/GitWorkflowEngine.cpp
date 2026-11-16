/**
 * @file GitWorkflowEngine.cpp
 * @brief Implementation of GitWorkflowEngine for merge conflict parsing/resolution,
 *        partial hunk unified patches, and stash/ref helpers.
 */

#include "phide/git/GitWorkflowEngine.hpp"
#include <windows.h>
#include <sstream>
#include <algorithm>

namespace phide::git {

namespace {

std::wstring TrimEdgeSpaces(const std::wstring& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == L' ' || s[start] == L'\t' || s[start] == L'\r' || s[start] == L'\n')) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && (s[end - 1] == L' ' || s[end - 1] == L'\t' || s[end - 1] == L'\r' || s[end - 1] == L'\n')) {
        --end;
    }
    return s.substr(start, end - start);
}

std::string WideToUtf8String(const std::wstring& w) {
    if (w.empty()) return "";
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return "";
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &out[0], needed, nullptr, nullptr);
    return out;
}

std::wstring JoinLines(const std::vector<std::wstring>& lines) {
    std::wstring out;
    size_t est = 0;
    for (const auto& l : lines) est += l.size() + 1;
    out.reserve(est);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) out.push_back(L'\n');
        out.append(lines[i]);
    }
    return out;
}

bool StartsWithMarker(const std::wstring& line, const wchar_t* marker) {
    size_t len = wcslen(marker);
    if (line.size() < len) return false;
    if (line.compare(0, len, marker) != 0) return false;
    return line.size() == len || line[len] == L' ' || line[len] == L'\t';
}

} // namespace

std::vector<MergeConflictBlock> GitWorkflowEngine::ParseMergeConflicts(const std::vector<std::wstring>& lines) {
    std::vector<MergeConflictBlock> blocks;
    const size_t n = lines.size();
    size_t i = 0;
    while (i < n) {
        if (!StartsWithMarker(lines[i], L"<<<<<<<")) {
            ++i;
            continue;
        }

        size_t startLine = i;
        size_t baseLine = static_cast<size_t>(-1);
        size_t sepLine = static_cast<size_t>(-1);
        size_t endLine = static_cast<size_t>(-1);

        size_t j = i + 1;
        while (j < n) {
            if (StartsWithMarker(lines[j], L"<<<<<<<")) {
                // Nested/incomplete previous marker: restart from the newer marker
                startLine = j;
                baseLine = static_cast<size_t>(-1);
                sepLine = static_cast<size_t>(-1);
                ++j;
                continue;
            }
            if (sepLine == static_cast<size_t>(-1) && baseLine == static_cast<size_t>(-1) && StartsWithMarker(lines[j], L"|||||||")) {
                baseLine = j;
            } else if (sepLine == static_cast<size_t>(-1) && StartsWithMarker(lines[j], L"=======")) {
                sepLine = j;
            } else if (sepLine != static_cast<size_t>(-1) && StartsWithMarker(lines[j], L">>>>>>>")) {
                endLine = j;
                break;
            }
            ++j;
        }

        if (sepLine != static_cast<size_t>(-1) && endLine != static_cast<size_t>(-1)) {
            MergeConflictBlock b;
            b.startLine = startLine;
            b.baseLine = baseLine;
            b.separatorLine = sepLine;
            b.endLine = endLine;
            b.currentLabel = TrimEdgeSpaces(lines[startLine].substr(7));
            if (b.currentLabel.empty()) b.currentLabel = L"Current Change";
            if (baseLine != static_cast<size_t>(-1)) {
                b.baseLabel = TrimEdgeSpaces(lines[baseLine].substr(7));
            }
            b.incomingLabel = TrimEdgeSpaces(lines[endLine].substr(7));
            if (b.incomingLabel.empty()) b.incomingLabel = L"Incoming Change";

            size_t curEnd = (baseLine != static_cast<size_t>(-1)) ? baseLine : sepLine;
            for (size_t k = startLine + 1; k < curEnd; ++k) {
                b.currentLines.push_back(lines[k]);
            }
            if (baseLine != static_cast<size_t>(-1)) {
                for (size_t k = baseLine + 1; k < sepLine; ++k) {
                    b.baseLines.push_back(lines[k]);
                }
            }
            for (size_t k = sepLine + 1; k < endLine; ++k) {
                b.incomingLines.push_back(lines[k]);
            }
            blocks.push_back(std::move(b));
            i = endLine + 1;
        } else {
            break;
        }
    }
    return blocks;
}

std::vector<MergeConflictBlock> GitWorkflowEngine::ParseMergeConflicts(const editor::Buffer& buf) {
    return ParseMergeConflicts(buf.GetLines());
}

std::vector<std::wstring> GitWorkflowEngine::ApplyConflictResolution(
    const std::vector<std::wstring>& lines,
    const MergeConflictBlock& block,
    ConflictResolutionChoice choice) {
    if (block.startLine >= lines.size() || block.endLine >= lines.size() || block.startLine > block.endLine) {
        return lines;
    }

    std::vector<std::wstring> replacement;
    if (choice == ConflictResolutionChoice::AcceptCurrent) {
        replacement = block.currentLines;
    } else if (choice == ConflictResolutionChoice::AcceptIncoming) {
        replacement = block.incomingLines;
    } else {
        replacement.reserve(block.currentLines.size() + block.incomingLines.size());
        replacement.insert(replacement.end(), block.currentLines.begin(), block.currentLines.end());
        replacement.insert(replacement.end(), block.incomingLines.begin(), block.incomingLines.end());
    }

    std::vector<std::wstring> out;
    out.reserve(lines.size() - (block.endLine - block.startLine + 1) + replacement.size());
    out.insert(out.end(), lines.begin(), lines.begin() + block.startLine);
    out.insert(out.end(), replacement.begin(), replacement.end());
    out.insert(out.end(), lines.begin() + block.endLine + 1, lines.end());
    if (out.empty()) out.push_back(L"");
    return out;
}

std::vector<std::wstring> GitWorkflowEngine::ResolveAllConflicts(
    const std::vector<std::wstring>& lines,
    ConflictResolutionChoice choice) {
    auto blocks = ParseMergeConflicts(lines);
    if (blocks.empty()) return lines;
    std::vector<std::wstring> out = lines;
    for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
        out = ApplyConflictResolution(out, *it, choice);
    }
    return out;
}

ThreeWayMergePreview GitWorkflowEngine::BuildThreeWayMergePreview(const std::vector<std::wstring>& lines) {
    ThreeWayMergePreview preview;
    auto blocks = ParseMergeConflicts(lines);
    preview.conflictCount = blocks.size();
    auto curLines = ResolveAllConflicts(lines, ConflictResolutionChoice::AcceptCurrent);
    auto incLines = ResolveAllConflicts(lines, ConflictResolutionChoice::AcceptIncoming);
    preview.currentText = JoinLines(curLines);
    preview.incomingText = JoinLines(incLines);
    preview.resultText = JoinLines(lines);
    return preview;
}

bool GitWorkflowEngine::IsUnmergedGitStatus(wchar_t indexCode, wchar_t workTreeCode) noexcept {
    if (indexCode == L'U' || workTreeCode == L'U') return true;
    if (indexCode == L'D' && workTreeCode == L'D') return true;
    if (indexCode == L'A' && workTreeCode == L'A') return true;
    return false;
}

std::vector<GitStashEntry> GitWorkflowEngine::ParseGitStashList(const std::wstring& stashListOutput) {
    std::vector<GitStashEntry> entries;
    std::wstringstream ss(stashListOutput);
    std::wstring line;
    while (std::getline(ss, line)) {
        line = TrimEdgeSpaces(line);
        if (line.empty()) continue;

        size_t colonPos = line.find(L':');
        if (colonPos == std::wstring::npos) continue;

        GitStashEntry entry;
        entry.refName = TrimEdgeSpaces(line.substr(0, colonPos));
        size_t openBrace = entry.refName.find(L'{');
        size_t closeBrace = entry.refName.find(L'}');
        if (openBrace != std::wstring::npos && closeBrace != std::wstring::npos && closeBrace > openBrace + 1) {
            try {
                entry.index = static_cast<size_t>(std::stoul(entry.refName.substr(openBrace + 1, closeBrace - openBrace - 1)));
            } catch (...) {
                entry.index = entries.size();
            }
        } else {
            entry.index = entries.size();
        }

        std::wstring rest = TrimEdgeSpaces(line.substr(colonPos + 1));
        // Typical format: "WIP on main: <hash> msg" or "On feature: custom msg"
        size_t secondColon = rest.find(L':');
        if (secondColon != std::wstring::npos) {
            std::wstring prefix = TrimEdgeSpaces(rest.substr(0, secondColon));
            size_t onPos = prefix.rfind(L"on ");
            if (onPos == std::wstring::npos) onPos = prefix.rfind(L"On ");
            if (onPos != std::wstring::npos) {
                entry.branch = TrimEdgeSpaces(prefix.substr(onPos + 3));
            }
            entry.message = TrimEdgeSpaces(rest.substr(secondColon + 1));
        } else {
            entry.message = rest;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::string GitWorkflowEngine::BuildUnifiedHunkPatch(
    const std::wstring& repoRelPath,
    const editor::Buffer& leftBuf,
    const editor::Buffer& rightBuf,
    const editor::OpenDocument::DiffHunkItem& hunk) {
    if (repoRelPath.empty()) return "";
    if (hunk.leftCount == 0 && hunk.rightCount == 0) return "";

    std::wstring slashPath = repoRelPath;
    for (auto& ch : slashPath) {
        if (ch == L'\\') ch = L'/';
    }
    while (!slashPath.empty() && slashPath.front() == L'/') {
        slashPath.erase(slashPath.begin());
    }
    std::string utf8Path = WideToUtf8String(slashPath);
    if (utf8Path.empty()) return "";

    size_t oldStart = (hunk.leftCount == 0) ? hunk.leftStart : (hunk.leftStart + 1);
    size_t newStart = (hunk.rightCount == 0) ? hunk.rightStart : (hunk.rightStart + 1);

    std::ostringstream oss;
    oss << "diff --git a/" << utf8Path << " b/" << utf8Path << "\n"
        << "--- a/" << utf8Path << "\n"
        << "+++ b/" << utf8Path << "\n"
        << "@@ -" << oldStart << "," << hunk.leftCount
        << " +" << newStart << "," << hunk.rightCount << " @@\n";

    const size_t leftTotal = leftBuf.GetLineCount();
    const size_t rightTotal = rightBuf.GetLineCount();
    for (size_t i = 0; i < hunk.leftCount && (hunk.leftStart + i) < leftTotal; ++i) {
        oss << "-" << WideToUtf8String(leftBuf.GetLine(hunk.leftStart + i)) << "\n";
    }
    for (size_t j = 0; j < hunk.rightCount && (hunk.rightStart + j) < rightTotal; ++j) {
        oss << "+" << WideToUtf8String(rightBuf.GetLine(hunk.rightStart + j)) << "\n";
    }
    return oss.str();
}

std::vector<std::wstring> GitWorkflowEngine::RevertDiffHunkInLines(
    const std::vector<std::wstring>& rightLines,
    const editor::Buffer& leftBuf,
    const editor::OpenDocument::DiffHunkItem& hunk) {
    std::vector<std::wstring> leftSlice;
    const size_t leftTotal = leftBuf.GetLineCount();
    for (size_t i = 0; i < hunk.leftCount && (hunk.leftStart + i) < leftTotal; ++i) {
        leftSlice.push_back(leftBuf.GetLine(hunk.leftStart + i));
    }

    size_t rBegin = (std::min)(hunk.rightStart, rightLines.size());
    size_t rEnd = (std::min)(rBegin + hunk.rightCount, rightLines.size());

    std::vector<std::wstring> out;
    out.reserve(rightLines.size() - (rEnd - rBegin) + leftSlice.size());
    out.insert(out.end(), rightLines.begin(), rightLines.begin() + rBegin);
    out.insert(out.end(), leftSlice.begin(), leftSlice.end());
    out.insert(out.end(), rightLines.begin() + rEnd, rightLines.end());
    if (out.empty()) out.push_back(L"");
    return out;
}

std::wstring GitWorkflowEngine::SanitizeBranchOrTagName(const std::wstring& raw) {
    std::wstring trimmed = TrimEdgeSpaces(raw);
    std::wstring out;
    out.reserve(trimmed.size());
    for (wchar_t ch : trimmed) {
        if (ch == L' ' || ch == L'\t') {
            if (out.empty() || out.back() != L'-') out.push_back(L'-');
            continue;
        }
        if (ch == L'~' || ch == L'^' || ch == L':' || ch == L'?' || ch == L'*' ||
            ch == L'[' || ch == L'\\' || ch == L'"' || ch == L'\'' || ch < 32) {
            continue;
        }
        out.push_back(ch);
    }
    // Strip forbidden ".." and leading/trailing '.' or '/'
    size_t dotdot = 0;
    while ((dotdot = out.find(L"..")) != std::wstring::npos) {
        out.erase(dotdot, 1);
    }
    while (!out.empty() && (out.front() == L'.' || out.front() == L'/')) out.erase(out.begin());
    while (!out.empty() && (out.back() == L'.' || out.back() == L'/')) out.pop_back();
    return out;
}

std::string GitWorkflowEngine::BuildUnifiedSelectedRangePatch(
    const std::wstring& repoRelPath,
    const editor::Buffer& leftBuf,
    const editor::Buffer& rightBuf,
    const std::vector<DiffHunkPatchSpec>& hunks,
    size_t selStartLine,
    size_t selEndLine,
    bool reverse) {
    if (repoRelPath.empty() || hunks.empty()) return "";
    if (selStartLine > selEndLine) std::swap(selStartLine, selEndLine);

    std::wstring rel = repoRelPath;
    for (auto& c : rel) if (c == L'\\') c = L'/';
    std::string utf8Path = WideToUtf8String(rel);
    if (utf8Path.empty()) return "";

    std::vector<std::string> emittedHunks;
    const size_t leftTotal = leftBuf.GetLineCount();
    const size_t rightTotal = rightBuf.GetLineCount();

    for (const auto& h : hunks) {
        size_t hRightEnd = (h.rightCount > 0) ? (h.rightStart + h.rightCount - 1) : h.rightStart;
        bool intersects = false;
        if (h.rightCount == 0) {
            intersects = (selStartLine <= h.rightStart && selEndLine + 1 >= h.rightStart);
        } else {
            intersects = !(selEndLine < h.rightStart || selStartLine > hRightEnd);
        }
        if (!intersects) continue;

        if (h.leftCount == 0 && h.rightCount > 0) {
            std::vector<std::string> adds;
            for (size_t j = 0; j < h.rightCount && (h.rightStart + j) < rightTotal; ++j) {
                size_t rLine = h.rightStart + j;
                if (rLine >= selStartLine && rLine <= selEndLine) {
                    adds.push_back(WideToUtf8String(rightBuf.GetLine(rLine)));
                }
            }
            if (adds.empty()) continue;
            size_t oldStart = reverse ? (h.leftStart + 1) : h.leftStart;
            size_t newStart = reverse ? h.leftStart : (h.leftStart + 1);
            std::ostringstream hs;
            if (reverse) {
                hs << "@@ -" << oldStart << "," << adds.size() << " +" << newStart << ",0 @@\n";
                for (const auto& l : adds) hs << "-" << l << "\n";
            } else {
                hs << "@@ -" << oldStart << ",0 +" << newStart << "," << adds.size() << " @@\n";
                for (const auto& l : adds) hs << "+" << l << "\n";
            }
            emittedHunks.push_back(hs.str());
        } else if (h.rightCount == 0 && h.leftCount > 0) {
            size_t oldStart = h.leftStart + 1;
            size_t newStart = h.rightStart;
            std::ostringstream hs;
            hs << "@@ -" << oldStart << "," << h.leftCount << " +" << newStart << ",0 @@\n";
            for (size_t i = 0; i < h.leftCount && (h.leftStart + i) < leftTotal; ++i) {
                hs << "-" << WideToUtf8String(leftBuf.GetLine(h.leftStart + i)) << "\n";
            }
            emittedHunks.push_back(hs.str());
        } else {
            // Mixed replacement hunk: select lines in [selStartLine, selEndLine], keep unselected old lines as context
            std::vector<std::string> body;
            size_t oldCount = 0;
            size_t newCount = 0;
            size_t maxPairs = (std::max)(h.leftCount, h.rightCount);
            bool anyChange = false;
            for (size_t k = 0; k < maxPairs; ++k) {
                bool hasL = (k < h.leftCount) && (h.leftStart + k < leftTotal);
                bool hasR = (k < h.rightCount) && (h.rightStart + k < rightTotal);
                size_t rLine = h.rightStart + (std::min)(k, h.rightCount > 0 ? h.rightCount - 1 : size_t(0));
                bool inSel = hasR ? (rLine >= selStartLine && rLine <= selEndLine)
                                  : (h.rightStart + h.rightCount >= selStartLine && h.rightStart <= selEndLine);
                if (inSel) {
                    if (hasL) { body.push_back("-" + WideToUtf8String(leftBuf.GetLine(h.leftStart + k))); ++oldCount; anyChange = true; }
                    if (hasR) { body.push_back("+" + WideToUtf8String(rightBuf.GetLine(h.rightStart + k))); ++newCount; anyChange = true; }
                } else if (hasL) {
                    std::string ctx = WideToUtf8String(leftBuf.GetLine(h.leftStart + k));
                    body.push_back(" " + ctx);
                    ++oldCount;
                    ++newCount;
                }
            }
            if (!anyChange || body.empty()) continue;
            size_t oldStart = (oldCount == 0) ? h.leftStart : (h.leftStart + 1);
            size_t newStart = (newCount == 0) ? h.rightStart : (h.leftStart + 1);
            std::ostringstream hs;
            hs << "@@ -" << oldStart << "," << oldCount << " +" << newStart << "," << newCount << " @@\n";
            for (const auto& row : body) hs << row << "\n";
            emittedHunks.push_back(hs.str());
        }
    }

    if (emittedHunks.empty()) return "";
    std::ostringstream oss;
    oss << "diff --git a/" << utf8Path << " b/" << utf8Path << "\n"
        << "--- a/" << utf8Path << "\n"
        << "+++ b/" << utf8Path << "\n";
    for (const auto& hStr : emittedHunks) oss << hStr;
    return oss.str();
}

std::vector<GitWorkflowEngine::InlineDiffRow> GitWorkflowEngine::BuildInlineUnifiedDiffRows(
    const editor::Buffer& leftBuf,
    const editor::Buffer& rightBuf,
    const std::vector<DiffHunkPatchSpec>& hunks,
    size_t maxRows) {
    std::vector<InlineDiffRow> rows;
    const size_t leftTotal = leftBuf.GetLineCount();
    const size_t rightTotal = rightBuf.GetLineCount();
    rows.reserve((std::min)(maxRows, rightTotal + 64));

    size_t lCur = 0;
    size_t rCur = 0;
    for (size_t hIdx = 0; hIdx < hunks.size() && rows.size() < maxRows; ++hIdx) {
        const auto& h = hunks[hIdx];
        while (rCur < h.rightStart && rCur < rightTotal && rows.size() < maxRows) {
            InlineDiffRow row;
            row.kind = InlineDiffRow::Kind::Context;
            row.leftLine = (lCur < leftTotal) ? (lCur + 1) : 0;
            row.rightLine = rCur + 1;
            row.text = rightBuf.GetLine(rCur);
            rows.push_back(std::move(row));
            ++rCur;
            if (lCur < h.leftStart) ++lCur;
        }
        lCur = (std::max)(lCur, h.leftStart);
        for (size_t i = 0; i < h.leftCount && (h.leftStart + i) < leftTotal && rows.size() < maxRows; ++i) {
            InlineDiffRow row;
            row.kind = InlineDiffRow::Kind::Deleted;
            row.leftLine = h.leftStart + i + 1;
            row.rightLine = 0;
            row.hunkIdx = hIdx;
            row.text = leftBuf.GetLine(h.leftStart + i);
            rows.push_back(std::move(row));
        }
        lCur = h.leftStart + h.leftCount;
        for (size_t j = 0; j < h.rightCount && (h.rightStart + j) < rightTotal && rows.size() < maxRows; ++j) {
            InlineDiffRow row;
            row.kind = InlineDiffRow::Kind::Added;
            row.leftLine = 0;
            row.rightLine = h.rightStart + j + 1;
            row.hunkIdx = hIdx;
            row.text = rightBuf.GetLine(h.rightStart + j);
            rows.push_back(std::move(row));
        }
        rCur = h.rightStart + h.rightCount;
    }
    while (rCur < rightTotal && rows.size() < maxRows) {
        InlineDiffRow row;
        row.kind = InlineDiffRow::Kind::Context;
        row.leftLine = (lCur < leftTotal) ? (lCur + 1) : 0;
        row.rightLine = rCur + 1;
        row.text = rightBuf.GetLine(rCur);
        rows.push_back(std::move(row));
        ++rCur;
        ++lCur;
    }
    return rows;
}

std::vector<GitWorkflowEngine::FileHistoryEntry> GitWorkflowEngine::ParseFollowHistoryLog(
    const std::wstring& logOutput,
    size_t maxEntries) {
    std::vector<FileHistoryEntry> out;
    std::wstringstream ss(logOutput);
    std::wstring line;
    while (std::getline(ss, line) && out.size() < maxEntries) {
        while (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::wstring> cols;
        size_t start = 0;
        while (start <= line.size()) {
            size_t sep = line.find(L'\x1f', start);
            if (sep == std::wstring::npos) {
                cols.push_back(line.substr(start));
                break;
            }
            cols.push_back(line.substr(start, sep - start));
            start = sep + 1;
        }
        if (cols.size() >= 4) {
            FileHistoryEntry e;
            e.shortHash = cols[0];
            e.date = cols[1];
            e.author = cols[2];
            e.message = cols[3];
            e.fullHash = cols.size() > 4 ? cols[4] : cols[0];
            out.push_back(std::move(e));
        }
    }
    return out;
}

std::vector<GitWorkflowEngine::WorktreeEntry> GitWorkflowEngine::ParseWorktreeListPorcelain(
    const std::wstring& porcelainOutput) {
    std::vector<WorktreeEntry> out;
    std::wstringstream ss(porcelainOutput);
    std::wstring line;
    WorktreeEntry cur;
    bool hasCur = false;
    auto flush = [&]() {
        if (hasCur && !cur.path.empty()) {
            out.push_back(std::move(cur));
            cur = WorktreeEntry{};
        }
        hasCur = false;
    };
    while (std::getline(ss, line)) {
        while (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty()) {
            flush();
            continue;
        }
        if (line.rfind(L"worktree ", 0) == 0) {
            flush();
            cur.path = line.substr(9);
            hasCur = true;
        } else if (hasCur) {
            if (line.rfind(L"HEAD ", 0) == 0) {
                cur.headHash = line.substr(5, (std::min)(size_t(7), line.size() - 5));
            } else if (line.rfind(L"branch ", 0) == 0) {
                std::wstring b = line.substr(7);
                const std::wstring prefix = L"refs/heads/";
                if (b.rfind(prefix, 0) == 0) b = b.substr(prefix.size());
                cur.branch = std::move(b);
            } else if (line == L"bare") {
                cur.isBare = true;
            } else if (line == L"detached") {
                cur.isDetached = true;
            } else if (line == L"locked" || line.rfind(L"locked ", 0) == 0) {
                cur.isLocked = true;
            }
        }
    }
    flush();
    return out;
}

std::vector<GitWorkflowEngine::SubmoduleEntry> GitWorkflowEngine::ParseSubmoduleStatus(
    const std::wstring& statusOutput) {
    std::vector<SubmoduleEntry> out;
    std::wstringstream ss(statusOutput);
    std::wstring line;
    while (std::getline(ss, line)) {
        while (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.size() < 4) continue;
        SubmoduleEntry e;
        size_t pos = 0;
        if (line[0] == L'-' || line[0] == L'+' || line[0] == L'U' || line[0] == L' ') {
            e.state = line[0];
            pos = 1;
        }
        while (pos < line.size() && line[pos] == L' ') ++pos;
        size_t sp1 = line.find(L' ', pos);
        if (sp1 == std::wstring::npos) continue;
        e.hash = line.substr(pos, sp1 - pos);
        if (e.hash.size() > 7) e.hash = e.hash.substr(0, 7);
        size_t pathStart = sp1 + 1;
        while (pathStart < line.size() && line[pathStart] == L' ') ++pathStart;
        size_t sp2 = line.find(L' ', pathStart);
        if (sp2 == std::wstring::npos) {
            e.path = line.substr(pathStart);
        } else {
            e.path = line.substr(pathStart, sp2 - pathStart);
            std::wstring desc = line.substr(sp2 + 1);
            if (!desc.empty() && desc.front() == L'(' && desc.back() == L')' && desc.size() >= 2) {
                desc = desc.substr(1, desc.size() - 2);
            }
            e.refDesc = std::move(desc);
        }
        if (!e.path.empty()) out.push_back(std::move(e));
    }
    return out;
}

std::string GitWorkflowEngine::BuildInteractiveRebaseSequence(
    const std::vector<RebaseTodoItem>& items) {
    std::ostringstream oss;
    for (const auto& it : items) {
        std::string act = WideToUtf8String(it.action);
        std::string hash = WideToUtf8String(it.hash);
        if (hash.empty()) continue;
        if (act != "pick" && act != "reword" && act != "squash" && act != "fixup" && act != "drop" && act != "edit") {
            act = "pick";
        }
        std::string subj = WideToUtf8String(it.subject);
        for (char& c : subj) {
            if (c == '\r' || c == '\n') c = ' ';
        }
        oss << act << " " << hash;
        if (!subj.empty()) oss << " " << subj;
        oss << "\n";
    }
    return oss.str();
}

} // namespace phide::git
