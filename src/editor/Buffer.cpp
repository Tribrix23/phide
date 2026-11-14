/**
 * @file Buffer.cpp
 * @brief Implementation of the text buffer and syntax lexer.
 */

#include "phide/editor/Buffer.hpp"
#include "phide/editor/EmmetEngine.hpp"
#include "phide/plugin/PluginManager.hpp"
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
    std::wstring lowerExt = phide::plugin::PluginManager::Instance().ResolveLanguageAlias(ext);
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

        bool isPython = (lowerExt == L".py" || lowerExt == L"py");

        // 6. Single-line & Block Comments
        if (isPython) {
            if (line[i] == L'#') {
                tokens.push_back({ TokenType::Comment, i, len - i });
                break;
            }
            if (i + 1 < len && line[i] == L'/' && line[i + 1] == L'/') {
                // In Python, // is the floor-division operator, NOT a comment!
                tokens.push_back({ TokenType::Operator, i, 2 });
                i += 2;
                continue;
            }
        } else {
            if (i + 1 < len && line[i] == L'/' && line[i + 1] == L'/') {
                tokens.push_back({ TokenType::Comment, i, len - i });
                break;
            }
            if (line[i] == L'#' && (lowerExt == L".sh" || lowerExt == L".bash" || lowerExt == L".php" || lowerExt == L"php" || lowerExt.empty())) {
                tokens.push_back({ TokenType::Comment, i, len - i });
                break;
            }
            // Block Comment opening or inside (/* ...)
            if (i + 1 < len && line[i] == L'/' && line[i + 1] == L'*') {
                tokens.push_back({ TokenType::Comment, i, len - i });
                break;
            }
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

            if (m_keywords.find(word) != m_keywords.end() || phide::plugin::PluginManager::Instance().IsCustomKeyword(ext, word) || phide::plugin::PluginManager::Instance().IsCustomKeyword(lowerExt, word)) {
                tokens.push_back({ TokenType::Keyword, start, i - start });
            } else if (m_types.find(word) != m_types.end() || isScopeClass || phide::plugin::PluginManager::Instance().IsCustomType(ext, word) || phide::plugin::PluginManager::Instance().IsCustomType(lowerExt, word)) {
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

namespace {
struct UndoCoalesceState {
    int lastOp = 0; // 0 = none, 1 = insert, 2 = backspace, 3 = deleteForward
    size_t line = 0;
    size_t col = 0;
    ULONGLONG tick = 0;
};
static std::unordered_map<const Buffer*, UndoCoalesceState> s_undoCoalesceMap;
} // namespace

size_t Buffer::UndoHunk::EstimateBytes() const noexcept {
    size_t total = sizeof(UndoHunk) + beforeLineEnding.capacity() * sizeof(wchar_t) + afterLineEnding.capacity() * sizeof(wchar_t);
    for (const auto& s : beforeLines) {
        total += sizeof(std::wstring) + (s.capacity() + 1) * sizeof(wchar_t);
    }
    return total;
}

void Buffer::EnforceUndoBudget() {
    const size_t pendingSlot = m_hasPendingUndo ? 1 : 0;
    const size_t maxCommitted = (kMaxUndoCount > pendingSlot) ? (kMaxUndoCount - pendingSlot) : 0;
    while (m_undoStack.size() > maxCommitted ||
           (m_undoStack.size() > kMinRetainedUndo && m_undoMemoryBytes > kMaxUndoMemoryBytes)) {
        m_undoMemoryBytes -= (std::min)(m_undoMemoryBytes, m_undoStack.front().EstimateBytes());
        m_undoStack.erase(m_undoStack.begin());
    }
    while (m_redoStack.size() > kMaxUndoCount ||
           (m_redoStack.size() > kMinRetainedUndo && m_redoMemoryBytes > kMaxUndoMemoryBytes)) {
        m_redoMemoryBytes -= (std::min)(m_redoMemoryBytes, m_redoStack.front().EstimateBytes());
        m_redoStack.erase(m_redoStack.begin());
    }
}

void Buffer::CommitPendingUndo() {
    if (!m_hasPendingUndo) return;
    m_hasPendingUndo = false;

    const size_t oldN = m_pendingUndoLines.size();
    const size_t newN = m_lines.size();

    size_t prefix = 0;
    while (prefix < oldN && prefix < newN && m_pendingUndoLines[prefix] == m_lines[prefix]) {
        ++prefix;
    }

    size_t suffix = 0;
    while (suffix < (oldN - prefix) && suffix < (newN - prefix) &&
           m_pendingUndoLines[oldN - 1 - suffix] == m_lines[newN - 1 - suffix]) {
        ++suffix;
    }

    const bool linesDiffer = !(prefix == oldN && oldN == newN);
    const bool eolDiffers = (m_pendingUndoLineEnding != m_lineEnding);
    if (linesDiffer || eolDiffers) {
        UndoHunk h;
        h.startLine = prefix;
        h.afterCount = newN - prefix - suffix;
        h.beforeLines.assign(m_pendingUndoLines.begin() + prefix, m_pendingUndoLines.begin() + (oldN - suffix));
        h.beforeCursor = m_pendingUndoCursor;
        h.afterCursor = m_cursor;
        h.beforeLineEnding = m_pendingUndoLineEnding;
        h.afterLineEnding = m_lineEnding;
        m_undoMemoryBytes += h.EstimateBytes();
        m_undoStack.push_back(std::move(h));
        EnforceUndoBudget();
    }

    std::vector<std::wstring>().swap(m_pendingUndoLines);
}

void Buffer::PushUndo() {
    s_undoCoalesceMap.erase(this);
    CommitPendingUndo();
    m_pendingUndoLines = m_lines;
    m_pendingUndoCursor = m_cursor;
    m_pendingUndoLineEnding = m_lineEnding;
    m_pendingUndoVersion = m_version;
    m_hasPendingUndo = true;
    m_redoStack.clear();
    m_redoMemoryBytes = 0;
    EnforceUndoBudget();
}

size_t Buffer::GetUndoCount() const noexcept {
    size_t pending = (m_hasPendingUndo && (m_version != m_pendingUndoVersion || m_lineEnding != m_pendingUndoLineEnding)) ? 1 : 0;
    return m_undoStack.size() + pending;
}

size_t Buffer::GetUndoMemoryBytes() const noexcept {
    size_t pending = 0;
    if (m_hasPendingUndo) {
        pending += sizeof(m_pendingUndoLines);
        for (const auto& l : m_pendingUndoLines) {
            pending += sizeof(std::wstring) + (l.capacity() + 1) * sizeof(wchar_t);
        }
    }
    return m_undoMemoryBytes + m_redoMemoryBytes + pending;
}

bool Buffer::CanUndo() const noexcept {
    return !m_undoStack.empty() ||
           (m_hasPendingUndo && (m_version != m_pendingUndoVersion || m_lineEnding != m_pendingUndoLineEnding));
}

void Buffer::RunGarbageCollection() {
    s_undoCoalesceMap.erase(this);
    CommitPendingUndo();
    EnforceUndoBudget();
    m_lines.shrink_to_fit();
    for (auto& l : m_lines) l.shrink_to_fit();
    m_undoStack.shrink_to_fit();
    for (auto& h : m_undoStack) {
        h.beforeLines.shrink_to_fit();
        for (auto& l : h.beforeLines) l.shrink_to_fit();
    }
    m_redoStack.shrink_to_fit();
    for (auto& h : m_redoStack) {
        h.beforeLines.shrink_to_fit();
        for (auto& l : h.beforeLines) l.shrink_to_fit();
    }
    m_tokenCache.clear();
    m_lineEntryStates.shrink_to_fit();
}

bool Buffer::Undo() {
    s_undoCoalesceMap.erase(this);
    CommitPendingUndo();
    if (m_undoStack.empty()) return false;

    UndoHunk h = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    m_undoMemoryBytes -= (std::min)(m_undoMemoryBytes, h.EstimateBytes());

    size_t startLine = (std::min)(h.startLine, m_lines.size());
    size_t afterCount = (std::min)(h.afterCount, m_lines.size() - startLine);

    UndoHunk redo;
    redo.startLine = startLine;
    redo.afterCount = h.beforeLines.size();
    redo.beforeLines.assign(m_lines.begin() + startLine, m_lines.begin() + startLine + afterCount);
    redo.beforeCursor = m_cursor;
    redo.afterCursor = h.beforeCursor;
    redo.beforeLineEnding = m_lineEnding;
    redo.afterLineEnding = h.beforeLineEnding;
    m_redoMemoryBytes += redo.EstimateBytes();
    m_redoStack.push_back(std::move(redo));

    m_lines.erase(m_lines.begin() + startLine, m_lines.begin() + startLine + afterCount);
    m_lines.insert(m_lines.begin() + startLine,
                   std::make_move_iterator(h.beforeLines.begin()),
                   std::make_move_iterator(h.beforeLines.end()));
    if (m_lines.empty()) m_lines.push_back(L"");

    m_cursor = h.beforeCursor;
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;
    if (m_cursor.col > m_lines[m_cursor.line].length()) m_cursor.col = m_lines[m_cursor.line].length();
    m_lineEnding = h.beforeLineEnding;
    m_isModified = true;
    m_tokenCache.clear();
    ++m_version;
    EnforceUndoBudget();
    return true;
}

bool Buffer::Redo() {
    s_undoCoalesceMap.erase(this);
    CommitPendingUndo();
    if (m_redoStack.empty()) return false;

    UndoHunk h = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    m_redoMemoryBytes -= (std::min)(m_redoMemoryBytes, h.EstimateBytes());

    size_t startLine = (std::min)(h.startLine, m_lines.size());
    size_t afterCount = (std::min)(h.afterCount, m_lines.size() - startLine);

    UndoHunk undo;
    undo.startLine = startLine;
    undo.afterCount = h.beforeLines.size();
    undo.beforeLines.assign(m_lines.begin() + startLine, m_lines.begin() + startLine + afterCount);
    undo.beforeCursor = m_cursor;
    undo.afterCursor = h.beforeCursor;
    undo.beforeLineEnding = m_lineEnding;
    undo.afterLineEnding = h.beforeLineEnding;
    m_undoMemoryBytes += undo.EstimateBytes();
    m_undoStack.push_back(std::move(undo));

    m_lines.erase(m_lines.begin() + startLine, m_lines.begin() + startLine + afterCount);
    m_lines.insert(m_lines.begin() + startLine,
                   std::make_move_iterator(h.beforeLines.begin()),
                   std::make_move_iterator(h.beforeLines.end()));
    if (m_lines.empty()) m_lines.push_back(L"");

    m_cursor = h.beforeCursor;
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;
    if (m_cursor.col > m_lines[m_cursor.line].length()) m_cursor.col = m_lines[m_cursor.line].length();
    m_lineEnding = h.beforeLineEnding;
    m_isModified = true;
    m_tokenCache.clear();
    ++m_version;
    EnforceUndoBudget();
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
    m_tokenCache.clear();
    m_isModified = true;
    ++m_version;
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

    const bool isPhp = (ext == L".php" || ext == L"php");
    const bool isPython = (ext == L".py" || ext == L"py");

    // Recompute per-line entry states in a single O(N) pass ONLY when buffer version or extension changes
    if (m_cachedTokenVersion != m_version || m_cachedTokenExt != ext || m_lineEntryStates.size() != m_lines.size()) {
        const bool fullClear = (m_cachedTokenExt != ext || m_lineEntryStates.size() != m_lines.size());
        m_cachedTokenVersion = m_version;
        m_cachedTokenExt = ext;
        if (fullClear) {
            m_tokenCache.clear();
            m_lineEntryStates.assign(m_lines.size(), 0);
        }

        bool inBlock = false;
        wchar_t inQuote = 0;
        for (size_t l = 0; l < m_lines.size(); ++l) {
            uint8_t newState = inBlock ? 1 : (inQuote == L'"' ? 2 : (inQuote == L'\'' ? 3 : 0));
            if (!fullClear && m_lineEntryStates[l] != newState) {
                m_tokenCache.erase(l);
            }
            m_lineEntryStates[l] = newState;
            const auto& prevLine = m_lines[l];
            size_t len = prevLine.length();
            for (size_t i = 0; i < len; ++i) {
                wchar_t c = prevLine[i];
                if (inBlock) {
                    if (c == L'*' && i + 1 < len && prevLine[i + 1] == L'/') {
                        inBlock = false;
                        ++i;
                    }
                } else if (inQuote != 0) {
                    if (c == L'\\' && i + 1 < len) {
                        ++i;
                    } else if (c == inQuote) {
                        inQuote = 0;
                    }
                } else {
                    if (isPython) {
                        if (c == L'#') {
                            break; // Python # comment
                        }
                    } else {
                        if (c == L'/' && i + 1 < len && prevLine[i + 1] == L'/') {
                            break; // Single-line comment
                        }
                        if (isPhp && c == L'#' && (i + 1 >= len || prevLine[i + 1] != L'[')) {
                            break; // PHP # comment
                        }
                        if (c == L'/' && i + 1 < len && prevLine[i + 1] == L'*') {
                            inBlock = true;
                            ++i;
                        } else if (isPhp && (c == L'"' || c == L'\'')) {
                            inQuote = c;
                        }
                    }
                }
            }
        }
    }

    auto cacheIt = m_tokenCache.find(lineIdx);
    if (cacheIt != m_tokenCache.end()) {
        return cacheIt->second;
    }

    const uint8_t entryState = m_lineEntryStates[lineIdx];
    const bool inBlockComment = (entryState == 1);
    const wchar_t inMultiLineQuote = (entryState == 2) ? L'"' : ((entryState == 3) ? L'\'' : 0);

    const auto& line = m_lines[lineIdx];
    size_t len = line.length();
    if (len == 0) {
        return {};
    }

    std::vector<SyntaxToken> resultTokens;
    if (inBlockComment) {
        size_t closePos = line.find(L"*/");
        if (closePos == std::wstring::npos) {
            resultTokens = { { TokenType::Comment, 0, len } };
        } else {
            size_t afterComment = closePos + 2;
            resultTokens.push_back({ TokenType::Comment, 0, afterComment });
            if (afterComment < len) {
                auto rest = m_lexer.TokenizeLine(line.substr(afterComment), ext);
                for (auto& t : rest) {
                    t.startCol += afterComment;
                    resultTokens.push_back(t);
                }
            }
        }
    } else if (inMultiLineQuote != 0) {
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
                    resultTokens.push_back({ TokenType::String, segStart, i - segStart });
                }
                size_t varStart = i;
                ++i;
                while (i < len && (iswalnum(line[i]) || line[i] == L'_')) {
                    ++i;
                }
                resultTokens.push_back({ TokenType::Attribute, varStart, i - varStart });
                segStart = i;
                continue;
            }
            if (line[i] == inMultiLineQuote) {
                ++i;
                resultTokens.push_back({ TokenType::String, segStart, i - segStart });
                closed = true;
                break;
            }
            ++i;
        }
        if (!closed) {
            if (len > segStart) {
                resultTokens.push_back({ TokenType::String, segStart, len - segStart });
            }
        } else if (i < len) {
            auto rest = m_lexer.TokenizeLine(line.substr(i), ext);
            for (auto& t : rest) {
                t.startCol += i;
                resultTokens.push_back(t);
            }
        }
    } else {
        resultTokens = m_lexer.TokenizeLine(line, ext);
    }

    m_tokenCache.emplace(lineIdx, resultTokens);
    return resultTokens;
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
    if (m_lines.empty()) m_lines.push_back(L"");
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;

    ULONGLONG now = GetTickCount64();
    bool isNewline = (ch == L'\r' || ch == L'\n');
    bool isWordBoundary = isNewline || ch == L' ' || ch == L'\t';
    auto itCoal = s_undoCoalesceMap.find(this);
    bool canCoalesce = (!isWordBoundary &&
                        (!m_undoStack.empty() || m_hasPendingUndo) &&
                        itCoal != s_undoCoalesceMap.end() &&
                        itCoal->second.lastOp == 1 &&
                        itCoal->second.line == m_cursor.line &&
                        itCoal->second.col == m_cursor.col &&
                        (now - itCoal->second.tick) <= 750);
    if (!canCoalesce) {
        PushUndo();
    } else {
        m_redoStack.clear();
        m_redoMemoryBytes = 0;
    }

    auto& line = m_lines[m_cursor.line];
    if (m_cursor.col > line.length()) m_cursor.col = line.length();

    if (isNewline) {
        // Split current line at cursor
        std::wstring remainder = line.substr(m_cursor.col);
        line = line.substr(0, m_cursor.col);
        m_lines.insert(m_lines.begin() + m_cursor.line + 1, remainder);
        m_cursor.line++;
        m_cursor.col = 0;
        m_tokenCache.clear();
        s_undoCoalesceMap.erase(this);
    } else {
        line.insert(line.begin() + m_cursor.col, ch);
        m_tokenCache.erase(m_cursor.line);
        m_cursor.col++;
        s_undoCoalesceMap[this] = { 1, m_cursor.line, m_cursor.col, now };
    }
    m_isModified = true;
    ++m_version;
}

