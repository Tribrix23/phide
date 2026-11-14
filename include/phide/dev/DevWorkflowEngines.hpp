/**
 * @file DevWorkflowEngines.hpp
 * @brief Native developer workflow engines for Phase 2:
 *        - #28 Integrated Test Explorer & Runner (Pytest, Unittest, Jest, Vitest, PHPUnit, CTest)
 *        - #29 Configurable Task Runner DAG (.vscode/tasks.json, CMakeLists.txt, package.json, composer.json, Makefile)
 *        - #30 Terminal Problem Matchers (MSVC, Clang, GCC, tsc, ESLint, PHPStan)
 */

#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace phide::dev {

// =========================================================================
// #30 Problem Matchers (MSVC, Clang, GCC, tsc, ESLint, PHPStan)
// =========================================================================

struct ProblemMatch {
    std::wstring filePath;
    int line1Based = 1;
    int col1Based = 1;
    std::wstring severity = L"error"; // "error", "warning", "info"
    std::wstring code;
    std::wstring message;
    std::wstring matcherName;         // "msvc", "gcc", "clang", "tsc", "eslint", "phpstan"
    size_t linkStart = 0;             ///< Character offset in terminal line for clickable span
    size_t linkLength = 0;            ///< Character count of clickable file:line:col span
};

struct ProblemMatcherState {
    std::wstring activeEslintFile;
    std::wstring activePhpstanFile;
};

class ProblemMatcherEngine {
public:
    /**
     * @brief Strips ANSI escape sequences and parses compiler/linter diagnostics from a single output line.
     */
    static bool ParseLine(const std::wstring& rawLine,
                          const std::wstring& workspaceRoot,
                          ProblemMatcherState& state,
                          ProblemMatch& outMatch);

    /**
     * @brief Convenience stateless overload for single-line logs.
     */
    static bool ParseLine(const std::wstring& rawLine,
                          const std::wstring& workspaceRoot,
                          ProblemMatch& outMatch);

    /**
     * @brief Strips ANSI CSI / OSC sequences cleanly.
     */
    [[nodiscard]] static std::wstring StripAnsi(const std::wstring& input);

    /**
     * @brief Resolves a relative file token against `workspaceRoot` if present.
     */
    [[nodiscard]] static std::wstring ResolvePath(const std::wstring& fileToken, const std::wstring& workspaceRoot);
};

// =========================================================================
// #29 Build & Task Runner (.vscode/tasks.json + CMake/npm/composer/Make)
// =========================================================================

struct WorkspaceTaskDef {
    std::wstring label;
    std::wstring type;            ///< "shell", "process", "npm", "composer", "cmake", "make", "script"
    std::wstring command;
    std::vector<std::wstring> args;
    std::wstring cwd;
    std::wstring group;           ///< "build", "test", or ""
    bool isDefault = false;
    std::vector<std::wstring> dependsOn;
    std::wstring problemMatcher;  ///< e.g. "$msCompile", "$gcc", "$tsc", "$eslint-stylish"
    std::wstring source;          ///< Origin file (e.g. ".vscode/tasks.json")
};

class TaskRunnerEngine {
public:
    /**
     * @brief Discovers tasks across .vscode/tasks.json, CMakeLists.txt, package.json, composer.json, Makefile, and scripts/.
     */
    [[nodiscard]] static std::vector<WorkspaceTaskDef> DiscoverAllTasks(const std::wstring& workspaceRoot);

    /**
     * @brief Parses `.vscode/tasks.json` content (handling comments and dependsOn / problemMatcher / group).
     */
    [[nodiscard]] static std::vector<WorkspaceTaskDef> ParseVscodeTasksJson(
        const std::wstring& jsonContent,
        const std::wstring& workspaceRoot);

    /**
     * @brief Topologically resolves `targetLabel` and all transitive `dependsOn` dependencies in execution order.
     */
    [[nodiscard]] static std::vector<WorkspaceTaskDef> ResolveExecutionOrder(
        const std::vector<WorkspaceTaskDef>& allTasks,
        const std::wstring& targetLabel);

    /**
     * @brief Builds a concrete shell command string substituting `${workspaceFolder}`.
     */
    [[nodiscard]] static std::wstring FormatCommandLine(
        const WorkspaceTaskDef& task,
        const std::wstring& workspaceRoot);
};

// =========================================================================
// #28 Integrated Test Explorer & Runner
// =========================================================================

enum class TestFramework {
    Unknown = 0,
    Pytest,
    Unittest,
    Jest,
    Vitest,
    PhpUnit,
    CTest
};

enum class TestStatus {
    Unknown = 0,
    Running,
    Passed,
    Failed,
    Skipped
};

struct TestItem {
    std::wstring id;
    std::wstring name;
    std::wstring suiteName;
    std::wstring filePath;
    size_t line0Based = 0;
    TestFramework framework = TestFramework::Unknown;
    TestStatus status = TestStatus::Unknown;
    double durationMs = 0.0;
    std::wstring failureMessage;
};

struct TestRunSummary {
    int total = 0;
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    int exitCode = 0;
    std::wstring rawOutput;
};

class TestExplorerEngine {
public:
    [[nodiscard]] static std::wstring FrameworkDisplayName(TestFramework fw);

    /**
     * @brief Scans workspace for Pytest/Unittest, Jest/Vitest, PHPUnit, and CTest/CMake test definitions.
     */
    [[nodiscard]] static std::vector<TestItem> DiscoverTests(const std::wstring& workspaceRoot, size_t maxFiles = 1500);

    /**
     * @brief Builds the CLI command line to run an entire suite or a single filtered test.
     */
    [[nodiscard]] static std::wstring BuildRunCommand(
        const std::wstring& workspaceRoot,
        TestFramework fw,
        const std::wstring& filterName = L"",
        const std::wstring& filePath = L"");

    /**
     * @brief Parses real runner stdout/stderr into TestItem statuses, timings, and failure summaries.
     */
    static TestRunSummary ParseTestOutput(
        TestFramework fw,
        const std::wstring& outputText,
        std::vector<TestItem>& items,
        int exitCode = 0);

    /**
     * @brief Executes a real test command in `workingDir` via CreateProcessW and captures combined stdout/stderr.
     */
    static TestRunSummary ExecuteTestsSync(
        const std::wstring& workspaceRoot,
        TestFramework fw,
        std::vector<TestItem>& items,
        const std::wstring& filterName = L"",
        const std::wstring& filePath = L"",
        int timeoutMs = 45000);
};

} // namespace phide::dev
