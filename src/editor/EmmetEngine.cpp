/**
 * @file EmmetEngine.cpp
 * @brief High-performance native Emmet abbreviation parser and multi-line structured code expander.
 * Authoritative implementation supporting standard Emmet markup and CSS stylesheet abbreviations.
 */

#include "phide/editor/EmmetEngine.hpp"
#include <algorithm>
#include <cwctype>
#include <sstream>
#include <stack>
#include <unordered_map>
#include <unordered_set>

namespace phide::editor {

namespace {

struct EmmetAttr {
    std::wstring name;
    std::wstring value;
    bool hasValue = true;
};

struct EmmetAstNode {
    std::wstring tag;
    std::wstring id;
    std::vector<std::wstring> classes;
    std::vector<EmmetAttr> attributes;
    std::wstring textContent;
    int count = 1;
    bool wrapEachLine = false;
    bool isLorem = false;
    size_t loremWords = 30;
    std::vector<size_t> childIds;
};

// Check if a character is a delimiter for Emmet abbreviations
bool IsEmmetChar(wchar_t c, bool isCss) {
    if (iswalnum(c)) return true;
    if (c == L'-' || c == L'_' || c == L':' || c == L'.' || c == L'#') return true;
    if (isCss) {
        return c == L'%' || c == L'!' || c == L'@';
    }
    return c == L'>' || c == L'+' || c == L'^' || c == L'*' || c == L'$' ||
           c == L'[' || c == L']' || c == L'=' || c == L'{' || c == L'}' ||
           c == L'(' || c == L')' || c == L'!' || c == L'@' || c == L'/' ||
           c == L'\"' || c == L'\'';
}

std::wstring Trim(const std::wstring& s) {
    size_t start = 0;
    while (start < s.length() && iswspace(s[start])) start++;
    size_t end = s.length();
    while (end > start && iswspace(s[end - 1])) end--;
    return s.substr(start, end - start);
}

} // anonymous namespace

bool EmmetEngine::IsVoidTag(const std::wstring& tag) {
    static const std::unordered_set<std::wstring> voidTags = {
        L"area", L"base", L"br", L"col", L"embed", L"hr", L"img", L"input",
        L"link", L"meta", L"param", L"source", L"track", L"wbr"
    };
    return voidTags.count(tag) > 0;
}

std::wstring EmmetEngine::ResolveDefaultTag(const std::wstring& parentTag) {
    if (parentTag == L"ul" || parentTag == L"ol") return L"li";
    if (parentTag == L"table" || parentTag == L"tbody" || parentTag == L"thead" || parentTag == L"tfoot") return L"tr";
    if (parentTag == L"tr") return L"td";
    if (parentTag == L"select" || parentTag == L"optgroup" || parentTag == L"datalist") return L"option";
    if (parentTag == L"audio" || parentTag == L"video") return L"source";
    if (parentTag == L"dl") return L"dt";
    return L"div";
}

std::wstring EmmetEngine::GenerateLorem(size_t wordCount) {
    static const std::vector<std::wstring> vocab = {
        L"lorem", L"ipsum", L"dolor", L"sit", L"amet", L"consectetur", L"adipiscing", L"elit",
        L"sed", L"do", L"eiusmod", L"tempor", L"incididunt", L"ut", L"labore", L"et",
        L"dolore", L"magna", L"aliqua", L"ut", L"enim", L"ad", L"minim", L"veniam",
        L"quis", L"nostrud", L"exercitation", L"ullamco", L"laboris", L"nisi", L"ut",
        L"aliquip", L"ex", L"ea", L"commodo", L"consequat", L"duis", L"aute", L"irure",
        L"in", L"reprehenderit", L"in", L"voluptate", L"velit", L"esse", L"cillum",
        L"eu", L"fugiat", L"nulla", L"pariatur", L"excepteur", L"sint", L"occaecat",
        L"cupidatat", L"non", L"proident", L"sunt", L"in", L"culpa", L"qui", L"officia",
        L"deserunt", L"mollit", L"anim", L"id", L"est", L"laborum"
    };
    if (wordCount == 0) wordCount = 30;
    std::wstring res;
    for (size_t i = 0; i < wordCount; ++i) {
        if (i > 0) res += L" ";
        std::wstring w = vocab[i % vocab.size()];
        if (i == 0 && !w.empty()) {
            w[0] = towupper(w[0]);
        }
        res += w;
    }
    res += L".";
    return res;
}

bool EmmetEngine::IsEmmetAbbreviation(const std::wstring& abbr, bool isCss) {
    if (abbr.empty()) return false;
    if (isCss) {
        return !ExpandCss(abbr).empty();
    }

    if (abbr == L"!" || abbr == L"html:5" || abbr == L"doc") return true;

    // Check for structural operators
    if (abbr.find_first_of(L">*+^$") != std::wstring::npos) return true;
    if (abbr.find(L'.') != std::wstring::npos && abbr.length() >= 2) return true;
    if (abbr.find(L'#') != std::wstring::npos && abbr.length() >= 2) return true;
    if (abbr.find(L'[') != std::wstring::npos && abbr.find(L']') != std::wstring::npos) return true;
    if (abbr.find(L'{') != std::wstring::npos && abbr.find(L'}') != std::wstring::npos) return true;

    // Known common HTML tags and tag:sub tags
    static const std::unordered_set<std::wstring> knownTags = {
        L"div", L"span", L"p", L"a", L"button", L"input", L"form", L"ul", L"ol", L"li",
        L"header", L"footer", L"nav", L"section", L"article", L"aside", L"main",
        L"h1", L"h2", L"h3", L"h4", L"h5", L"h6", L"table", L"tr", L"td", L"th",
        L"thead", L"tbody", L"tfoot", L"label", L"select", L"option", L"img", L"link",
        L"script", L"style", L"meta", L"hr", L"br", L"textarea", L"iframe", L"canvas",
        L"svg", L"audio", L"video", L"source", L"figure", L"figcaption", L"blockquote",
        L"code", L"pre", L"strong", L"em", L"i", L"b", L"small", L"details", L"summary",
        L"input:text", L"input:password", L"input:checkbox", L"input:radio", L"input:submit",
        L"input:button", L"input:file", L"input:email", L"input:number", L"input:hidden",
        L"link:css", L"link:favicon", L"script:src", L"form:post", L"form:get", L"a:link",
        L"a:mail", L"a:tel", L"a:blank", L"btn", L"btn:s", L"btn:b"
    };

    if (knownTags.count(abbr) > 0) return true;
    if (abbr.rfind(L"lorem", 0) == 0 || abbr.rfind(L"lipsum", 0) == 0) return true;

    return false;
}

std::wstring EmmetEngine::ExtractAbbreviation(
    const std::wstring& line,
    size_t cursorCol,
    bool isCss,
    size_t* pStartCol
) {
    if (line.empty() || cursorCol == 0) {
        if (pStartCol) *pStartCol = 0;
        return L"";
    }

    size_t col = (std::min)(cursorCol, line.length());
    size_t start = col;

    int bracketDepth = 0;
    int braceDepth = 0;
    int parenDepth = 0;

    while (start > 0) {
        wchar_t c = line[start - 1];

        if (c == L']') bracketDepth++;
        else if (c == L'[') bracketDepth = (std::max)(0, bracketDepth - 1);
        else if (c == L'}') braceDepth++;
        else if (c == L'{') braceDepth = (std::max)(0, braceDepth - 1);
        else if (c == L')') parenDepth++;
        else if (c == L'(') parenDepth = (std::max)(0, parenDepth - 1);

        if (bracketDepth == 0 && braceDepth == 0 && parenDepth == 0) {
            if (c == L' ' || c == L'\t' || c == L'<' || c == L';') {
                break;
            }
            if (c == L'>') {
                // If preceded by '/' (e.g. /> or </tag>), this is tag close, not Emmet child
                if (start >= 2 && line[start - 2] == L'/') {
                    break;
                }
            }
        }

        start--;
    }

    // Skip any leading whitespace or stray operators
    while (start < col && (line[start] == L' ' || line[start] == L'\t' || line[start] == L'>')) {
        start++;
    }

    if (start >= col) {
        if (pStartCol) *pStartCol = col;
        return L"";
    }

    if (pStartCol) *pStartCol = start;
    return line.substr(start, col - start);
}

std::wstring EmmetEngine::ExpandCss(const std::wstring& abbr) {
    if (abbr.empty()) return L"";

    // Common exact CSS abbreviations
    static const std::unordered_map<std::wstring, std::wstring> exactCss = {
        { L"df", L"display: flex;" },
        { L"db", L"display: block;" },
        { L"dib", L"display: inline-block;" },
        { L"dn", L"display: none;" },
        { L"dg", L"display: grid;" },
        { L"di", L"display: inline;" },
        { L"pos:a", L"position: absolute;" },
        { L"poa", L"position: absolute;" },
        { L"pos:r", L"position: relative;" },
        { L"por", L"position: relative;" },
        { L"pos:f", L"position: fixed;" },
        { L"pof", L"position: fixed;" },
        { L"pos:s", L"position: static;" },
        { L"pos", L"position: static;" },
        { L"pos:sk", L"position: sticky;" },
        { L"posk", L"position: sticky;" },
        { L"tac", L"text-align: center;" },
        { L"tar", L"text-align: right;" },
        { L"tal", L"text-align: left;" },
        { L"taj", L"text-align: justify;" },
        { L"aic", L"align-items: center;" },
        { L"aifs", L"align-items: flex-start;" },
        { L"aife", L"align-items: flex-end;" },
        { L"ais", L"align-items: stretch;" },
        { L"aib", L"align-items: baseline;" },
        { L"jcc", L"justify-content: center;" },
        { L"jcsb", L"justify-content: space-between;" },
        { L"jc:sb", L"justify-content: space-between;" },
        { L"jcsa", L"justify-content: space-around;" },
        { L"jc:sa", L"justify-content: space-around;" },
        { L"jcse", L"justify-content: space-evenly;" },
        { L"jc:se", L"justify-content: space-evenly;" },
        { L"jcfs", L"justify-content: flex-start;" },
        { L"jcfe", L"justify-content: flex-end;" },
        { L"fdr", L"flex-direction: row;" },
        { L"fdc", L"flex-direction: column;" },
        { L"fdrr", L"flex-direction: row-reverse;" },
        { L"fdcr", L"flex-direction: column-reverse;" },
        { L"fww", L"flex-wrap: wrap;" },
        { L"fwnw", L"flex-wrap: nowrap;" },
        { L"fwb", L"font-weight: bold;" },
        { L"fwn", L"font-weight: normal;" },
        { L"fsi", L"font-style: italic;" },
        { L"fsn", L"font-style: normal;" },
        { L"tdn", L"text-decoration: none;" },
        { L"tdu", L"text-decoration: underline;" },
        { L"ttu", L"text-transform: uppercase;" },
        { L"ttl", L"text-transform: lowercase;" },
        { L"ttc", L"text-transform: capitalize;" },
        { L"ovh", L"overflow: hidden;" },
        { L"ova", L"overflow: auto;" },
        { L"ovs", L"overflow: scroll;" },
        { L"cur:p", L"cursor: pointer;" },
        { L"cup", L"cursor: pointer;" },
        { L"cur:d", L"cursor: default;" },
        { L"cur:not", L"cursor: not-allowed;" },
        { L"bs:bb", L"box-sizing: border-box;" },
        { L"bs:cb", L"box-sizing: content-box;" },
        { L"usn", L"user-select: none;" },
        { L"pen", L"pointer-events: none;" },
        { L"pea", L"pointer-events: auto;" }
    };

    auto it = exactCss.find(abbr);
    if (it != exactCss.end()) {
        return it->second;
    }

    // Property prefixes
    static const std::vector<std::pair<std::wstring, std::wstring>> propPrefixes = {
        { L"maw", L"max-width" },
        { L"mah", L"max-height" },
        { L"miw", L"min-width" },
        { L"mih", L"min-height" },
        { L"mt", L"margin-top" },
        { L"mr", L"margin-right" },
        { L"mb", L"margin-bottom" },
        { L"ml", L"margin-left" },
        { L"pt", L"padding-top" },
        { L"pr", L"padding-right" },
        { L"pb", L"padding-bottom" },
        { L"pl", L"padding-left" },
        { L"bdt", L"border-top" },
        { L"bdb", L"border-bottom" },
        { L"bdl", L"border-left" },
        { L"bdr", L"border-right" },
        { L"bdrs", L"border-radius" },
        { L"bgc", L"background-color" },
        { L"bd", L"border" },
        { L"fz", L"font-size" },
        { L"fs", L"font-size" },
        { L"lh", L"line-height" },
        { L"op", L"opacity" },
        { L"m", L"margin" },
        { L"p", L"padding" },
        { L"w", L"width" },
        { L"h", L"height" },
        { L"t", L"top" },
        { L"r", L"right" },
        { L"b", L"bottom" },
        { L"l", L"left" },
        { L"z", L"z-index" },
        { L"c", L"color" }
    };

    for (const auto& [prefix, prop] : propPrefixes) {
        if (abbr.rfind(prefix, 0) == 0 && abbr.length() > prefix.length()) {
            std::wstring rest = abbr.substr(prefix.length());
            if (rest[0] == L':') rest = rest.substr(1);

            // Check if rest is color (e.g. #fff or 333)
            if (rest[0] == L'#' || (rest.length() >= 3 && std::all_of(rest.begin(), rest.end(), iswxdigit))) {
                std::wstring hex = rest[0] == L'#' ? rest : L"#" + rest;
                return prop + L": " + hex + L";";
            }

            // Check if rest has multiple numbers separated by hyphens (e.g. 10-20 or 10-20-30-40)
            if (rest.find(L'-') != std::wstring::npos && (rest[0] == L'-' ? rest.rfind(L'-') > 0 : true)) {
                std::wstringstream ss(rest);
                std::wstring token;
                std::wstring combined;
                while (std::getline(ss, token, L'-')) {
                    if (token.empty()) continue;
                    if (!combined.empty()) combined += L" ";
                    if (token == L"a" || token == L"auto") {
                        combined += L"auto";
                    } else {
                        combined += token + L"px";
                    }
                }
                if (!combined.empty()) {
                    return prop + L": " + combined + L";";
                }
            }

            // Rest has single number + optional unit
            size_t numEnd = 0;
            while (numEnd < rest.length() && (iswdigit(rest[numEnd]) || rest[numEnd] == L'.' || (numEnd == 0 && rest[numEnd] == L'-'))) {
                numEnd++;
            }

            if (numEnd > 0) {
                std::wstring numVal = rest.substr(0, numEnd);
                std::wstring unitPart = rest.substr(numEnd);
                std::wstring unit = L"px";

                if (unitPart == L"p" || unitPart == L"%") unit = L"%";
                else if (unitPart == L"e" || unitPart == L"em") unit = L"em";
                else if (unitPart == L"rem") unit = L"rem";
                else if (unitPart == L"vh") unit = L"vh";
                else if (unitPart == L"vw") unit = L"vw";
                else if (unitPart == L"pt") unit = L"pt";
                else if (!unitPart.empty()) unit = unitPart;

                // For properties like z-index, opacity, line-height without explicit unit: unitless
                if (prop == L"z-index" || prop == L"opacity" || prop == L"line-height") {
                    if (unitPart.empty()) unit = L"";
                }

                return prop + L": " + numVal + unit + L";";
            }
        }
    }

    return L"";
}

namespace {

// Parses a single tag token like "div.container#main[title='demo']{Hello}*3"
EmmetAstNode ParseSingleToken(const std::wstring& s) {
    EmmetAstNode node;
    if (s.empty()) return node;

    std::wstring str = s;

    // Check multiplier *N at the end (not inside {} or [])
    int bDepth = 0, braceDepth = 0;
    size_t starPos = std::wstring::npos;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == L'[') bDepth++;
        else if (str[i] == L']') bDepth = (std::max)(0, bDepth - 1);
        else if (str[i] == L'{') braceDepth++;
        else if (str[i] == L'}') braceDepth = (std::max)(0, braceDepth - 1);
        else if (str[i] == L'*' && bDepth == 0 && braceDepth == 0) {
            starPos = i;
        }
    }