void Buffer::Backspace() {
    if (m_lines.empty()) return;
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;
    if (m_cursor.col == 0 && m_cursor.line == 0) return;

    ULONGLONG now = GetTickCount64();
    auto itCoal = s_undoCoalesceMap.find(this);
    bool canCoalesce = (m_cursor.col > 0 &&
                        (!m_undoStack.empty() || m_hasPendingUndo) &&
                        itCoal != s_undoCoalesceMap.end() &&
                        itCoal->second.lastOp == 2 &&
                        itCoal->second.line == m_cursor.line &&
                        itCoal->second.col == m_cursor.col &&
                        (now - itCoal->second.tick) <= 750);
    if (!canCoalesce) {
        PushUndo();
    } else {
        m_redoStack.clear();
        m_redoMemoryBytes = 0;
    }

    auto& line = m_lines[m_cursor.line];
    if (m_cursor.col > 0) {
        if (m_cursor.col > line.length()) m_cursor.col = line.length();
        line.erase(m_cursor.col - 1, 1);
        m_tokenCache.erase(m_cursor.line);
        m_cursor.col--;
        m_isModified = true;
        s_undoCoalesceMap[this] = { 2, m_cursor.line, m_cursor.col, now };
    } else if (m_cursor.line > 0) {
        // Join with previous line
        size_t prevLen = m_lines[m_cursor.line - 1].length();
        m_lines[m_cursor.line - 1] += line;
        m_lines.erase(m_lines.begin() + m_cursor.line);
        m_cursor.line--;
        m_cursor.col = prevLen;
        m_tokenCache.clear();
        m_isModified = true;
        s_undoCoalesceMap.erase(this);
    }
    ++m_version;
}

