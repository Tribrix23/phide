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

} // namespace phide::git
