#pragma once

#include <string>
#include <vector>
#include "phide/editor/IntelliSenseEngine.hpp"

namespace phide::editor {

/**
 * @brief Retrieves the complete official React 19, React DOM, and Next.js App Router
 *        hooks, components, APIs, and TypeScript symbols.
 */
const std::vector<IntelliSenseItem>& GetReactGlobalSymbols();

/**
 * @brief Retrieves the complete React.* namespace members (hooks, components, utilities,
 *        and all @types/react TypeScript interfaces/event/attribute types).
 */
const std::vector<IntelliSenseItem>& GetReactNamespaceMembers();

/**
 * @brief Retrieves ReactDOM.* namespace members (createPortal, flushSync, createRoot, etc.).
 */
const std::vector<IntelliSenseItem>& GetReactDomNamespaceMembers();

/**
 * @brief Retrieves the complete official React JSX attributes (core React props,
 *        all DOM event handlers, all HTML element attributes, all WAI-ARIA attributes,
 *        and SVG JSX attributes).
 */
const std::vector<IntelliSenseItem>& GetReactJsxAttributes();

/**
 * @brief Retrieves specialized high-priority JSX prop completions for Next.js built-in
 *        components (Image, Link, Script, Form). Returns empty vector for other tags.
 */
const std::vector<IntelliSenseItem>& GetNextJsComponentProps(const std::wstring& tagName);

} // namespace phide::editor