    if (starPos != std::wstring::npos) {
        std::wstring countStr = Trim(str.substr(starPos + 1));
        if (countStr.empty()) {
            node.wrapEachLine = true;
            node.count = 1;
            str = str.substr(0, starPos);
        } else {
            bool allDig = true;
            for (wchar_t c : countStr) {
                if (!iswdigit(c)) { allDig = false; break; }
            }
            if (allDig) {
                node.count = (std::max)(1, _wtoi(countStr.c_str()));
                str = str.substr(0, starPos);
            }
        }
    }

    // Check for lorem / lipsum
    if (str.rfind(L"lorem", 0) == 0 || str.rfind(L"lipsum", 0) == 0) {
        node.isLorem = true;
        size_t prefixLen = (str.rfind(L"lorem", 0) == 0) ? 5 : 6;
        if (str.length() > prefixLen) {
            node.loremWords = (std::max)(1, _wtoi(str.substr(prefixLen).c_str()));
        } else {
            node.loremWords = 30;
        }
        return node;
    }

    // Parse tag name
    size_t pos = 0;
    size_t len = str.length();
    size_t tagEnd = 0;
    while (tagEnd < len && str[tagEnd] != L'.' && str[tagEnd] != L'#' &&
           str[tagEnd] != L'[' && str[tagEnd] != L'{') {
        tagEnd++;
    }

