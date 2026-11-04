/**
 * @file Buffer.cpp
 * @brief Implementation of the text buffer and syntax lexer.
 */

#include "phide/editor/Buffer.hpp"
#include <windows.h>
#include <cwctype>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unordered_set>

namespace phide::editor {

// --- SyntaxLexer Implementation ---

SyntaxLexer::SyntaxLexer() {
    // PHP, JS/TS & C++ Keywords (matching user requirements for <?php, var, let, require, etc.)
    m_keywords = {
        L"class", L"function", L"public", L"private", L"protected",
        L"return", L"namespace", L"use", L"echo", L"new", L"if", L"else",
        L"while", L"for", L"foreach", L"switch", L"case", L"break", L"default",
        L"continue", L"goto", L"try", L"catch", L"finally", L"throw", L"static",
        L"const", L"auto", L"template", L"typename", L"struct", L"enum", L"virtual",
        L"override", L"final", L"constexpr", L"explicit", L"inline", L"noexcept",
        L"var", L"let", L"require", L"import", L"export", L"from", L"as", L"async",
        L"await", L"yield", L"fn", L"def", L"print", L"die", L"exit", L"isset",
        L"empty", L"include", L"include_once", L"require_once", L"declare",
        L"strict_types", L"match", L"global", L"extends", L"implements", L"interface",
        L"trait", L"abstract", L"readonly", L"null", L"nullptr", L"true", L"false",
        L"this", L"self", L"parent", L"delete", L"sizeof", L"typedef"
    };

    // Common Primitive & Standard Types + Built-in PHP/JS Classes
    m_types = {
        L"string", L"int", L"bool", L"float", L"void", L"array",
        L"object", L"mixed", L"iterable", L"callable", L"never",
        L"std::string", L"std::wstring", L"std::vector", L"std::map", L"std::set",
        L"uint32_t", L"uint64_t", L"size_t", L"int32_t", L"int64_t", L"double",
        L"char", L"wchar_t",
        L"PDO", L"PDOStatement", L"PDOException", L"Exception", L"Error",
        L"Throwable", L"ErrorException", L"RuntimeException", L"InvalidArgumentException",
        L"LogicException", L"OutOfBoundsException", L"UnexpectedValueException",
        L"JsonException", L"DateTime", L"DateTimeImmutable", L"DateTimeZone",
        L"DateInterval", L"DatePeriod", L"mysqli", L"mysqli_stmt", L"mysqli_result",
        L"mysqli_sql_exception", L"stdClass", L"Closure", L"Generator", L"Fiber",
        L"WeakMap", L"WeakReference", L"ArrayObject", L"ArrayIterator",
        L"DirectoryIterator", L"RecursiveDirectoryIterator", L"RecursiveIteratorIterator",
        L"FilesystemIterator", L"SplFileInfo", L"SplFileObject", L"ReflectionClass",
        L"ReflectionMethod", L"ReflectionProperty", L"DOMDocument", L"SimpleXMLElement"
    };
}

static std::vector<SyntaxToken> TokenizeCssLine(const std::wstring& line) {
    std::vector<SyntaxToken> tokens;
    if (line.empty()) return tokens;

    size_t i = 0;
    const size_t len = line.length();

    while (i < len) {
        if (iswspace(line[i])) {
            ++i;
            continue;
        }

        // 1. Comments /* ... */
        if (i + 1 < len && line[i] == L'/' && line[i + 1] == L'*') {
            size_t start = i;
            auto endPos = line.find(L"*/", i + 2);
            if (endPos != std::wstring::npos) {
                i = endPos + 2;
            } else {
                i = len;
            }
            tokens.push_back({ TokenType::Comment, start, i - start });
            continue;
        }

        // 2. Single line comment // (for scss/less)
        if (i + 1 < len && line[i] == L'/' && line[i + 1] == L'/') {
            tokens.push_back({ TokenType::Comment, i, len - i });
            break;
        }

        // 3. Strings "..." or '...'
        if (line[i] == L'"' || line[i] == L'\'') {
            wchar_t quote = line[i];
            size_t start = i++;
            while (i < len && line[i] != quote) {
                if (line[i] == L'\\' && i + 1 < len) i += 2;
                else ++i;
            }
            if (i < len) ++i;
            tokens.push_back({ TokenType::String, start, i - start });
            continue;
        }

        // 4. Universal selector *
        if (line[i] == L'*') {
            tokens.push_back({ TokenType::Tag, i, 1 });
            ++i;
            continue;
        }

        // 5. Class selector .class-name or dot selector '.'
        if (line[i] == L'.') {
            size_t start = i++;
            while (i < len && (iswalnum(line[i]) || line[i] == L'-' || line[i] == L'_')) ++i;
            tokens.push_back({ TokenType::Tag, start, i - start });
            continue;
        }

        // 6. ID selector #id or Hex color #fff or hash selector '#'
        if (line[i] == L'#') {
            size_t start = i++;
            while (i < len && (iswalnum(line[i]) || line[i] == L'-' || line[i] == L'_')) ++i;
            size_t tokenLen = i - start;
            bool isHex = (tokenLen == 4 || tokenLen == 5 || tokenLen == 7 || tokenLen == 9);
            if (isHex) {
                for (size_t k = start + 1; k < i; ++k) {
                    if (!iswxdigit(line[k])) { isHex = false; break; }
                }
            }
            if (isHex) {
                tokens.push_back({ TokenType::Number, start, tokenLen });
            } else {
                tokens.push_back({ TokenType::Tag, start, tokenLen });
            }
            continue;
        }

        // 7. Pseudo-classes & pseudo-elements: ::before, :hover
        if (line[i] == L':' && i + 1 < len && (line[i + 1] == L':' || iswalpha(line[i + 1]) || line[i + 1] == L'-')) {
            size_t start = i;
            if (line[i + 1] == L':') i += 2;
            else i += 1;
            while (i < len && (iswalnum(line[i]) || line[i] == L'-')) ++i;
            tokens.push_back({ TokenType::Keyword, start, i - start });
            continue;
        }

        // 8. At-rules: @keyframes, @media, @import
        if (line[i] == L'@') {
            size_t start = i++;
            while (i < len && (iswalnum(line[i]) || line[i] == L'-')) ++i;
            tokens.push_back({ TokenType::Keyword, start, i - start });
            continue;
        }

        // 9. Numeric literals with optional unit: 100%, 15px, 1.75, 8s, 0
        if (iswdigit(line[i]) || (line[i] == L'.' && i + 1 < len && iswdigit(line[i + 1]))) {
            size_t start = i++;
            while (i < len && (iswalnum(line[i]) || line[i] == L'.' || line[i] == L'%')) ++i;
            tokens.push_back({ TokenType::Number, start, i - start });
            continue;
        }

        // 10. Identifiers (Property names, HTML tag names, CSS functions, values)
        if (iswalpha(line[i]) || line[i] == L'-' || line[i] == L'_') {
            size_t start = i++;
            while (i < len && (iswalnum(line[i]) || line[i] == L'-' || line[i] == L'_')) ++i;
            std::wstring word = line.substr(start, i - start);

            size_t p = i;
            while (p < len && iswspace(line[p])) p++;

            if (p < len && line[p] == L'(') {
                // Function call: var(), clamp(), calc(), url()
                tokens.push_back({ TokenType::Keyword, start, i - start });
            } else if (p < len && line[p] == L':') {
                // CSS property: color:, display:, font-size:, --var:
                tokens.push_back({ TokenType::Attribute, start, i - start });
            } else if (word.rfind(L"--", 0) == 0) {
                // CSS custom property variable reference: --background inside var(--background)
                tokens.push_back({ TokenType::Attribute, start, i - start });
            } else {
                static const std::unordered_set<std::wstring> htmlTags = {
                    L"div", L"span", L"h1", L"h2", L"h3", L"h4", L"h5", L"h6",
                    L"p", L"a", L"svg", L"path", L"body", L"html", L"ul", L"ol", L"li",
                    L"button", L"input", L"form", L"table", L"tr", L"td", L"th",
                    L"header", L"footer", L"nav", L"section", L"main", L"aside", L"img"
                };
                if (htmlTags.count(word) > 0) {
                    tokens.push_back({ TokenType::Tag, start, i - start });
                } else if (word == L"important") {
                    tokens.push_back({ TokenType::Keyword, start, i - start });
                } else {
                    tokens.push_back({ TokenType::Default, start, i - start });
                }
            }
            continue;
        }

        // 11. Operators and punctuation
        tokens.push_back({ TokenType::Operator, i, 1 });
        ++i;
    }

    return tokens;
}

static std::vector<SyntaxToken> TokenizeSqlLine(const std::wstring& line) {
    std::vector<SyntaxToken> tokens;
    if (line.empty()) return tokens;

    static const std::unordered_set<std::wstring> sqlKeywords = {
        L"select", L"from", L"where", L"insert", L"into", L"values", L"update", L"set", L"delete",
        L"create", L"database", L"databases", L"schema", L"schemas", L"table", L"tables", L"view", L"views",
        L"index", L"indexes", L"trigger", L"triggers", L"procedure", L"procedures", L"function", L"functions",
        L"alter", L"drop", L"truncate", L"rename", L"add", L"column", L"columns", L"modify", L"change",
        L"if", L"not", L"exists", L"use", L"default", L"primary", L"key", L"unique", L"foreign", L"references",
        L"auto_increment", L"cascade", L"check", L"constraint", L"collate", L"character",
        L"engine", L"charset", L"join", L"inner", L"left", L"right", L"full", L"outer", L"cross", L"natural", L"on", L"using",
        L"group", L"by", L"order", L"having", L"limit", L"offset", L"union", L"all", L"distinct", L"as",
        L"and", L"or", L"xor", L"in", L"is", L"null", L"like", L"ilike", L"between", L"case", L"when", L"then", L"else", L"end",
        L"begin", L"commit", L"rollback", L"transaction", L"savepoint", L"grant", L"revoke",
        L"show", L"describe", L"desc", L"asc", L"explain", L"pragma", L"declare", L"cursor", L"fetch", L"open", L"close",
        L"current_date", L"current_time", L"current_timestamp", L"utc_timestamp", L"now", L"count", L"sum", L"avg", L"min", L"max", L"coalesce",
        L"true", L"false", L"replace", L"with", L"recursive", L"lock", L"unlock", L"temporary", L"temp"
    };

    static const std::unordered_set<std::wstring> sqlTypes = {
        L"int", L"integer", L"tinyint", L"smallint", L"mediumint", L"bigint",
        L"varchar", L"char", L"text", L"tinytext", L"mediumtext", L"longtext",
        L"blob", L"tinyblob", L"mediumblob", L"longblob",
        L"decimal", L"numeric", L"float", L"double", L"real", L"fixed",
        L"boolean", L"bool", L"bit",
        L"date", L"datetime", L"timestamp", L"time", L"year", L"interval",
        L"json", L"binary", L"varbinary", L"enum", L"set",
        L"unsigned", L"signed", L"zerofill", L"uuid", L"bytea", L"nvarchar", L"nchar", L"clob"
    };

    size_t i = 0;
    const size_t len = line.length();

    while (i < len) {
        if (iswspace(line[i])) {
            ++i;
            continue;
        }

        // 1. Single-line comments: -- or #
        if ((i + 1 < len && line[i] == L'-' && line[i + 1] == L'-') || line[i] == L'#') {
            tokens.push_back({ TokenType::Comment, i, len - i });
            break;
        }

        // 2. Block comments: /* ... */
        if (i + 1 < len && line[i] == L'/' && line[i + 1] == L'*') {
            size_t start = i;
            auto endPos = line.find(L"*/", i + 2);
            if (endPos != std::wstring::npos) {
                i = endPos + 2;
            } else {
                i = len;
            }
            tokens.push_back({ TokenType::Comment, start, i - start });
            continue;
        }

        // 3. String literals: '...' or "..." (supports escaping and doubled quotes)
        if (line[i] == L'\'' || line[i] == L'"') {
            wchar_t quote = line[i];
            size_t start = i++;
            while (i < len) {
                if (line[i] == L'\\' && i + 1 < len) {
                    i += 2;
                } else if (line[i] == quote) {
                    if (i + 1 < len && line[i + 1] == quote) {
                        i += 2;
                    } else {
                        ++i;
                        break;
                    }
                } else {
                    ++i;
                }
            }
            tokens.push_back({ TokenType::String, start, i - start });
            continue;
        }

        // 4. Quoted Identifiers: `...` or [...]
        if (line[i] == L'`' || line[i] == L'[') {
            wchar_t closeChar = (line[i] == L'`') ? L'`' : L']';
            size_t start = i++;
            while (i < len && line[i] != closeChar) {
                if (line[i] == L'\\' && i + 1 < len) i += 2;
                else ++i;
            }
            if (i < len) ++i;
            tokens.push_back({ TokenType::Attribute, start, i - start });
            continue;
        }

        // 5. Numeric literals: integer, decimal, hex (0x...)
        if (iswdigit(line[i]) || (line[i] == L'.' && i + 1 < len && iswdigit(line[i + 1]))) {
            size_t start = i++;
            if (start + 1 < len && line[start] == L'0' && (line[start + 1] == L'x' || line[start + 1] == L'X')) {
                i = start + 2;
                while (i < len && iswxdigit(line[i])) ++i;
            } else {
                while (i < len && (iswalnum(line[i]) || line[i] == L'.')) ++i;
            }
            tokens.push_back({ TokenType::Number, start, i - start });
            continue;
        }

        // 6. Identifiers / Keywords / Types
        if (iswalpha(line[i]) || line[i] == L'_' || line[i] == L'@') {
            size_t start = i++;
            while (i < len && (iswalnum(line[i]) || line[i] == L'_' || line[i] == L'$')) ++i;
            std::wstring word = line.substr(start, i - start);

            std::wstring lowerWord = word;
            for (auto& c : lowerWord) c = towlower(c);

            if (sqlKeywords.find(lowerWord) != sqlKeywords.end()) {
                tokens.push_back({ TokenType::Keyword, start, i - start });
            } else if (sqlTypes.find(lowerWord) != sqlTypes.end()) {
                tokens.push_back({ TokenType::Type, start, i - start });
            } else {
                tokens.push_back({ TokenType::Default, start, i - start });
            }
            continue;
        }

        // 7. Multi-character operators
        if (i + 1 < len) {
            std::wstring twoChar = line.substr(i, 2);
            if (twoChar == L"<=" || twoChar == L">=" || twoChar == L"<>" ||
                twoChar == L"!=" || twoChar == L":=" || twoChar == L"==" ||
                twoChar == L"||" || twoChar == L"&&") {
                tokens.push_back({ TokenType::Operator, i, 2 });
                i += 2;
                continue;
            }
        }

        // 8. Single-character operators & punctuation
        tokens.push_back({ TokenType::Operator, i, 1 });
        ++i;
    }

    return tokens;
}

std::vector<SyntaxToken> SyntaxLexer::TokenizeLine(const std::wstring& line, const std::wstring& ext) const {
    std::wstring lowerExt = ext;
    for (auto& c : lowerExt) c = towlower(c);

    if (lowerExt == L".css" || lowerExt == L".scss" || lowerExt == L".less" ||
        lowerExt == L"css" || lowerExt == L"scss" || lowerExt == L"less") {
        return TokenizeCssLine(line);
    }
    if (lowerExt == L".sql" || lowerExt == L"sql") {
        return TokenizeSqlLine(line);
    }

    std::vector<SyntaxToken> tokens;
    if (line.empty()) return tokens;

    size_t i = 0;
    const size_t len = line.length();

    while (i < len) {
        // Skip whitespace
        if (iswspace(line[i])) {
            ++i;
            continue;
        }

        // 1. PHP Open Tags: <?php, <?=, <?
        if (i + 1 < len && line[i] == L'<' && line[i + 1] == L'?') {
            size_t start = i;
            i += 2;
            if (i + 3 <= len && line.substr(i, 3) == L"php") {
                i += 3;
            } else if (i < len && line[i] == L'=') {
                i += 1;
            }
            tokens.push_back({ TokenType::Keyword, start, i - start });
            continue;
        }

        // 2. PHP Close Tag: ?>
        if (i + 1 < len && line[i] == L'?' && line[i + 1] == L'>') {
            tokens.push_back({ TokenType::Keyword, i, 2 });
            i += 2;
            continue;
        }

        // 3. HTML/XML Comment (<!-- ... -->)
        if (i + 3 < len && line.substr(i, 4) == L"<!--") {
            size_t start = i;
            auto endPos = line.find(L"-->", i + 4);
            if (endPos != std::wstring::npos) {
                i = endPos + 3;
            } else {
                i = len;
            }
            tokens.push_back({ TokenType::Comment, start, i - start });
            continue;
        }

        // 4. HTML DOCTYPE (<!DOCTYPE ...>)
        if (i + 8 < len && _wcsnicmp(line.c_str() + i, L"<!DOCTYPE", 9) == 0) {
            size_t start = i;
            auto endPos = line.find(L'>', i + 9);
            if (endPos != std::wstring::npos) {
                i = endPos + 1;
            } else {
                i = len;
            }
            tokens.push_back({ TokenType::Keyword, start, i - start });
            continue;
        }

        // 5. HTML / XML / JSX Tags (<tag ...>, </tag>)
        if (line[i] == L'<' && (i + 1 < len && (iswalpha(line[i + 1]) || (line[i + 1] == L'/' && i + 2 < len && iswalpha(line[i + 2]))))) {
            // Opening bracket < or </
            size_t bracketStart = i;
            if (line[i + 1] == L'/') i += 2;
            else i += 1;
            tokens.push_back({ TokenType::Operator, bracketStart, i - bracketStart });

            // Tag name (e.g. div, span, form, input, Button, Component)
            size_t tagStart = i;
            while (i < len && (iswalnum(line[i]) || line[i] == L'-' || line[i] == L':' || line[i] == L'_')) ++i;
            tokens.push_back({ TokenType::Tag, tagStart, i - tagStart });

            // Attributes inside the tag until > or end of line
            while (i < len && line[i] != L'>') {
                if (iswspace(line[i])) {
                    ++i;
                    continue;
                }
                // Self-closing slash />
                if (line[i] == L'/' && i + 1 < len && line[i + 1] == L'>') {
                    tokens.push_back({ TokenType::Operator, i, 2 });
                    i += 2;
                    break;
                }
                // Attribute name (e.g. class, id, href, src, onClick)
                if (iswalpha(line[i]) || line[i] == L'@' || line[i] == L':' || line[i] == L'-' || line[i] == L'_') {
                    size_t attrStart = i++;
                    while (i < len && (iswalnum(line[i]) || line[i] == L'-' || line[i] == L':' || line[i] == L'_' || line[i] == L'.')) ++i;
                    tokens.push_back({ TokenType::Attribute, attrStart, i - attrStart });
                    continue;
                }
                // Attribute string value ("..." or '...')
                if (line[i] == L'"' || line[i] == L'\'') {
                    wchar_t quote = line[i];
                    size_t strStart = i++;
                    while (i < len && line[i] != quote) {
                        if (line[i] == L'\\' && i + 1 < len) i += 2;
                        else ++i;
                    }
                    if (i < len) ++i;
                    tokens.push_back({ TokenType::String, strStart, i - strStart });
                    continue;
                }
                // Equals sign or other punctuation inside tag
                tokens.push_back({ TokenType::Operator, i, 1 });
                ++i;
            }

            if (i < len && line[i] == L'>') {
                tokens.push_back({ TokenType::Operator, i, 1 });
                ++i;
            }
            continue;
        }

        // 6. Single-line Comments (// or #)
        if ((i + 1 < len && line[i] == L'/' && line[i + 1] == L'/') || (line[i] == L'#' && (ext == L".py" || ext == L".sh" || ext == L".bash" || ext == L".php" || ext.empty()))) {
            tokens.push_back({ TokenType::Comment, i, len - i });
            break;
        }

        // 7. Block Comment opening or inside (/* ...)
        if (i + 1 < len && line[i] == L'/' && line[i + 1] == L'*') {
            tokens.push_back({ TokenType::Comment, i, len - i });
            break;
        }

        // 8. String literals ("..." or '...' or `...`) with PHP "$var" interpolation highlighting
        if (line[i] == L'"' || line[i] == L'\'' || line[i] == L'`') {
            wchar_t quote = line[i];
            bool allowVarInterp = (quote == L'"' && (lowerExt == L".php" || lowerExt == L"php" || lowerExt.empty()));
            size_t segStart = i++;
            while (i < len && line[i] != quote) {
                if (line[i] == L'\\' && i + 1 < len) {
                    i += 2;
                    continue;
                }
                if (allowVarInterp && line[i] == L'$' && i + 1 < len && (iswalpha(line[i + 1]) || line[i + 1] == L'_')) {
                    if (i > segStart) {
                        tokens.push_back({ TokenType::String, segStart, i - segStart });
                    }
                    size_t varStart = i++;
                    while (i < len && (iswalnum(line[i]) || line[i] == L'_')) ++i;
                    tokens.push_back({ TokenType::Attribute, varStart, i - varStart });
                    segStart = i;
                    continue;
                }
                ++i;
            }
            if (i < len) ++i; // Consume closing quote
            if (i > segStart) {
                tokens.push_back({ TokenType::String, segStart, i - segStart });
            }
            continue;
        }

        // 9. Numeric Literals
        if (iswdigit(line[i])) {
            size_t start = i++;
            while (i < len && (iswalnum(line[i]) || line[i] == L'.')) ++i;
            tokens.push_back({ TokenType::Number, start, i - start });
            continue;
        }

        // 10. Identifiers, Keywords, Classes, Constants, Functions, and PHP $Variables
        if (iswalpha(line[i]) || line[i] == L'_' || line[i] == L'$') {
            size_t start = i++;
            while (i < len && (iswalnum(line[i]) || line[i] == L'_')) ++i;

            std::wstring word = line.substr(start, i - start);
            if (word[0] == L'$') {
                // PHP Variable ($host, $pdo, $_SERVER, $_POST, etc.) -> VS Code Variable Light Blue (#9CDCFE / TokenType::Attribute)
                if (word == L"$this") {
                    tokens.push_back({ TokenType::Keyword, start, i - start });
                } else {
                    tokens.push_back({ TokenType::Attribute, start, i - start });
                }
                continue;
            }

            // Check if followed by '(' (function or method call)
            size_t nextNonSpace = i;
            while (nextNonSpace < len && (line[nextNonSpace] == L' ' || line[nextNonSpace] == L'\t')) ++nextNonSpace;
            bool isCall = (nextNonSpace < len && line[nextNonSpace] == L'(');
            bool isScopeClass = (i + 1 < len && line[i] == L':' && line[i + 1] == L':');

            // Check if ALL_CAPS constant (e.g. ATTR_ERRMODE, FILTER_VALIDATE_EMAIL, ENT_QUOTES, __DIR__)
            bool isAllCaps = (word.length() >= 2);
            bool hasAlpha = false;
            for (wchar_t wc : word) {
                if (iswlower(wc)) { isAllCaps = false; break; }
                if (iswupper(wc)) hasAlpha = true;
            }
            if (!hasAlpha) isAllCaps = false;

            if (m_keywords.find(word) != m_keywords.end()) {
                tokens.push_back({ TokenType::Keyword, start, i - start });
            } else if (m_types.find(word) != m_types.end() || isScopeClass) {
                tokens.push_back({ TokenType::Type, start, i - start });
            } else if (isAllCaps) {
                tokens.push_back({ TokenType::Tag, start, i - start });
            } else if (isCall) {
                tokens.push_back({ TokenType::Type, start, i - start });
            } else if (!word.empty() && iswupper(word[0])) {
                tokens.push_back({ TokenType::Type, start, i - start });
            } else {
                tokens.push_back({ TokenType::Default, start, i - start });
            }
            continue;
        }

        // Operators & Punctuation
        tokens.push_back({ TokenType::Operator, i, 1 });
        ++i;
    }

    return tokens;
}

// --- Buffer Implementation ---

Buffer::Buffer() {
    m_lines = { L"" };
    m_cursor = { 0, 0 };
    m_isModified = false;
}

Buffer::Buffer(const std::wstring& initialContent) {
    if (initialContent.empty()) {
        m_lines = { L"" };
        m_cursor = { 0, 0 };
        m_isModified = false;
    } else {
        SetText(initialContent);
    }
}

void Buffer::PushUndo() {
    if (!m_undoStack.empty() && m_undoStack.back().lines == m_lines) {
        m_undoStack.back().cursor = m_cursor;
        return;
    }
    m_undoStack.push_back({ m_lines, m_cursor });
    if (m_undoStack.size() > 2500) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
    ++m_version;
}

bool Buffer::Undo() {
    if (m_undoStack.empty()) return false;
    m_redoStack.push_back({ m_lines, m_cursor });
    auto last = m_undoStack.back();
    m_undoStack.pop_back();
    m_lines = last.lines;
    m_cursor = last.cursor;
    m_isModified = true;
    ++m_version;
    return true;
}

bool Buffer::Redo() {
    if (m_redoStack.empty()) return false;
    m_undoStack.push_back({ m_lines, m_cursor });
    auto next = m_redoStack.back();
    m_redoStack.pop_back();
    m_lines = next.lines;
    m_cursor = next.cursor;
    m_isModified = true;
    ++m_version;
    return true;
}

void Buffer::InsertText(const std::wstring& text) {
    if (text.empty()) return;
    PushUndo();
    for (wchar_t ch : text) {
        if (ch == L'\r') continue;
        if (m_lines.empty()) m_lines.push_back(L"");
        if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;

        auto& line = m_lines[m_cursor.line];
        if (m_cursor.col > line.length()) m_cursor.col = line.length();

        if (ch == L'\n') {
            std::wstring remainder = line.substr(m_cursor.col);
            line = line.substr(0, m_cursor.col);
            m_lines.insert(m_lines.begin() + m_cursor.line + 1, remainder);
            m_cursor.line++;
            m_cursor.col = 0;
        } else {
            line.insert(line.begin() + m_cursor.col, ch);
            m_cursor.col++;
        }
    }
    m_isModified = true;
}

void Buffer::LoadDefaultSample() {
    m_lines = {
        L"<?php",
        L"namespace Phide\\Core;",
        L"",
        L"/**",
        L" * EchoForge Engine - Next Generation PHP & C++ IDE",
        L" */",
        L"class EchoIDE {",
        L"    private string $workspace;",
        L"    private bool $isLspActive = true;",
        L"",
        L"    public function __construct(string $workspace) {",
        L"        $this->workspace = $workspace;",
        L"        echo \"Echo IDE initialized: \" . $this->workspace . \"\\n\";",
        L"    }",
        L"",
        L"    public function startLanguageServer(): void {",
        L"        // Launch Intelephense / Clangd via asynchronous JSON-RPC pipes",
        L"        $this->isLspActive = true;",
        L"    }",
        L"}"
    };
    m_cursor = { 12, 10 };
}

const std::wstring& Buffer::GetLine(size_t lineIdx) const {
    static const std::wstring emptyLine;
    if (lineIdx < m_lines.size()) {
        return m_lines[lineIdx];
    }
    return emptyLine;
}

std::vector<SyntaxToken> Buffer::GetTokensForLine(size_t lineIdx, const std::wstring& ext) const {
    if (lineIdx >= m_lines.size()) {
        return {};
    }

    bool isPhp = (ext == L".php");
    bool inBlockComment = false;
    wchar_t inMultiLineQuote = 0;

    // Track multi-line block comments (/* ... */) and PHP multi-line string literals across preceding lines
    for (size_t l = 0; l < lineIdx; ++l) {
        const auto& prevLine = m_lines[l];
        size_t len = prevLine.length();
        for (size_t i = 0; i < len; ++i) {
            wchar_t c = prevLine[i];
            if (inBlockComment) {
                if (c == L'*' && i + 1 < len && prevLine[i + 1] == L'/') {
                    inBlockComment = false;
                    ++i;
                }
            } else if (inMultiLineQuote != 0) {
                if (c == L'\\' && i + 1 < len) {
                    ++i;
                } else if (c == inMultiLineQuote) {
                    inMultiLineQuote = 0;
                }
            } else {
                if (c == L'/' && i + 1 < len && prevLine[i + 1] == L'/') {
                    break; // Single-line comment
                }
                if (isPhp && c == L'#' && (i + 1 >= len || prevLine[i + 1] != L'[')) {
                    break; // PHP # comment
                }
                if (c == L'/' && i + 1 < len && prevLine[i + 1] == L'*') {
                    inBlockComment = true;
                    ++i;
                } else if (isPhp && (c == L'"' || c == L'\'')) {
                    inMultiLineQuote = c;
                }
            }
        }
    }

    const auto& line = m_lines[lineIdx];
    size_t len = line.length();
    if (len == 0) return {};

    if (inBlockComment) {
        size_t closePos = line.find(L"*/");
        if (closePos == std::wstring::npos) {
            return { { TokenType::Comment, 0, len } };
        }
        size_t afterComment = closePos + 2;
        std::vector<SyntaxToken> tokens;
        tokens.push_back({ TokenType::Comment, 0, afterComment });
        if (afterComment < len) {
            auto rest = m_lexer.TokenizeLine(line.substr(afterComment), ext);
            for (auto& t : rest) {
                t.startCol += afterComment;
                tokens.push_back(t);
            }
        }
        return tokens;
    }

    if (inMultiLineQuote != 0) {
        std::vector<SyntaxToken> tokens;
        size_t segStart = 0;
        size_t i = 0;
        bool closed = false;
        while (i < len) {
            if (line[i] == L'\\' && i + 1 < len) {
                i += 2;
                continue;
            }
            if (isPhp && inMultiLineQuote == L'"' && line[i] == L'$' && i + 1 < len && (iswalpha(line[i + 1]) || line[i + 1] == L'_')) {
                if (i > segStart) {
                    tokens.push_back({ TokenType::String, segStart, i - segStart });
                }
                size_t varStart = i;
                ++i;
                while (i < len && (iswalnum(line[i]) || line[i] == L'_')) {
                    ++i;
                }
                tokens.push_back({ TokenType::Attribute, varStart, i - varStart });
                segStart = i;
                continue;
            }
            if (line[i] == inMultiLineQuote) {
                ++i;
                tokens.push_back({ TokenType::String, segStart, i - segStart });
                closed = true;
                break;
            }
            ++i;
        }
        if (!closed) {
            if (len > segStart) {
                tokens.push_back({ TokenType::String, segStart, len - segStart });
            }
            return tokens;
        }
        if (i < len) {
            auto rest = m_lexer.TokenizeLine(line.substr(i), ext);
            for (auto& t : rest) {
                t.startCol += i;
                tokens.push_back(t);
            }
        }
        return tokens;
    }

    return m_lexer.TokenizeLine(line, ext);
}

void Buffer::SetCursor(size_t line, size_t col) {
    if (m_lines.empty()) {
        m_cursor = { 0, 0 };
        return;
    }
    m_cursor.line = (std::min)(line, m_lines.size() - 1);
    m_cursor.col  = (std::min)(col, m_lines[m_cursor.line].length());
}

void Buffer::InsertChar(wchar_t ch) {
    PushUndo();
    if (m_lines.empty()) m_lines.push_back(L"");
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;

    auto& line = m_lines[m_cursor.line];
    if (m_cursor.col > line.length()) m_cursor.col = line.length();

    if (ch == L'\r' || ch == L'\n') {
        // Split current line at cursor
        std::wstring remainder = line.substr(m_cursor.col);
        line = line.substr(0, m_cursor.col);
        m_lines.insert(m_lines.begin() + m_cursor.line + 1, remainder);
        m_cursor.line++;
        m_cursor.col = 0;
    } else {
        line.insert(line.begin() + m_cursor.col, ch);
        m_cursor.col++;
    }
    m_isModified = true;
}

void Buffer::Backspace() {
    if (m_lines.empty()) return;
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;
    if (m_cursor.col == 0 && m_cursor.line == 0) return;

    PushUndo();
    auto& line = m_lines[m_cursor.line];
    if (m_cursor.col > 0) {
        if (m_cursor.col > line.length()) m_cursor.col = line.length();
        line.erase(m_cursor.col - 1, 1);
        m_cursor.col--;
        m_isModified = true;
    } else if (m_cursor.line > 0) {
        // Join with previous line
        size_t prevLen = m_lines[m_cursor.line - 1].length();
        m_lines[m_cursor.line - 1] += line;
        m_lines.erase(m_lines.begin() + m_cursor.line);
        m_cursor.line--;
        m_cursor.col = prevLen;
        m_isModified = true;
    }
    ++m_version;
}

void Buffer::DeleteForward() {
    if (m_lines.empty()) return;
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;

    auto& line = m_lines[m_cursor.line];
    if (m_cursor.col > line.length()) m_cursor.col = line.length();
    if (m_cursor.col == line.length() && m_cursor.line + 1 >= m_lines.size()) return;

    PushUndo();
    if (m_cursor.col < line.length()) {
        line.erase(m_cursor.col, 1);
        m_isModified = true;
    } else if (m_cursor.line + 1 < m_lines.size()) {
        line += m_lines[m_cursor.line + 1];
        m_lines.erase(m_lines.begin() + m_cursor.line + 1);
        m_isModified = true;
    }
    ++m_version;
}

void Buffer::DeleteLine(size_t lineIdx) {
    if (m_lines.empty() || lineIdx >= m_lines.size()) return;
    PushUndo();
    if (m_lines.size() == 1) {
        m_lines[0].clear();
        m_cursor = { 0, 0 };
    } else {
        m_lines.erase(m_lines.begin() + lineIdx);
        if (m_cursor.line >= m_lines.size()) {
            m_cursor.line = m_lines.size() - 1;
        }
        if (m_cursor.col > m_lines[m_cursor.line].length()) {
            m_cursor.col = m_lines[m_cursor.line].length();
        }
    }
    m_isModified = true;
    ++m_version;
}

void Buffer::DeleteRange(size_t sLine, size_t sCol, size_t eLine, size_t eCol) {
    if (m_lines.empty()) return;
    if (sLine >= m_lines.size()) sLine = m_lines.size() - 1;
    if (eLine >= m_lines.size()) eLine = m_lines.size() - 1;
    if (sLine > eLine || (sLine == eLine && sCol >= eCol)) return;

    PushUndo();
    if (sLine == eLine) {
        auto& line = m_lines[sLine];
        if (sCol > line.length()) sCol = line.length();
        if (eCol > line.length()) eCol = line.length();
        line.erase(sCol, eCol - sCol);
    } else {
        const auto& startLineText = m_lines[sLine];
        const auto& endLineText = m_lines[eLine];
        size_t safeSCol = (std::min)(sCol, startLineText.length());
        size_t safeECol = (std::min)(eCol, endLineText.length());
        std::wstring merged = startLineText.substr(0, safeSCol) + endLineText.substr(safeECol);
        m_lines[sLine] = std::move(merged);
        m_lines.erase(m_lines.begin() + sLine + 1, m_lines.begin() + eLine + 1);
        if (m_lines.empty()) m_lines.push_back(L"");
    }
    m_cursor = { sLine, (std::min)(sCol, m_lines[sLine].length()) };
    m_isModified = true;
    ++m_version;
}

void Buffer::MoveCursorLeft() {
    if (m_cursor.col > 0) {
        m_cursor.col--;
    } else if (m_cursor.line > 0) {
        m_cursor.line--;
        m_cursor.col = m_lines[m_cursor.line].length();
    }
}

void Buffer::MoveCursorRight() {
    if (m_cursor.line < m_lines.size()) {
        if (m_cursor.col < m_lines[m_cursor.line].length()) {
            m_cursor.col++;
        } else if (m_cursor.line + 1 < m_lines.size()) {
            m_cursor.line++;
            m_cursor.col = 0;
        }
    }
}

void Buffer::MoveCursorUp() {
    if (m_cursor.line > 0) {
        m_cursor.line--;
        if (m_cursor.col > m_lines[m_cursor.line].length()) {
            m_cursor.col = m_lines[m_cursor.line].length();
        }
    }
}

void Buffer::MoveCursorDown() {
    if (m_cursor.line + 1 < m_lines.size()) {
        m_cursor.line++;
        if (m_cursor.col > m_lines[m_cursor.line].length()) {
            m_cursor.col = m_lines[m_cursor.line].length();
        }
    }
}

void Buffer::Clear() {
    m_lines.clear();
    m_lines.push_back(L"");
    m_cursor = { 0, 0 };
    m_isModified = false;
    ++m_version;
}

void Buffer::SetLines(const std::vector<std::wstring>& lines) {
    m_lines = lines;
    if (m_lines.empty()) m_lines.push_back(L"");
    m_cursor = { 0, 0 };
    m_isModified = false;
    ++m_version;
}

void Buffer::SetLine(size_t lineIdx, const std::wstring& text) {
    if (lineIdx < m_lines.size()) {
        m_lines[lineIdx] = text;
        m_isModified = true;
        ++m_version;
    }
}

void Buffer::SetText(const std::wstring& text) {
    m_lines.clear();
    std::wstring cleanText = text;
    if (!cleanText.empty() && cleanText[0] == 0xFEFF) {
        cleanText.erase(0, 1);
    }
    std::wistringstream ss(cleanText);
    std::wstring line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        m_lines.push_back(line);
    }
    if (m_lines.empty()) m_lines.push_back(L"");
    m_cursor = { 0, 0 };
    m_isModified = false;
    ++m_version;
}

