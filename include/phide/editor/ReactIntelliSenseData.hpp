#pragma once

#include <string>
#include <vector>
#include "phide/editor/IntelliSenseEngine.hpp"

namespace phide::editor {

/**
 * @brief Retrieves the compiled official DefinitelyTyped @types/react & React snippets library.
 */
const std::vector<IntelliSenseItem>& GetReactGlobalSymbols();

/**
 * @brief Retrieves the compiled official React JSX attributes (e.g. className, style, onClick, etc.).
 */
const std::vector<IntelliSenseItem>& GetReactJsxAttributes();

} // namespace phide::editor
