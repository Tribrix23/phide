/**
 * @file SmartImportHelper.hpp
 * @brief Generates context-aware relative import and include statements when dropping files
 *        from the Explorer tree onto the editor canvas, matching Godot Engine and VS Code behavior.
 */

#pragma once

#include <string>
#include <filesystem>
#include <cwctype>
#include <algorithm>

namespace phide::editor {

class SmartImportHelper {
public:
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
};

} // namespace phide::editor