    if (tagEnd > 0) {
        node.tag = str.substr(0, tagEnd);
        pos = tagEnd;
    }

    // Parse classes, ids, attributes, and text
    while (pos < len) {
        wchar_t c = str[pos];

        if (c == L'#') {
            pos++;
            size_t idEnd = pos;
            while (idEnd < len && str[idEnd] != L'.' && str[idEnd] != L'#' &&
                   str[idEnd] != L'[' && str[idEnd] != L'{') {
                idEnd++;
            }
            node.id = str.substr(pos, idEnd - pos);
            pos = idEnd;
        } else if (c == L'.') {
            pos++;
            size_t clsEnd = pos;
            while (clsEnd < len && str[clsEnd] != L'.' && str[clsEnd] != L'#' &&
                   str[clsEnd] != L'[' && str[clsEnd] != L'{') {
                clsEnd++;
            }
            node.classes.push_back(str.substr(pos, clsEnd - pos));
            pos = clsEnd;
        } else if (c == L'[') {
            pos++;
            size_t bEnd = pos;
            int depth = 1;
            while (bEnd < len && depth > 0) {
                if (str[bEnd] == L'[') depth++;
                else if (str[bEnd] == L']') depth--;
                bEnd++;
            }
            std::wstring attrContent = str.substr(pos, bEnd - 1 - pos);
            pos = bEnd;

            // Tokenize attributes inside [...]
            size_t aPos = 0;
            while (aPos < attrContent.length()) {
                while (aPos < attrContent.length() && iswspace(attrContent[aPos])) aPos++;
                if (aPos >= attrContent.length()) break;

                size_t nameStart = aPos;
                while (aPos < attrContent.length() && !iswspace(attrContent[aPos]) &&
                       attrContent[aPos] != L'=') {
                    aPos++;
                }
                std::wstring aName = attrContent.substr(nameStart, aPos - nameStart);
                std::wstring aVal = L"";
                bool hasVal = false;

                if (aPos < attrContent.length() && attrContent[aPos] == L'=') {
                    aPos++;
                    hasVal = true;
                    if (aPos < attrContent.length() && (attrContent[aPos] == L'\"' || attrContent[aPos] == L'\'')) {
                        wchar_t quote = attrContent[aPos++];
                        size_t valStart = aPos;
                        while (aPos < attrContent.length() && attrContent[aPos] != quote) {
                            aPos++;
                        }
                        aVal = attrContent.substr(valStart, aPos - valStart);
                        if (aPos < attrContent.length() && attrContent[aPos] == quote) aPos++;
                    } else {
                        size_t valStart = aPos;
                        while (aPos < attrContent.length() && !iswspace(attrContent[aPos])) {
                            aPos++;
                        }
                        aVal = attrContent.substr(valStart, aPos - valStart);
                    }
                }
                node.attributes.push_back({ aName, aVal, hasVal });
            }
        } else if (c == L'{') {
            pos++;
            size_t tEnd = pos;
            int depth = 1;
            while (tEnd < len && depth > 0) {
                if (str[tEnd] == L'{') depth++;
                else if (str[tEnd] == L'}') depth--;
                tEnd++;
            }
            node.textContent = str.substr(pos, tEnd - 1 - pos);
            pos = tEnd;
        } else {
            pos++;
        }
    }

