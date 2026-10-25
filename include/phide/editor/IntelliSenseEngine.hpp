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
    Snippet,    ///< ⓢ Snippet / Template
    Folder,     ///< 📁 Folder / Directory
    File        ///< 📄 File
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
     * @brief Sets workspace root directory for resolving file path completions.
     */
    void SetWorkspaceRoot(const std::wstring& root) {
        m_workspaceRoot = root;
    }

    [[nodiscard]] const std::wstring& GetWorkspaceRoot() const noexcept {
        return m_workspaceRoot;
    }

    /**
     * @brief Queries code completions for a file based on file extension, prefix, and document buffer.
     */
    [[nodiscard]] std::vector<IntelliSenseItem> QueryCompletions(
        const std::wstring& fileName,
        const std::wstring& currentLine,
        size_t cursorCol,
        const std::vector<std::wstring>* pDocLines = nullptr
    ) const;

    /**
     * @brief Dynamically extracts user-defined variables and typed symbols from document lines.
     */
    [[nodiscard]] std::vector<IntelliSenseItem> ExtractDocumentSymbols(
        const std::vector<std::wstring>& docLines,
        const std::wstring& fileExt
    ) const;

    /**
     * @brief Returns the prefix query token currently being typed under cursor.
     */
    static std::wstring ExtractQueryPrefix(const std::wstring& line, size_t cursorCol);

    /**
     * @brief Extracts an Emmet abbreviation string from line ending at cursorCol.
     */
    static std::wstring ExtractEmmetPrefix(const std::wstring& line, size_t cursorCol);

    /**
     * @brief Expands an Emmet abbreviation into full structured HTML.
     */
    static std::wstring ExpandEmmet(const std::wstring& abbr);

private:
    void PopulateHtmlTags();
    void PopulateHtmlAttributes();
    void PopulateCssProperties();
    void PopulateCssValues();
    void PopulateJavaScriptKeywords();
    void PopulateJavaScriptGlobals();
    void PopulateJavaScriptDomApis();
    void PopulateJavaScriptPrototypeMethods();
    void PopulateCommonVocabulary();

    std::vector<IntelliSenseItem> m_htmlTags;
    std::vector<IntelliSenseItem> m_htmlAttributes;
    std::vector<IntelliSenseItem> m_cssProperties;
    std::vector<IntelliSenseItem> m_cssValues;
    std::vector<IntelliSenseItem> m_jsKeywords;
    std::vector<IntelliSenseItem> m_jsGlobals;
    std::vector<IntelliSenseItem> m_jsDomApis;
    std::vector<IntelliSenseItem> m_jsPrototypes;
    std::vector<IntelliSenseItem> m_commonVocabulary;
    const SnippetManager* m_pSnippetManager = nullptr;
    std::wstring m_workspaceRoot;
};

} // namespace phide::editor