bool Buffer::LoadFromFile(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string bytes = buffer.str();

    // Strip UTF-8 BOM (EF BB BF) if present so PHP header() calls never fail due to BOM output
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }

    // Convert UTF-8 to wstring
    std::wstring wide;
    if (!bytes.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (wlen > 0) {
            wide.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), &wide[0], wlen);
        }
    }

    SetText(wide);
    m_filePath = path;

    // Extract filename from path
    auto pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        m_fileName = path.substr(pos + 1);
    } else {
        m_fileName = path;
    }

    m_isModified = false;
    return true;
}

bool Buffer::SaveToFile(const std::wstring& path) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    // Concatenate lines with \r\n
    std::wstring combined;
    for (size_t i = 0; i < m_lines.size(); ++i) {
        combined += m_lines[i];
        if (i + 1 < m_lines.size()) combined += L"\r\n";
    }

    // Convert wstring to UTF-8
    if (!combined.empty()) {
        int ulen = WideCharToMultiByte(CP_UTF8, 0, combined.data(), static_cast<int>(combined.size()), nullptr, 0, nullptr, nullptr);
        if (ulen > 0) {
            std::string u8(ulen, 0);
            WideCharToMultiByte(CP_UTF8, 0, combined.data(), static_cast<int>(combined.size()), &u8[0], ulen, nullptr, nullptr);
            file.write(u8.data(), u8.size());
        }
    }

    m_filePath = path;
    auto pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        m_fileName = path.substr(pos + 1);
    } else {
        m_fileName = path;
    }

    m_isModified = false;
    return true;
}

