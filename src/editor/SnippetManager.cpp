#include "phide/editor/SnippetManager.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cwctype>

namespace phide::editor {

void SnippetManager::Initialize(const std::wstring& snippetsFilePath) {
    m_snippetsFilePath = snippetsFilePath;
    LoadDefaults();
    EnsureDefaultSnippetsFile();
    LoadSnippets();
}

void SnippetManager::LoadDefaults() {
    m_snippets.clear();

    // 1. HTML5 Boilerplate & HTML Templates (Image 3 request: "like you know ! in vs code create html template")
    m_snippets.push_back({
        L"HTML5 Boilerplate Template (!)",
        L"!",
        {
            L"<!DOCTYPE html>",
            L"<html lang=\"en\">",
            L"<head>",
            L"    <meta charset=\"UTF-8\">",
            L"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">",
            L"    <title>${1:Document}</title>",
            L"</head>",
            L"<body>",
            L"    $0",
            L"</body>",
            L"</html>"
        },
        L"Complete HTML5 boilerplate skeleton with DOCTYPE, meta charset, viewport, title, and body",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"HTML5 Boilerplate (html:5)",
        L"html:5",
        {
            L"<!DOCTYPE html>",
            L"<html lang=\"en\">",
            L"<head>",
            L"    <meta charset=\"UTF-8\">",
            L"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">",
            L"    <title>${1:Document}</title>",
            L"</head>",
            L"<body>",
            L"    $0",
            L"</body>",
            L"</html>"
        },
        L"Complete HTML5 boilerplate skeleton with DOCTYPE, meta charset, viewport, title, and body",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Link External CSS Stylesheet",
        L"link:css",
        { L"<link rel=\"stylesheet\" href=\"${1:style.css}\" />" },
        L"Link external stylesheet link element",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"External JavaScript Script Tag",
        L"script:src",
        { L"<script src=\"${1:app.js}\"></script>" },
        L"Include external JavaScript source script element",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Meta Viewport Tag",
        L"meta:vp",
        { L"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />" },
        L"Responsive viewport meta tag",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Meta Charset UTF-8",
        L"meta:utf",
        { L"<meta charset=\"UTF-8\" />" },
        L"Standard UTF-8 encoding meta tag",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Anchor Hyperlink",
        L"a",
        { L"<a href=\"${1:#}\">${2:Link}</a>" },
        L"HTML anchor hyperlink tag",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Image Element",
        L"img",
        { L"<img src=\"${1:image.png}\" alt=\"${2:description}\" />" },
        L"HTML image tag with src and alt",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Button Element",
        L"btn",
        { L"<button type=\"${1:button}\">${2:Click}</button>" },
        L"HTML button element with type and text",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Form Post Element",
        L"form:post",
        {
            L"<form action=\"${1:#}\" method=\"post\">",
            L"    $0",
            L"</form>"
        },
        L"HTML form element with POST action",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Text Field",
        L"input:text",
        { L"<input type=\"text\" name=\"${1:name}\" placeholder=\"${2:Enter text...}\" />" },
        L"HTML text input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Password Field",
        L"input:password",
        { L"<input type=\"password\" name=\"${1:password}\" />" },
        L"HTML password input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Email Field",
        L"input:email",
        { L"<input type=\"email\" name=\"${1:email}\" placeholder=\"${2:name@example.com}\" />" },
        L"HTML email input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Submit Button",
        L"input:submit",
        { L"<input type=\"submit\" value=\"${1:Submit}\" />" },
        L"HTML form submit button",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"HTML Table Structure",
        L"table",
        {
            L"<table>",
            L"    <thead>",
            L"        <tr>",
            L"            <th>${1:Header}</th>",
            L"        </tr>",
            L"    </thead>",
            L"    <tbody>",
            L"        <tr>",
            L"            <td>$0</td>",
            L"        </tr>",
            L"    </tbody>",
            L"</table>"
        },
        L"Complete HTML table skeleton structure",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Unordered List with Item",
        L"ul>li",
        {
            L"<ul>",
            L"    <li>$0</li>",
            L"</ul>"
        },
        L"HTML unordered list with list item",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Ordered List with Item",
        L"ol>li",
        {
            L"<ol>",
            L"    <li>$0</li>",
            L"</ol>"
        },
        L"HTML ordered list with list item",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Container Div Element",
        L"div:container",
        {
            L"<div class=\"${1:container}\">",
            L"    $0",
            L"</div>"
        },
        L"HTML div container element",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Embedded Style Block",
        L"style",
        {
            L"<style>",
            L"    $0",
            L"</style>"
        },
        L"HTML embedded CSS style block",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Embedded Script Block",
        L"script",
        {
            L"<script>",
            L"    $0",
            L"</script>"
        },
        L"HTML embedded JavaScript script block",
        L"html,htm,php"
    });

    // 2. CSS Snippets
    m_snippets.push_back({
        L"Flexbox Center Alignment",
        L"flexcenter",
        {
            L"display: flex;",
            L"justify-content: center;",
            L"align-items: center;"
        },
        L"CSS flexbox centering layout",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Flexbox Column Layout",
        L"flexcol",
        {
            L"display: flex;",
            L"flex-direction: column;",
            L"gap: ${1:12px};"
        },
        L"CSS flexbox vertical column layout",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"CSS Grid Columns Layout",
        L"grid",
        {
            L"display: grid;",
            L"grid-template-columns: repeat(${1:3}, 1fr);",
            L"gap: ${2:16px};"
        },
        L"CSS grid responsive columns layout",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Universal CSS Reset",
        L"reset",
        {
            L"*, *::before, *::after {",
            L"    box-sizing: border-box;",
            L"    margin: 0;",
            L"    padding: 0;",
            L"}"
        },
        L"Universal CSS box-sizing and margin reset",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"CSS Media Query",
        L"media",
        {
            L"@media (max-width: ${1:768px}) {",
            L"    $0",
            L"}"
        },
        L"CSS responsive media query",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"CSS Animation Keyframes",
        L"keyframes",
        {
            L"@keyframes ${1:fadeIn} {",
            L"    from { opacity: 0; }",
            L"    to   { opacity: 1; }",
            L"}"
        },
        L"CSS keyframe animation definition",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Glassmorphism Card Effect",
        L"glass",
        {
            L"background: rgba(255, 255, 255, 0.08);",
            L"backdrop-filter: blur(12px);",
            L"border: 1px solid rgba(255, 255, 255, 0.15);",
            L"border-radius: 8px;"
        },
        L"Modern glassmorphism translucent card styling",
        L"css,scss,less,html"
    });

    // 3. TypeScript / JavaScript Snippets
    m_snippets.push_back({
        L"Console Log (JS/TS/HTML)",
        L"log",
        { L"console.log($1);" },
        L"Log output to developer console",
        L"html,htm,js,ts,jsx,tsx,php"
    });

    m_snippets.push_back({
        L"Console Log (clg)",
        L"clg",
        { L"console.log($1);" },
        L"Log output to developer console",
        L"ts,js,tsx,jsx,html,htm,php"
    });

    m_snippets.push_back({
        L"Console Error",
        L"cerr",
        { L"console.error($1);" },
        L"Log error diagnostic to developer console",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Console Warn",
        L"cwarn",
        { L"console.warn($1);" },
        L"Log warning alert to developer console",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Console Table",
        L"ctable",
        { L"console.table($1);" },
        L"Log tabular dataset to developer console",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Arrow Function",
        L"afn",
        {
            L"const ${1:name} = ($2) => {",
            L"    $0",
            L"};"
        },
        L"ES6 Arrow Function declaration",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Standard Function",
        L"fn",
        {
            L"function ${1:name}($2) {",
            L"    $0",
            L"}"
        },
        L"Standard function declaration",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Async Function",
        L"asyncfn",
        {
            L"async function ${1:name}($2) {",
            L"    $0",
            L"}"
        },
        L"Asynchronous function declaration",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"New Promise",
        L"prom",
        {
            L"new Promise((resolve, reject) => {",
            L"    $0",
            L"});"
        },
        L"Construct new ES6 Promise",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Fetch API Request",
        L"fetch",
        {
            L"fetch('${1:url}')",
            L"    .then(res => res.json())",
            L"    .then(data => {",
            L"        $0",
            L"    });"
        },
        L"Standard fetch JSON request promise chain",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Try Catch Block",
        L"trycatch",
        {
            L"try {",
            L"    $1",
            L"} catch (error) {",
            L"    console.error(error);",
            L"    $0",
            L"}"
        },
        L"Try Catch error handling block",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"For Loop (JS)",
        L"fori",
        {
            L"for (let ${1:i} = 0; ${1:i} < ${2:array}.length; ${1:i}++) {",
            L"    $0",
            L"}"
        },
        L"Standard indexed for loop",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"For Of Loop (JS)",
        L"forof",
        {
            L"for (const ${1:item} of ${2:iterable}) {",
            L"    $0",
            L"}"
        },
        L"ES6 for..of iterable loop",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Add Event Listener",
        L"ael",
        {
            L"${1:element}.addEventListener('${2:click}', (e) => {",
            L"    $0",
            L"});"
        },
        L"DOM addEventListener callback binding",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Document Query Selector",
        L"qs",
        { L"document.querySelector('${1:selector}')" },
        L"Query single DOM element",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Document Query Selector All",
        L"qsa",
        { L"document.querySelectorAll('${1:selector}')" },
        L"Query all matching DOM elements",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Import Statement",
        L"imp",
        { L"import { $1 } from '$2';" },
        L"ES6 import statement",
        L"ts,js,tsx,jsx"
    });

    m_snippets.push_back({
        L"CommonJS Require",
        L"req",
        { L"const $1 = require('$2');" },
        L"CommonJS module require statement",
        L"ts,js"
    });

    m_snippets.push_back({
        L"Export Default",
        L"ed",
        { L"export default $1;" },
        L"ES6 export default statement",
        L"ts,js,tsx,jsx"
    });

    m_snippets.push_back({
        L"React Functional Component",
        L"rfc",
        {
            L"export const ${1:ComponentName} = () => {",
            L"    return (",
            L"        <div>",
            L"            $0",
            L"        </div>",
            L"    );",
            L"};"
        },
        L"React functional component export",
        L"tsx,jsx"
    });

    m_snippets.push_back({
        L"React useState Hook",
        L"us",
        { L"const [${1:state}, set${1:state}] = useState($2);" },
        L"React useState hook",
        L"tsx,jsx,ts,js"
    });

    m_snippets.push_back({
        L"React useEffect Hook",
        L"ue",
        {
            L"useEffect(() => {",
            L"    $0",
            L"}, []);"
        },
        L"React useEffect hook",
        L"tsx,jsx,ts,js"
    });

    // 4. C / C++ Snippets
    m_snippets.push_back({
        L"Include Header",
        L"inc",
        { L"#include <$1>" },
        L"Include standard library header",
        L"cpp,hpp,c,h,cc,cxx"
    });

    m_snippets.push_back({
        L"Include Local Header",
        L"incl",
        { L"#include \"$1\"" },
        L"Include project local header",
        L"cpp,hpp,c,h,cc,cxx"
    });

    m_snippets.push_back({
        L"Main Function",
        L"main",
        {
            L"int main(int argc, char* argv[]) {",
            L"    $0",
            L"    return 0;",
            L"}"
        },
        L"Standard main entry function",
        L"cpp,c,cc,cxx"
    });

    m_snippets.push_back({
        L"Print to Console (C++ log)",
        L"log",
        { L"std::cout << \"$1\" << std::endl;" },
        L"Log output to console in C++",
        L"cpp,hpp,c,h,cc,cxx"
    });

    m_snippets.push_back({
        L"Print to Console (C++ cout)",
        L"cout",
        { L"std::cout << $1 << std::endl;" },
        L"Print expression to standard console output",
        L"cpp,hpp,c,h,cc,cxx"
    });

    m_snippets.push_back({
        L"For Loop (C++)",
        L"fori",
        {
            L"for (size_t ${1:i} = 0; ${1:i} < ${2:count}; ++${1:i}) {",
            L"    $0",
            L"}"
        },
        L"Standard indexed for loop",
        L"cpp,c,cc,cxx"
    });

    m_snippets.push_back({
        L"Class Definition",
        L"class",
        {
            L"class ${1:MyClass} {",
            L"public:",
            L"    ${1:MyClass}();",
            L"    virtual ~${1:MyClass}();",
            L"private:",
            L"    $0",
            L"};"
        },
        L"C++ Class boilerplate",
        L"cpp,hpp,cc,h"
    });

    m_snippets.push_back({
        L"Struct Definition",
        L"struct",
        {
            L"struct ${1:MyStruct} {",
            L"    $0",
            L"};"
        },
        L"C++ Struct boilerplate",
        L"cpp,hpp,cc,h"
    });

    m_snippets.push_back({
        L"Std Vector Declaration",
        L"vec",
        { L"std::vector<${1:int}> ${2:items};" },
        L"Standard vector container declaration",
        L"cpp,hpp,cc,h"
    });

    // 5. Python Snippets
    m_snippets.push_back({
        L"Python Function",
        L"def",
        {
            L"def ${1:function_name}($2):",
            L"    \"\"\"$3\"\"\"",
            L"    $0"
        },
        L"Python function definition",
        L"py"
    });

    m_snippets.push_back({
        L"Python If Main",
        L"ifmain",
        {
            L"if __name__ == '__main__':",
            L"    $0"
        },
        L"Python entrypoint boilerplate",
        L"py"
    });

    m_snippets.push_back({
        L"Python Class",
        L"class",
        {
            L"class ${1:ClassName}:",
            L"    def __init__(self$2):",
            L"        $0"
        },
        L"Python class definition",
        L"py"
    });

    m_snippets.push_back({
        L"Python Print",
        L"print",
        { L"print($1)" },
        L"Python print statement",
        L"py"
    });

    m_snippets.push_back({
        L"Print to Console (Python)",
        L"log",
        { L"print($1)" },
        L"Print expression to standard console output in Python",
        L"py"
    });
}

static std::wstring EscapeJsonString(const std::wstring& input) {
    std::wstring out;
    out.reserve(input.length() * 2);
    for (wchar_t c : input) {
        if (c == L'\\') out += L"\\\\";
        else if (c == L'"') out += L"\\\"";
        else if (c == L'\n') out += L"\\n";
        else if (c == L'\r') out += L"\\r";
        else if (c == L'\t') out += L"\\t";
        else out += c;
    }
    return out;
}

void SnippetManager::SaveSnippets(const std::wstring& filePath) const {
    std::wofstream out(filePath);
    if (!out.is_open()) return;

    out << L"{\n";
    out << L"    // User Code Snippets for phide IDE\n";
    out << L"    // Each snippet is defined under a snippet name and has a prefix, body, description, and scope.\n";
    out << L"    // Use $1, $2 for tab stops, $0 for final cursor position, and ${1:label} for placeholders.\n\n";

    for (size_t sIdx = 0; sIdx < m_snippets.size(); ++sIdx) {
        const auto& sn = m_snippets[sIdx];
        out << L"    \"" << EscapeJsonString(sn.name) << L"\": {\n";
        out << L"        \"prefix\": \"" << EscapeJsonString(sn.prefix) << L"\",\n";
        out << L"        \"body\": [\n";
        for (size_t bIdx = 0; bIdx < sn.body.size(); ++bIdx) {
            out << L"            \"" << EscapeJsonString(sn.body[bIdx]) << L"\"";
            if (bIdx + 1 < sn.body.size()) out << L",";
            out << L"\n";
        }
        out << L"        ],\n";
        out << L"        \"description\": \"" << EscapeJsonString(sn.description) << L"\",\n";
        out << L"        \"scope\": \"" << EscapeJsonString(sn.scope) << L"\"\n";
        out << L"    }";
        if (sIdx + 1 < m_snippets.size()) out << L",";
        out << L"\n\n";
    }

    out << L"}\n";
    out.close();
}

void SnippetManager::EnsureDefaultSnippetsFile() {
    if (m_snippetsFilePath.empty()) return;

    std::filesystem::path p(m_snippetsFilePath);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);

