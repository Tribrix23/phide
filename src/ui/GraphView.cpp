/**
 * @file GraphView.cpp
 * @brief Obsidian-styled interactive File Network Graph View implementation.
 */

#include "phide/ui/GraphView.hpp"
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <fstream>

namespace phide::ui {

GraphView::GraphView() {
    RefreshFromWorkspace(L"");
}

D2D1_COLOR_F GraphView::GetExtensionColor(const std::wstring& ext, bool isDir) {
    if (isDir) {
        return D2D1::ColorF(0.98f, 0.42f, 0.42f, 1.0f); // Crimson / Coral
    }
    if (ext == L"php") {
        return D2D1::ColorF(0.68f, 0.58f, 0.98f, 1.0f); // Soft Purple / Violet
    } else if (ext == L"js" || ext == L"ts" || ext == L"jsx" || ext == L"tsx") {
        return D2D1::ColorF(0.98f, 0.82f, 0.16f, 1.0f); // Vibrant Yellow / Gold
    } else if (ext == L"css" || ext == L"scss" || ext == L"less") {
        return D2D1::ColorF(0.25f, 0.75f, 0.98f, 1.0f); // Sky Blue
    } else if (ext == L"html" || ext == L"htm") {
        return D2D1::ColorF(0.98f, 0.56f, 0.22f, 1.0f); // Coral / Orange
    } else if (ext == L"sql" || ext == L"db") {
        return D2D1::ColorF(0.20f, 0.84f, 0.62f, 1.0f); // Emerald / Mint
    } else if (ext == L"json" || ext == L"yaml" || ext == L"yml" || ext == L"xml") {
        return D2D1::ColorF(0.22f, 0.84f, 0.78f, 1.0f); // Cyan / Teal
    } else if (ext == L"md" || ext == L"txt" || ext == L"log") {
        return D2D1::ColorF(0.96f, 0.45f, 0.72f, 1.0f); // Pink / Rose
    } else if (ext == L"py") {
        return D2D1::ColorF(0.98f, 0.76f, 0.18f, 1.0f); // Warm Amber
    } else if (ext == L"cpp" || ext == L"hpp" || ext == L"c" || ext == L"h") {
        return D2D1::ColorF(0.38f, 0.65f, 0.98f, 1.0f); // Cobalt Blue
    }
    return D2D1::ColorF(0.70f, 0.75f, 0.82f, 1.0f); // Lavender Slate
}

std::vector<std::string> GraphView::ExtractReferencedTargets(const std::string& content, const std::wstring& ext) {
    std::vector<std::string> refs;

    auto addCleanRef = [&refs](std::string raw) {
        // Strip query parameters (?...) and anchor fragments (#...)
        auto qPos = raw.find_first_of("?#");
        if (qPos != std::string::npos) raw.erase(qPos);

        // Normalize backslashes to forward slashes
        for (char& c : raw) {
            if (c == '\\') c = '/';
        }

        // Trim leading and trailing whitespace, quotes, and punctuation
        while (!raw.empty() && (isspace(static_cast<unsigned char>(raw.front())) || raw.front() == '\'' || raw.front() == '"')) {
            raw.erase(raw.begin());
        }
        while (!raw.empty() && (isspace(static_cast<unsigned char>(raw.back())) || raw.back() == '\'' || raw.back() == '"' || raw.back() == ';')) {
            raw.pop_back();
        }

        // Trim leading ./ or /
        if (raw.rfind("./", 0) == 0) raw.erase(0, 2);
        else if (raw.rfind("/", 0) == 0) raw.erase(0, 1);

        // Lowercase
        for (char& c : raw) {
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }

        if (raw.length() >= 2) {
            refs.push_back(std::move(raw));
        }
    };

    // 1. Markdown (.md, .markdown) and Plain Text (.txt, .log):
    // ONLY explicit Obsidian wiki-links [[target]] or Markdown links [text](target)!
    // Casual English prose mentions (e.g. "Check config.php credentials") are strictly IGNORED!
    if (ext == L"md" || ext == L"markdown" || ext == L"txt" || ext == L"log") {
        // A. Obsidian Wiki-links: [[target]], [[target|alias]], [[target#heading]]
        size_t pos = 0;
        while ((pos = content.find("[[", pos)) != std::string::npos) {
            size_t endPos = content.find("]]", pos + 2);
            if (endPos == std::string::npos) break;

            std::string link = content.substr(pos + 2, endPos - (pos + 2));
            auto pipePos = link.find_first_of("|#");
            if (pipePos != std::string::npos) link.erase(pipePos);
            addCleanRef(link);
            pos = endPos + 2;
        }

        // B. Markdown links: [title](target) or ![img](target)
        pos = 0;
        while ((pos = content.find("](", pos)) != std::string::npos) {
            size_t endPos = content.find(")", pos + 2);
            if (endPos == std::string::npos) break;

            std::string link = content.substr(pos + 2, endPos - (pos + 2));
            // Filter out external URLs and mailto
            if (link.rfind("http://", 0) != 0 && link.rfind("https://", 0) != 0 && link.rfind("mailto:", 0) != 0) {
                addCleanRef(link);
            }
            pos = endPos + 1;
        }

        return refs;
    }

    // 2. PHP files (.php, .phtml, .inc):
    if (ext == L"php" || ext == L"phtml" || ext == L"inc") {
        // A. require, require_once, include, include_once
        // Matches e.g. require 'config.php'; or require __DIR__ . '/config.php';
        const char* const incKws[] = { "require_once", "include_once", "require", "include" };
        for (const char* kw : incKws) {
            size_t kwLen = strlen(kw);
            size_t pos = 0;
            while ((pos = content.find(kw, pos)) != std::string::npos) {
                bool validBefore = (pos == 0 || (!isalnum(static_cast<unsigned char>(content[pos - 1])) && content[pos - 1] != '_'));
                bool validAfter = (pos + kwLen < content.length() && (isspace(static_cast<unsigned char>(content[pos + kwLen])) || content[pos + kwLen] == '('));

                if (validBefore && validAfter) {
                    size_t stmtEnd = content.find_first_of(";\n\r?>", pos + kwLen);
                    if (stmtEnd == std::string::npos) stmtEnd = content.length();

                    // Find quoted strings inside statement: '...' or "..."
                    for (size_t k = pos + kwLen; k < stmtEnd; ++k) {
                        char q = content[k];
                        if (q == '\'' || q == '"') {
                            size_t qEnd = content.find(q, k + 1);
                            if (qEnd != std::string::npos && qEnd <= stmtEnd) {
                                std::string lit = content.substr(k + 1, qEnd - (k + 1));
                                addCleanRef(lit);
                                k = qEnd;
                            }
                        }
                    }
                }
                pos += kwLen;
            }
        }

        // B. HTML / View attributes in PHP: href="...", src="...", action="..."
        const char* const attrs[] = { "href=", "src=", "action=" };
        for (const char* attr : attrs) {
            size_t attrLen = strlen(attr);
            size_t pos = 0;
            while ((pos = content.find(attr, pos)) != std::string::npos) {
                size_t valStart = pos + attrLen;
                if (valStart < content.length() && (content[valStart] == '\'' || content[valStart] == '"')) {
                    char q = content[valStart];
                    size_t valEnd = content.find(q, valStart + 1);
                    if (valEnd != std::string::npos) {
                        std::string val = content.substr(valStart + 1, valEnd - (valStart + 1));
                        if (val.rfind("http://", 0) != 0 && val.rfind("https://", 0) != 0 && val.rfind("#", 0) != 0 && val.rfind("javascript:", 0) != 0) {
                            addCleanRef(val);
                        }
                    }
                }
                pos += attrLen;
            }
        }

        // C. HTTP Navigation / Redirects: header('Location: index.php')
        size_t locPos = 0;
        while ((locPos = content.find("Location:", locPos)) != std::string::npos) {
            size_t targetStart = locPos + 9;
            while (targetStart < content.length() && isspace(static_cast<unsigned char>(content[targetStart]))) {
                targetStart++;
            }
            size_t targetEnd = content.find_first_of("'\";\r\n", targetStart);
            if (targetEnd != std::string::npos && targetEnd > targetStart) {
                std::string redir = content.substr(targetStart, targetEnd - targetStart);
                if (redir.rfind("http://", 0) != 0 && redir.rfind("https://", 0) != 0) {
                    addCleanRef(redir);
                }
            }
            locPos += 9;
        }

        return refs;
    }

    // 3. JavaScript / TypeScript (.js, .jsx, .ts, .tsx, .mjs, .cjs):
    if (ext == L"js" || ext == L"jsx" || ext == L"ts" || ext == L"tsx" || ext == L"mjs" || ext == L"cjs") {
        // A. import ... from '...'
        size_t pos = 0;
        while ((pos = content.find("import", pos)) != std::string::npos) {
            bool validBefore = (pos == 0 || !isalnum(static_cast<unsigned char>(content[pos - 1])));
            bool validAfter = (pos + 6 < content.length() && (isspace(static_cast<unsigned char>(content[pos + 6])) || content[pos + 6] == '('));
            if (validBefore && validAfter) {
                size_t stmtEnd = content.find_first_of(";\n\r", pos + 6);
                if (stmtEnd == std::string::npos) stmtEnd = content.length();

                size_t lastQ1 = content.find_last_of("'", stmtEnd);
                size_t lastQ2 = content.find_last_of("\"", stmtEnd);
                size_t qEnd = (lastQ1 != std::string::npos && lastQ1 > pos) ? lastQ1 : (lastQ2 != std::string::npos && lastQ2 > pos ? lastQ2 : std::string::npos);
                if (qEnd != std::string::npos) {
                    char q = content[qEnd];
                    size_t qStart = content.rfind(q, qEnd - 1);
                    if (qStart != std::string::npos && qStart >= pos) {
                        addCleanRef(content.substr(qStart + 1, qEnd - (qStart + 1)));
                    }
                }
            }
            pos += 6;
        }

        // B. require('...')
        pos = 0;
        while ((pos = content.find("require", pos)) != std::string::npos) {
            bool validBefore = (pos == 0 || !isalnum(static_cast<unsigned char>(content[pos - 1])));
            if (validBefore && pos + 7 < content.length()) {
                size_t pOpen = content.find('(', pos + 7);
                if (pOpen != std::string::npos && pOpen <= pos + 9) {
                    size_t pClose = content.find(')', pOpen + 1);
                    if (pClose != std::string::npos) {
                        for (size_t k = pOpen + 1; k < pClose; ++k) {
                            char q = content[k];
                            if (q == '\'' || q == '"') {
                                size_t qEnd = content.find(q, k + 1);
                                if (qEnd != std::string::npos && qEnd <= pClose) {
                                    addCleanRef(content.substr(k + 1, qEnd - (k + 1)));
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            pos += 7;
        }

        return refs;
    }

    // 4. HTML files (.html, .htm):
    if (ext == L"html" || ext == L"htm") {
        const char* const attrs[] = { "href=", "src=", "action=" };
        for (const char* attr : attrs) {
            size_t attrLen = strlen(attr);
            size_t pos = 0;
            while ((pos = content.find(attr, pos)) != std::string::npos) {
                size_t valStart = pos + attrLen;
                if (valStart < content.length() && (content[valStart] == '\'' || content[valStart] == '"')) {
                    char q = content[valStart];
                    size_t valEnd = content.find(q, valStart + 1);
                    if (valEnd != std::string::npos) {
                        std::string val = content.substr(valStart + 1, valEnd - (valStart + 1));
                        if (val.rfind("http://", 0) != 0 && val.rfind("https://", 0) != 0 && val.rfind("#", 0) != 0 && val.rfind("javascript:", 0) != 0) {
                            addCleanRef(val);
                        }
                    }
                }
                pos += attrLen;
            }
        }
        return refs;
    }

    // 5. CSS files (.css, .scss, .less):
    if (ext == L"css" || ext == L"scss" || ext == L"less") {
        size_t pos = 0;
        while ((pos = content.find("@import", pos)) != std::string::npos) {
            size_t stmtEnd = content.find_first_of(";\n\r", pos + 7);
            if (stmtEnd == std::string::npos) stmtEnd = content.length();
            for (size_t k = pos + 7; k < stmtEnd; ++k) {
                char q = content[k];
                if (q == '\'' || q == '"') {
                    size_t qEnd = content.find(q, k + 1);
                    if (qEnd != std::string::npos && qEnd <= stmtEnd) {
                        addCleanRef(content.substr(k + 1, qEnd - (k + 1)));
                        break;
                    }
                }
            }
            pos += 7;
        }
        return refs;
    }

    // 6. C/C++ files (.c, .cpp, .cc, .cxx, .h, .hpp, .hxx):
    if (ext == L"c" || ext == L"cpp" || ext == L"cc" || ext == L"cxx" || ext == L"h" || ext == L"hpp" || ext == L"hxx") {
        size_t pos = 0;
        while ((pos = content.find("#include", pos)) != std::string::npos) {
            size_t rest = pos + 8;
            while (rest < content.length() && isspace(static_cast<unsigned char>(content[rest]))) {
                rest++;
            }
            if (rest < content.length() && (content[rest] == '"' || content[rest] == '<')) {
                char closer = (content[rest] == '"') ? '"' : '>';
                size_t closePos = content.find(closer, rest + 1);
                if (closePos != std::string::npos) {
                    addCleanRef(content.substr(rest + 1, closePos - (rest + 1)));
                }
            }
            pos += 8;
        }
        return refs;
    }

    // 7. Python files (.py):
    if (ext == L"py") {
        size_t pos = 0;
        while (pos < content.length()) {
            size_t lineEnd = content.find_first_of("\r\n", pos);
            if (lineEnd == std::string::npos) lineEnd = content.length();
            std::string line = content.substr(pos, lineEnd - pos);
            size_t startIdx = 0;
            while (startIdx < line.length() && isspace(static_cast<unsigned char>(line[startIdx]))) startIdx++;
            if (line.rfind("from ", startIdx) == startIdx) {
                size_t modStart = startIdx + 5;
                while (modStart < line.length() && (line[modStart] == '.' || isspace(static_cast<unsigned char>(line[modStart])))) modStart++;
                size_t modEnd = line.find_first_of(" \t", modStart);
                if (modEnd != std::string::npos) {
                    std::string mod = line.substr(modStart, modEnd - modStart);
                    if (!mod.empty()) addCleanRef(mod + ".py");
                }
            } else if (line.rfind("import ", startIdx) == startIdx) {
                size_t modStart = startIdx + 7;
                while (modStart < line.length() && isspace(static_cast<unsigned char>(line[modStart]))) modStart++;
                size_t modEnd = line.find_first_of(" \t,", modStart);
                if (modEnd == std::string::npos) modEnd = line.length();
                std::string mod = line.substr(modStart, modEnd - modStart);
                if (!mod.empty()) addCleanRef(mod + ".py");
            }
            pos = lineEnd + 1;
        }
        return refs;
    }

    return refs;
}

bool GraphView::MatchesCandidate(const std::string& ref, const std::wstring& candLabel, const std::wstring& candRelPath) {
    if (ref.empty()) return false;

    // Convert candidate label and relative path to lowercase std::string with '/'
    std::string labelA;
    labelA.reserve(candLabel.length());
    for (wchar_t wc : candLabel) {
        labelA.push_back(static_cast<char>(towlower(wc)));
    }

    std::string relA;
    relA.reserve(candRelPath.length());
    for (wchar_t wc : candRelPath) {
        wchar_t low = towlower(wc);
        if (low == L'\\') low = L'/';
        relA.push_back(static_cast<char>(low));
    }
    while (relA.rfind("./", 0) == 0) relA.erase(0, 2);
    while (relA.rfind("/", 0) == 0) relA.erase(0, 1);

    // Basename of extracted ref
    std::string refBase = ref;
    auto lastSlash = refBase.find_last_of('/');
    if (lastSlash != std::string::npos) {
        refBase = refBase.substr(lastSlash + 1);
    }

    // 1. Exact relative path match (e.g. "assets/style.css" == "assets/style.css")
    if (ref == relA) return true;

    // 2. Exact filename match (e.g. "config.php" == "config.php", "style.css" == "style.css")
    if (refBase == labelA) return true;

    // 3. Stem match (for JS imports without extension e.g. import './app' or wiki-links [[config]])
    auto dotCand = labelA.find_last_of('.');
    std::string candStem = (dotCand != std::string::npos) ? labelA.substr(0, dotCand) : labelA;
    auto dotRef = refBase.find_last_of('.');
    std::string refStem = (dotRef != std::string::npos) ? refBase.substr(0, dotRef) : refBase;

    if (refStem.length() >= 2 && refStem == candStem) {
        return true;
    }

    return false;
}

void GraphView::ParseFileDependencies() {
    m_edges.clear();
    for (auto& n : m_nodes) {
        n.neighbors.clear();
    }
    if (m_nodes.empty()) return;

    auto addEdge = [this](size_t a, size_t b) {
        if (a >= m_nodes.size() || b >= m_nodes.size() || a == b) return;
        for (const auto& e : m_edges) {
            if ((e.from == a && e.to == b) || (e.from == b && e.to == a)) return;
        }
        m_edges.push_back({ a, b, 1.0f });
        m_nodes[a].neighbors.push_back(b);
        m_nodes[b].neighbors.push_back(a);
    };

    // If files exist on disk, read contents and parse actual references
    std::vector<std::string> fileContents(m_nodes.size());
    std::vector<std::wstring> relativePaths(m_nodes.size());

    std::filesystem::path rootP = m_currentRootPath;
    std::error_code ec;

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const auto& node = m_nodes[i];
        if (node.isDirectory) continue;

        std::filesystem::path p = node.id;
        if (std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec)) {
            // Compute relative path from workspace root
            try {
                if (!rootP.empty() && std::filesystem::exists(rootP, ec)) {
                    relativePaths[i] = std::filesystem::relative(p, rootP, ec).wstring();
                }
            } catch (...) {}
            if (relativePaths[i].empty()) {
                relativePaths[i] = node.label;
            }

            // Read up to 512 KB of file content
            auto fSize = std::filesystem::file_size(p, ec);
            if (!ec && fSize > 0 && fSize <= 512 * 1024) {
                FILE* fp = nullptr;
                if (_wfopen_s(&fp, p.c_str(), L"rb") == 0 && fp) {
                    std::string buf(static_cast<size_t>(fSize), '\0');
                    size_t readBytes = fread(buf.data(), 1, static_cast<size_t>(fSize), fp);
                    fclose(fp);
                    buf.resize(readBytes);
                    fileContents[i] = std::move(buf);
                }
            }
        }
    }

    bool hasDiskContents = false;
    if (!m_currentRootPath.empty()) {
        for (const auto& s : fileContents) {
            if (!s.empty()) { hasDiskContents = true; break; }
        }
    }

    if (hasDiskContents) {
        // Precision syntax-aware dependency extraction
        for (size_t i = 0; i < m_nodes.size(); ++i) {
            if (fileContents[i].empty()) continue;
            auto targets = ExtractReferencedTargets(fileContents[i], m_nodes[i].ext);
            for (const auto& target : targets) {
                for (size_t j = 0; j < m_nodes.size(); ++j) {
                    if (i == j) continue;
                    if (MatchesCandidate(target, m_nodes[j].label, relativePaths[j])) {
                        addEdge(i, j);
                    }
                }
            }
        }
    } else {
        // Fallback sample set: connect only logical code pairs; README, vuln, database, license remain strictly orphan nodes
        auto findNodeByLabel = [this](const std::wstring& name) -> int {
            for (size_t idx = 0; idx < m_nodes.size(); ++idx) {
                if (m_nodes[idx].label == name) return static_cast<int>(idx);
            }
            return -1;
        };

        const std::pair<const wchar_t*, const wchar_t*> sampleLinks[] = {
            { L"index.php", L"config.php" },
            { L"index.php", L"action.php" },
            { L"index.php", L"styles.css" },
            { L"index.php", L"app.js" },
            { L"action.php", L"config.php" },
            { L"routes.php", L"index.php" },
            { L"db_connect.php", L"config.php" },
            { L"student.php", L"config.php" },
            { L"helper.php", L"index.php" }
        };

        for (const auto& sl : sampleLinks) {
            int a = findNodeByLabel(sl.first);
            int b = findNodeByLabel(sl.second);
            if (a >= 0 && b >= 0) {
                addEdge(static_cast<size_t>(a), static_cast<size_t>(b));
            }
        }
    }
}

void GraphView::RefreshFromWorkspace(const std::wstring& rootPath, bool preserveExistingPositions) {
    if (!rootPath.empty()) {
        m_currentRootPath = rootPath;
    }
    m_hoveredNode = -1;
    m_draggedNode = -1;

    std::filesystem::path targetDir = m_currentRootPath;
    std::error_code ec;

    // 1. Scan filesystem for project files (up to 75 files for a balanced, responsive graph)
    const size_t maxFiles = 75;
    std::vector<std::filesystem::path> collected;

    if (!m_currentRootPath.empty() && std::filesystem::exists(targetDir, ec) && std::filesystem::is_directory(targetDir, ec)) {
        try {
            for (auto const& entry : std::filesystem::recursive_directory_iterator(targetDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (collected.size() >= maxFiles) break;
                if (ec) break;

                const auto& path = entry.path();
                std::wstring uPath = path.wstring();
                // Filter out version control, binary build dirs and package caches
                if (uPath.find(L"\\.git") != std::wstring::npos ||
                    uPath.find(L"\\node_modules") != std::wstring::npos ||
                    uPath.find(L"\\vendor") != std::wstring::npos ||
                    uPath.find(L"\\.vs") != std::wstring::npos ||
                    uPath.find(L"\\bin") != std::wstring::npos ||
                    uPath.find(L"\\obj") != std::wstring::npos) {
                    continue;
                }

                if (entry.is_regular_file(ec)) {
                    collected.push_back(path);
                }
            }
        } catch (...) {
            // Fallback gracefully
        }
    }

    // If preserveExistingPositions is requested (e.g. from file creation, update, rename, delete, move, or save)
    if (preserveExistingPositions && !m_nodes.empty() && !collected.empty()) {
        std::vector<GraphNode> updatedNodes;
        bool hasNewNodes = false;

        for (const auto& p : collected) {
            std::wstring pStr = p.wstring();
            std::wstring fName = p.filename().wstring();

            // Match against existing node
            auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [&](const GraphNode& gn) {
                return gn.id == pStr || gn.label == fName;
            });

            if (it != m_nodes.end()) {
                GraphNode existing = *it;
                existing.id = pStr;
                existing.label = fName;
                updatedNodes.push_back(existing);
            } else {
                // Brand new file created dynamically (e.g. hello.txt)
                GraphNode n;
                n.id = pStr;
                n.label = fName;
                std::wstring ext = p.extension().wstring();
                if (!ext.empty() && ext.front() == L'.') ext.erase(0, 1);
                for (auto& c : ext) c = towlower(c);
                n.ext = ext;
                n.isDirectory = false;
                n.radius = 5.5f;
                n.color = GetExtensionColor(n.ext, false);

                // Place near center with gentle initial velocity for organic entrance
                float offset = static_cast<float>((updatedNodes.size() * 41) % 70) - 35.0f;
                n.x = offset;
                n.y = offset * 0.75f;
                n.vx = (offset >= 0 ? 2.2f : -2.2f);
                n.vy = 2.0f;

                updatedNodes.push_back(n);
                hasNewNodes = true;
            }
        }

        m_nodes = std::move(updatedNodes);
        ParseFileDependencies();
        LoadLayout();

        if (hasNewNodes) {
            StartBounceAnimation();
        }
        return;
    }

    // Full fresh initial scan
    m_nodes.clear();
    m_edges.clear();

    // If directory scan produced very few or no files, create standard representative project network
    if (collected.size() < 4) {
        struct SampleDef {
            const wchar_t* name;
            const wchar_t* ext;
            bool isDir;
            float rad;
        };
        const SampleDef samples[] = {
            { L"index.php",       L"php",  false, 7.5f },
            { L"config.php",      L"php",  false, 7.0f },
            { L"action.php",      L"php",  false, 6.0f },
            { L"database.sql",    L"sql",  false, 6.5f },
            { L"README.md",       L"md",   false, 5.5f },
            { L"styles.css",      L"css",  false, 5.5f },
            { L"app.js",          L"js",   false, 6.0f },
            { L"composer.json",   L"json", false, 5.5f },
            { L"assets",          L"",     true,  7.0f },
            { L"logo.png",        L"png",  false, 4.5f },
            { L"auth.php",        L"php",  false, 6.0f },
            { L"student.php",     L"php",  false, 6.0f },
            { L"api.js",          L"js",   false, 5.5f },
            { L"test.txt",        L"txt",  false, 4.5f },
            { L".gitignore",      L"git",  false, 4.5f },
            { L"Notebook.ipynb",  L"json", false, 5.5f },
            { L"routes.php",      L"php",  false, 6.0f },
            { L"helper.php",      L"php",  false, 5.0f },
            { L"main.css",        L"css",  false, 5.0f },
            { L"db_connect.php",  L"php",  false, 6.0f },
            { L"schema.sql",      L"sql",  false, 5.5f },
            { L"install.sh",      L"sh",   false, 4.5f },
            { L"vuln.txt",        L"txt",  false, 4.5f },
            { L"license.txt",     L"txt",  false, 4.5f }
        };

        for (const auto& s : samples) {
            GraphNode n;
            n.id = s.name;
            n.label = s.name;
            n.ext = s.ext;
            n.isDirectory = s.isDir;
            n.radius = s.rad;
            n.color = GetExtensionColor(s.ext, s.isDir);
            m_nodes.push_back(n);
        }
    } else {
        for (const auto& p : collected) {
            GraphNode n;
            n.id = p.wstring();
            n.label = p.filename().wstring();
            std::wstring ext = p.extension().wstring();
            if (!ext.empty() && ext.front() == L'.') ext.erase(0, 1);
            for (auto& c : ext) c = towlower(c);
            n.ext = ext;
            n.isDirectory = false;

            // Main files get larger radius
            std::wstring lowerLabel = n.label;
            for (auto& c : lowerLabel) c = towlower(c);
            if (lowerLabel.find(L"index") != std::wstring::npos ||
                lowerLabel.find(L"config") != std::wstring::npos ||
                lowerLabel.find(L"main") != std::wstring::npos ||
                lowerLabel.find(L"app") != std::wstring::npos) {
                n.radius = 7.5f;
            } else if (ext == L"php" || ext == L"js" || ext == L"sql") {
                n.radius = 6.0f;
            } else {
                n.radius = 5.0f;
            }
            n.color = GetExtensionColor(n.ext, false);
            m_nodes.push_back(n);
        }
    }

    if (m_nodes.empty()) return;

    // 2. Initial compact cluster layout with gentle outward burst (Obsidian intro)
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        float r = 12.0f + 25.0f * (static_cast<float>(i) / static_cast<float>((std::max)(size_t(1), m_nodes.size())));
        float theta = static_cast<float>(i) * 2.39996f; // Golden ratio spiral
        m_nodes[i].x = r * std::cos(theta);
        m_nodes[i].y = r * std::sin(theta);
        m_nodes[i].vx = std::cos(theta) * 2.5f;
        m_nodes[i].vy = std::sin(theta) * 2.5f;
    }

    // 3. Connect nodes based strictly on real references
    ParseFileDependencies();

    // 4. Restore saved layout positions if available
    bool hasSavedLayout = LoadLayout();

    if (!hasSavedLayout && !preserveExistingPositions) {
        StartIntroAnimation();
    } else {
        StartBounceAnimation();
    }
}

void GraphView::StepPhysics(float alpha) {
    if (m_nodes.size() < 2) return;

    const float kRepulse   = 2200.0f * (std::max)(0.25f, alpha);
    const float kSpring    = 0.052f * (std::max)(0.25f, alpha);
    const float springLen  = 72.0f;
    const float centerPull = 0.006f * (std::max)(0.15f, alpha);
    const float damping    = 0.86f; // Elastic, springy bouncy motion like Obsidian!

    // A. Repulsion between all node pairs
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        for (size_t j = i + 1; j < m_nodes.size(); ++j) {
            float dx = m_nodes[j].x - m_nodes[i].x;
            float dy = m_nodes[j].y - m_nodes[i].y;
            float distSq = dx * dx + dy * dy + 80.0f;
            float dist = std::sqrt(distSq);
            float force = kRepulse / distSq;
            float fx = (dx / dist) * force;
            float fy = (dy / dist) * force;

            m_nodes[i].vx -= fx;
            m_nodes[i].vy -= fy;
            m_nodes[j].vx += fx;
            m_nodes[j].vy += fy;
        }
    }

    // B. Spring attraction along edges
    for (const auto& e : m_edges) {
        float dx = m_nodes[e.to].x - m_nodes[e.from].x;
        float dy = m_nodes[e.to].y - m_nodes[e.from].y;
        float dist = std::sqrt(dx * dx + dy * dy + 1.0f);
        float disp = dist - springLen;
        float force = kSpring * disp;
        float fx = (dx / dist) * force;
        float fy = (dy / dist) * force;

        m_nodes[e.from].vx += fx;
        m_nodes[e.from].vy += fy;
        m_nodes[e.to].vx   -= fx;
        m_nodes[e.to].vy   -= fy;
    }

    // C. Center gravity & update positions (skip currently dragged node and pinned nodes)
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (static_cast<int>(i) == m_draggedNode) continue;
        if (m_nodes[i].isPinned) {
            m_nodes[i].vx = 0.0f;
            m_nodes[i].vy = 0.0f;
            continue;
        }

        m_nodes[i].vx -= m_nodes[i].x * centerPull;
        m_nodes[i].vy -= m_nodes[i].y * centerPull;

        m_nodes[i].vx *= damping;
        m_nodes[i].vy *= damping;

        m_nodes[i].x += m_nodes[i].vx;
        m_nodes[i].y += m_nodes[i].vy;
    }
}

void GraphView::StartIntroAnimation() {
    m_animAlpha = 1.0f;
    m_animFrames = 110;
    m_isAnimating = true;
}

void GraphView::StartBounceAnimation() {
    m_animAlpha = (std::max)(m_animAlpha, 0.45f);
    m_animFrames = (std::max)(m_animFrames, 40);
    m_isAnimating = true;
}

bool GraphView::UpdateAnimation() {
    if (!m_isAnimating) return false;

    StepPhysics(m_animAlpha);
    m_animAlpha *= 0.970f;
    m_animFrames--;

    if (m_animFrames <= 0 || m_animAlpha < 0.015f) {
        m_animAlpha = 0.0f;
        m_animFrames = 0;
        m_isAnimating = false;
        return false;
    }
    return true;
}

void GraphView::ResetView() {
    m_panX = 0.0f;
    m_panY = 0.0f;
    m_zoom = 1.0f;
    m_hoveredNode = -1;
    m_draggedNode = -1;
}

void GraphView::Render(ID2D1RenderTarget* pRT, Theme& theme, const app::AppConfig& config,
                       float left, float width, float top, float height) {
    if (!pRT || width < 10.0f || height < 10.0f) return;

    // Advance organic Obsidian bouncy intro animation if active
    if (m_isAnimating) {
        UpdateAnimation();
    }

    m_canvasBounds = D2D1::RectF(left, top, left + width, top + height);

    // 1. Canvas Deep Background (Obsidian dark background)
    D2D1_COLOR_F bgColor = theme.IsDarkMode() ? D2D1::ColorF(0.08f, 0.08f, 0.09f, 1.0f)
                                              : D2D1::ColorF(0.97f, 0.98f, 0.99f, 1.0f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    pRT->CreateSolidColorBrush(bgColor, &bgBrush);
    if (bgBrush) {
        pRT->FillRectangle(m_canvasBounds, bgBrush.Get());
    }

    pRT->PushAxisAlignedClip(m_canvasBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const float cx = left + width * 0.5f + m_panX;
    const float cy = top + height * 0.5f + m_panY;

    auto worldToScreen = [cx, cy, this](float wx, float wy) -> D2D1_POINT_2F {
        return D2D1::Point2F(cx + wx * m_zoom, cy + wy * m_zoom);
    };

    // 2. Draw Edges (Translucent connecting lines)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> edgeBrushNormal;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> edgeBrushHighlight;
    D2D1_COLOR_F normalEdgeColor = theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f)
                                                      : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.10f);
    D2D1_COLOR_F highlightEdgeColor = theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.80f)
                                                         : D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.75f);
    pRT->CreateSolidColorBrush(normalEdgeColor, &edgeBrushNormal);
    pRT->CreateSolidColorBrush(highlightEdgeColor, &edgeBrushHighlight);

