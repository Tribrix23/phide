/**
 * @file Buffer.hpp
 * @brief In-memory text buffer with line-indexing, cursor tracking, and syntax tokenization.
 */

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "phide/editor/SyntaxToken.hpp"

namespace phide::editor {

/**
 * @enum TextEncoding
 * @brief Character encoding used when reading/writing a buffer from/to disk.
 */
enum class TextEncoding {
    UTF8 = 0,
    UTF8_BOM = 1,
    UTF16_LE = 2,
    UTF16_BE = 3,
    Windows1252 = 4
};

/**
 * @struct CursorPosition
 * @brief Zero-based line and column coordinates of the editor caret.
 */
struct CursorPosition {
    size_t line = 0;
    size_t col  = 0;
};

/**
 * @class Buffer
 * @brief Represents an open text document, providing fast line access and syntax highlights.
 */
class Buffer {
public:
    Buffer();
    explicit Buffer(const std::wstring& initialContent);

    /**
     * @brief Gets the number of lines in the document.
     */
    [[nodiscard]] size_t GetLineCount() const noexcept { return m_lines.size(); }
    [[nodiscard]] const std::vector<std::wstring>& GetLines() const noexcept { return m_lines; }
    [[nodiscard]] std::wstring GetText() const {
        std::wstring res;
        for (size_t i = 0; i < m_lines.size(); ++i) {
            res += m_lines[i];
            if (i + 1 < m_lines.size()) res += L"\n";
        }
        return res;
    }

    /**
     * @brief Retrieves the text content of a line by index.
     * @param lineIdx 0-based line number.
     * @return Const reference to the line string, or empty string if out of bounds.
     */
    [[nodiscard]] const std::wstring& GetLine(size_t lineIdx) const;

    /**
     * @brief Returns syntax tokens for the given line index.
     * @param lineIdx 0-based line number.
     * @param ext Optional file extension (e.g. ".css", ".js").
     * @return Vector of SyntaxToken descriptors.
     */
    [[nodiscard]] std::vector<SyntaxToken> GetTokensForLine(size_t lineIdx, const std::wstring& ext = L"") const;

    /**
     * @brief Gets the active cursor position.
     */
    [[nodiscard]] CursorPosition GetCursor() const noexcept { return m_cursor; }

    /**
     * @brief Sets the active cursor position, clamping to valid line/column bounds.
     */
    void SetCursor(size_t line, size_t col);

    /**
     * @brief Inserts a character at the current cursor position.
     * @param ch Wide character to insert.
     */
    void InsertChar(wchar_t ch);

    /**
     * @brief Deletes the character before the cursor (backspace).
     */
    void Backspace();

    /**
     * @brief Deletes the character at the cursor (Delete key).
     */
    void DeleteForward();

    /**
     * @brief Deletes an entire line at lineIdx in a single undoable operation.
     */
    void DeleteLine(size_t lineIdx);

    /**
     * @brief Deletes the text range [sLine:sCol, eLine:eCol] in a single undoable operation.
     */
    void DeleteRange(size_t sLine, size_t sCol, size_t eLine, size_t eCol);

    /**
     * @brief Navigates cursor up, down, left, right.
     */
    void MoveCursorLeft();
    void MoveCursorRight();
    void MoveCursorUp();
    void MoveCursorDown();

    /**
     * @brief Clears the buffer to empty state.
     */
    void Clear();

    /**
     * @brief Sets text content from lines or string.
     */
    void SetLines(const std::vector<std::wstring>& lines);
    void SetText(const std::wstring& text);
    void SetLine(size_t lineIdx, const std::wstring& text);
    void ReplaceLineSlice(size_t startLine, size_t removeCount, const std::vector<std::wstring>& insertLines);

    /**
     * @brief Loads text file from disk into buffer lines.
     */
    bool LoadFromFile(const std::wstring& path);

    /**
     * @brief Saves buffer lines to file on disk.
     */
    bool SaveToFile(const std::wstring& path);

    [[nodiscard]] bool HasTrailingNewline() const noexcept { return m_hasTrailingNewline; }
    void SetHasTrailingNewline(bool v) noexcept { m_hasTrailingNewline = v; }
    [[nodiscard]] const std::wstring& GetLineEnding() const noexcept { return m_lineEnding; }
    [[nodiscard]] std::wstring GetLineEndingDisplayName() const;
    void SetLineEnding(const std::wstring& le) { m_lineEnding = le; }
    bool ConvertLineEnding(const std::wstring& le);

    [[nodiscard]] TextEncoding GetEncoding() const noexcept { return m_encoding; }
    void SetEncoding(TextEncoding enc) noexcept { m_encoding = enc; }
    [[nodiscard]] std::wstring GetEncodingDisplayName() const;
    bool ConvertEncoding(TextEncoding newEnc);
    bool ReopenWithEncoding(TextEncoding enc);

    /**
     * @brief Performs undo and redo operations with compact line-delta history and memory budget.
     */
    bool Undo();
    bool Redo();
    void PushUndo();
    [[nodiscard]] size_t GetUndoCount() const noexcept;
    [[nodiscard]] size_t GetUndoMemoryBytes() const noexcept;

    /**
     * @brief Checks if there are actions available to undo or redo.
     */
    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept { return !m_redoStack.empty(); }

