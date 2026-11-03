/**
 * @file BracketPairAnalyzer.hpp
 * @brief Analysis engine for Rainbow Brackets, Bracket Matching, and Connector Guide Lines.
 */

#pragma once

#include <vector>
#include <string>
#include <cwctype>
#include <algorithm>
#include "phide/editor/Buffer.hpp"

namespace phide::editor {

/**
 * @struct BracketItem
 * @brief Represents an individual bracket character in a document buffer.
 */
struct BracketItem {
    size_t line = 0;
    size_t col = 0;
    wchar_t ch = 0;
    int depth = 0;         ///< 0: Purple, 1: Yellow, 2: Cyan (repeats depth % 3)
    bool isOpening = true; ///< True for (, {, [; false for ), }, ]
    size_t matchLine = static_cast<size_t>(-1);
    size_t matchCol = static_cast<size_t>(-1);
};

/**
 * @struct BracketPair
 * @brief Represents a matched pair of opening and closing brackets with alignment metrics.
 */
struct BracketPair {
    size_t openLine = 0;
    size_t openCol = 0;
    wchar_t openChar = 0;
    size_t closeLine = 0;
    size_t closeCol = 0;
    wchar_t closeChar = 0;
    int depth = 0;
    size_t guideCol = 0;   ///< Indentation column for the vertical connector guide line
};

/**
 * @struct BracketAnalysisResult
 * @brief Holds full document bracket descriptors and the currently active bracket pair.
 */
struct BracketAnalysisResult {
    std::vector<BracketItem> brackets;
    std::vector<BracketPair> pairs;
    int activePairIdx = -1; ///< Index into pairs of the active/focused bracket pair, or -1
};

/**
 * @class BracketPairAnalyzer
 * @brief High-performance scanner that categorizes bracket hierarchies and guides.
 */
class BracketPairAnalyzer {
public:
    static bool IsBracket(wchar_t ch) noexcept {
        return ch == L'(' || ch == L')' ||
               ch == L'{' || ch == L'}' ||
               ch == L'[' || ch == L']';
    }

    static bool IsOpening(wchar_t ch) noexcept {
        return ch == L'(' || ch == L'{' || ch == L'[';
    }

    static bool Matches(wchar_t open, wchar_t close) noexcept {
        return (open == L'(' && close == L')') ||
               (open == L'{' && close == L'}') ||
               (open == L'[' && close == L']');
    }