    return node;
}

// Substitute Emmet numbering placeholders: $, $$, $$$, $@-, $@N
std::wstring ApplyNumbering(const std::wstring& text, int index, int count) {
    if (text.empty() || text.find(L'$') == std::wstring::npos) return text;

    std::wstring out;
    size_t i = 0;
    while (i < text.length()) {
        if (text[i] == L'$') {
            size_t dollarCount = 0;
            while (i + dollarCount < text.length() && text[i + dollarCount] == L'$') {
                dollarCount++;
            }

            int numVal = index;
            size_t nextPos = i + dollarCount;

            // Check for modifier $@- or $@N
            if (nextPos + 1 < text.length() && text[nextPos] == L'@') {
                if (text[nextPos + 1] == L'-') {
                    numVal = count - index + 1;
                    nextPos += 2;
                } else if (iswdigit(text[nextPos + 1])) {
                    size_t startDig = nextPos + 1;
                    while (startDig < text.length() && iswdigit(text[startDig])) startDig++;
                    int startOffset = _wtoi(text.substr(nextPos + 1, startDig - (nextPos + 1)).c_str());
                    numVal = startOffset + index - 1;
                    nextPos = startDig;
                }
            }

            std::wstring numStr = std::to_wstring(numVal);
            if (numStr.length() < dollarCount) {
                numStr = std::wstring(dollarCount - numStr.length(), L'0') + numStr;
            }
            out += numStr;
            i = nextPos;
        } else {
            out += text[i++];
        }
    }
    return out;
}

