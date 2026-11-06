#pragma once

#include <filesystem>
#include <string>
#include <vector>

#ifdef SearchPath
#undef SearchPath
#endif

namespace phide::editor::pyenv {

/**
 * @brief Where an importable Python package directory comes from.
 *
 * ProjectEnv  - a virtual environment inside the workspace (any folder containing pyvenv.cfg, e.g. .venv / venv / env).
 * External    - a site-packages folder of a Python interpreter installed on the machine (External Libraries).
 * StdLib      - the standard library folder of the base interpreter.
 */
enum class PackageSource {
    ProjectEnv,
    External,
    StdLib
};

/**
 * @brief An importable search-path folder, discovered dynamically from the workspace and the installed interpreters.
 */
struct SearchPath {
    std::filesystem::path path;
    std::wstring label;          ///< Human readable origin, e.g. L"Project .venv" or L"Python 3.15 (Global)"
    PackageSource source = PackageSource::External;
};

/**
 * @brief A top-level importable module / package found in one of the search paths.
 */
struct ModuleEntry {
    std::wstring name;
    std::wstring label;
    PackageSource source = PackageSource::External;
};

/**
 * @brief A member of a module, reported by the real interpreter (dir() + inspect).
 */
struct ModuleMember {
    std::wstring name;
    wchar_t kind = L'v';         ///< 'm' module, 'c' class, 'f' function / callable, 'v' value
    std::wstring signature;      ///< e.g. L"(object, dtype=None, *, copy=True)"
};

/**
 * @brief Dynamically resolves the import search paths for a workspace.
 *
 * Order: project virtual environments first, then (only when the project env allows system site-packages, or no
 * project env exists) the site-packages of the machine's Python interpreters, then the standard library.
 * Results are cached briefly so it is safe to call on every keystroke.
 */
std::vector<SearchPath> GetSearchPaths(const std::filesystem::path& projectRoot);

/**
 * @brief Lists every top-level importable module across the search paths (deduplicated, project env wins).
 */
std::vector<ModuleEntry> ListTopLevelModules(const std::filesystem::path& projectRoot);

/**
 * @brief Lists the direct sub-modules / sub-packages of a dotted package (e.g. L"numpy" -> linalg, fft, random...).
 */
std::vector<ModuleEntry> ListSubModules(const std::filesystem::path& projectRoot, const std::wstring& dottedPackage);

/**
 * @brief The interpreter that will actually run project code: project venv interpreter if present, else a system one.
 */
std::filesystem::path GetPreferredInterpreter(const std::filesystem::path& projectRoot);

/**
 * @brief Pre-warms the module member cache in the background without blocking the caller thread.
 */
void PrefetchModule(const std::filesystem::path& projectRoot, const std::wstring& dottedModule);

/**
 * @brief Gets the real members of a module by asking the project's interpreter (import + dir + inspect).
 *
 * Non-blocking on UI when maxWaitMs is small: starts a background introspection and waits up to @p maxWaitMs;
 * once finished the result is cached and subsequent calls return true in O(1).
 */
bool GetModuleMembers(const std::filesystem::path& projectRoot, const std::wstring& dottedModule, std::vector<ModuleMember>& out, int maxWaitMs = 25);

/**
 * @brief Drops all cached search paths, module listings and introspection results (call after installing packages).
 */
void InvalidateCaches();

} // namespace phide::editor::pyenv
