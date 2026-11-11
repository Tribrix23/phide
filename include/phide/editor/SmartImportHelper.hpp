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
        std::filesystem::path rootP(workspaceRoot);
        std::filesystem::path targetP(targetFilePath);
        if (!std::filesystem::exists(targetP, ec)) return referencingFiles;

        std::wstring targetStem = targetP.stem().wstring();
        std::wstring targetFilename = targetP.filename().wstring();

        static const std::unordered_set<std::wstring> ignoreDirs = {
            L".git", L".phide", L"node_modules", L"vendor", L".venv", L"venv",
            L"bin", L"obj", L"dist", L"build", L".next", L".cache", L"__pycache__"
        };

        static const std::unordered_set<std::wstring> validExts = {
            L".js", L".jsx", L".ts", L".tsx", L".mjs", L".cjs",
            L".html", L".htm", L".php", L".css", L".scss", L".sass", L".less",
            L".py", L".json", L".vue", L".svelte"
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

            std::wstring candidatePath = it->path().wstring();
            if (_wcsicmp(candidatePath.c_str(), targetFilePath.c_str()) == 0) continue;

            std::wstring relWithExt = ComputeRelativePath(candidatePath, targetFilePath);
            std::wstring relNoExt = relWithExt;
            size_t dotPos = relNoExt.rfind(L'.');
            if (dotPos != std::wstring::npos && dotPos > relNoExt.rfind(L'/')) {
                relNoExt = relNoExt.substr(0, dotPos);
            }

            std::wstring bareWithExt = relWithExt.starts_with(L"./") ? relWithExt.substr(2) : relWithExt;
            std::wstring bareNoExt = relNoExt.starts_with(L"./") ? relNoExt.substr(2) : relNoExt;

            auto fSize = it->file_size(ec);
            if (ec || fSize > 2 * 1024 * 1024) { ec.clear(); continue; }

            std::ifstream fin(it->path(), std::ios::binary);
            if (!fin.is_open()) continue;
            std::string utf8Content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
            fin.close();

            if (utf8Content.empty()) continue;

            std::string narrowStem = ToUtf8(targetStem);
            if (utf8Content.find(narrowStem) == std::string::npos) continue;

            std::string narrowRelWithExt = ToUtf8(relWithExt);
            std::string narrowRelNoExt = ToUtf8(relNoExt);
            std::string narrowBareWithExt = ToUtf8(bareWithExt);
            std::string narrowBareNoExt = ToUtf8(bareNoExt);

            bool hasRef = false;
            if (utf8Content.find(narrowRelWithExt) != std::string::npos ||
                utf8Content.find(narrowRelNoExt) != std::string::npos ||
                utf8Content.find(narrowBareWithExt) != std::string::npos ||
                utf8Content.find(narrowBareNoExt) != std::string::npos) {
                hasRef = true;
            } else if (fileExt == L".py") {
                if (utf8Content.find("import " + narrowStem) != std::string::npos ||
                    utf8Content.find("from " + narrowStem) != std::string::npos) {
                    hasRef = true;
                }
            } else if (fileExt == L".php") {
                std::string narrowFn = ToUtf8(targetFilename);
                if (utf8Content.find(narrowFn) != std::string::npos) {
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

        std::filesystem::path oldP(oldTargetPath);
        std::filesystem::path newP(newTargetPath);
        std::wstring oldStem = oldP.stem().wstring();
        std::wstring newStem = newP.stem().wstring();
        std::wstring oldFn = oldP.filename().wstring();
        std::wstring newFn = newP.filename().wstring();

        std::wstring oldRelWithExt = ComputeRelativePath(filePath, oldTargetPath);
        std::wstring newRelWithExt = ComputeRelativePath(filePath, newTargetPath);

        std::wstring oldRelNoExt = oldRelWithExt;
        size_t d1 = oldRelNoExt.rfind(L'.');
        if (d1 != std::wstring::npos && d1 > oldRelNoExt.rfind(L'/')) oldRelNoExt = oldRelNoExt.substr(0, d1);

        std::wstring newRelNoExt = newRelWithExt;
        size_t d2 = newRelNoExt.rfind(L'.');
        if (d2 != std::wstring::npos && d2 > newRelNoExt.rfind(L'/')) newRelNoExt = newRelNoExt.substr(0, d2);

        std::wstring oldBareWithExt = oldRelWithExt.starts_with(L"./") ? oldRelWithExt.substr(2) : oldRelWithExt;
        std::wstring newBareWithExt = newRelWithExt.starts_with(L"./") ? newRelWithExt.substr(2) : newRelWithExt;

        std::wstring oldBareNoExt = oldRelNoExt.starts_with(L"./") ? oldRelNoExt.substr(2) : oldRelNoExt;
        std::wstring newBareNoExt = newRelNoExt.starts_with(L"./") ? newRelNoExt.substr(2) : newRelNoExt;

        bool modified = false;

        auto replaceAll = [&](const std::wstring& from, const std::wstring& to) {
            if (from.empty() || from == to) return;
            size_t pos = 0;
            while ((pos = content.find(from, pos)) != std::wstring::npos) {
                content.replace(pos, from.length(), to);
                pos += to.length();
                modified = true;
            }
        };

        // 1. Full relative with extension (e.g. "./components/Button.jsx")
        replaceAll(oldRelWithExt, newRelWithExt);

        // 2. Relative without extension (e.g. "./components/Button")
        replaceAll(oldRelNoExt, newRelNoExt);

        // 3. Bare paths (e.g. "components/Button.jsx", "components/Button")
        if (oldBareWithExt != oldRelWithExt) {
            replaceAll(oldBareWithExt, newBareWithExt);
        }
        if (oldBareNoExt != oldRelNoExt) {
            replaceAll(oldBareNoExt, newBareNoExt);
        }

        // 4. Filename replacement inside quotes for PHP / HTML (e.g. include 'old.php', <script src="old.js">)
        if (oldFn != newFn) {
            replaceAll(L"'" + oldFn + L"'", L"'" + newFn + L"'");
            replaceAll(L"\"" + oldFn + L"\"", L"\"" + newFn + L"\"");
        }

        // 5. Python stem replacement (e.g. import oldStem -> import newStem, from oldStem -> from newStem)
        if (oldStem != newStem) {
            replaceAll(L"import " + oldStem, L"import " + newStem);
            replaceAll(L"from " + oldStem, L"from " + newStem);
            replaceAll(L"from ." + oldStem, L"from ." + newStem);
        }

        return modified;
    }
};

} // namespace phide::editor