void Buffer::DeleteForward() {
    if (m_lines.empty()) return;
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;

    auto& line = m_lines[m_cursor.line];
    if (m_cursor.col > line.length()) m_cursor.col = line.length();
    if (m_cursor.col == line.length() && m_cursor.line + 1 >= m_lines.size()) return;

    ULONGLONG now = GetTickCount64();
    auto itCoal = s_undoCoalesceMap.find(this);
    bool canCoalesce = (m_cursor.col < line.length() &&
                        (!m_undoStack.empty() || m_hasPendingUndo) &&
                        itCoal != s_undoCoalesceMap.end() &&
                        itCoal->second.lastOp == 3 &&
                        itCoal->second.line == m_cursor.line &&
                        itCoal->second.col == m_cursor.col &&
                        (now - itCoal->second.tick) <= 750);
    if (!canCoalesce) {
        PushUndo();
    } else {
        m_redoStack.clear();
        m_redoMemoryBytes = 0;
    }

    if (m_cursor.col < line.length()) {
        line.erase(m_cursor.col, 1);
        m_tokenCache.erase(m_cursor.line);
        m_isModified = true;
        s_undoCoalesceMap[this] = { 3, m_cursor.line, m_cursor.col, now };
    } else if (m_cursor.line + 1 < m_lines.size()) {
        line += m_lines[m_cursor.line + 1];
        m_lines.erase(m_lines.begin() + m_cursor.line + 1);
        m_tokenCache.clear();
        m_isModified = true;
        s_undoCoalesceMap.erase(this);
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
        m_tokenCache.erase(sLine);
    } else {
        const auto& startLineText = m_lines[sLine];
        const auto& endLineText = m_lines[eLine];
        size_t safeSCol = (std::min)(sCol, startLineText.length());
        size_t safeECol = (std::min)(eCol, endLineText.length());
        std::wstring merged = startLineText.substr(0, safeSCol) + endLineText.substr(safeECol);
        m_lines[sLine] = std::move(merged);
        m_lines.erase(m_lines.begin() + sLine + 1, m_lines.begin() + eLine + 1);
        if (m_lines.empty()) m_lines.push_back(L"");
        m_tokenCache.clear();
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
        m_tokenCache.erase(lineIdx);
        m_isModified = true;
        ++m_version;
    }
}