    /**
     * @brief Analyzes all bracket pairs in the buffer, determines nesting depths,
     * and locates the active bracket pair enclosing or adjacent to the cursor.
     * @param buffer Document buffer reference.
     * @param cursorLine 0-based line index of editor cursor.
     * @param cursorCol 0-based column index of editor cursor.
     * @return Complete BracketAnalysisResult.
     */
    static BracketAnalysisResult Analyze(const Buffer& buffer, size_t cursorLine, size_t cursorCol) {
        BracketAnalysisResult result;
        const size_t totalLines = buffer.GetLineCount();
        if (totalLines == 0) return result;

        struct OpenStackItem {
            size_t line;
            size_t col;
            wchar_t ch;
            int depth;
            size_t bracketIdx;
        };
        std::vector<OpenStackItem> stack;

        for (size_t l = 0; l < totalLines; ++l) {
            const auto& lineStr = buffer.GetLine(l);
            if (lineStr.empty()) continue;

            auto tokens = buffer.GetTokensForLine(l);

            // Fast helper checking if a column position is inside a comment or string literal
            auto isInsideStringOrComment = [&](size_t col) -> bool {
                for (const auto& tok : tokens) {
                    if (tok.type == TokenType::String || tok.type == TokenType::Comment) {
                        if (col >= tok.startCol && col < tok.startCol + tok.length) {
                            return true;
                        }
                    }
                }
                return false;
            };

            for (size_t c = 0; c < lineStr.length(); ++c) {
                wchar_t ch = lineStr[c];
                if (!IsBracket(ch)) continue;
                if (isInsideStringOrComment(c)) continue;

                if (IsOpening(ch)) {
                    int depth = static_cast<int>(stack.size());
                    BracketItem item;
                    item.line = l;
                    item.col = c;
                    item.ch = ch;
                    item.depth = depth;
                    item.isOpening = true;
                    size_t itemIdx = result.brackets.size();
                    result.brackets.push_back(item);

                    stack.push_back({ l, c, ch, depth, itemIdx });
                } else {
                    // Closing bracket
                    if (!stack.empty()) {
                        int matchStackIdx = -1;
                        for (int s = static_cast<int>(stack.size()) - 1; s >= 0; --s) {
                            if (Matches(stack[s].ch, ch)) {
                                matchStackIdx = s;
                                break;
                            }
                        }

                        if (matchStackIdx >= 0) {
                            auto matched = stack[matchStackIdx];
                            stack.erase(stack.begin() + matchStackIdx, stack.end());

                            BracketItem closeItem;
                            closeItem.line = l;
                            closeItem.col = c;
                            closeItem.ch = ch;
                            closeItem.depth = matched.depth;
                            closeItem.isOpening = false;
                            closeItem.matchLine = matched.line;
                            closeItem.matchCol = matched.col;
                            result.brackets.push_back(closeItem);

                            // Link opening bracket to closing bracket
                            result.brackets[matched.bracketIdx].matchLine = l;
                            result.brackets[matched.bracketIdx].matchCol = c;

                            // Calculate indentation guide column for connector line
                            size_t guideCol = closeItem.col;
                            const auto& openLineStr = buffer.GetLine(matched.line);
                            size_t openIndent = 0;
                            while (openIndent < openLineStr.length() && iswspace(openLineStr[openIndent])) {
                                ++openIndent;
                            }
                            if (openIndent < guideCol) {
                                guideCol = openIndent;
                            }

                            BracketPair pair;
                            pair.openLine = matched.line;
                            pair.openCol = matched.col;
                            pair.openChar = matched.ch;
                            pair.closeLine = l;
                            pair.closeCol = c;
                            pair.closeChar = ch;
                            pair.depth = matched.depth;
                            pair.guideCol = guideCol;
                            result.pairs.push_back(pair);
                        } else {
                            BracketItem closeItem;
                            closeItem.line = l;
                            closeItem.col = c;
                            closeItem.ch = ch;
                            closeItem.depth = 0;
                            closeItem.isOpening = false;
                            result.brackets.push_back(closeItem);
                        }
                    } else {
                        BracketItem closeItem;
                        closeItem.line = l;
                        closeItem.col = c;
                        closeItem.ch = ch;
                        closeItem.depth = 0;
                        closeItem.isOpening = false;
                        result.brackets.push_back(closeItem);
                    }
                }
            }
        }

        // Determine active bracket pair (touches bracket first, then innermost enclosing)
        int directlyTouchedPair = -1;
        int innermostEnclosingPair = -1;
        size_t minLineSpan = static_cast<size_t>(-1);

        for (size_t p = 0; p < result.pairs.size(); ++p) {
            const auto& pair = result.pairs[p];
            bool touchOpen = (cursorLine == pair.openLine && (cursorCol == pair.openCol || (cursorCol > 0 && cursorCol - 1 == pair.openCol)));
            bool touchClose = (cursorLine == pair.closeLine && (cursorCol == pair.closeCol || (cursorCol > 0 && cursorCol - 1 == pair.closeCol)));

            if (touchOpen || touchClose) {
                directlyTouchedPair = static_cast<int>(p);
                break;
            }

            bool afterOpen = (cursorLine > pair.openLine) || (cursorLine == pair.openLine && cursorCol >= pair.openCol);
            bool beforeClose = (cursorLine < pair.closeLine) || (cursorLine == pair.closeLine && cursorCol <= pair.closeCol);
            if (afterOpen && beforeClose) {
                size_t span = pair.closeLine - pair.openLine;
                if (span < minLineSpan) {
                    minLineSpan = span;
                    innermostEnclosingPair = static_cast<int>(p);
                }
            }
        }

        if (directlyTouchedPair >= 0) {
            result.activePairIdx = directlyTouchedPair;
        } else if (innermostEnclosingPair >= 0) {
            result.activePairIdx = innermostEnclosingPair;
        }

        return result;
    }
};

} // namespace phide::editor