    bool needsExport = !std::filesystem::exists(p, ec);
    if (!needsExport) {
        // If file exists, check if it's the old 6-snippet stub or lacks the "!" boilerplate snippet
        std::wifstream in(m_snippetsFilePath);
        if (in.is_open()) {
            std::wstring content((std::istreambuf_iterator<wchar_t>(in)), std::istreambuf_iterator<wchar_t>());
            in.close();
            // If missing "!" or file is too small to contain the full catalog
            if (content.find(L"\"prefix\": \"!\"") == std::wstring::npos || content.length() < 3000) {
                needsExport = true;
            }
        } else {
            needsExport = true;
        }
    }

    if (needsExport) {
        SaveSnippets(m_snippetsFilePath);
    }
}

void SnippetManager::LoadSnippets() {
    if (m_snippetsFilePath.empty() || !std::filesystem::exists(m_snippetsFilePath)) return;

    std::wifstream in(m_snippetsFilePath);
    if (!in.is_open()) return;

    std::wstring content((std::istreambuf_iterator<wchar_t>(in)), std::istreambuf_iterator<wchar_t>());
    in.close();

    // Custom lightweight JSON parser for user snippets
    size_t i = 0;
    size_t len = content.length();

    auto skipWs = [&]() {
        while (i < len) {
            if (content[i] == L' ' || content[i] == L'\t' || content[i] == L'\r' || content[i] == L'\n') {
                i++;
            } else if (i + 1 < len && content[i] == L'/' && content[i + 1] == L'/') {
                while (i < len && content[i] != L'\n') i++;
            } else {
                break;
            }
        }
    };

    auto readString = [&]() -> std::wstring {
        if (i >= len || content[i] != L'"') return L"";
        i++; // skip open quote
        std::wstring str;
        while (i < len && content[i] != L'"') {
            if (content[i] == L'\\' && i + 1 < len) {
                i++;
                if (content[i] == L'n') str += L'\n';
                else if (content[i] == L't') str += L'\t';
                else if (content[i] == L'"') str += L'"';
                else if (content[i] == L'\\') str += L'\\';
                else str += content[i];
            } else {
                str += content[i];
            }
            i++;
        }
        if (i < len && content[i] == L'"') i++; // skip close quote
        return str;
    };

    skipWs();
    if (i >= len || content[i] != L'{') return;
    i++; // skip root {

    while (i < len) {
        skipWs();
        if (i >= len || content[i] == L'}') break;

        std::wstring snippetName = readString();
        skipWs();
        if (i >= len || content[i] != L':') break;
        i++; // skip :
        skipWs();
        if (i >= len || content[i] != L'{') break;
        i++; // skip {

        Snippet sn;
        sn.name = snippetName;

        while (i < len) {
            skipWs();
            if (i >= len || content[i] == L'}') {
                i++;
                break;
            }

            std::wstring key = readString();
            skipWs();
            if (i < len && content[i] == L':') i++;
            skipWs();

            if (key == L"prefix") {
                sn.prefix = readString();
            } else if (key == L"description") {
                sn.description = readString();
            } else if (key == L"scope") {
                sn.scope = readString();
            } else if (key == L"body") {
                if (i < len && content[i] == L'[') {
                    i++; // skip [
                    while (i < len) {
                        skipWs();
                        if (i >= len || content[i] == L']') {
                            i++;
                            break;
                        }
                        sn.body.push_back(readString());
                        skipWs();
                        if (i < len && content[i] == L',') i++;
                    }
                } else if (i < len && content[i] == L'"') {
                    sn.body.push_back(readString());
                }
            } else {
                // Unknown field - skip string or array
                if (i < len && content[i] == L'"') readString();
                else if (i < len && content[i] == L'[') {
                    while (i < len && content[i] != L']') i++;
                    if (i < len) i++;
                }
            }

            skipWs();
            if (i < len && content[i] == L',') i++;
        }

        if (!sn.prefix.empty() && !sn.body.empty()) {
            // Replace existing or add
            auto it = std::find_if(m_snippets.begin(), m_snippets.end(), [&](const Snippet& s) {
                return (s.name == sn.name && s.prefix == sn.prefix) || (s.prefix == sn.prefix && s.scope == sn.scope);
            });
            if (it != m_snippets.end()) {
                *it = sn;
            } else {
                m_snippets.push_back(sn);
            }
        }

        skipWs();
        if (i < len && content[i] == L',') i++;
    }
}