    const float baseEdgeWidth = (std::max)(0.6f, 0.9f * m_zoom);

    for (const auto& e : m_edges) {
        if (e.from >= m_nodes.size() || e.to >= m_nodes.size()) continue;
        D2D1_POINT_2F p1 = worldToScreen(m_nodes[e.from].x, m_nodes[e.from].y);
        D2D1_POINT_2F p2 = worldToScreen(m_nodes[e.to].x, m_nodes[e.to].y);

        bool isConnectedToHover = (m_hoveredNode >= 0 && (static_cast<int>(e.from) == m_hoveredNode || static_cast<int>(e.to) == m_hoveredNode));

        if (isConnectedToHover) {
            pRT->DrawLine(p1, p2, edgeBrushHighlight ? edgeBrushHighlight.Get() : edgeBrushNormal.Get(), baseEdgeWidth * 1.6f);
        } else {
            pRT->DrawLine(p1, p2, edgeBrushNormal.Get(), baseEdgeWidth);
        }
    }

    // 3. Draw Nodes (Circular points with colored or monochrome theme)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> nodeBrush;
    pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &nodeBrush);

    const bool isColored = config.isGraphColored;
    const D2D1_COLOR_F monoColor = theme.IsDarkMode() ? D2D1::ColorF(0.94f, 0.95f, 0.98f, 1.0f)
                                                      : D2D1::ColorF(0.20f, 0.22f, 0.26f, 1.0f);

    IDWriteTextFormat* pFont = theme.fontHint ? theme.fontHint.Get() : theme.fontBody.Get();

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const auto& node = m_nodes[i];
        D2D1_POINT_2F sp = worldToScreen(node.x, node.y);

        // Viewport culling
        if (sp.x < left - 60.0f || sp.x > left + width + 60.0f ||
            sp.y < top - 60.0f || sp.y > top + height + 60.0f) {
            continue;
        }

        const float r = (std::clamp)(node.radius * m_zoom, 2.8f, 26.0f);
        const bool isHovered = (static_cast<int>(i) == m_hoveredNode);
        const bool isDragged = (static_cast<int>(i) == m_draggedNode);

        // Determine node color (Colored mode vs Monochrome mode)
        D2D1_COLOR_F c = isColored ? node.color : monoColor;

        // Dim unconnected nodes when another node is hovered
        if (m_hoveredNode >= 0 && !isHovered) {
            bool isNeighbor = false;
            for (size_t nIdx : m_nodes[m_hoveredNode].neighbors) {
                if (nIdx == i) { isNeighbor = true; break; }
            }
            if (!isNeighbor) {
                c.a = 0.28f;
            }
        }

        // Draw halo ring on hover or drag
        if ((isHovered || isDragged) && nodeBrush) {
            D2D1_COLOR_F haloColor = c;
            haloColor.a = 0.35f;
            nodeBrush->SetColor(haloColor);
            pRT->FillEllipse(D2D1::Ellipse(sp, r + 4.5f * m_zoom, r + 4.5f * m_zoom), nodeBrush.Get());
            haloColor.a = 0.90f;
            nodeBrush->SetColor(haloColor);
            pRT->DrawEllipse(D2D1::Ellipse(sp, r + 2.0f * m_zoom, r + 2.0f * m_zoom), nodeBrush.Get(), 1.2f);
        }

        if (nodeBrush) {
            nodeBrush->SetColor(c);
            pRT->FillEllipse(D2D1::Ellipse(sp, r, r), nodeBrush.Get());
        }

        // 4. Node Labels ("the namiong shouuldnt have a div color it transparent and it should be under the note itself")
        const bool showLabel = (m_zoom >= 0.85f) || isHovered || isDragged;
        if (showLabel && pFont && theme.dwriteFactory) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> pLayout;
            theme.dwriteFactory->CreateTextLayout(
                node.label.c_str(),
                static_cast<UINT32>(node.label.length()),
                pFont,
                240.0f,
                30.0f,
                &pLayout
            );

            if (pLayout) {
                pLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                pLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

                // Centered horizontally directly beneath the node (NO horizontal offset/gap)
                float labelX = sp.x - 120.0f;
                // Positioned right under the node circle
                float labelY = sp.y + r + 3.0f;

                ID2D1Brush* textBrush = isHovered ? (theme.brushTextWhite ? theme.brushTextWhite.Get() : theme.brushTextPrimary.Get())
                                                  : (theme.brushTextSecondary ? theme.brushTextSecondary.Get() : theme.brushTextMuted.Get());

                // Pure transparent text - NO div / pill background box
                pRT->DrawTextLayout(D2D1::Point2F(labelX, labelY), pLayout.Get(), textBrush);
            }
        }
    }

    // Floating HUD badge moved from canvas to status bar per user requirement:
    // "also can you move this infor inside here" (pointing to bottom status bar)

    pRT->PopAxisAlignedClip();
}

