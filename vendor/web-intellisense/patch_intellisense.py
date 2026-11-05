import os

with open(r'src/editor/IntelliSenseEngine.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# Add includes
if '#include "phide/editor/WebIntelliSenseData.hpp"' not in content:
    content = content.replace(
        '#include "phide/editor/ReactIntelliSenseData.hpp"',
        '#include "phide/editor/ReactIntelliSenseData.hpp"\n#include "phide/editor/WebIntelliSenseData.hpp"\n#include "phide/editor/EmmetEngine.hpp"'
    )

# 1. Update PopulateHtmlTags
if 'void IntelliSenseEngine::PopulateHtmlTags() {' in content:
    old_tags_start = 'void IntelliSenseEngine::PopulateHtmlTags() {'
    content = content.replace(
        old_tags_start,
        old_tags_start + '\n    const auto& officialTags = WebIntelliSenseData::GetHtmlTags();\n    if (!officialTags.empty()) {\n        m_htmlTags = officialTags;\n        return;\n    }'
    )

# 2. Update PopulateHtmlAttributes
if 'void IntelliSenseEngine::PopulateHtmlAttributes() {' in content:
    old_attrs_start = 'void IntelliSenseEngine::PopulateHtmlAttributes() {'
    content = content.replace(
        old_attrs_start,
        old_attrs_start + '\n    const auto& officialAttrs = WebIntelliSenseData::GetHtmlGlobalAttributes();\n    if (!officialAttrs.empty()) {\n        m_htmlGlobalAttributes = officialAttrs;\n        return;\n    }'
    )

# 3. Update PopulateCssProperties
if 'void IntelliSenseEngine::PopulateCssProperties() {' in content:
    old_props_start = 'void IntelliSenseEngine::PopulateCssProperties() {'
    content = content.replace(
        old_props_start,
        old_props_start + '\n    const auto& officialProps = WebIntelliSenseData::GetCssProperties();\n    if (!officialProps.empty()) {\n        m_cssProperties = officialProps;\n        return;\n    }'
    )

# 4. Update PopulateCssValues
if 'void IntelliSenseEngine::PopulateCssValues() {' in content:
    old_vals_start = 'void IntelliSenseEngine::PopulateCssValues() {'
    content = content.replace(
        old_vals_start,
        old_vals_start + '\n    const auto& officialVals = WebIntelliSenseData::GetCssValues();\n    if (!officialVals.empty()) {\n        m_cssValues = officialVals;\n        const auto& pseudo = WebIntelliSenseData::GetCssPseudo();\n        m_cssValues.insert(m_cssValues.end(), pseudo.begin(), pseudo.end());\n        return;\n    }'
    )

# 5. Update PopulateJavaScriptGlobals
if 'void IntelliSenseEngine::PopulateJavaScriptGlobals() {' in content:
    old_jsg_start = 'void IntelliSenseEngine::PopulateJavaScriptGlobals() {'
    content = content.replace(
        old_jsg_start,
        old_jsg_start + '\n    const auto& officialGlobals = WebIntelliSenseData::GetJavaScriptGlobals();\n    if (!officialGlobals.empty()) {\n        m_jsGlobals = officialGlobals;\n        return;\n    }'
    )

# 6. Update PopulateJavaScriptDomApis
if 'void IntelliSenseEngine::PopulateJavaScriptDomApis() {' in content:
    old_dom_start = 'void IntelliSenseEngine::PopulateJavaScriptDomApis() {'
    content = content.replace(
        old_dom_start,
        old_dom_start + '\n    const auto& officialDom = WebIntelliSenseData::GetJavaScriptDomMethods();\n    if (!officialDom.empty()) {\n        m_jsDomApis = officialDom;\n        return;\n    }'
    )

# 7. Update PopulateJavaScriptPrototypeMethods
if 'void IntelliSenseEngine::PopulateJavaScriptPrototypeMethods() {' in content:
    old_proto_start = 'void IntelliSenseEngine::PopulateJavaScriptPrototypeMethods() {'
    content = content.replace(
        old_proto_start,
        old_proto_start + '\n    const auto& officialProto = WebIntelliSenseData::GetJavaScriptPrototypeMethods();\n    if (!officialProto.empty()) {\n        m_jsPrototypeMethods = officialProto;\n        return;\n    }'
    )

# 8. Delegate ExtractEmmetPrefix and ExpandEmmet
old_emmet_extract_and_expand = '''std::wstring IntelliSenseEngine::ExtractEmmetPrefix(const std::wstring& line, size_t cursorCol) {
    if (line.empty() || cursorCol == 0) return L"";
    size_t col = (std::min)(cursorCol, line.length());
    size_t start = col;
    while (start > 0) {
        wchar_t c = line[start - 1];
        if (c == L' ' || c == L'\\t' || c == L'<' || c == L';' || c == L'\\"' || c == L'\\'') {
            break;
        }
        if (c == L'>') {
            // '>' can be child operator in Emmet like ul>li, but if preceded by '/' or tag close '</tag>', stop
            if (start >= 2 && line[start - 2] == L'/') {
                break;
            }
        }
        start--;
    }
    while (start < col && (line[start] == L'>' || line[start] == L' ' || line[start] == L'\\t')) {
        start++;
    }
    if (start >= col) return L"";
    return line.substr(start, col - start);
}'''

new_emmet_extract_and_expand = '''std::wstring IntelliSenseEngine::ExtractEmmetPrefix(const std::wstring& line, size_t cursorCol) {
    return EmmetEngine::ExtractAbbreviation(line, cursorCol, false, nullptr);
}'''

norm_content = content.replace('\r\n', '\n')
norm_old = old_emmet_extract_and_expand.replace('\r\n', '\n')
if norm_old in norm_content:
    norm_content = norm_content.replace(norm_old, new_emmet_extract_and_expand)
    print("Replaced ExtractEmmetPrefix successfully!")
else:
    print("Warning: ExtractEmmetPrefix pattern not matched")

# Also replace ExpandEmmet body
old_expand_emmet = 'std::wstring IntelliSenseEngine::ExpandEmmet(const std::wstring& abbr) {'
idx_expand = norm_content.find(old_expand_emmet)
if idx_expand != -1:
    idx_next_func = norm_content.find('std::wstring IntelliSenseEngine::ExtractQueryPrefix', idx_expand)
    if idx_next_func != -1:
        new_expand_body = '''std::wstring IntelliSenseEngine::ExpandEmmet(const std::wstring& abbr) {
    return EmmetEngine::Expand(abbr, L"    ", L"", false, nullptr);
}

'''
        norm_content = norm_content[:idx_expand] + new_expand_body + norm_content[idx_next_func:]
        print("Replaced ExpandEmmet body successfully!")

# Add CSS Emmet abbreviation check to QueryCompletions
css_target = 'for (const auto& p : m_cssProperties) rawCandidates.push_back(p);'
css_replacement = '''for (const auto& p : m_cssProperties) rawCandidates.push_back(p);

            // Check for CSS Emmet abbreviation
            std::wstring cssEmmet = EmmetEngine::ExtractAbbreviation(currentLine, cursorCol, true, nullptr);
            if (!cssEmmet.empty() && EmmetEngine::IsEmmetAbbreviation(cssEmmet, true)) {
                std::wstring expandedCss = EmmetEngine::ExpandCss(cssEmmet);
                if (!expandedCss.empty()) {
                    IntelliSenseItem cItem;
                    cItem.label = cssEmmet;
                    cItem.detail = L"CSS Emmet\\n" + expandedCss;
                    cItem.insertText = expandedCss;
                    cItem.kind = CompletionKind::Snippet;
                    cItem.origin = L"emmet";
                    cItem.score = 4000;
                    rawCandidates.push_back(std::move(cItem));
                }
            }'''

if css_target in norm_content:
    norm_content = norm_content.replace(css_target, css_replacement)
    print("Added CSS Emmet to QueryCompletions!")

with open(r'src/editor/IntelliSenseEngine.cpp', 'w', encoding='utf-8') as f:
    f.write(norm_content)

print("IntelliSenseEngine.cpp updated successfully!")