bool SnippetManager::MatchesScope(const std::wstring& scope, const std::wstring& fileExt) {
    if (scope.empty()) return true;
    if (fileExt.empty()) return true;

    std::wstring lowerExt = fileExt;
    for (auto& c : lowerExt) c = towlower(c);
    if (!lowerExt.empty() && lowerExt[0] == L'.') {
        lowerExt = lowerExt.substr(1);
    }

    std::wstringstream ss(scope);
    std::wstring item;
    while (std::getline(ss, item, L',')) {
        size_t f = item.find_first_not_of(L" ");
        size_t l = item.find_last_not_of(L" ");
        if (f != std::wstring::npos) item = item.substr(f, l - f + 1);
        for (auto& c : item) c = towlower(c);
        if (!item.empty() && item[0] == L'.') {
            item = item.substr(1);
        }
        if (item == lowerExt) return true;
    }
    return false;
}

const Snippet* SnippetManager::FindSnippetByPrefix(const std::wstring& prefix, const std::wstring& fileExt) const {
    for (const auto& sn : m_snippets) {
        if (sn.prefix == prefix && MatchesScope(sn.scope, fileExt)) {
            return &sn;
        }
    }
    for (const auto& sn : m_snippets) {
        if (sn.prefix == prefix && sn.scope.empty()) {
            return &sn;
        }
    }
    return nullptr;
}