std::wstring GraphView::GetStatusString(const app::AppConfig& config) const {
    int zoomPct = static_cast<int>(m_zoom * 100.0f + 0.5f);
    return L"Graph view • " + std::to_wstring(zoomPct) + L"% • " +
           std::to_wstring(m_nodes.size()) + L" files • " +
           (config.isGraphColored ? L"Colored" : L"Monochrome");
}

std::wstring GraphView::GetLayoutFilePath() const {
    if (!m_currentRootPath.empty()) {
        std::filesystem::path root(m_currentRootPath);
        std::filesystem::path phideDir = root / L".phide";
        return (phideDir / L"graph_layout.json").wstring();
    }
    wchar_t* pAppData = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&pAppData, &len, L"APPDATA") == 0 && pAppData) {
        std::filesystem::path dir = std::filesystem::path(pAppData) / L"phide";
        free(pAppData);
        return (dir / L"graph_layout_default.json").wstring();
    }
    return L"graph_layout.json";
}

bool GraphView::SaveLayout() {
    std::wstring path = GetLayoutFilePath();
    if (path.empty()) return false;

    std::filesystem::path p(path);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);

    std::wofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << L"{\n";
    out << L"  \"zoom\": " << m_zoom << L",\n";
    out << L"  \"panX\": " << m_panX << L",\n";
    out << L"  \"panY\": " << m_panY << L",\n";
    out << L"  \"nodes\": [\n";

    size_t count = 0;
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        const auto& n = m_nodes[i];
        out << L"    { \"id\": \"" << n.label << L"\", \"x\": " << n.x << L", \"y\": " << n.y
            << L", \"pinned\": " << (n.isPinned ? L"true" : L"false") << L" }";
        if (++count < m_nodes.size()) out << L",";
        out << L"\n";
    }

    out << L"  ]\n";
    out << L"}\n";
    return true;
}

