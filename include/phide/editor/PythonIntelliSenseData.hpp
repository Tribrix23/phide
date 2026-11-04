#pragma once

#include <string>
#include <vector>
#include "phide/editor/IntelliSenseEngine.hpp"

namespace phide::editor {

/**
 * @brief Retrieves the compiled official VS Code & Typeshed Python symbol library.
 */
const std::vector<IntelliSenseItem>& GetPythonGlobalSymbols();

/**
 * @brief Retrieves member functions and attributes of a standard Python module (e.g. "os", "sys", "json", "math").
 */
std::vector<IntelliSenseItem> QueryPythonModuleMembers(const std::wstring& moduleName);

} // namespace phide::editor
