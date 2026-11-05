#pragma once

#include <string>
#include <vector>

namespace phide::editor {

struct GitChangeInfo {
    std::wstring path;
    std::wstring fileName;
    std::wstring status;
};

class SemanticCommitEngine {
public:
    /**
     * @brief Analyzes git changes and diff output to synthesize a conventional commit message.
     * @param changes List of changed files.
     * @param diffOutput Standard git diff output (diff -U1 / diff --cached).
     * @return Synthesized conventional commit message (e.g. "feat(Test): update JSX render return in Test component").
     */
    static std::wstring Generate(const std::vector<GitChangeInfo>& changes, const std::wstring& diffOutput);
};

} // namespace phide::editor