bool GraphView::LoadLayout() {
    std::wstring path = GetLayoutFilePath();
    if (path.empty()) return false;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;

    std::wifstream in(path);
    if (!in.is_open()) return false;

    std::wstring line;
    bool foundAny = false;

    while (std::getline(in, line)) {
        size_t first = line.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos) continue;
        line = line.substr(first);

        if (line.find(L"\"zoom\"") != std::wstring::npos) {
            auto colon = line.find(L':');
            if (colon != std::wstring::npos) {
                float z = static_cast<float>(_wtof(line.substr(colon + 1).c_str()));
                if (z >= 0.20f && z <= 5.0f) m_zoom = z;
            }
        } else if (line.find(L"\"panX\"") != std::wstring::npos) {
            auto colon = line.find(L':');
            if (colon != std::wstring::npos) {
                m_panX = static_cast<float>(_wtof(line.substr(colon + 1).c_str()));
            }
        } else if (line.find(L"\"panY\"") != std::wstring::npos) {
            auto colon = line.find(L':');
            if (colon != std::wstring::npos) {
                m_panY = static_cast<float>(_wtof(line.substr(colon + 1).c_str()));
            }
        } else if (line.find(L"\"id\"") != std::wstring::npos) {
            auto q1 = line.find(L'"', line.find(L"\"id\"") + 4);
            auto q2 = (q1 != std::wstring::npos) ? line.find(L'"', q1 + 1) : std::wstring::npos;
            if (q1 != std::wstring::npos && q2 != std::wstring::npos) {
                std::wstring id = line.substr(q1 + 1, q2 - q1 - 1);
                float x = 0.0f, y = 0.0f;
                bool pinned = false;

                auto xPos = line.find(L"\"x\":");
                if (xPos != std::wstring::npos) {
                    x = static_cast<float>(_wtof(line.substr(xPos + 4).c_str()));
                }
                auto yPos = line.find(L"\"y\":");
                if (yPos != std::wstring::npos) {
                    y = static_cast<float>(_wtof(line.substr(yPos + 4).c_str()));
                }
                if (line.find(L"\"pinned\": true") != std::wstring::npos || line.find(L"\"pinned\":true") != std::wstring::npos) {
                    pinned = true;
                }

                for (auto& n : m_nodes) {
                    if (n.label == id || n.id == id) {
                        n.x = x;
                        n.y = y;
                        n.isPinned = pinned;
                        n.vx = 0.0f;
                        n.vy = 0.0f;
                        foundAny = true;
                        break;
                    }
                }
            }
        }
    }
    return foundAny;
}