void Buffer::ToggleLineComment() {
    if (m_lines.empty()) return;
    PushUndo();
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;
    auto& line = m_lines[m_cursor.line];

    size_t firstNonWs = 0;
    while (firstNonWs < line.length() && (line[firstNonWs] == L' ' || line[firstNonWs] == L'\t')) {
        ++firstNonWs;
    }

    if (firstNonWs + 1 < line.length() && line[firstNonWs] == L'/' && line[firstNonWs + 1] == L'/') {
        size_t removeLen = (firstNonWs + 2 < line.length() && line[firstNonWs + 2] == L' ') ? 3 : 2;
        line.erase(firstNonWs, removeLen);
        if (m_cursor.col > firstNonWs) {
            m_cursor.col = (m_cursor.col >= firstNonWs + removeLen) ? (m_cursor.col - removeLen) : firstNonWs;
        }
    } else {
        line.insert(firstNonWs, L"// ");
        if (m_cursor.col >= firstNonWs) {
            m_cursor.col += 3;
        }
    }
    m_isModified = true;
}

void Buffer::ToggleBlockComment() {
    if (m_lines.empty()) return;
    PushUndo();
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;
    auto& line = m_lines[m_cursor.line];

    size_t openPos = line.find(L"/*");
    size_t closePos = line.rfind(L"*/");
    if (openPos != std::wstring::npos && closePos != std::wstring::npos && closePos >= openPos + 2) {
        // Erase closing tag (and preceding space if present)
        size_t closeStart = (closePos > 0 && line[closePos - 1] == L' ') ? (closePos - 1) : closePos;
        size_t closeLen = (closePos > 0 && line[closePos - 1] == L' ') ? 3 : 2;
        line.erase(closeStart, closeLen);

        // Erase opening tag (and following space if present)
        size_t openLen = (openPos + 2 < line.length() && line[openPos + 2] == L' ') ? 3 : 2;
        line.erase(openPos, openLen);
    } else {
        line = L"/* " + line + L" */";
        m_cursor.col += 3;
    }
    m_isModified = true;
}