// Render an AST node to HTML/JSX string using index-based arena
void RenderNode(
    const std::vector<EmmetAstNode>& arena,
    size_t nodeIdx,
    const std::wstring& parentTag,
    int index,
    int depth,
    const std::wstring& indentUnit,
    const std::wstring& lineIndent,
    bool isJsx,
    const std::vector<std::wstring>* pWrapLines,
    bool hasWrapLines,
    std::wstring& out,
    size_t* pFirstCursorPos
) {
    const EmmetAstNode& node = arena[nodeIdx];
    if (node.wrapEachLine && hasWrapLines && pWrapLines && !pWrapLines->empty() && pWrapLines->size() > 1) {
        // Handled per-item below when caller didn't split, or iterate here:
    }
    if (node.isLorem) {
        std::wstring lorem = EmmetEngine::GenerateLorem(node.loremWords);
        std::wstring curIndent = lineIndent;
        for (int d = 0; d < depth; ++d) curIndent += indentUnit;
        out += curIndent + lorem + L"\n";
        return;
    }

    std::wstring tag = node.tag;
    if (tag.empty()) {
        tag = EmmetEngine::ResolveDefaultTag(parentTag);
    }

    std::vector<EmmetAttr> attrs = node.attributes;
    if (tag == L"a" && std::none_of(attrs.begin(), attrs.end(), [](const auto& a) { return a.name == L"href"; })) {
        attrs.insert(attrs.begin(), { L"href", L"", true });
    } else if (tag == L"img") {
        if (std::none_of(attrs.begin(), attrs.end(), [](const auto& a) { return a.name == L"src"; })) {
            attrs.insert(attrs.begin(), { L"src", L"", true });
        }
        if (std::none_of(attrs.begin(), attrs.end(), [](const auto& a) { return a.name == L"alt"; })) {
            attrs.push_back({ L"alt", L"", true });
        }
    } else if (tag == L"link" && std::none_of(attrs.begin(), attrs.end(), [](const auto& a) { return a.name == L"rel"; })) {
        attrs.insert(attrs.begin(), { L"rel", L"stylesheet", true });
        attrs.push_back({ L"href", L"", true });
    }

    std::wstring curIndent = lineIndent;
    for (int d = 0; d < depth; ++d) curIndent += indentUnit;

    out += curIndent + L"<" + tag;

    if (!node.id.empty()) {
        out += L" id=\"" + ApplyNumbering(node.id, index, node.count) + L"\"";
    }

    if (!node.classes.empty()) {
        out += (isJsx ? L" className=\"" : L" class=\"");
        for (size_t c = 0; c < node.classes.size(); ++c) {
            if (c > 0) out += L" ";
            out += ApplyNumbering(node.classes[c], index, node.count);
        }
        out += L"\"";
    }

    for (const auto& attr : attrs) {
        out += L" " + attr.name;
        if (attr.hasValue) {
            out += L"=\"" + ApplyNumbering(attr.value, index, node.count) + L"\"";
        }
    }

    bool isVoid = EmmetEngine::IsVoidTag(tag);
    if (isVoid) {
        out += L" />\n";
        return;
    }

    out += L">";

    std::wstring text = ApplyNumbering(node.textContent, index, node.count);
    if (node.childIds.empty() && node.wrapEachLine && pWrapLines && index >= 1 && static_cast<size_t>(index) <= pWrapLines->size()) {
        const std::wstring& wrapLine = (*pWrapLines)[static_cast<size_t>(index - 1)];
        if (text.empty()) {
            text = wrapLine;
        } else {
            // Substitute $* or $0 or $# or append
            size_t ph = text.find(L"$#");
            if (ph != std::wstring::npos) {
                text.replace(ph, 2, wrapLine);
            } else {
                text += wrapLine;
            }
        }
    }

    if (node.childIds.empty()) {
        if (!text.empty()) {
            out += text;
        } else {
            if (pFirstCursorPos && *pFirstCursorPos == std::wstring::npos) {
                *pFirstCursorPos = out.length();
            }
        }
        out += L"</" + tag + L">\n";
        return;
    }

    if (!text.empty()) {
        out += text;
    }
    out += L"\n";

    for (size_t childIdx : node.childIds) {
        const auto& child = arena[childIdx];
        int repeatCount = (child.wrapEachLine && pWrapLines && !pWrapLines->empty())
            ? static_cast<int>(pWrapLines->size())
            : child.count;
        for (int k = 1; k <= repeatCount; ++k) {
            RenderNode(arena, childIdx, tag, k, depth + 1, indentUnit, lineIndent, isJsx,
                       child.wrapEachLine ? pWrapLines : nullptr, hasWrapLines, out, pFirstCursorPos);
        }
    }

    out += curIndent + L"</" + tag + L">\n";
}

bool BuildEmmetArena(
    const std::wstring& exprInput,
    const std::unordered_map<std::wstring, std::wstring>& aliases,
    std::vector<EmmetAstNode>& arena,
    std::vector<size_t>& rootIds
) {
    std::wstring expr = exprInput;
    auto aIt = aliases.find(expr);
    if (aIt != aliases.end()) {
        expr = aIt->second;
    }

    std::vector<std::wstring> tokens;
    std::vector<wchar_t> ops;
    size_t prev = 0;
    int bDepth = 0, braceDepth = 0, parenDepth = 0;

    for (size_t i = 0; i < expr.length(); ++i) {
        wchar_t c = expr[i];
        if (c == L'[') bDepth++;
        else if (c == L']') bDepth = (std::max)(0, bDepth - 1);
        else if (c == L'{') braceDepth++;
        else if (c == L'}') braceDepth = (std::max)(0, braceDepth - 1);
        else if (c == L'(') parenDepth++;
        else if (c == L')') parenDepth = (std::max)(0, parenDepth - 1);
        else if (bDepth == 0 && braceDepth == 0 && parenDepth == 0) {
            if (c == L'>' || c == L'+' || c == L'^') {
                tokens.push_back(expr.substr(prev, i - prev));
                ops.push_back(c);
                prev = i + 1;
            }
        }
    }
    tokens.push_back(expr.substr(prev));
    if (tokens.empty() || tokens[0].empty()) return false;

    // Integer index stack: -1 = rootIds, >= 0 = arena[idx].childIds
    std::vector<int> containerStack;
    containerStack.push_back(-1);

    auto appendToContainer = [&](int containerIdx, size_t nodeIdx) {
        if (containerIdx < 0) rootIds.push_back(nodeIdx);
        else arena[static_cast<size_t>(containerIdx)].childIds.push_back(nodeIdx);
    };
    auto getLastInContainer = [&](int containerIdx) -> int {
        const auto& vec = (containerIdx < 0) ? rootIds : arena[static_cast<size_t>(containerIdx)].childIds;
        return vec.empty() ? -1 : static_cast<int>(vec.back());
    };

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            wchar_t op = ops[i - 1];
            if (op == L'>') {
                int lastIdx = getLastInContainer(containerStack.back());
                if (lastIdx >= 0) containerStack.push_back(lastIdx);
            } else if (op == L'^') {
                if (containerStack.size() > 1) containerStack.pop_back();
            }
        }

        std::wstring rawTok = Trim(tokens[i]);
        if (rawTok.empty()) continue;

        // Check grouped expression: (...) or (...)*N
        if (rawTok.front() == L'(') {
            size_t closeP = std::wstring::npos;
            int pD = 0;
            for (size_t j = 0; j < rawTok.size(); ++j) {
                if (rawTok[j] == L'(') pD++;
                else if (rawTok[j] == L')') {
                    pD--;
                    if (pD == 0) { closeP = j; break; }
                }
            }
            if (closeP != std::wstring::npos) {
                std::wstring subExpr = rawTok.substr(1, closeP - 1);
                int groupRepeat = 1;
                if (closeP + 1 < rawTok.size() && rawTok[closeP + 1] == L'*') {
                    std::wstring repStr = Trim(rawTok.substr(closeP + 2));
                    if (!repStr.empty()) groupRepeat = (std::max)(1, _wtoi(repStr.c_str()));
                }
                for (int rep = 0; rep < groupRepeat; ++rep) {
                    std::vector<size_t> subRoots;
                    if (BuildEmmetArena(subExpr, aliases, arena, subRoots)) {
                        for (size_t sr : subRoots) {
                            appendToContainer(containerStack.back(), sr);
                        }
                    }
                }
                continue;
            }
        }

        auto subIt = aliases.find(rawTok);
        if (subIt != aliases.end()) {
            rawTok = subIt->second;
        }

        size_t newIdx = arena.size();
        arena.push_back(ParseSingleToken(rawTok));
        appendToContainer(containerStack.back(), newIdx);
    }

    return !rootIds.empty();
}

