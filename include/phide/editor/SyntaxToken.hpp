/**
 * @file SyntaxToken.hpp
 * @brief Token types and keyword registries for syntax highlighting in PHP and C++.
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_set>

namespace phide::editor {

/**
 * @enum TokenType
 * @brief Categorizes tokens for visual syntax coloring.
 */
enum class TokenType {
    Default,
    Keyword,    ///< Language keywords (e.g., class, public, function, return, if, namespace)
    Type,       ///< Primitive and standard types (e.g., int, void, string, std::vector)
    String,     ///< String literals ("...", '...')
    Comment,    ///< Line comments (//, #) and block comments
    Number,     ///< Numeric literals
    Operator    ///< Operators and punctuation
};

/**
 * @struct SyntaxToken
 * @brief Represents a single colored span within a source line.
 */
struct SyntaxToken {
    TokenType type = TokenType::Default;
    size_t startCol = 0;
    size_t length = 0;
};

/**
 * @class SyntaxLexer
 * @brief Lightweight line-by-line syntax lexer supporting PHP and C++.
 */
class SyntaxLexer {
public:
    SyntaxLexer();

    /**
     * @brief Tokenizes a single line of code into colored spans.
     * @param line Text content of the line.
     * @return Vector of SyntaxToken descriptors.
     */
    std::vector<SyntaxToken> TokenizeLine(const std::wstring& line) const;

private:
    std::unordered_set<std::wstring> m_keywords;
    std::unordered_set<std::wstring> m_types;
};

} // namespace phide::editor
