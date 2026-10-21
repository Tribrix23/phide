/**
 * @file Buffer.hpp
 * @brief In-memory text buffer with line-indexing, cursor tracking, and syntax tokenization.
 */

#pragma once

#include <string>
#include <vector>
#include "phide/editor/SyntaxToken.hpp"

namespace phide::editor {

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

    /**
     * @brief Retrieves the text content of a line by index.
     * @param lineIdx 0-based line number.
     * @return Const reference to the line string, or empty string if out of bounds.
     */
    [[nodiscard]] const std::wstring& GetLine(size_t lineIdx) const;

    /**
     * @brief Returns syntax tokens for the given line index.
     * @param lineIdx 0-based line number.
     * @return Vector of SyntaxToken descriptors.
     */
    [[nodiscard]] std::vector<SyntaxToken> GetTokensForLine(size_t lineIdx) const;

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

    /**
     * @brief Loads text file from disk into buffer lines.
     */
    bool LoadFromFile(const std::wstring& path);

    /**
     * @brief Saves buffer lines to file on disk.
     */
    bool SaveToFile(const std::wstring& path);

    /**
     * @brief Performs undo and redo operations.
     */
    bool Undo();
    bool Redo();

    /**
     * @brief Inserts a string (possibly multi-line) at the cursor.
     */
    void InsertText(const std::wstring& text);

    [[nodiscard]] const std::wstring& GetFilePath() const noexcept { return m_filePath; }
    void SetFilePath(const std::wstring& path) { m_filePath = path; }

    [[nodiscard]] const std::wstring& GetFileName() const noexcept { return m_fileName; }
    void SetFileName(const std::wstring& name) { m_fileName = name; }

    [[nodiscard]] bool IsModified() const noexcept { return m_isModified; }
    void SetModified(bool mod) noexcept { m_isModified = mod; }

private:
    std::vector<std::wstring> m_lines;
    CursorPosition m_cursor{ 0, 0 };
    SyntaxLexer m_lexer;

    std::wstring m_filePath;
    std::wstring m_fileName;
    bool m_isModified = false;

    struct BufferSnapshot {
        std::vector<std::wstring> lines;
        CursorPosition cursor;
    };
    std::vector<BufferSnapshot> m_undoStack;
    std::vector<BufferSnapshot> m_redoStack;
    void PushUndo();

    void LoadDefaultSample();
};

} // namespace phide::editor