bool GraphView::HandleMouseMove(POINT clientPt) {
    if (m_canvasBounds.right - m_canvasBounds.left < 10.0f) return false;

    const float cx = (m_canvasBounds.left + m_canvasBounds.right) * 0.5f + m_panX;
    const float cy = (m_canvasBounds.top + m_canvasBounds.bottom) * 0.5f + m_panY;
    const float mouseX = static_cast<float>(clientPt.x);
    const float mouseY = static_cast<float>(clientPt.y);

    // Node Arranging / Dragging (User requirement: "and the user can drag the node to arrange it")
    if (m_draggedNode >= 0 && m_draggedNode < static_cast<int>(m_nodes.size())) {
        float wx = (mouseX - cx) / m_zoom;
        float wy = (mouseY - cy) / m_zoom;
        m_nodes[m_draggedNode].x = wx;
        m_nodes[m_draggedNode].y = wy;
        m_nodes[m_draggedNode].vx = 0.0f;
        m_nodes[m_draggedNode].vy = 0.0f;
        m_hasDragged = true;
        return true;
    }

    // Canvas Background Panning
    if (m_isPanning) {
        m_panX += (mouseX - static_cast<float>(m_lastMousePos.x));
        m_panY += (mouseY - static_cast<float>(m_lastMousePos.y));
        m_lastMousePos = clientPt;
        return true;
    }

    // Node Hover Testing
    int prevHover = m_hoveredNode;
    m_hoveredNode = -1;

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        float sx = cx + m_nodes[i].x * m_zoom;
        float sy = cy + m_nodes[i].y * m_zoom;
        float r = (std::clamp)(m_nodes[i].radius * m_zoom, 3.0f, 26.0f) + 6.0f;

        float dx = mouseX - sx;
        float dy = mouseY - sy;
        if (dx * dx + dy * dy <= r * r) {
            m_hoveredNode = static_cast<int>(i);
            break;
        }
    }

    return (m_hoveredNode != prevHover);
}