bool Buffer::ExpandEmmet() {
    if (m_lines.empty()) return false;
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;
    auto& line = m_lines[m_cursor.line];
    if (m_cursor.col == 0 || line.empty()) return false;

    size_t startCol = m_cursor.col;
    while (startCol > 0 && iswalnum(line[startCol - 1])) {
        --startCol;
    }
    if (startCol == m_cursor.col) return false;

    std::wstring tag = line.substr(startCol, m_cursor.col - startCol);
    static const std::unordered_set<std::wstring> knownTags = {
        L"div", L"span", L"p", L"a", L"button", L"input", L"form", L"ul", L"ol", L"li",
        L"header", L"footer", L"nav", L"section", L"article", L"h1", L"h2", L"h3", L"h4",
        L"h5", L"h6", L"table", L"tr", L"td", L"th", L"main", L"label", L"select", L"option"
    };

    if (knownTags.count(tag)) {
        PushUndo();
        std::wstring expansion;
        if (tag == L"input") {
            expansion = L"<input type=\"text\" />";
        } else {
            expansion = L"<" + tag + L"></" + tag + L">";
        }
        line.replace(startCol, tag.length(), expansion);
        m_cursor.col = startCol + tag.length() + 2;
        m_isModified = true;
        return true;
    }
    return false;
}

