#pragma once

#include "phide/editor/IntelliSenseEngine.hpp"
#include <string>
#include <vector>

namespace phide::editor {

struct PhpClassConstantEntry {
    const wchar_t* className;
    const wchar_t* memberName;
    const wchar_t* detail;
};

struct PhpClassMethodEntry {
    const wchar_t* className;
    const wchar_t* methodName;
    const wchar_t* detail;
    bool isStatic;
};

/**
 * @brief Populates the global PHP IntelliSense symbol table generated from
 * Microsoft VS Code (php-language-features), JetBrains phpstorm-stubs, and PHP 8.5 Reflection.
 */
void PopulateBuiltInPhpIntelliSense(std::vector<IntelliSenseItem>& outSymbols);

/**
 * @brief Returns static constants and static methods for a given PHP class (e.g., "PDO", "DateTime") after `::`.
 */
std::vector<IntelliSenseItem> QueryPhpClassStaticMembers(const std::wstring& className);

/**
 * @brief Returns instance methods for a given PHP class (e.g., "PDO", "PDOStatement", "PDOException", "mysqli") after `->`.
 */
std::vector<IntelliSenseItem> QueryPhpClassInstanceMethods(const std::wstring& receiverClass);

} // namespace phide::editor
