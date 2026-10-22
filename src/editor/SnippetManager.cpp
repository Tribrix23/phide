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

    // 1. C / C++ Snippets
    m_snippets.push_back({
        L"Print to Console (C++)",
        L"log",
        { L"std::cout << \"$1\" << std::endl;" },
        L"Print string to standard console output",
        L"cpp,hpp,c,h,cc,cxx"
    });
    m_snippets.push_back({
        L"Print to Console (C++)",
        L"cout",
        { L"std::cout << $1 << std::endl;" },
        L"Print expression to standard console output",
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
        L"For Loop (C++)",
        L"for",
        {
            L"for (size_t ${1:i} = 0; ${1:i} < ${2:count}; ++${1:i}) {",
            L"    $0",
            L"}"
        },
        L"Standard for loop",
        L"cpp,c,cc,cxx"
    });
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

    // 2. TypeScript / JavaScript Snippets
    m_snippets.push_back({
        L"Console Log",
        L"clg",
        { L"console.log($1);" },
        L"Log output to developer console",
        L"ts,js,tsx,jsx"
    });
    m_snippets.push_back({
        L"Console Log",
        L"log",
        { L"console.log($1);" },
        L"Log output to developer console",
        L"ts,js,tsx,jsx"
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
        L"ts,js,tsx,jsx"
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
        L"ts,js,tsx,jsx"
    });
    m_snippets.push_back({
        L"Try Catch Block",
        L"try",
        {
            L"try {",
            L"    $1",
            L"} catch (error) {",
            L"    console.error(error);",
            L"    $0",
            L"}"
        },
        L"Try Catch error handling block",
        L"ts,js,tsx,jsx"
    });
    m_snippets.push_back({
        L"For Loop (JS)",
        L"for",
        {
            L"for (let ${1:i} = 0; ${1:i} < ${2:array}.length; ${1:i}++) {",
            L"    $0",
            L"}"
        },
        L"Standard JS for loop",
        L"ts,js,tsx,jsx"
    });
    m_snippets.push_back({
        L"Import Statement",
        L"imp",
        { L"import { $1 } from '$2';" },
        L"ES6 import statement",
        L"ts,js,tsx,jsx"
    });

    // 3. React / JSX Snippets
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

    // 4. Python Snippets
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
        L"Python Print",
        L"print",
        { L"print($1)" },
        L"Python print statement",
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
}

void SnippetManager::EnsureDefaultSnippetsFile() {
    if (m_snippetsFilePath.empty()) return;

    std::filesystem::path p(m_snippetsFilePath);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);

    if (!std::filesystem::exists(p, ec)) {
        std::wofstream out(m_snippetsFilePath);
        if (out.is_open()) {
            out << LR"RAW_SNIPPETS({
    // User Code Snippets for phide IDE
    // Each snippet is defined under a snippet name and has a prefix, body and 
    // description. The prefix is what is used to trigger the snippet and the body will be
    // expanded and inserted. Possible variables are:
    // $1, $2 for tab stops, $0 for the final cursor position, and ${1:label}, ${2:another} for placeholders.

    "Print to Console (C++)": {
        "prefix": "log",
        "body": [
            "std::cout << \"$1\" << std::endl;"
        ],
        "description": "Log output to console in C++",
        "scope": "cpp,hpp,c,h"
    },
    "Main Function": {
        "prefix": "main",
        "body": [
            "int main(int argc, char* argv[]) {",
            "    $0",
            "    return 0;",
            "}"
        ],
        "description": "Standard C++ main entry point",
        "scope": "cpp,c"
    },
    "Console Log (JS/TS)": {
        "prefix": "clg",
        "body": [
            "console.log($1);"
        ],
        "description": "Console log in JS/TS",
        "scope": "ts,js,tsx,jsx"
    },
    "Arrow Function": {
        "prefix": "afn",
        "body": [
            "const ${1:name} = ($2) => {",
            "    $0",
            "};"
        ],
        "description": "Arrow function declaration",
        "scope": "ts,js,tsx,jsx"
    },
    "React Functional Component": {
        "prefix": "rfc",
        "body": [
            "export const ${1:ComponentName} = () => {",
            "    return (",
            "        <div>",
            "            $0",
            "        </div>",
            "    );",
            "};"
        ],
        "description": "React functional component",
        "scope": "tsx,jsx"
    },
    "Python Function": {
        "prefix": "def",
        "body": [
            "def ${1:function_name}($2):",
            "    \"\"\"$3\"\"\"",
            "    $0"
        ],
        "description": "Python function definition",
        "scope": "py"
    }
}
)RAW_SNIPPETS";
            out.close();
        }
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
                return s.name == sn.name || (s.prefix == sn.prefix && s.scope == sn.scope);
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

bool SnippetManager::MatchesScope(const std::wstring& scope, const std::wstring& fileExt) const {
    if (scope.empty()) return true;
    if (fileExt.empty()) return true;

    std::wstring lowerExt = fileExt;
    for (auto& c : lowerExt) c = towlower(c);

    std::wstringstream ss(scope);
    std::wstring item;
    while (std::getline(ss, item, L',')) {
        size_t f = item.find_first_not_of(L" ");
        size_t l = item.find_last_not_of(L" ");
        if (f != std::wstring::npos) item = item.substr(f, l - f + 1);
        for (auto& c : item) c = towlower(c);
        if (item == lowerExt) return true;
    }
    return false;
}

bool SnippetManager::TryExpand(Buffer& buffer, const std::wstring& fileExt) {
    auto cursor = buffer.GetCursor();
    std::wstring line = buffer.GetLine(cursor.line);
    if (cursor.col == 0 || line.empty()) return false;

    size_t startCol = cursor.col;
    while (startCol > 0 && (iswalnum(line[startCol - 1]) || line[startCol - 1] == L'_')) {
        --startCol;
    }
    if (startCol == cursor.col) return false;

    std::wstring prefix = line.substr(startCol, cursor.col - startCol);

    // Search matching snippet
    const Snippet* pMatch = nullptr;
    for (const auto& sn : m_snippets) {
        if (sn.prefix == prefix && MatchesScope(sn.scope, fileExt)) {
            pMatch = &sn;
            break;
        }
    }
    if (!pMatch) {
        // Fallback search with empty/global scope
        for (const auto& sn : m_snippets) {
            if (sn.prefix == prefix && sn.scope.empty()) {
                pMatch = &sn;
                break;
            }
        }
    }
    if (!pMatch) return false;

    // Process placeholders ($1, ${1:default}, $0) in body
    std::vector<std::wstring> processedLines;
    int targetCursorLineOffset = -1;
    int targetCursorCol = -1;
    bool foundPlaceholder1 = false;
    bool foundPlaceholder0 = false;

    for (size_t lIdx = 0; lIdx < pMatch->body.size(); ++lIdx) {
        std::wstring raw = pMatch->body[lIdx];
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

    return buffer.ReplacePrefixWithSnippet(prefix.length(), processedLines, targetCursorLineOffset, targetCursorCol);
}

} // namespace phide::editor