    /**
     * @brief Inserts a string (possibly multi-line) at the cursor.
     */
    void InsertText(const std::wstring& text);

    /**
     * @brief Line manipulation commands (Move Up/Down, Duplicate, Join, Sort, Trim).
     */
    bool MoveLinesUp(size_t sLine, size_t eLine, size_t& outNewSLine, size_t& outNewELine);
    bool MoveLinesDown(size_t sLine, size_t eLine, size_t& outNewSLine, size_t& outNewELine);
    bool DuplicateLines(size_t sLine, size_t eLine, bool up, size_t& outNewSLine, size_t& outNewELine);
    bool JoinLines(size_t sLine, size_t eLine, bool hasSel, CursorPosition& outCursor);
    bool SortLines(size_t sLine, size_t eLine, bool ascending, bool caseSensitive = false);
    bool TrimTrailingWhitespace(size_t sLine, size_t eLine);

    /**
     * @brief Toggles single-line comment on the current line or a range of lines.
     */
    void ToggleLineComment();
    bool ToggleLineCommentRange(size_t sLine, size_t eLine, const std::wstring& ext, std::vector<int>* outLineColDeltas = nullptr);

    /**
     * @brief Toggles block comment on the current line or an arbitrary multi-line selection.
     */
    void ToggleBlockComment();
    bool ToggleBlockCommentRange(size_t sLine, size_t sCol, size_t eLine, size_t eCol, bool hasSel, const std::wstring& ext,
                                 CursorPosition& outAnchor, CursorPosition& outCursor);

    /**
     * @brief Expands Emmet-style HTML tag abbreviation at cursor (e.g. div -> <div></div>).
     */
    bool ExpandEmmet(const std::wstring& fileExt = L"");

    /**
     * @brief Replaces prefix before cursor with snippet body lines, preserving indentation.
     */
    bool ReplacePrefixWithSnippet(size_t prefixLen, const std::vector<std::wstring>& bodyLines, int targetCursorLineOffset, int targetCursorCol);

    [[nodiscard]] const std::wstring& GetFilePath() const noexcept { return m_filePath; }
    void SetFilePath(const std::wstring& path) { m_filePath = path; }

    [[nodiscard]] const std::wstring& GetFileName() const noexcept { return m_fileName; }
    void SetFileName(const std::wstring& name) { m_fileName = name; }

    [[nodiscard]] bool IsModified() const noexcept { return m_isModified; }
    void SetModified(bool mod) noexcept { m_isModified = mod; }

    [[nodiscard]] uint64_t GetVersion() const noexcept { return m_version; }
    void IncrementVersion() noexcept { ++m_version; }

    /** @brief Manual Garbage Collector: compacts pending undo, reclaims vector capacities and trims memory. */
    void RunGarbageCollection();

private:
    std::vector<std::wstring> m_lines;
    CursorPosition m_cursor{ 0, 0 };
    SyntaxLexer m_lexer;

    std::wstring m_filePath;
    std::wstring m_fileName;
    bool m_isModified = false;
    uint64_t m_version = 0;
    bool m_hasTrailingNewline = true;
    std::wstring m_lineEnding = L"\n";
    TextEncoding m_encoding = TextEncoding::UTF8;

    // O(1) version-tracked per-line state and syntax token cache for large files
    mutable uint64_t m_cachedTokenVersion = UINT64_MAX;
    mutable std::wstring m_cachedTokenExt;
    mutable std::vector<uint8_t> m_lineEntryStates; // 0 = normal, 1 = blockComment, 2 = doubleQuote, 3 = singleQuote
    mutable std::unordered_map<size_t, std::vector<SyntaxToken>> m_tokenCache;

    struct UndoHunk {
        size_t startLine = 0;
        size_t afterCount = 0;
        std::vector<std::wstring> beforeLines;
        CursorPosition beforeCursor{ 0, 0 };
        CursorPosition afterCursor{ 0, 0 };
        std::wstring beforeLineEnding = L"\n";
        std::wstring afterLineEnding = L"\n";

        [[nodiscard]] size_t EstimateBytes() const noexcept;
    };

    static constexpr size_t kMaxUndoCount = 200;
    static constexpr size_t kMinRetainedUndo = 15;
    static constexpr size_t kMaxUndoMemoryBytes = 32ULL * 1024ULL * 1024ULL;

    std::vector<UndoHunk> m_undoStack;
    std::vector<UndoHunk> m_redoStack;
    size_t m_undoMemoryBytes = 0;
    size_t m_redoMemoryBytes = 0;

    bool m_hasPendingUndo = false;
    uint64_t m_pendingUndoVersion = 0;
    std::vector<std::wstring> m_pendingUndoLines;
    CursorPosition m_pendingUndoCursor{ 0, 0 };
    std::wstring m_pendingUndoLineEnding = L"\n";

    void CommitPendingUndo();
    void EnforceUndoBudget();
    static bool LoadBytesWithEncoding(const std::string& bytes, TextEncoding enc, std::wstring& outText);
    static bool EncodeTextBytes(const std::wstring& text, TextEncoding enc, std::string& outBytes);

    void LoadDefaultSample();
};

} // namespace phide::editor
