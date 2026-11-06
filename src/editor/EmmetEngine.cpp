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
    bool isLorem = false;
    size_t loremWords = 30;
    std::vector<EmmetAstNode> children;
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

    if (starPos != std::wstring::npos && starPos + 1 < str.length()) {
        std::wstring countStr = str.substr(starPos + 1);
        bool allDig = true;
        for (wchar_t c : countStr) {
            if (!iswdigit(c)) { allDig = false; break; }
        }
        if (allDig && !countStr.empty()) {
            node.count = (std::max)(1, _wtoi(countStr.c_str()));
            str = str.substr(0, starPos);
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

// Render an AST node to HTML string
void RenderNode(
    const EmmetAstNode& node,
    const std::wstring& parentTag,
    int index,
    int depth,
    const std::wstring& indentUnit,
    const std::wstring& lineIndent,
    std::wstring& out,
    size_t* pFirstCursorPos
) {
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

    // Apply official standard tag expansions (e.g. "a" -> "a[href=""]", "img" -> "img[src="" alt=""]")
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
        out += L" class=\"";
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

    if (node.children.empty()) {
        if (!text.empty()) {
            out += text;
        } else {
            // First empty tag without children is where cursor belongs
            if (pFirstCursorPos && *pFirstCursorPos == std::wstring::npos) {
                *pFirstCursorPos = out.length();
            }
        }
        out += L"</" + tag + L">\n";
        return;
    }

    // Children present
    if (!text.empty()) {
        out += text;
    }
    out += L"\n";

    for (const auto& child : node.children) {
        for (int k = 1; k <= child.count; ++k) {
            RenderNode(child, tag, k, depth + 1, indentUnit, lineIndent, out, pFirstCursorPos);
        }
    }

    out += curIndent + L"</" + tag + L">\n";
}

} // anonymous namespace

std::wstring EmmetEngine::Expand(
    const std::wstring& abbr,
    const std::wstring& indentUnit,
    const std::wstring& lineIndent,
    bool isCss,
    size_t* pCursorOffset
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

    // HTML5 Boilerplate: ! or html:5 or doc
    if (abbr == L"!" || abbr == L"html:5" || abbr == L"doc") {
        std::wstring ind1 = lineIndent + indentUnit;
        std::wstring ind2 = ind1 + indentUnit;
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

    // Standard specific tags with aliases
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

    std::wstring expr = abbr;
    auto aIt = s_aliases.find(expr);
    if (aIt != s_aliases.end()) {
        expr = aIt->second;
    }

    // Tokenize into elements and operators (>, +, ^)
    // Respect brackets [...] and braces {...}
    std::vector<std::wstring> tokens;
    std::vector<wchar_t> ops; // op before token[i] for i > 0

    size_t prev = 0;
    int bDepth = 0;
    int braceDepth = 0;
    int parenDepth = 0;

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

    if (tokens.empty() || tokens[0].empty()) return L"";

    // Build AST tree
    std::vector<EmmetAstNode> rootForest;
    // Stack of pointers to children vectors
    std::vector<std::vector<EmmetAstNode>*> containerStack;
    containerStack.push_back(&rootForest);

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            wchar_t op = ops[i - 1];
            if (op == L'>') {
                // Descend into the last inserted node's children
                if (!containerStack.empty() && !containerStack.back()->empty()) {
                    containerStack.push_back(&containerStack.back()->back().children);
                }
            } else if (op == L'+') {
                // Sibling: remain in the same container
            } else if (op == L'^') {
                // Climb up parent
                if (containerStack.size() > 1) {
                    containerStack.pop_back();
                }
            }
        }

        std::wstring rawTok = Trim(tokens[i]);
        if (rawTok.empty()) continue;

        // Strip grouping parens if wrapped
        if (rawTok.front() == L'(' && rawTok.back() == L')') {
            rawTok = rawTok.substr(1, rawTok.length() - 2);
        }

        auto subIt = s_aliases.find(rawTok);
        if (subIt != s_aliases.end()) {
            rawTok = subIt->second;
        }

        EmmetAstNode node = ParseSingleToken(rawTok);
        if (containerStack.empty()) containerStack.push_back(&rootForest);
        containerStack.back()->push_back(std::move(node));
    }

    if (rootForest.empty()) return L"";

    // Render tree to string
    std::wstring result;
    size_t firstCursorPos = std::wstring::npos;

    for (const auto& rootNode : rootForest) {
        for (int k = 1; k <= rootNode.count; ++k) {
            RenderNode(rootNode, L"", k, 0, indentUnit, lineIndent, result, &firstCursorPos);
        }
    }

    // Strip trailing newline if single line expansion
    if (rootForest.size() == 1 && rootForest[0].children.empty() && rootForest[0].count == 1 &&
        result.find(L'\n') == result.length() - 1) {
        result.pop_back();
    }

    if (pCursorOffset) {
        *pCursorOffset = (firstCursorPos != std::wstring::npos) ? firstCursorPos : result.length();
    }

    return result;
}

} // namespace phide::editor
