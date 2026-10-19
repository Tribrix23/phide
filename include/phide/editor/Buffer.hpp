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

private:
    std::vector<std::wstring> m_lines;
    CursorPosition m_cursor{ 8, 4 };
    SyntaxLexer m_lexer;

    void LoadDefaultSample();
};

} // namespace phide::editor