void Buffer::ReplaceLineSlice(size_t startLine, size_t removeCount, const std::vector<std::wstring>& insertLines) {
    if (startLine > m_lines.size()) startLine = m_lines.size();
    if (removeCount > m_lines.size() - startLine) removeCount = m_lines.size() - startLine;
    m_lines.erase(m_lines.begin() + startLine, m_lines.begin() + startLine + removeCount);
    m_lines.insert(m_lines.begin() + startLine, insertLines.begin(), insertLines.end());
    if (m_lines.empty()) m_lines.push_back(L"");
    m_tokenCache.clear();
    m_isModified = true;
    ++m_version;
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

bool Buffer::LoadBytesWithEncoding(const std::string& bytes, TextEncoding enc, std::wstring& outText) {
    outText.clear();
    if (bytes.empty()) return true;

    std::string view = bytes;
    if (enc == TextEncoding::UTF8 || enc == TextEncoding::UTF8_BOM) {
        if (view.size() >= 3 &&
            static_cast<unsigned char>(view[0]) == 0xEF &&
            static_cast<unsigned char>(view[1]) == 0xBB &&
            static_cast<unsigned char>(view[2]) == 0xBF) {
            view.erase(0, 3);
        }
        if (view.empty()) return true;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, view.data(), static_cast<int>(view.size()), nullptr, 0);
        if (wlen <= 0) return false;
        outText.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, view.data(), static_cast<int>(view.size()), &outText[0], wlen);
        return true;
    }

    if (enc == TextEncoding::UTF16_LE || enc == TextEncoding::UTF16_BE) {
        size_t offset = 0;
        if (bytes.size() >= 2) {
            unsigned char b0 = static_cast<unsigned char>(bytes[0]);
            unsigned char b1 = static_cast<unsigned char>(bytes[1]);
            if ((enc == TextEncoding::UTF16_LE && b0 == 0xFF && b1 == 0xFE) ||
                (enc == TextEncoding::UTF16_BE && b0 == 0xFE && b1 == 0xFF)) {
                offset = 2;
            }
        }
        size_t avail = (bytes.size() > offset) ? ((bytes.size() - offset) / 2) : 0;
        outText.resize(avail);
        for (size_t i = 0; i < avail; ++i) {
            unsigned char c0 = static_cast<unsigned char>(bytes[offset + i * 2]);
            unsigned char c1 = static_cast<unsigned char>(bytes[offset + i * 2 + 1]);
            wchar_t wc = (enc == TextEncoding::UTF16_LE)
                ? static_cast<wchar_t>(c0 | (c1 << 8))
                : static_cast<wchar_t>((c0 << 8) | c1);
            outText[i] = wc;
        }
        return true;
    }

    // Windows-1252 / ISO-8859-1 fallback
    int wlen = MultiByteToWideChar(1252, 0, view.data(), static_cast<int>(view.size()), nullptr, 0);
    if (wlen <= 0) return false;
    outText.resize(wlen);
    MultiByteToWideChar(1252, 0, view.data(), static_cast<int>(view.size()), &outText[0], wlen);
    return true;
}

bool Buffer::EncodeTextBytes(const std::wstring& text, TextEncoding enc, std::string& outBytes) {
    outBytes.clear();
    if (enc == TextEncoding::UTF8_BOM) {
        outBytes.push_back(static_cast<char>(0xEF));
        outBytes.push_back(static_cast<char>(0xBB));
        outBytes.push_back(static_cast<char>(0xBF));
    } else if (enc == TextEncoding::UTF16_LE) {
        outBytes.push_back(static_cast<char>(0xFF));
        outBytes.push_back(static_cast<char>(0xFE));
        outBytes.reserve(2 + text.size() * 2);
        for (wchar_t wc : text) {
            uint16_t u = static_cast<uint16_t>(wc);
            outBytes.push_back(static_cast<char>(u & 0xFF));
            outBytes.push_back(static_cast<char>((u >> 8) & 0xFF));
        }
        return true;
    } else if (enc == TextEncoding::UTF16_BE) {
        outBytes.push_back(static_cast<char>(0xFE));
        outBytes.push_back(static_cast<char>(0xFF));
        outBytes.reserve(2 + text.size() * 2);
        for (wchar_t wc : text) {
            uint16_t u = static_cast<uint16_t>(wc);
            outBytes.push_back(static_cast<char>((u >> 8) & 0xFF));
            outBytes.push_back(static_cast<char>(u & 0xFF));
        }
        return true;
    }

    if (text.empty()) return true;
    UINT cp = (enc == TextEncoding::Windows1252) ? 1252u : CP_UTF8;
    int ulen = WideCharToMultiByte(cp, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return false;
    size_t prefixLen = outBytes.size();
    outBytes.resize(prefixLen + static_cast<size_t>(ulen));
    WideCharToMultiByte(cp, 0, text.data(), static_cast<int>(text.size()), &outBytes[prefixLen], ulen, nullptr, nullptr);
    return true;
}

std::wstring Buffer::GetEncodingDisplayName() const {
    switch (m_encoding) {
        case TextEncoding::UTF8:        return L"UTF-8";
        case TextEncoding::UTF8_BOM:    return L"UTF-8 with BOM";
        case TextEncoding::UTF16_LE:    return L"UTF-16 LE";
        case TextEncoding::UTF16_BE:    return L"UTF-16 BE";
        case TextEncoding::Windows1252: return L"Windows 1252";
    }
    return L"UTF-8";
}

std::wstring Buffer::GetLineEndingDisplayName() const {
    return (m_lineEnding == L"\r\n") ? L"CRLF" : L"LF";
}

bool Buffer::ConvertLineEnding(const std::wstring& le) {
    std::wstring norm = le;
    if (norm == L"CRLF" || norm == L"crlf") norm = L"\r\n";
    else if (norm == L"LF" || norm == L"lf") norm = L"\n";
    if (norm != L"\n" && norm != L"\r\n") return false;
    if (m_lineEnding == norm) return false;
    PushUndo();
    m_lineEnding = norm;
    m_isModified = true;
    ++m_version;
    return true;
}

bool Buffer::ConvertEncoding(TextEncoding newEnc) {
    if (m_encoding == newEnc) return false;
    m_encoding = newEnc;
    m_isModified = true;
    ++m_version;
    return true;
}

bool Buffer::ReopenWithEncoding(TextEncoding enc) {
    if (m_filePath.empty()) {
        return ConvertEncoding(enc);
    }
    std::ifstream file(std::filesystem::path(m_filePath), std::ios::binary);
    if (!file.is_open()) return false;
    std::stringstream ss;
    ss << file.rdbuf();
    std::string bytes = ss.str();

    std::wstring wide;
    if (!LoadBytesWithEncoding(bytes, enc, wide)) return false;

    if (!wide.empty()) {
        m_hasTrailingNewline = (wide.back() == L'\n' || wide.back() == L'\r');
        m_lineEnding = (wide.find(L"\r\n") != std::wstring::npos) ? L"\r\n" : L"\n";
    } else {
        m_hasTrailingNewline = false;
        m_lineEnding = L"\n";
    }

    PushUndo();
    SetText(wide);
    m_encoding = enc;
    m_isModified = false;
    ++m_version;
    return true;
}

bool Buffer::LoadFromFile(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string bytes = buffer.str();

    TextEncoding detectedEnc = TextEncoding::UTF8;
    std::wstring wide;

    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        detectedEnc = TextEncoding::UTF8_BOM;
        LoadBytesWithEncoding(bytes, detectedEnc, wide);
    } else if (bytes.size() >= 2 &&
               static_cast<unsigned char>(bytes[0]) == 0xFF &&
               static_cast<unsigned char>(bytes[1]) == 0xFE) {
        detectedEnc = TextEncoding::UTF16_LE;
        LoadBytesWithEncoding(bytes, detectedEnc, wide);
    } else if (bytes.size() >= 2 &&
               static_cast<unsigned char>(bytes[0]) == 0xFE &&
               static_cast<unsigned char>(bytes[1]) == 0xFF) {
        detectedEnc = TextEncoding::UTF16_BE;
        LoadBytesWithEncoding(bytes, detectedEnc, wide);
    } else if (!bytes.empty()) {
        int strictLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (strictLen > 0) {
            detectedEnc = TextEncoding::UTF8;
            wide.resize(strictLen);
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), &wide[0], strictLen);
        } else {
            detectedEnc = TextEncoding::Windows1252;
            LoadBytesWithEncoding(bytes, detectedEnc, wide);
        }
    }

    if (!wide.empty()) {
        m_hasTrailingNewline = (wide.back() == L'\n' || wide.back() == L'\r');
        m_lineEnding = (wide.find(L"\r\n") != std::wstring::npos) ? L"\r\n" : L"\n";
    } else {
        m_hasTrailingNewline = false;
        m_lineEnding = L"\n";
    }

    s_undoCoalesceMap.erase(this);
    m_undoStack.clear();
    m_redoStack.clear();
    m_undoMemoryBytes = 0;
    m_redoMemoryBytes = 0;
    m_hasPendingUndo = false;
    std::vector<std::wstring>().swap(m_pendingUndoLines);

    SetText(wide);
    m_encoding = detectedEnc;
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