const std::unordered_map<std::wstring, std::wstring>& GetEmmetAliases() {
    static const std::unordered_map<std::wstring, std::wstring> s_aliases = {
        { L"input", L"input[type=\"text\"]" },
        { L"inp", L"input[type=\"text\"]" },
        { L"input:text", L"input[type=\"text\" name=\"\" id=\"\"]" },
        { L"input:password", L"input[type=\"password\" name=\"\" id=\"\"]" },
        { L"input:email", L"input[type=\"email\" name=\"\" id=\"\"]" },
        { L"input:number", L"input[type=\"number\" name=\"\" id=\"\"]" },
        { L"input:checkbox", L"input[type=\"checkbox\" name=\"\" id=\"\"]" },
        { L"input:radio", L"input[type=\"radio\" name=\"\" id=\"\"]" },
        { L"input:submit", L"input[type=\"submit\" value=\"\"]" },
        { L"input:button", L"input[type=\"button\" value=\"\"]" },
        { L"input:file", L"input[type=\"file\" name=\"\" id=\"\"]" },
        { L"input:hidden", L"input[type=\"hidden\" name=\"\"]" },
        { L"input:date", L"input[type=\"date\" name=\"\" id=\"\"]" },
        { L"input:search", L"input[type=\"search\" name=\"\" id=\"\"]" },
        { L"input:tel", L"input[type=\"tel\" name=\"\" id=\"\"]" },
        { L"input:url", L"input[type=\"url\" name=\"\" id=\"\"]" },
        { L"link:css", L"link[rel=\"stylesheet\" href=\"style.css\"]" },
        { L"link:favicon", L"link[rel=\"shortcut icon\" href=\"favicon.ico\" type=\"image/x-icon\"]" },
        { L"script:src", L"script[src=\"\"]" },
        { L"form:get", L"form[action=\"\" method=\"get\"]" },
        { L"form:post", L"form[action=\"\" method=\"post\"]" },
        { L"btn", L"button" },
        { L"btn:s", L"button[type=\"submit\"]" },
        { L"btn:r", L"button[type=\"reset\"]" },
        { L"btn:b", L"button[type=\"button\"]" },
        { L"a:link", L"a[href=\"http://\"]" },
        { L"a:mail", L"a[href=\"mailto:\"]" },
        { L"a:tel", L"a[href=\"tel:\"]" },
        { L"a:blank", L"a[href=\"http://\" target=\"_blank\" rel=\"noopener noreferrer\"]" }
    };
    return s_aliases;
}

} // anonymous namespace

std::wstring EmmetEngine::Expand(
    const std::wstring& abbr,
    const std::wstring& indentUnit,
    const std::wstring& lineIndent,
    bool isCss,
    size_t* pCursorOffset,
    bool isJsx
) {
    if (abbr.empty()) return L"";

    if (pCursorOffset) *pCursorOffset = std::wstring::npos;

    if (isCss) {
        std::wstring css = ExpandCss(abbr);
        if (!css.empty() && pCursorOffset) {
            *pCursorOffset = css.length();
        }
        return css;
    }

    if (abbr == L"!" || abbr == L"html:5" || abbr == L"doc") {
        std::wstring ind1 = lineIndent + indentUnit;
        std::wstring doc = lineIndent + L"<!DOCTYPE html>\n" +
                           lineIndent + L"<html lang=\"en\">\n" +
                           lineIndent + L"<head>\n" +
                           ind1 + L"<meta charset=\"UTF-8\">\n" +
                           ind1 + L"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n" +
                           ind1 + L"<title>Document</title>\n" +
                           lineIndent + L"</head>\n" +
                           lineIndent + L"<body>\n" +
                           ind1;

        if (pCursorOffset) {
            *pCursorOffset = doc.length();
        }

        doc += L"\n" + lineIndent + L"</body>\n" +
               lineIndent + L"</html>\n";
        return doc;
    }

    std::vector<EmmetAstNode> arena;
    std::vector<size_t> rootIds;
    if (!BuildEmmetArena(abbr, GetEmmetAliases(), arena, rootIds)) {
        return L"";
    }

    std::wstring result;
    size_t firstCursorPos = std::wstring::npos;

    for (size_t rIdx : rootIds) {
        const auto& rootNode = arena[rIdx];
        for (int k = 1; k <= rootNode.count; ++k) {
            RenderNode(arena, rIdx, L"", k, 0, indentUnit, lineIndent, isJsx, nullptr, false, result, &firstCursorPos);
        }
    }

    if (rootIds.size() == 1 && arena[rootIds[0]].childIds.empty() && arena[rootIds[0]].count == 1 &&
        !result.empty() && result.back() == L'\n') {
        result.pop_back();
    }

    if (pCursorOffset) {
        *pCursorOffset = (firstCursorPos != std::wstring::npos) ? firstCursorPos : result.length();
    }

    return result;
}

