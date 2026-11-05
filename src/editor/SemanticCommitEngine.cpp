#include "phide/editor/SemanticCommitEngine.hpp"
#include <algorithm>
#include <cwctype>
#include <sstream>
#include <regex>

namespace phide::editor {

static std::wstring ToLower(const std::wstring& s) {
    std::wstring res = s;
    for (auto& c : res) c = towlower(c);
    return res;
}

static std::wstring Trim(const std::wstring& s) {
    size_t first = s.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    size_t last = s.find_last_not_of(L" \t\r\n");
    return s.substr(first, (last - first + 1));
}

std::wstring SemanticCommitEngine::Generate(const std::vector<GitChangeInfo>& changes, const std::wstring& diffOutput) {
    if (changes.empty()) {
        return L"chore: update repository files";
    }

    const auto& primary = changes[0];
    const std::wstring& path = primary.path;
    const std::wstring& fileName = primary.fileName;
    const std::wstring& status = primary.status;

    // Extract file extension and base name
    size_t dotPos = fileName.rfind(L'.');
    std::wstring baseName = (dotPos != std::wstring::npos) ? fileName.substr(0, dotPos) : fileName;
    std::wstring ext = (dotPos != std::wstring::npos) ? ToLower(fileName.substr(dotPos)) : L"";

    // Deleted file handling
    if (status == L"D") {
        if (changes.size() > 1) {
            return L"refactor: remove " + fileName + L" and " + std::to_wstring(changes.size() - 1) + L" other files";
        }
        return L"refactor: remove " + fileName;
    }

    // Parse diff lines into added and deleted lines
    std::vector<std::wstring> addedLines;
    std::vector<std::wstring> deletedLines;
    std::wistringstream diffStream(diffOutput);
    std::wstring dLine;
    while (std::getline(diffStream, dLine)) {
        if (dLine.empty()) continue;
        if (dLine[0] == L'+' && dLine.substr(0, 3) != L"+++") {
            addedLines.push_back(Trim(dLine.substr(1)));
        } else if (dLine[0] == L'-' && dLine.substr(0, 3) != L"---") {
            deletedLines.push_back(Trim(dLine.substr(1)));
        }
    }

    std::wstring addedText;
    for (const auto& l : addedLines) {
        addedText += l + L" ";
    }
    std::wstring deletedText;
    for (const auto& l : deletedLines) {
        deletedText += l + L" ";
    }

    std::wstring lowerAdded = ToLower(addedText);
    std::wstring lowerDeleted = ToLower(deletedText);

    // 1. Symbol Extraction (Components, Classes, Functions)
    std::wstring symbol;
    std::wistringstream fullDiffStream(diffOutput);
    std::wstring scanLine;
    std::wregex symRegex(LR"((?:const|let|var|function|class|def)\s+([A-Za-z0-9_$]+))");
    while (std::getline(fullDiffStream, scanLine)) {
        std::wsmatch match;
        if (std::regex_search(scanLine, match, symRegex) && match.size() > 1) {
            std::wstring found = match[1].str();
            if (found != L"React" && found != L"useState" && found != L"useEffect" &&
                found != L"useCallback" && found != L"useMemo" && found != L"useRef" &&
                found != L"require" && found != L"import" && found != L"export") {
                symbol = found;
                break;
            }
        }
    }

    if (symbol.empty()) {
        symbol = baseName;
    }
    std::wstring scope = symbol;

    // 2. Dependency updates (package.json / composer.json)
    if (fileName == L"package.json" || fileName == L"composer.json" || fileName == L"package-lock.json") {
        std::wregex pkgRegex(LR"(["'](@?[A-Za-z0-9_\-\/]+)["']:\s*["'][\^~]?[0-9])");
        std::wsmatch pkgMatch;
        if (std::regex_search(addedText, pkgMatch, pkgRegex) && pkgMatch.size() > 1) {
            return L"chore(deps): add " + pkgMatch[1].str() + L" dependency";
        }
        if (std::regex_search(deletedText, pkgMatch, pkgRegex) && pkgMatch.size() > 1) {
            return L"chore(deps): remove " + pkgMatch[1].str() + L" dependency";
        }
        return L"chore(deps): update dependencies in " + fileName;
    }

    // 3. Documentation files
    if (ext == L".md" || ext == L".txt" || ext == L".rst" || ToLower(baseName).find(L"readme") != std::wstring::npos) {
        return L"docs: update " + fileName + L" documentation";
    }

    // 4. Styling files
    if (ext == L".css" || ext == L".scss" || ext == L".sass" || ext == L".less") {
        return L"style(" + scope + L"): update " + baseName + L" styles";
    }

    // 5. Database & SQL files
    if (ext == L".sql" || ToLower(baseName).find(L"schema") != std::wstring::npos || ToLower(baseName).find(L"migration") != std::wstring::npos) {
        if (lowerAdded.find(L"create table") != std::wstring::npos) {
            return L"feat(db): create database tables in " + fileName;
        }
        return L"feat(db): update database schema in " + fileName;
    }

    // 6. Bug Fix / Error Handling intent
    bool hasFixIndicator = (
        lowerAdded.find(L"nullptr") != std::wstring::npos ||
        lowerAdded.find(L"catch") != std::wstring::npos ||
        lowerAdded.find(L"throw") != std::wstring::npos ||
        lowerAdded.find(L"die(") != std::wstring::npos ||
        lowerAdded.find(L"exit(") != std::wstring::npos ||
        lowerAdded.find(L"error") != std::wstring::npos ||
        lowerAdded.find(L"assert") != std::wstring::npos ||
        lowerAdded.find(L"isset") != std::wstring::npos ||
        lowerAdded.find(L"empty(") != std::wstring::npos ||
        lowerAdded.find(L"=== null") != std::wstring::npos ||
        lowerAdded.find(L"!== null") != std::wstring::npos
    );
    bool delHadFix = (
        lowerDeleted.find(L"catch") != std::wstring::npos ||
        lowerDeleted.find(L"throw") != std::wstring::npos
    );
    if (hasFixIndicator && !delHadFix) {
        return L"fix(" + scope + L"): handle validation and error checks in " + scope;
    }

    // 7. React JSX / TSX / Vue / Svelte components
    if (ext == L".jsx" || ext == L".tsx" || ext == L".vue" || ext == L".svelte" ||
        (ext == L".js" && addedText.find(L"<") != std::wstring::npos && addedText.find(L">") != std::wstring::npos)) {
        if (lowerAdded.find(L"return") != std::wstring::npos && (addedText.find(L"<") != std::wstring::npos || addedText.find(L">") != std::wstring::npos)) {
            return L"feat(" + scope + L"): update JSX render return in " + scope + L" component";
        }
        if (addedText.find(L"useState") != std::wstring::npos || addedText.find(L"useEffect") != std::wstring::npos ||
            addedText.find(L"useCallback") != std::wstring::npos || addedText.find(L"useMemo") != std::wstring::npos ||
            addedText.find(L"useRef") != std::wstring::npos) {
            return L"feat(" + scope + L"): add state hooks and lifecycle logic in " + scope;
        }
        if (addedText.find(L"<") != std::wstring::npos && addedText.find(L">") != std::wstring::npos) {
            return L"feat(" + scope + L"): update JSX template structure in " + scope;
        }
    }

    // 8. PHP Backend Logic & Database
    if (ext == L".php") {
        if (addedText.find(L"PDO") != std::wstring::npos || addedText.find(L"mysqli") != std::wstring::npos ||
            addedText.find(L"SELECT ") != std::wstring::npos || addedText.find(L"INSERT ") != std::wstring::npos ||
            addedText.find(L"UPDATE ") != std::wstring::npos || addedText.find(L"DELETE ") != std::wstring::npos ||
            addedText.find(L"query(") != std::wstring::npos) {
            return L"feat(" + scope + L"): update database queries and data access in " + fileName;
        }
        std::wregex phpFuncRegex(LR"(function\s+([A-Za-z0-9_]+)\s*\()");
        std::wsmatch fnMatch;
        if (std::regex_search(addedText, fnMatch, phpFuncRegex) && fnMatch.size() > 1) {
            return L"feat(" + scope + L"): add " + fnMatch[1].str() + L" function in " + fileName;
        }
    }

    // 9. Python Functions & Classes
    if (ext == L".py") {
        std::wregex pyFuncRegex(LR"(def\s+([A-Za-z0-9_]+)\s*\()");
        std::wsmatch fnMatch;
        if (std::regex_search(addedText, fnMatch, pyFuncRegex) && fnMatch.size() > 1) {
            return L"feat(" + scope + L"): add " + fnMatch[1].str() + L" function in " + fileName;
        }
    }

    // 10. Imports / Exports Only Refactor
    if (!addedLines.empty()) {
        bool allImports = true;
        for (const auto& l : addedLines) {
            if (!l.empty() && !l.starts_with(L"import ") && !l.starts_with(L"from ") &&
                !l.starts_with(L"require(") && !l.starts_with(L"#include") && !l.starts_with(L"export ")) {
                allImports = false;
                break;
            }
        }
        if (allImports) {
            return L"refactor(" + scope + L"): update imports and module exports in " + fileName;
        }
    }

    // 11. Multi-File Changes
    if (changes.size() > 1) {
        return L"feat: update " + fileName + L" and " + std::to_wstring(changes.size() - 1) + L" other files";
    }

    // 12. New File Creation
    if (status == L"A" || status == L"??" || deletedLines.empty()) {
        return L"feat(" + scope + L"): add " + fileName;
    }

    // Fallback: Clear, standard Conventional Commit
    return L"feat(" + scope + L"): update " + fileName;
}

} // namespace phide::editor