bool GraphView::HandleLButtonDown(POINT clientPt) {
    if (clientPt.x < m_canvasBounds.left || clientPt.x > m_canvasBounds.right ||
        clientPt.y < m_canvasBounds.top || clientPt.y > m_canvasBounds.bottom) {
        return false;
    }

    const float cx = (m_canvasBounds.left + m_canvasBounds.right) * 0.5f + m_panX;
    const float cy = (m_canvasBounds.top + m_canvasBounds.bottom) * 0.5f + m_panY;
    const float mouseX = static_cast<float>(clientPt.x);
    const float mouseY = static_cast<float>(clientPt.y);

    m_draggedNode = -1;
    m_hasDragged = false;
    m_lastMousePos = clientPt;

    // Check hit on nodes
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        float sx = cx + m_nodes[i].x * m_zoom;
        float sy = cy + m_nodes[i].y * m_zoom;
        float r = (std::clamp)(m_nodes[i].radius * m_zoom, 3.0f, 26.0f) + 6.0f;

        float dx = mouseX - sx;
        float dy = mouseY - sy;
        if (dx * dx + dy * dy <= r * r) {
            m_draggedNode = static_cast<int>(i);
            m_hoveredNode = static_cast<int>(i);
            return true;
        }
    }

    // Canvas background drag initiates pan
    m_isPanning = true;
    return true;
}