std::wstring EmmetEngine::WrapWithAbbreviation(
    const std::wstring& selectedText,
    const std::wstring& abbr,
    const std::wstring& indentUnit,
    const std::wstring& lineIndent,
    bool isJsx
) {
    std::wstring trimmedAbbr = Trim(abbr);
    if (trimmedAbbr.empty()) return L"";

    // Build AST first so we can check if any node uses per-line `*` wrapping (e.g. `ul>li*`)
    std::vector<EmmetAstNode> arena;
    std::vector<size_t> rootIds;
    if (!BuildEmmetArena(trimmedAbbr, GetEmmetAliases(), arena, rootIds)) {
        return L"";
    }

    bool usesPerLineWrap = false;
    for (const auto& n : arena) {
        if (n.wrapEachLine) { usesPerLineWrap = true; break; }
    }

    if (usesPerLineWrap) {
        std::vector<std::wstring> wrapLines;
        size_t pos = 0;
        while (pos <= selectedText.size()) {
            size_t nl = selectedText.find(L'\n', pos);
            size_t end = (nl == std::wstring::npos) ? selectedText.size() : nl;
            std::wstring seg = selectedText.substr(pos, end - pos);
            if (!seg.empty() && seg.back() == L'\r') seg.pop_back();
            std::wstring t = Trim(seg);
            if (!t.empty()) wrapLines.push_back(std::move(t));
            if (nl == std::wstring::npos) break;
            pos = nl + 1;
        }
        if (wrapLines.empty()) wrapLines.push_back(L"");

        std::wstring result;
        size_t firstCursorPos = std::wstring::npos;
        for (size_t rIdx : rootIds) {
            const auto& rootNode = arena[rIdx];
            int repCount = rootNode.wrapEachLine ? static_cast<int>(wrapLines.size()) : rootNode.count;
            for (int k = 1; k <= repCount; ++k) {
                RenderNode(arena, rIdx, L"", k, 0, indentUnit, lineIndent, isJsx,
                           &wrapLines, true, result, &firstCursorPos);
            }
        }
        if (!result.empty() && result.back() == L'\n' && selectedText.find(L'\n') == std::wstring::npos &&
            rootIds.size() == 1 && arena[rootIds[0]].childIds.empty() && wrapLines.size() == 1) {
            result.pop_back();
        }
        return result;
    }

    size_t cursorOffset = std::wstring::npos;
    std::wstring expanded = Expand(trimmedAbbr, indentUnit, lineIndent, false, &cursorOffset, isJsx);
    if (expanded.empty()) return L"";
    if (!expanded.empty() && expanded.back() == L'\n') expanded.pop_back();

    if (cursorOffset == std::wstring::npos || cursorOffset > expanded.length()) {
        size_t lastClose = expanded.rfind(L"</");
        cursorOffset = (lastClose != std::wstring::npos) ? lastClose : expanded.length();
    }

    if (selectedText.find(L'\n') == std::wstring::npos) {
        std::wstring inner = selectedText;
        if (!inner.empty() && inner.back() == L'\r') inner.pop_back();
        return expanded.substr(0, cursorOffset) + inner + expanded.substr(cursorOffset);
    }

    // Multi-line wrapped content: re-indent smoothly inside deepest container tag
    size_t lineStart = expanded.rfind(L'\n', cursorOffset);
    size_t indentStart = (lineStart == std::wstring::npos) ? 0 : (lineStart + 1);
    size_t indentEnd = indentStart;
    while (indentEnd < cursorOffset && (expanded[indentEnd] == L' ' || expanded[indentEnd] == L'\t')) {
        ++indentEnd;
    }
    const std::wstring containerIndent = expanded.substr(indentStart, indentEnd - indentStart);
    const std::wstring innerIndent = containerIndent + indentUnit;

    std::vector<std::wstring> rawLines;
    size_t pos = 0;
    size_t minCommonIndent = SIZE_MAX;
    while (pos <= selectedText.size()) {
        size_t nl = selectedText.find(L'\n', pos);
        size_t end = (nl == std::wstring::npos) ? selectedText.size() : nl;
        std::wstring seg = selectedText.substr(pos, end - pos);
        if (!seg.empty() && seg.back() == L'\r') seg.pop_back();
        bool hasNonSpace = false;
        size_t ws = 0;
        while (ws < seg.size() && (seg[ws] == L' ' || seg[ws] == L'\t')) ++ws;
        hasNonSpace = (ws < seg.size());
        if (hasNonSpace) minCommonIndent = (std::min)(minCommonIndent, ws);
        rawLines.push_back(std::move(seg));
        if (nl == std::wstring::npos) break;
        pos = nl + 1;
    }
    if (minCommonIndent == SIZE_MAX) minCommonIndent = 0;

    std::wstring block = L"\n";
    for (const auto& rl : rawLines) {
        if (rl.empty()) {
            block += L"\n";
        } else {
            size_t cut = (std::min)(minCommonIndent, rl.size());
            block += innerIndent + rl.substr(cut) + L"\n";
        }
    }
    block += containerIndent;

    return expanded.substr(0, cursorOffset) + block + expanded.substr(cursorOffset);
}

