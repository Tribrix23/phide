/**
 * @file SmartImportHelper.hpp
 * @brief Generates context-aware relative import and include statements when dropping files
 *        from the Explorer tree onto the editor canvas, matching Godot Engine and VS Code behavior.
 */

#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <filesystem>
#include <cwctype>
#include <algorithm>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace phide::editor {

class SmartImportHelper {
public:
    static std::string ToUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0) return {};
        std::string s(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), size, nullptr, nullptr);
        return s;
    }

    static std::wstring ToWide(const std::string& s) {
        if (s.empty()) return {};
        int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (size <= 0) return {};
        std::wstring w(size, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), size);
        return w;
    }

    /**
     * @brief Converts a filename stem into PascalCase (e.g. "nav-bar" -> "NavBar", "button" -> "Button").
     */
    static std::wstring ToPascalCase(const std::wstring& str) {
        std::wstring res;
        bool capNext = true;
        for (wchar_t ch : str) {
            if (ch == L'-' || ch == L'_' || ch == L'.' || ch == L' ') {
                capNext = true;
            } else if (capNext) {
                res += static_cast<wchar_t>(towupper(ch));
                capNext = false;
            } else {
                res += ch;
            }
        }
        if (!res.empty() && iswdigit(res[0])) {
            res = L"_" + res;
        }
        return res.empty() ? L"Component" : res;
    }

    /**
     * @brief Converts a filename stem into camelCase (e.g. "hero-image" -> "heroImage", "data" -> "data").
     */
    static std::wstring ToCamelCase(const std::wstring& str) {
        std::wstring p = ToPascalCase(str);
        if (!p.empty()) {
            p[0] = static_cast<wchar_t>(towlower(p[0]));
        }
        return p.empty() ? L"asset" : p;
    }

    /**
     * @brief Computes a normalized relative path from destination file directory to source file.
     *        Always uses forward slashes '/' and ensures leading './' or '../'.
     */
    static std::wstring ComputeRelativePath(const std::wstring& destFilePath, const std::wstring& sourceFilePath) {
        if (sourceFilePath.empty()) return L"";
        if (destFilePath.empty()) {
            std::filesystem::path sp(sourceFilePath);
            return L"./" + sp.filename().wstring();
        }

        std::error_code ec;
        std::filesystem::path destP(destFilePath);
        std::filesystem::path srcP(sourceFilePath);

        std::filesystem::path baseDir = destP.parent_path();
        if (baseDir.empty()) {
            baseDir = std::filesystem::current_path(ec);
        }

        std::filesystem::path relP = std::filesystem::relative(srcP, baseDir, ec);
        std::wstring relStr;
        if (ec || relP.empty()) {
            relStr = srcP.filename().wstring();
        } else {
            relStr = relP.wstring();
        }

        // Convert backslashes to forward slashes for all cross-platform / web code
        for (auto& ch : relStr) {
            if (ch == L'\\') ch = L'/';
        }

        // Ensure leading ./ if not starting with ./ or ../ or /
        if (!relStr.starts_with(L"./") && !relStr.starts_with(L"../") && !relStr.starts_with(L"/")) {
            relStr = L"./" + relStr;
        }

        return relStr;
    }

    /**
     * @brief Generates the smart import / include statement based on destination and source file types.
     */
    static std::wstring Generate(const std::wstring& destFilePath, const std::wstring& sourceFilePath) {
        if (sourceFilePath.empty()) return L"";

        std::filesystem::path srcP(sourceFilePath);
        std::filesystem::path destP(destFilePath);

        std::wstring srcExt = srcP.extension().wstring();
        for (auto& c : srcExt) c = static_cast<wchar_t>(towlower(c));

        std::wstring destExt = destP.extension().wstring();
        for (auto& c : destExt) c = static_cast<wchar_t>(towlower(c));

        std::wstring stem = srcP.stem().wstring();
        std::wstring rel = ComputeRelativePath(destFilePath, sourceFilePath);

        // 1. Godot Engine GDScript / Scene (.gd, .tscn, .tres, .gdshader)
        if (destExt == L".gd" || destExt == L".tscn" || destExt == L".tres" || destExt == L".gdshader") {
            std::wstring varName = ToPascalCase(stem);
            return L"const " + varName + L" = preload(\"" + rel + L"\")\n";
        }

        // 2. JavaScript / TypeScript / React / Vue / Svelte
        if (destExt == L".js" || destExt == L".jsx" || destExt == L".ts" || destExt == L".tsx" ||
            destExt == L".mjs" || destExt == L".cjs" || destExt == L".vue" || destExt == L".svelte") {
            
            // Code modules: strip code extension from import path
            if (srcExt == L".js" || srcExt == L".jsx" || srcExt == L".ts" || srcExt == L".tsx" ||
                srcExt == L".mjs" || srcExt == L".cjs") {
                std::wstring importPath = rel;
                size_t dotPos = importPath.rfind(L'.');
                if (dotPos != std::wstring::npos) {
                    importPath = importPath.substr(0, dotPos);
                }
                std::wstring compName = ToPascalCase(stem);
                return L"import " + compName + L" from '" + importPath + L"';\n";
            }

            // Stylesheets
            if (srcExt == L".css" || srcExt == L".scss" || srcExt == L".sass" || srcExt == L".less") {
                return L"import '" + rel + L"';\n";
            }

            // JSON data
            if (srcExt == L".json") {
                std::wstring varName = ToCamelCase(stem);
                return L"import " + varName + L" from '" + rel + L"';\n";
            }

            // Images / Media / Assets
            if (srcExt == L".png" || srcExt == L".jpg" || srcExt == L".jpeg" || srcExt == L".svg" ||
                srcExt == L".gif" || srcExt == L".webp" || srcExt == L".ico" || srcExt == L".bmp" ||
                srcExt == L".mp4" || srcExt == L".webm" || srcExt == L".mp3" || srcExt == L".wav") {
                std::wstring assetName = ToCamelCase(stem);
                return L"import " + assetName + L" from '" + rel + L"';\n";
            }

            // Other files: generic import
            std::wstring varName = ToCamelCase(stem);
            return L"import " + varName + L" from '" + rel + L"';\n";
        }

        // 3. HTML / PHP / Blade
        if (destExt == L".html" || destExt == L".htm" || destExt == L".php") {
            if (srcExt == L".css" || srcExt == L".scss" || srcExt == L".less") {
                return L"<link rel=\"stylesheet\" href=\"" + rel + L"\" />\n";
            }
            if (srcExt == L".js" || srcExt == L".ts" || srcExt == L".mjs") {
                return L"<script src=\"" + rel + L"\"></script>\n";
            }
            if (srcExt == L".png" || srcExt == L".jpg" || srcExt == L".jpeg" || srcExt == L".svg" ||
                srcExt == L".gif" || srcExt == L".webp" || srcExt == L".ico" || srcExt == L".bmp") {
                return L"<img src=\"" + rel + L"\" alt=\"" + stem + L"\" />\n";
            }
            if (srcExt == L".mp4" || srcExt == L".webm" || srcExt == L".mov") {
                return L"<video src=\"" + rel + L"\" controls></video>\n";
            }
            if (srcExt == L".mp3" || srcExt == L".wav" || srcExt == L".ogg") {
                return L"<audio src=\"" + rel + L"\" controls></audio>\n";
            }
            if (srcExt == L".html" || srcExt == L".htm" || srcExt == L".php") {
                return L"<a href=\"" + rel + L"\">" + stem + L"</a>\n";
            }
            return L"<a href=\"" + rel + L"\">" + stem + L"</a>\n";
        }

        // 4. CSS / SCSS / LESS
        if (destExt == L".css" || destExt == L".scss" || destExt == L".sass" || destExt == L".less") {
            if (srcExt == L".css" || srcExt == L".scss" || srcExt == L".sass" || srcExt == L".less") {
                return L"@import '" + rel + L"';\n";
            }
            return L"url('" + rel + L"')";
        }

        // 5. C / C++
        if (destExt == L".c" || destExt == L".cpp" || destExt == L".h" || destExt == L".hpp" ||
            destExt == L".cc" || destExt == L".cxx") {
            std::wstring incPath = rel;
            if (incPath.starts_with(L"./")) {
                incPath = incPath.substr(2);
            }
            return L"#include \"" + incPath + L"\"\n";
        }

        // 6. Python
        if (destExt == L".py" || destExt == L".pyw") {
            if (srcExt == L".py" || srcExt == L".pyw") {
                // If in same folder: import stem
                if (rel == L"./" + srcP.filename().wstring()) {
                    return L"import " + stem + L"\n";
                }
                // If in subfolder: from subfolder import stem
                if (rel.starts_with(L"./")) {
                    std::wstring sub = rel.substr(2);
                    size_t slash = sub.rfind(L'/');
                    if (slash != std::wstring::npos) {
                        std::wstring pkg = sub.substr(0, slash);
                        for (auto& c : pkg) if (c == L'/') c = L'.';
                        return L"from " + pkg + L" import " + stem + L"\n";
                    }
                }
                return L"import " + stem + L"\n";
            }
            return L"\"" + rel + L"\"\n";
        }

        // 7. Markdown
        if (destExt == L".md" || destExt == L".markdown") {
            if (srcExt == L".png" || srcExt == L".jpg" || srcExt == L".jpeg" || srcExt == L".svg" ||
                srcExt == L".gif" || srcExt == L".webp") {
                return L"![" + stem + L"](" + rel + L")\n";
            }
            return L"[" + stem + L"](" + rel + L")\n";
        }

        // 8. Fallback
        return L"\"" + rel + L"\"\n";
    }

    /**
     * @brief Finds all files in the workspace that reference targetFilePath via relative import or path.
     */
    static std::vector<std::wstring> FindReferencesToPath(const std::wstring& workspaceRoot, const std::wstring& targetFilePath) {
        std::vector<std::wstring> referencingFiles;
        if (workspaceRoot.empty() || targetFilePath.empty()) return referencingFiles;

        std::error_code ec;
        std::filesystem::path rootP = std::filesystem::path(workspaceRoot).lexically_normal();
        std::filesystem::path targetP = std::filesystem::path(targetFilePath).lexically_normal();

        std::wstring targetStem = targetP.stem().wstring();
        std::wstring targetFilename = targetP.filename().wstring();
        if (targetFilename.empty()) return referencingFiles;

        static const std::unordered_set<std::wstring> ignoreDirs = {
            L".git", L".phide", L"node_modules", L"vendor", L".venv", L"venv",
            L"bin", L"obj", L"dist", L"build", L".next", L".cache", L"__pycache__"
        };

        static const std::unordered_set<std::wstring> validExts = {
            L".js", L".jsx", L".ts", L".tsx", L".mjs", L".cjs",
            L".html", L".htm", L".php", L".css", L".scss", L".sass", L".less",
            L".py", L".json", L".vue", L".svelte", L".md"
        };

        for (auto it = std::filesystem::recursive_directory_iterator(rootP, std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator(); ++it) {
            if (ec) { ec.clear(); break; }
            if (it->is_directory(ec)) {
                std::wstring dirName = it->path().filename().wstring();
                for (auto& c : dirName) c = static_cast<wchar_t>(towlower(c));
                if (ignoreDirs.count(dirName) > 0 || dirName.starts_with(L".")) {
                    it.disable_recursion_pending();
                }
                continue;
            }

            if (!it->is_regular_file(ec)) continue;

            std::wstring fileExt = it->path().extension().wstring();
            for (auto& c : fileExt) c = static_cast<wchar_t>(towlower(c));
            if (validExts.count(fileExt) == 0) continue;

            std::filesystem::path candNorm = it->path().lexically_normal();
            if (_wcsicmp(candNorm.c_str(), targetP.c_str()) == 0) continue;

            std::wstring candidatePath = candNorm.wstring();
            std::wstring relWithExt = ComputeRelativePath(candidatePath, targetP.wstring());
            std::wstring relNoExt = relWithExt;
            size_t dotPos = relNoExt.rfind(L'.');
            if (dotPos != std::wstring::npos && dotPos > relNoExt.rfind(L'/')) {
                relNoExt = relNoExt.substr(0, dotPos);
            }

            auto fSize = it->file_size(ec);
            if (ec || fSize > 2 * 1024 * 1024) { ec.clear(); continue; }

            std::ifstream fin(it->path(), std::ios::binary);
            if (!fin.is_open()) continue;
            std::string utf8Content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
            fin.close();

            if (utf8Content.empty()) continue;

            std::string narrowStem = ToUtf8(targetStem);
            std::string narrowFn = ToUtf8(targetFilename);
            std::string narrowRelWithExt = ToUtf8(relWithExt);
            std::string narrowRelNoExt = ToUtf8(relNoExt);

            // Quick check: does the file mention the filename or stem?
            if (utf8Content.find(narrowFn) == std::string::npos &&
                utf8Content.find(narrowStem) == std::string::npos) {
                continue;
            }

            bool hasRef = false;

            // 1. Check relative path with extension (e.g. "./test.jsx" or "../utils/test.jsx")
            if (!narrowRelWithExt.empty()) {
                size_t pos = 0;
                while ((pos = utf8Content.find(narrowRelWithExt, pos)) != std::string::npos) {
                    size_t after = pos + narrowRelWithExt.length();
                    if (after >= utf8Content.length() ||
                        (!isalnum(static_cast<unsigned char>(utf8Content[after])) &&
                         utf8Content[after] != '_' && utf8Content[after] != '-' && utf8Content[after] != '.')) {
                        hasRef = true;
                        break;
                    }
                    pos += narrowRelWithExt.length();
                }
            }

            // 2. Check relative path without extension (e.g. "./test" or "../utils/test")
            if (!hasRef && !narrowRelNoExt.empty()) {
                size_t pos = 0;
                while ((pos = utf8Content.find(narrowRelNoExt, pos)) != std::string::npos) {
                    size_t after = pos + narrowRelNoExt.length();
                    if (after >= utf8Content.length() ||
                        (!isalnum(static_cast<unsigned char>(utf8Content[after])) &&
                         utf8Content[after] != '_' && utf8Content[after] != '-' && utf8Content[after] != '.')) {
                        hasRef = true;
                        break;
                    }
                    pos += narrowRelNoExt.length();
                }
            }

            // 3. Check filename inside quotes or after / (e.g. "test.jsx", 'test.jsx', `test.jsx`, href="test.jsx", include 'test.php')
            if (!hasRef && !narrowFn.empty()) {
                std::string q1 = "\"" + narrowFn + "\"";
                std::string q2 = "'" + narrowFn + "'";
                std::string q3 = "`" + narrowFn + "`";
                std::string sl = "/" + narrowFn;
                if (utf8Content.find(q1) != std::string::npos ||
                    utf8Content.find(q2) != std::string::npos ||
                    utf8Content.find(q3) != std::string::npos) {
                    hasRef = true;
                } else {
                    size_t pos = 0;
                    while ((pos = utf8Content.find(sl, pos)) != std::string::npos) {
                        size_t after = pos + sl.length();
                        if (after >= utf8Content.length() ||
                            (!isalnum(static_cast<unsigned char>(utf8Content[after])) &&
                             utf8Content[after] != '_' && utf8Content[after] != '-' && utf8Content[after] != '.')) {
                            hasRef = true;
                            break;
                        }
                        pos += sl.length();
                    }
                }
            }

            // 4. Python module imports (e.g. import test, from test import, from .test import)
            if (!hasRef && fileExt == L".py" && !narrowStem.empty()) {
                auto checkPy = [&](const std::string& prefix) -> bool {
                    size_t pos = 0;
                    while ((pos = utf8Content.find(prefix + narrowStem, pos)) != std::string::npos) {
                        size_t after = pos + prefix.length() + narrowStem.length();
                        if (after >= utf8Content.length() ||
                            utf8Content[after] == ' ' || utf8Content[after] == '\t' ||
                            utf8Content[after] == '\r' || utf8Content[after] == '\n' ||
                            utf8Content[after] == ',' || utf8Content[after] == '#' ||
                            utf8Content[after] == ':' || utf8Content[after] == ';') {
                            return true;
                        }
                        pos += prefix.length() + narrowStem.length();
                    }
                    return false;
                };

                if (checkPy("import ") || checkPy("from ") || checkPy("from .") || checkPy("from ..") || checkPy("from . import ")) {
                    hasRef = true;
                }
            }

            if (hasRef) {
                referencingFiles.push_back(candidatePath);
            }
        }

        referencingFiles.shrink_to_fit();
        return referencingFiles;
    }

    /**
     * @brief Updates all relative references in content pointing to oldTargetPath so they now point to newTargetPath.
     */
    static bool UpdateReferencesInContent(std::wstring& content, const std::wstring& filePath, const std::wstring& oldTargetPath, const std::wstring& newTargetPath) {
        if (content.empty() || oldTargetPath.empty() || newTargetPath.empty()) return false;

        std::filesystem::path oldP = std::filesystem::path(oldTargetPath).lexically_normal();
        std::filesystem::path newP = std::filesystem::path(newTargetPath).lexically_normal();
        std::filesystem::path candP = std::filesystem::path(filePath).lexically_normal();

        std::wstring oldStem = oldP.stem().wstring();
        std::wstring newStem = newP.stem().wstring();
        std::wstring oldFn = oldP.filename().wstring();
        std::wstring newFn = newP.filename().wstring();

        std::wstring oldRelWithExt = ComputeRelativePath(candP.wstring(), oldP.wstring());
        std::wstring newRelWithExt = ComputeRelativePath(candP.wstring(), newP.wstring());

        std::wstring oldRelNoExt = oldRelWithExt;
        size_t d1 = oldRelNoExt.rfind(L'.');
        if (d1 != std::wstring::npos && d1 > oldRelNoExt.rfind(L'/')) oldRelNoExt = oldRelNoExt.substr(0, d1);

        std::wstring newRelNoExt = newRelWithExt;
        size_t d2 = newRelNoExt.rfind(L'.');
        if (d2 != std::wstring::npos && d2 > newRelNoExt.rfind(L'/')) newRelNoExt = newRelNoExt.substr(0, d2);

        bool modified = false;

        auto replaceWithBoundary = [&](const std::wstring& from, const std::wstring& to, bool isNoExt) {
            if (from.empty() || from == to) return;
            size_t pos = 0;
            while ((pos = content.find(from, pos)) != std::wstring::npos) {
                size_t after = pos + from.length();
                if (after < content.length()) {
                    wchar_t ch = content[after];
                    if (isNoExt) {
                        if (iswalnum(ch) || ch == L'_' || ch == L'-' || ch == L'.') {
                            pos += from.length();
                            continue;
                        }
                    } else {
                        if (iswalnum(ch) || ch == L'_' || ch == L'-' || ch == L'.') {
                            pos += from.length();
                            continue;
                        }
                    }
                }
                content.replace(pos, from.length(), to);
                pos += to.length();
                modified = true;
            }
        };

        // 1. Full relative path with extension (e.g. "./test.jsx" -> "./x.jsx", "../folder/test.jsx" -> "../folder/x.jsx")
        replaceWithBoundary(oldRelWithExt, newRelWithExt, false);

        // 2. Relative path without extension (e.g. "./test" -> "./x")
        replaceWithBoundary(oldRelNoExt, newRelNoExt, true);

        // 3. Quoted filename replacement (e.g. href="test.jsx" -> href="x.jsx", include 'test.php' -> include 'x.php')
        if (oldFn != newFn) {
            auto replaceQuoted = [&](const std::wstring& qOpen, const std::wstring& qClose) {
                std::wstring from = qOpen + oldFn + qClose;
                std::wstring to = qOpen + newFn + qClose;
                size_t pos = 0;
                while ((pos = content.find(from, pos)) != std::wstring::npos) {
                    content.replace(pos, from.length(), to);
                    pos += to.length();
                    modified = true;
                }
            };
            replaceQuoted(L"\"", L"\"");
            replaceQuoted(L"'", L"'");
            replaceQuoted(L"`", L"`");

            // Also check for slash-prefixed filename e.g. /test.jsx
            replaceWithBoundary(L"/" + oldFn, L"/" + newFn, false);
        }

        // 4. Python imports (import test -> import x, from test import -> from x import, from .test import -> from .x import)
        if (oldStem != newStem) {
            auto replacePy = [&](const std::wstring& prefix) {
                std::wstring from = prefix + oldStem;
                std::wstring to = prefix + newStem;
                size_t pos = 0;
                while ((pos = content.find(from, pos)) != std::wstring::npos) {
                    size_t after = pos + from.length();
                    if (after >= content.length() ||
                        content[after] == L' ' || content[after] == L'\t' ||
                        content[after] == L'\r' || content[after] == L'\n' ||
                        content[after] == L',' || content[after] == L'#' ||
                        content[after] == L':' || content[after] == L';') {
                        content.replace(pos, from.length(), to);
                        pos += to.length();
                        modified = true;
                    } else {
                        pos += from.length();
                    }
                }
            };

            replacePy(L"import ");
            replacePy(L"from ");
            replacePy(L"from .");
            replacePy(L"from ..");
            replacePy(L"from . import ");
        }

        return modified;
    }
};

} // namespace phide::editor