bool GraphView::HandleLButtonUp(POINT clientPt) {
    (void)clientPt;
    bool hadAction = (m_draggedNode >= 0 || m_isPanning);

    if (m_draggedNode >= 0) {
        if (!m_hasDragged) {
            // User clicked node without dragging -> store last clicked file
            if (m_draggedNode < static_cast<int>(m_nodes.size())) {
                m_lastClickedFile = m_nodes[m_draggedNode].id;
            }
        } else {
            // User finished dragging and arranging a node -> pin it, persist layout, and start settling
            if (m_draggedNode < static_cast<int>(m_nodes.size())) {
                m_nodes[m_draggedNode].isPinned = true;
                m_nodes[m_draggedNode].vx = 0.0f;
                m_nodes[m_draggedNode].vy = 0.0f;
            }
            SaveLayout();
            StartBounceAnimation();
        }
    } else if (m_isPanning) {
        SaveLayout();
    }

    m_draggedNode = -1;
    m_isPanning = false;
    m_hasDragged = false;
    return hadAction;
}

bool GraphView::HandleMouseWheel(short delta, POINT clientPt) {
    if (clientPt.x < m_canvasBounds.left || clientPt.x > m_canvasBounds.right ||
        clientPt.y < m_canvasBounds.top || clientPt.y > m_canvasBounds.bottom) {
        return false;
    }

    const float midX = (m_canvasBounds.left + m_canvasBounds.right) * 0.5f;
    const float midY = (m_canvasBounds.top + m_canvasBounds.bottom) * 0.5f;
    const float mouseX = static_cast<float>(clientPt.x);
    const float mouseY = static_cast<float>(clientPt.y);

    // World coordinate under cursor before zoom
    float wx = (mouseX - midX - m_panX) / m_zoom;
    float wy = (mouseY - midY - m_panY) / m_zoom;

    float zoomFactor = (delta > 0) ? 1.15f : 0.87f;
    float newZoom = (std::clamp)(m_zoom * zoomFactor, 0.25f, 3.5f);

    // Keep point under cursor stable after zoom
    m_panX = mouseX - midX - wx * newZoom;
    m_panY = mouseY - midY - wy * newZoom;
    m_zoom = newZoom;

    SaveLayout();
    return true;
}

} // namespace phide::ui