bool Buffer::ReplacePrefixWithSnippet(size_t prefixLen, const std::vector<std::wstring>& bodyLines, int targetCursorLineOffset, int targetCursorCol) {
    if (m_lines.empty() || bodyLines.empty() || prefixLen == 0) return false;
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;
    auto& line = m_lines[m_cursor.line];
    if (m_cursor.col < prefixLen) return false;

    PushUndo();
    size_t startCol = m_cursor.col - prefixLen;
    std::wstring suffix = line.substr(m_cursor.col);

    // Determine leading indentation on current line
    size_t indentLen = 0;
    while (indentLen < startCol && (line[indentLen] == L' ' || line[indentLen] == L'\t')) {
        indentLen++;
    }
    std::wstring indent = line.substr(0, indentLen);

    if (bodyLines.size() == 1) {
        line = line.substr(0, startCol) + bodyLines[0] + suffix;
        if (targetCursorCol >= 0) {
            m_cursor.col = startCol + static_cast<size_t>(targetCursorCol);
        } else {
            m_cursor.col = startCol + bodyLines[0].length();
        }
    } else {
        line = line.substr(0, startCol) + bodyLines[0];
        size_t baseLine = m_cursor.line;
        for (size_t i = 1; i < bodyLines.size(); ++i) {
            std::wstring nextLine = indent + bodyLines[i];
            if (i == bodyLines.size() - 1) {
                nextLine += suffix;
            }
            m_lines.insert(m_lines.begin() + baseLine + i, nextLine);
        }
        if (targetCursorLineOffset >= 0 && targetCursorLineOffset < static_cast<int>(bodyLines.size())) {
            m_cursor.line = baseLine + static_cast<size_t>(targetCursorLineOffset);
            if (targetCursorLineOffset == 0) {
                m_cursor.col = startCol + static_cast<size_t>(targetCursorCol >= 0 ? targetCursorCol : bodyLines[0].length());
            } else {
                m_cursor.col = indent.length() + static_cast<size_t>(targetCursorCol >= 0 ? targetCursorCol : bodyLines[targetCursorLineOffset].length());
            }
        } else {
            m_cursor.line = baseLine + bodyLines.size() - 1;
            m_cursor.col = indent.length() + bodyLines.back().length();
        }
    }

    m_isModified = true;
    return true;
}

} // namespace phide::editor