bool Buffer::SaveToFile(const std::wstring& path) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    std::wstring combined;
    size_t reserveHint = 0;
    for (const auto& l : m_lines) reserveHint += l.size() + m_lineEnding.size();
    combined.reserve(reserveHint);
    for (size_t i = 0; i < m_lines.size(); ++i) {
        combined += m_lines[i];
        if (i + 1 < m_lines.size()) combined += m_lineEnding;
    }
    if (m_hasTrailingNewline && !m_lines.empty()) {
        combined += m_lineEnding;
    }

    std::string encoded;
    if (EncodeTextBytes(combined, m_encoding, encoded) && !encoded.empty()) {
        file.write(encoded.data(), encoded.size());
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

namespace {
std::wstring ResolveLowerExt(const std::wstring& explicitExt, const std::wstring& filePath, const std::wstring& fileName) {
    std::wstring ext = explicitExt;
    if (ext.empty()) {
        std::wstring src = !filePath.empty() ? filePath : fileName;
        auto dot = src.find_last_of(L'.');
        if (dot != std::wstring::npos) ext = src.substr(dot + 1);
    }
    if (!ext.empty() && ext[0] == L'.') ext = ext.substr(1);
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    return ext;
}

bool IsInsidePhpBlockAtLine(const std::vector<std::wstring>& lines, size_t targetLine) {
    bool inPhp = false;
    for (size_t i = 0; i <= targetLine && i < lines.size(); ++i) {
        const auto& ln = lines[i];
        size_t pos = 0;
        while (pos < ln.size()) {
            if (!inPhp) {
                size_t open = ln.find(L"<?", pos);
                if (open == std::wstring::npos) break;
                inPhp = true;
                pos = open + 2;
            } else {
                size_t close = ln.find(L"?>", pos);
                if (close == std::wstring::npos) break;
                inPhp = false;
                pos = close + 2;
            }
        }
        if (i == targetLine) {
            size_t firstNonWs = ln.find_first_not_of(L" \t");
            if (firstNonWs != std::wstring::npos && ln[firstNonWs] == L'<' &&
                ln.compare(firstNonWs, 2, L"<?") != 0 && !inPhp) {
                return false;
            }
        }
    }
    return inPhp;
}
} // namespace

bool Buffer::MoveLinesUp(size_t sLine, size_t eLine, size_t& outNewSLine, size_t& outNewELine) {
    if (m_lines.empty() || sLine == 0 || sLine > eLine || eLine >= m_lines.size()) return false;
    PushUndo();
    std::wstring above = std::move(m_lines[sLine - 1]);
    for (size_t l = sLine; l <= eLine; ++l) {
        m_lines[l - 1] = std::move(m_lines[l]);
    }
    m_lines[eLine] = std::move(above);
    outNewSLine = sLine - 1;
    outNewELine = eLine - 1;
    if (m_cursor.line > 0) m_cursor.line--;
    m_tokenCache.clear();
    m_isModified = true;
    ++m_version;
    return true;
}

bool Buffer::MoveLinesDown(size_t sLine, size_t eLine, size_t& outNewSLine, size_t& outNewELine) {
    if (m_lines.empty() || sLine > eLine || eLine + 1 >= m_lines.size()) return false;
    PushUndo();
    std::wstring below = std::move(m_lines[eLine + 1]);
    for (size_t l = eLine + 1; l > sLine; --l) {
        m_lines[l] = std::move(m_lines[l - 1]);
    }
    m_lines[sLine] = std::move(below);
    outNewSLine = sLine + 1;
    outNewELine = eLine + 1;
    if (m_cursor.line + 1 < m_lines.size()) m_cursor.line++;
    m_tokenCache.clear();
    m_isModified = true;
    ++m_version;
    return true;
}

bool Buffer::DuplicateLines(size_t sLine, size_t eLine, bool up, size_t& outNewSLine, size_t& outNewELine) {
    if (m_lines.empty() || sLine > eLine || eLine >= m_lines.size()) return false;
    PushUndo();
    std::vector<std::wstring> copySlice(m_lines.begin() + sLine, m_lines.begin() + eLine + 1);
    const size_t count = copySlice.size();
    if (up) {
        m_lines.insert(m_lines.begin() + sLine, copySlice.begin(), copySlice.end());
        outNewSLine = sLine;
        outNewELine = eLine;
    } else {
        m_lines.insert(m_lines.begin() + eLine + 1, copySlice.begin(), copySlice.end());
        outNewSLine = eLine + 1;
        outNewELine = eLine + count;
        m_cursor.line += count;
    }
    m_tokenCache.clear();
    m_isModified = true;
    ++m_version;
    return true;
}

bool Buffer::JoinLines(size_t sLine, size_t eLine, bool hasSel, CursorPosition& outCursor) {
    if (m_lines.empty()) return false;
    if (sLine >= m_lines.size()) sLine = m_lines.size() - 1;
    if (eLine >= m_lines.size()) eLine = m_lines.size() - 1;
    if (!hasSel || sLine == eLine) {
        if (sLine + 1 >= m_lines.size()) return false;
        eLine = sLine + 1;
    }
    if (sLine >= eLine) return false;

    PushUndo();
    std::wstring joined = m_lines[sLine];
    size_t seamCol = joined.length();
    for (size_t l = sLine + 1; l <= eLine; ++l) {
        const auto& nextLine = m_lines[l];
        size_t firstNonWs = nextLine.find_first_not_of(L" \t");
        std::wstring trimmedNext = (firstNonWs == std::wstring::npos) ? L"" : nextLine.substr(firstNonWs);
        if (!joined.empty() && !trimmedNext.empty()) {
            wchar_t lastCh = joined.back();
            if (lastCh != L' ' && lastCh != L'\t') {
                joined += L' ';
            }
        }
        seamCol = joined.length();
        joined += trimmedNext;
    }
    m_lines[sLine] = std::move(joined);
    m_lines.erase(m_lines.begin() + sLine + 1, m_lines.begin() + eLine + 1);
    m_cursor = { sLine, seamCol };
    outCursor = m_cursor;
    m_tokenCache.clear();
    m_isModified = true;
    ++m_version;
    return true;
}

bool Buffer::SortLines(size_t sLine, size_t eLine, bool ascending, bool caseSensitive) {
    if (m_lines.size() < 2) return false;
    if (sLine >= m_lines.size()) sLine = m_lines.size() - 1;
    if (eLine >= m_lines.size()) eLine = m_lines.size() - 1;
    if (sLine >= eLine) return false;

    PushUndo();
    auto comp = [ascending, caseSensitive](const std::wstring& a, const std::wstring& b) {
        if (caseSensitive) {
            return ascending ? (a < b) : (a > b);
        }
        std::wstring al = a, bl = b;
        for (auto& c : al) c = static_cast<wchar_t>(towlower(c));
        for (auto& c : bl) c = static_cast<wchar_t>(towlower(c));
        if (al != bl) return ascending ? (al < bl) : (al > bl);
        return ascending ? (a < b) : (a > b);
    };
    std::stable_sort(m_lines.begin() + sLine, m_lines.begin() + eLine + 1, comp);
    m_tokenCache.clear();
    m_isModified = true;
    ++m_version;
    return true;
}

bool Buffer::TrimTrailingWhitespace(size_t sLine, size_t eLine) {
    if (m_lines.empty()) return false;
    if (sLine >= m_lines.size()) sLine = m_lines.size() - 1;
    if (eLine >= m_lines.size()) eLine = m_lines.size() - 1;
    if (sLine > eLine) return false;

    bool anyChanged = false;
    for (size_t l = sLine; l <= eLine; ++l) {
        size_t endPos = m_lines[l].find_last_not_of(L" \t");
        size_t newLen = (endPos == std::wstring::npos) ? 0 : (endPos + 1);
        if (newLen < m_lines[l].length()) {
            if (!anyChanged) {
                PushUndo();
                anyChanged = true;
            }
            m_lines[l].resize(newLen);
            m_tokenCache.erase(l);
        }
    }
    if (anyChanged) {
        if (m_cursor.line < m_lines.size() && m_cursor.col > m_lines[m_cursor.line].length()) {
            m_cursor.col = m_lines[m_cursor.line].length();
        }
        m_isModified = true;
        ++m_version;
    }
    return anyChanged;
}

void Buffer::ToggleLineComment() {
    if (m_lines.empty()) return;
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;
    std::vector<int> deltas;
    if (ToggleLineCommentRange(m_cursor.line, m_cursor.line, L"", &deltas) && !deltas.empty()) {
        int d = deltas[0];
        if (d > 0) m_cursor.col += static_cast<size_t>(d);
        else if (d < 0) {
            size_t sub = static_cast<size_t>(-d);
            m_cursor.col = (m_cursor.col >= sub) ? (m_cursor.col - sub) : 0;
        }
        if (m_cursor.col > m_lines[m_cursor.line].length()) m_cursor.col = m_lines[m_cursor.line].length();
    }
}

bool Buffer::ToggleLineCommentRange(size_t sLine, size_t eLine, const std::wstring& explicitExt, std::vector<int>* outLineColDeltas) {
    if (m_lines.empty()) return false;
    if (sLine >= m_lines.size()) sLine = m_lines.size() - 1;
    if (eLine >= m_lines.size()) eLine = m_lines.size() - 1;
    if (sLine > eLine) std::swap(sLine, eLine);

    if (outLineColDeltas) outLineColDeltas->assign(eLine - sLine + 1, 0);

    std::wstring ext = ResolveLowerExt(explicitExt, m_filePath, m_fileName);
    bool useXml = (ext == L"html" || ext == L"htm" || ext == L"xml" || ext == L"svg" || ext == L"md" || ext == L"xhtml" || ext == L"vue");
    bool useBlockAsLine = (ext == L"css" || ext == L"scss" || ext == L"less");
    bool useHash = (ext == L"py" || ext == L"sh" || ext == L"bash" || ext == L"yml" || ext == L"yaml" ||
                    ext == L"toml" || ext == L"env" || ext == L"rb" || ext == L"pl" || ext == L"ini");
    bool useDash = (ext == L"sql" || ext == L"lua");
    if ((ext == L"php" || ext == L"phtml" || ext == L"php8" || ext == L"php7") && !IsInsidePhpBlockAtLine(m_lines, sLine)) {
        // Selected markup lines in a mixed PHP/HTML template outside <?php ... ?>
        bool looksLikeHtml = false;
        for (size_t l = sLine; l <= eLine; ++l) {
            size_t fnw = m_lines[l].find_first_not_of(L" \t");
            if (fnw != std::wstring::npos && m_lines[l][fnw] == L'<') {
                looksLikeHtml = true;
                break;
            }
        }
        if (looksLikeHtml) useXml = true;
    }

    PushUndo();

    if (useXml || useBlockAsLine) {
        const std::wstring openTok = useXml ? L"<!--" : L"/*";
        const std::wstring closeTok = useXml ? L"-->" : L"*/";
        bool allCommented = true;
        bool hasNonBlank = false;
        for (size_t l = sLine; l <= eLine; ++l) {
            const auto& ln = m_lines[l];
            size_t firstNonWs = ln.find_first_not_of(L" \t");
            if (firstNonWs == std::wstring::npos) continue;
            hasNonBlank = true;
            size_t lastNonWs = ln.find_last_not_of(L" \t");
            std::wstring trimmed = ln.substr(firstNonWs, lastNonWs - firstNonWs + 1);
            if (trimmed.rfind(openTok, 0) != 0 || trimmed.size() < openTok.size() + closeTok.size() ||
                trimmed.compare(trimmed.size() - closeTok.size(), closeTok.size(), closeTok) != 0) {
                allCommented = false;
                break;
            }
        }
        if (!hasNonBlank) return false;
        for (size_t l = sLine; l <= eLine; ++l) {
            auto& ln = m_lines[l];
            size_t firstNonWs = ln.find_first_not_of(L" \t");
            if (firstNonWs == std::wstring::npos) continue;
            size_t lastNonWs = ln.find_last_not_of(L" \t");
            if (allCommented) {
                size_t closePos = lastNonWs + 1 - closeTok.size();
                size_t closeErasePos = (closePos > firstNonWs + openTok.size() && ln[closePos - 1] == L' ') ? (closePos - 1) : closePos;
                size_t closeEraseLen = closeTok.size() + (closePos - closeErasePos);
                ln.erase(closeErasePos, closeEraseLen);
                size_t openEraseLen = openTok.size() + ((firstNonWs + openTok.size() < ln.size() && ln[firstNonWs + openTok.size()] == L' ') ? 1 : 0);
                ln.erase(firstNonWs, openEraseLen);
                if (outLineColDeltas) (*outLineColDeltas)[l - sLine] = -static_cast<int>(openEraseLen);
            } else {
                std::wstring body = ln.substr(firstNonWs, lastNonWs - firstNonWs + 1);
                ln = ln.substr(0, firstNonWs) + openTok + L" " + body + L" " + closeTok;
                if (outLineColDeltas) (*outLineColDeltas)[l - sLine] = static_cast<int>(openTok.size() + 1);
            }
            m_tokenCache.erase(l);
        }
        m_isModified = true;
        ++m_version;
        return true;
    }

    const std::wstring prefix = useHash ? L"#" : (useDash ? L"--" : L"//");
    bool allCommented = true;
    bool hasNonBlank = false;
    size_t minIndent = (std::numeric_limits<size_t>::max)();
    for (size_t l = sLine; l <= eLine; ++l) {
        const auto& ln = m_lines[l];
        size_t firstNonWs = ln.find_first_not_of(L" \t");
        if (firstNonWs == std::wstring::npos) continue;
        hasNonBlank = true;
        if (firstNonWs < minIndent) minIndent = firstNonWs;
        if (ln.compare(firstNonWs, prefix.length(), prefix) != 0) {
            allCommented = false;
        }
    }
    if (!hasNonBlank) {
        minIndent = 0;
        allCommented = false;
    }

    for (size_t l = sLine; l <= eLine; ++l) {
        auto& ln = m_lines[l];
        size_t firstNonWs = ln.find_first_not_of(L" \t");
        if (hasNonBlank && firstNonWs == std::wstring::npos) continue;
        if (allCommented) {
            size_t removeLen = prefix.length();
            if (firstNonWs + removeLen < ln.length() && ln[firstNonWs + removeLen] == L' ') {
                removeLen++;
            }
            ln.erase(firstNonWs, removeLen);
            if (outLineColDeltas) (*outLineColDeltas)[l - sLine] = -static_cast<int>(removeLen);
        } else {
            size_t insertPos = (std::min)(minIndent, ln.length());
            std::wstring tok = prefix + L" ";
            ln.insert(insertPos, tok);
            if (outLineColDeltas) (*outLineColDeltas)[l - sLine] = static_cast<int>(tok.length());
        }
        m_tokenCache.erase(l);
    }

    m_isModified = true;
    ++m_version;
    return true;
}

void Buffer::ToggleBlockComment() {
    if (m_lines.empty()) return;
    CursorPosition outA{}, outC{};
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;
    ToggleBlockCommentRange(m_cursor.line, m_cursor.col, m_cursor.line, m_cursor.col, false, L"", outA, outC);
    m_cursor = outC;
}

bool Buffer::ToggleBlockCommentRange(size_t sLine, size_t sCol, size_t eLine, size_t eCol, bool hasSel, const std::wstring& explicitExt,
                                     CursorPosition& outAnchor, CursorPosition& outCursor) {
    if (m_lines.empty()) return false;
    if (sLine >= m_lines.size()) sLine = m_lines.size() - 1;
    if (eLine >= m_lines.size()) eLine = m_lines.size() - 1;
    if (sLine > eLine || (sLine == eLine && sCol > eCol)) {
        std::swap(sLine, eLine);
        std::swap(sCol, eCol);
    }

    std::wstring ext = ResolveLowerExt(explicitExt, m_filePath, m_fileName);
    bool useXml = (ext == L"html" || ext == L"htm" || ext == L"xml" || ext == L"svg" || ext == L"md" || ext == L"xhtml" || ext == L"vue");
    if ((ext == L"php" || ext == L"phtml" || ext == L"php8" || ext == L"php7") && !IsInsidePhpBlockAtLine(m_lines, sLine)) {
        size_t fnw = m_lines[sLine].find_first_not_of(L" \t");
        if (fnw != std::wstring::npos && m_lines[sLine][fnw] == L'<') useXml = true;
    }
    const std::wstring openTok = useXml ? L"<!--" : L"/*";
    const std::wstring closeTok = useXml ? L"-->" : L"*/";

    PushUndo();

    if (!hasSel || (sLine == eLine && sCol == eCol)) {
        auto& line = m_lines[sLine];
        size_t openPos = line.find(openTok);
        size_t closePos = line.rfind(closeTok);
        if (openPos != std::wstring::npos && closePos != std::wstring::npos && closePos >= openPos + openTok.size()) {
            size_t closeStart = (closePos > 0 && line[closePos - 1] == L' ') ? (closePos - 1) : closePos;
            size_t closeLen = closeTok.size() + (closePos - closeStart);
            line.erase(closeStart, closeLen);
            size_t openLen = openTok.size() + ((openPos + openTok.size() < line.length() && line[openPos + openTok.size()] == L' ') ? 1 : 0);
            line.erase(openPos, openLen);
            outCursor = { sLine, (m_cursor.col > openPos) ? (m_cursor.col >= openLen ? m_cursor.col - openLen : openPos) : m_cursor.col };
        } else {
            line = openTok + L" " + line + L" " + closeTok;
            outCursor = { sLine, m_cursor.col + openTok.size() + 1 };
        }
        outAnchor = outCursor;
        m_tokenCache.erase(sLine);
        m_isModified = true;
        ++m_version;
        return true;
    }

    sCol = (std::min)(sCol, m_lines[sLine].length());
    eCol = (std::min)(eCol, m_lines[eLine].length());

    if (sLine == eLine) {
        auto& line = m_lines[sLine];
        std::wstring sel = line.substr(sCol, eCol - sCol);
        if (sel.size() >= openTok.size() + closeTok.size() &&
            sel.rfind(openTok, 0) == 0 &&
            sel.compare(sel.size() - closeTok.size(), closeTok.size(), closeTok) == 0) {
            std::wstring inner = sel.substr(openTok.size(), sel.size() - openTok.size() - closeTok.size());
            if (!inner.empty() && inner.front() == L' ') inner.erase(0, 1);
            if (!inner.empty() && inner.back() == L' ') inner.pop_back();
            line.replace(sCol, eCol - sCol, inner);
            outAnchor = { sLine, sCol };
            outCursor = { sLine, sCol + inner.size() };
        } else {
            std::wstring wrapped = openTok + L" " + sel + L" " + closeTok;
            line.replace(sCol, eCol - sCol, wrapped);
            outAnchor = { sLine, sCol };
            outCursor = { sLine, sCol + wrapped.size() };
        }
        m_tokenCache.erase(sLine);
    } else {
        std::wstring firstSeg = m_lines[sLine].substr(sCol);
        std::wstring lastSeg = m_lines[eLine].substr(0, eCol);
        bool firstHasOpen = (firstSeg.rfind(openTok, 0) == 0);
        bool lastHasClose = (lastSeg.size() >= closeTok.size() && lastSeg.compare(lastSeg.size() - closeTok.size(), closeTok.size(), closeTok) == 0);
        if (firstHasOpen && lastHasClose) {
            size_t openStrip = openTok.size() + ((openTok.size() < firstSeg.size() && firstSeg[openTok.size()] == L' ') ? 1 : 0);
            m_lines[sLine].erase(sCol, openStrip);
            size_t closePos = eCol - closeTok.size();
            size_t closeStrip = closeTok.size() + ((closePos > 0 && m_lines[eLine][closePos - 1] == L' ') ? 1 : 0);
            size_t eraseAt = eCol - closeStrip;
            m_lines[eLine].erase(eraseAt, closeStrip);
            outAnchor = { sLine, sCol };
            outCursor = { eLine, eraseAt };
        } else {
            m_lines[sLine].insert(sCol, openTok + L" ");
            m_lines[eLine].insert(eCol, L" " + closeTok);
            outAnchor = { sLine, sCol };
            outCursor = { eLine, eCol + 1 + closeTok.size() };
        }
        m_tokenCache.clear();
    }

    m_cursor = outCursor;
    m_isModified = true;
    ++m_version;
    return true;
}

bool Buffer::ExpandEmmet(const std::wstring& fileExt) {
    if (m_lines.empty()) return false;
    if (m_cursor.line >= m_lines.size()) m_cursor.line = m_lines.size() - 1;
    auto& line = m_lines[m_cursor.line];
    if (m_cursor.col == 0 || line.empty()) return false;

    std::wstring ext = fileExt;
    if (ext.empty() && !m_filePath.empty()) {
        std::filesystem::path p(m_filePath);
        ext = p.extension().wstring();
        if (!ext.empty() && ext[0] == L'.') ext = ext.substr(1);
    }

    bool isCss = (ext == L"css" || ext == L"scss" || ext == L"less");

    size_t startCol = 0;
    std::wstring abbr = EmmetEngine::ExtractAbbreviation(line, m_cursor.col, isCss, &startCol);
    if (abbr.empty() || !EmmetEngine::IsEmmetAbbreviation(abbr, isCss)) {
        return false;
    }

    // Determine current line indentation
    size_t indentLen = 0;
    while (indentLen < startCol && (line[indentLen] == L' ' || line[indentLen] == L'\t')) {
        indentLen++;
    }
    std::wstring lineIndent = line.substr(0, indentLen);
    std::wstring indentUnit = L"    "; // 4 spaces default

    size_t cursorOffset = std::wstring::npos;
    std::wstring expansion = EmmetEngine::Expand(abbr, indentUnit, lineIndent, isCss, &cursorOffset);
    if (expansion.empty()) {
        return false;
    }

    PushUndo();

    // Split expansion into lines
    std::vector<std::wstring> expLines;
    {
        std::wstringstream ss(expansion);
        std::wstring l;
        while (std::getline(ss, l)) {
            if (!l.empty() && l.back() == L'\r') l.pop_back();
            expLines.push_back(l);
        }
    }
    if (expLines.empty()) expLines.push_back(expansion);

    std::wstring suffix = line.substr(m_cursor.col);

    if (expLines.size() == 1) {
        std::wstring rep = expLines[0];
        if (!lineIndent.empty() && rep.rfind(lineIndent, 0) == 0) {
            rep = rep.substr(lineIndent.length());
        }
        line = line.substr(0, startCol) + rep + suffix;
        if (cursorOffset != std::wstring::npos && cursorOffset <= expLines[0].length()) {
            size_t relOffset = cursorOffset;
            if (!lineIndent.empty() && relOffset >= lineIndent.length()) {
                relOffset -= lineIndent.length();
            }
            m_cursor.col = startCol + relOffset;
        } else {
            m_cursor.col = startCol + rep.length();
        }
    } else {
        std::wstring repFirst = expLines[0];
        if (!lineIndent.empty() && repFirst.rfind(lineIndent, 0) == 0) {
            repFirst = repFirst.substr(lineIndent.length());
        }
        line = line.substr(0, startCol) + repFirst;

        size_t baseLine = m_cursor.line;
        for (size_t i = 1; i < expLines.size(); ++i) {
            std::wstring nextLine = expLines[i];
            if (i == expLines.size() - 1) {
                nextLine += suffix;
            }
            m_lines.insert(m_lines.begin() + baseLine + i, nextLine);
        }

        if (cursorOffset != std::wstring::npos) {
            size_t running = 0;
            size_t targetLine = 0;
            size_t targetCol = 0;
            bool found = false;
            for (size_t i = 0; i < expLines.size(); ++i) {
                size_t lLen = expLines[i].length() + 1;
                if (cursorOffset <= running + lLen) {
                    targetLine = i;
                    targetCol = cursorOffset - running;
                    found = true;
                    break;
                }
                running += lLen;
            }
            if (found) {
                m_cursor.line = baseLine + targetLine;
                if (targetLine == 0) {
                    size_t relCol = targetCol;
                    if (!lineIndent.empty() && relCol >= lineIndent.length()) relCol -= lineIndent.length();
                    m_cursor.col = startCol + relCol;
                } else {
                    m_cursor.col = targetCol;
                }
            } else {
                m_cursor.line = baseLine + expLines.size() - 1;
                m_cursor.col = expLines.back().length();
            }
        } else {
            m_cursor.line = baseLine + expLines.size() - 1;
            m_cursor.col = expLines.back().length();
        }
    }

    m_tokenCache.clear();
    m_isModified = true;
    ++m_version;
    return true;
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

    m_tokenCache.clear();
    m_isModified = true;
    ++m_version;
    return true;
}

} // namespace phide::editor
