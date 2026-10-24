#pragma once

#include <string>
#include <vector>

namespace phide::editor {

/**
 * @brief Categorization of code completion proposals for authentic icon badges.
 */
enum class CompletionKind {
    Property,   ///< ⓟ Property
    Method,     ///< ⓜ Method
    Function,   ///< ⓕ Function
    Variable,   ///< ⓥ Variable
    Keyword,    ///< ⓚ Keyword
    Tag,        ///< ⓣ HTML / XML Element Tag
    Attribute,  ///< ⓐ HTML Attribute
    Snippet     ///< ⓢ Snippet / Template
};

/**
 * @brief Representation of an IntelliSense code completion proposal.
 */
struct IntelliSenseItem {
    std::wstring label;
    std::wstring detail;
    std::wstring insertText;
    CompletionKind kind = CompletionKind::Keyword;
    int score = 0;
    size_t matchStart = 0;
    size_t matchLen = 0;
    std::wstring origin; ///< Provider origin / source (e.g. L"snippets", L"builtins", L"html", L"css")
};

class SnippetManager;

/**
 * @brief Massive complete HTML5 and JavaScript/TypeScript IntelliSense knowledge engine.
 */
class IntelliSenseEngine {
public:
    IntelliSenseEngine();

    /**
     * @brief Attaches SnippetManager for exposing snippet proposals in autocomplete.
     */
    void SetSnippetManager(const SnippetManager* pSnippets) noexcept {
        m_pSnippetManager = pSnippets;
    }

    /**
     * @brief Queries code completions for a file based on file extension and prefix.
     */
    [[nodiscard]] std::vector<IntelliSenseItem> QueryCompletions(
        const std::wstring& fileName,
        const std::wstring& currentLine,
        size_t cursorCol
    ) const;

    /**
     * @brief Returns the prefix query token currently being typed under cursor.
     */
    static std::wstring ExtractQueryPrefix(const std::wstring& line, size_t cursorCol);

private:
    void PopulateHtmlTags();
    void PopulateHtmlAttributes();
    void PopulateJavaScriptKeywords();
    void PopulateJavaScriptGlobals();
    void PopulateJavaScriptDomApis();
    void PopulateJavaScriptPrototypeMethods();
    void PopulateCommonVocabulary();

    std::vector<IntelliSenseItem> m_htmlTags;
    std::vector<IntelliSenseItem> m_htmlAttributes;
    std::vector<IntelliSenseItem> m_jsKeywords;
    std::vector<IntelliSenseItem> m_jsGlobals;
    std::vector<IntelliSenseItem> m_jsDomApis;
    std::vector<IntelliSenseItem> m_jsPrototypes;
    std::vector<IntelliSenseItem> m_commonVocabulary;
    const SnippetManager* m_pSnippetManager = nullptr;
};

} // namespace phide::editor
