#pragma once

#include <string>
#include <vector>
#include "phide/editor/IntelliSenseEngine.hpp"

namespace phide::editor {

/**
 * @brief Retrieves the compiled official DefinitelyTyped @types/react & React snippets library.
 */
const std::vector<IntelliSenseItem>& GetReactGlobalSymbols();

} // namespace phide::editor