bool SnippetManager::ExpandSnippet(const Snippet& snippet, Buffer& buffer, size_t prefixLen) {
    std::vector<std::wstring> processedLines;
    int targetCursorLineOffset = -1;
    int targetCursorCol = -1;
    bool foundPlaceholder1 = false;
    bool foundPlaceholder0 = false;

    for (size_t lIdx = 0; lIdx < snippet.body.size(); ++lIdx) {
        std::wstring raw = snippet.body[lIdx];
        std::wstring clean;

        for (size_t c = 0; c < raw.length(); ++c) {
            if (raw[c] == L'$') {
                if (c + 1 < raw.length() && raw[c + 1] == L'{') {
                    // ${1:name}
                    size_t colon = raw.find(L':', c + 2);
                    size_t close = raw.find(L'}', c + 2);
                    if (colon != std::wstring::npos && close != std::wstring::npos && colon < close) {
                        std::wstring defVal = raw.substr(colon + 1, close - colon - 1);
                        if (!foundPlaceholder1) {
                            targetCursorLineOffset = static_cast<int>(lIdx);
                            targetCursorCol = static_cast<int>(clean.length());
                            foundPlaceholder1 = true;
                        }
                        clean += defVal;
                        c = close;
                        continue;
                    }
                } else if (c + 1 < raw.length() && raw[c + 1] == L'1') {
                    if (!foundPlaceholder1) {
                        targetCursorLineOffset = static_cast<int>(lIdx);
                        targetCursorCol = static_cast<int>(clean.length());
                        foundPlaceholder1 = true;
                    }
                    c++; // skip 1
                    continue;
                } else if (c + 1 < raw.length() && raw[c + 1] == L'0') {
                    if (!foundPlaceholder0 && !foundPlaceholder1) {
                        targetCursorLineOffset = static_cast<int>(lIdx);
                        targetCursorCol = static_cast<int>(clean.length());
                        foundPlaceholder0 = true;
                    }
                    c++; // skip 0
                    continue;
                } else if (c + 1 < raw.length() && iswdigit(raw[c + 1])) {
                    c++; // skip other digits $2, $3...
                    continue;
                }
            }
            clean += raw[c];
        }
        processedLines.push_back(clean);
    }

    return buffer.ReplacePrefixWithSnippet(prefixLen, processedLines, targetCursorLineOffset, targetCursorCol);
}

bool SnippetManager::TryExpand(Buffer& buffer, const std::wstring& fileExt) {
    auto cursor = buffer.GetCursor();
    std::wstring line = buffer.GetLine(cursor.line);
    if (cursor.col == 0 || line.empty()) return false;

    size_t startCol = cursor.col;
    while (startCol > 0) {
        wchar_t ch = line[startCol - 1];
        if (iswalnum(ch) || ch == L'_' || ch == L'!' || ch == L':') {
            --startCol;
        } else {
            break;
        }
    }
    if (startCol == cursor.col) return false;

    std::wstring prefix = line.substr(startCol, cursor.col - startCol);
    const Snippet* pMatch = FindSnippetByPrefix(prefix, fileExt);
    if (!pMatch) return false;

    return ExpandSnippet(*pMatch, buffer, prefix.length());
}

} // namespace phide::editor