EmmetEngine::TextRange EmmetEngine::BalanceTagRange(
    const std::wstring& docText,
    size_t selStartOffset,
    size_t selEndOffset,
    bool outward
) {
    TextRange empty{};
    if (docText.empty() || docText.size() > (2ULL * 1024ULL * 1024ULL)) {
        return empty;
    }
    if (selStartOffset > selEndOffset) std::swap(selStartOffset, selEndOffset);
    selStartOffset = (std::min)(selStartOffset, docText.size());
    selEndOffset = (std::min)(selEndOffset, docText.size());

    struct OpenTagItem {
        size_t openStart = 0;
        size_t openEnd = 0;
        std::wstring lowerName;
    };
    struct PairedTag {
        size_t openStart = 0;
        size_t openEnd = 0;
        size_t closeStart = 0;
        size_t closeEnd = 0;
    };

    std::vector<OpenTagItem> stack;
    std::vector<PairedTag> pairs;
    const size_t n = docText.size();

    for (size_t i = 0; i < n; ) {
        if (docText[i] != L'<') {
            ++i;
            continue;
        }
        // HTML comment <!-- ... -->
        if (i + 3 < n && docText[i + 1] == L'!' && docText[i + 2] == L'-' && docText[i + 3] == L'-') {
            size_t endC = docText.find(L"-->", i + 4);
            i = (endC == std::wstring::npos) ? n : (endC + 3);
            continue;
        }
        if (i + 1 >= n) break;

        wchar_t nextC = docText[i + 1];
        if (nextC == L'!' || nextC == L'?') {
            ++i;
            continue;
        }

        bool isClose = (nextC == L'/');
        size_t nameStart = isClose ? (i + 2) : (i + 1);
        bool isFragment = (nameStart < n && docText[nameStart] == L'>');
        if (!isFragment && (nameStart >= n || !iswalpha(docText[nameStart]))) {
            ++i;
            continue;
        }

        size_t nameEnd = nameStart;
        while (nameEnd < n) {
            wchar_t tc = docText[nameEnd];
            if (iswalnum(tc) || tc == L'-' || tc == L'_' || tc == L':' || tc == L'.') {
                ++nameEnd;
            } else {
                break;
            }
        }
        if (!isFragment && nameEnd < n) {
            wchar_t afterName = docText[nameEnd];
            if (afterName != L'>' && afterName != L'/' && !iswspace(afterName)) {
                ++i;
                continue;
            }
        }

        // Scan to closing '>' outside quotes and JSX attribute braces
        size_t gtPos = std::wstring::npos;
        int braceDepth = 0;
        wchar_t quoteChar = 0;
        for (size_t j = nameEnd; j < n; ++j) {
            wchar_t c = docText[j];
            if (quoteChar != 0) {
                if (c == L'\\' && j + 1 < n) ++j;
                else if (c == quoteChar) quoteChar = 0;
                continue;
            }
            if (c == L'"' || c == L'\'') {
                quoteChar = c;
            } else if (c == L'{') {
                ++braceDepth;
            } else if (c == L'}') {
                if (braceDepth > 0) --braceDepth;
            } else if (c == L'>' && braceDepth == 0) {
                gtPos = j;
                break;
            }
        }
        if (gtPos == std::wstring::npos) {
            ++i;
            continue;
        }

        std::wstring lowerName = docText.substr(nameStart, nameEnd - nameStart);
        for (wchar_t& ch : lowerName) ch = static_cast<wchar_t>(::towlower(ch));

        size_t openEndPos = gtPos + 1;
        if (isClose) {
            for (int k = static_cast<int>(stack.size()) - 1; k >= 0; --k) {
                if (stack[static_cast<size_t>(k)].lowerName == lowerName) {
                    auto matched = stack[static_cast<size_t>(k)];
                    stack.resize(static_cast<size_t>(k));
                    pairs.push_back({ matched.openStart, matched.openEnd, i, openEndPos });
                    break;
                }
            }
        } else {
            size_t prevNonWs = gtPos;
            while (prevNonWs > nameEnd && iswspace(docText[prevNonWs - 1])) --prevNonWs;
            bool selfClosing = (prevNonWs > nameEnd && docText[prevNonWs - 1] == L'/');
            if (selfClosing || (!lowerName.empty() && IsVoidTag(lowerName))) {
                pairs.push_back({ i, openEndPos, openEndPos, openEndPos });
            } else {
                stack.push_back({ i, openEndPos, std::move(lowerName) });
            }
        }

        i = openEndPos;
    }

    if (pairs.empty()) return empty;

    auto spanLen = [](const PairedTag& p) noexcept -> size_t {
        return p.closeEnd - p.openStart;
    };

    if (outward) {
        std::vector<const PairedTag*> enclosing;
        for (const auto& p : pairs) {
            if (p.openStart <= selStartOffset && selEndOffset <= p.closeEnd) {
                enclosing.push_back(&p);
            }
        }
        if (enclosing.empty()) return empty;
        std::sort(enclosing.begin(), enclosing.end(), [&](const PairedTag* a, const PairedTag* b) {
            size_t la = spanLen(*a), lb = spanLen(*b);
            if (la != lb) return la < lb;
            return a->openStart > b->openStart;
        });

        for (const PairedTag* p : enclosing) {
            const bool hasInner = (p->openEnd < p->closeStart);
            if (hasInner &&
                selStartOffset >= p->openEnd && selEndOffset <= p->closeStart &&
                (selStartOffset > p->openEnd || selEndOffset < p->closeStart)) {
                return { p->openEnd, p->closeStart, true };
            }
            if (selStartOffset > p->openStart || selEndOffset < p->closeEnd) {
                return { p->openStart, p->closeEnd, true };
            }
        }
        return empty;
    }

    // Inward balance
    std::vector<const PairedTag*> enclosing;
    for (const auto& p : pairs) {
        if (p.openStart <= selStartOffset && selEndOffset <= p.closeEnd) {
            enclosing.push_back(&p);
        }
    }
    if (enclosing.empty()) return empty;
    std::sort(enclosing.begin(), enclosing.end(), [&](const PairedTag* a, const PairedTag* b) {
        return spanLen(*a) < spanLen(*b);
    });
    const PairedTag* cur = enclosing.front();
    if (selStartOffset == cur->openStart && selEndOffset == cur->closeEnd && cur->openEnd < cur->closeStart) {
        return { cur->openEnd, cur->closeStart, true };
    }

    // Otherwise step into the first direct child tag inside [selStartOffset, selEndOffset]
    const PairedTag* bestChild = nullptr;
    for (const auto& p : pairs) {
        if (&p == cur) continue;
        if (p.openStart >= selStartOffset && p.closeEnd <= selEndOffset &&
            (p.openStart > selStartOffset || p.closeEnd < selEndOffset)) {
            if (!bestChild || p.openStart < bestChild->openStart ||
                (p.openStart == bestChild->openStart && spanLen(p) > spanLen(*bestChild))) {
                bestChild = &p;
            }
        }
    }
    if (bestChild) {
        return { bestChild->openStart, bestChild->closeEnd, true };
    }
    return empty;
}

} // namespace phide::editor
