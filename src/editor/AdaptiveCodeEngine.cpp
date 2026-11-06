#include "phide/editor/AdaptiveCodeEngine.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <thread>

namespace phide::editor {

namespace {

uint64_t GetCurrentTimestampSec() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

} // namespace

AdaptiveCodeEngine::AdaptiveCodeEngine() = default;

AdaptiveCodeEngine::~AdaptiveCodeEngine() {
    // Wait for any active background save to complete cleanly
    while (m_savingInProgress.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void AdaptiveCodeEngine::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_symbolHabits.clear();
    m_transitions.clear();
    m_codePatterns.clear();
    m_style = UserStylePreferences{};
    m_actionCount.store(0);
    m_lastDecayAction = 0;
}

void AdaptiveCodeEngine::ApplyDecayLocked() {
    uint64_t currentActions = m_actionCount.load();
    if (currentActions <= m_lastDecayAction) return;

    float deltaActions = static_cast<float>(currentActions - m_lastDecayAction);
    m_lastDecayAction = currentActions;

    // Exponential decay: gamma = 2 ^ (-delta / half_life)
    float decayFactor = std::pow(2.0f, -deltaActions / DECAY_HALF_LIFE_ACTIONS);

    // Apply decay to all symbols
    for (auto it = m_symbolHabits.begin(); it != m_symbolHabits.end();) {
        it->second.weight *= decayFactor;
        // Cull negligible weights (< 0.05) to keep table lean and fast
        if (it->second.weight < 0.05f) {
            it = m_symbolHabits.erase(it);
        } else {
            ++it;
        }
    }
}

void AdaptiveCodeEngine::PruneExcessEntriesLocked() {
    if (m_symbolHabits.size() > MAX_SYMBOLS) {
        // Collect pointers and partition to discard lowest 25% weights
        std::vector<std::pair<std::wstring, float>> items;
        items.reserve(m_symbolHabits.size());
        for (const auto& [k, v] : m_symbolHabits) {
            items.push_back({ k, v.weight });
        }
        size_t keepCount = (MAX_SYMBOLS * 3) / 4;
        std::nth_element(items.begin(), items.begin() + keepCount, items.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        for (size_t i = keepCount; i < items.size(); ++i) {
            m_symbolHabits.erase(items[i].first);
        }
    }

    if (m_transitions.size() > MAX_TRANSITIONS) {
        // Prune single-occurrence transitions
        for (auto it = m_transitions.begin(); it != m_transitions.end();) {
            auto& vec = it->second;
            std::erase_if(vec, [](const MarkovTransition& t) { return t.frequency <= 1; });
            if (vec.empty()) {
                it = m_transitions.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void AdaptiveCodeEngine::RecordSymbolUsage(const std::wstring& symbol, const std::wstring& lang) {
    if (symbol.length() <= 1) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t act = m_actionCount.fetch_add(1) + 1;

    // Periodically apply decay (every 100 actions)
    if (act - m_lastDecayAction >= 100) {
        ApplyDecayLocked();
    }

    auto nowSec = GetCurrentTimestampSec();
    auto it = m_symbolHabits.find(symbol);
    if (it != m_symbolHabits.end()) {
        it->second.weight += 1.0f;
        it->second.rawUsageCount++;
        it->second.lastUsedTimestampSec = nowSec;
        if (!lang.empty()) it->second.language = lang;
    } else {
        SymbolHabit habit;
        habit.weight = 1.0f;
        habit.rawUsageCount = 1;
        habit.lastUsedTimestampSec = nowSec;
        habit.language = lang;
        m_symbolHabits.emplace(symbol, std::move(habit));

        if (m_symbolHabits.size() > MAX_SYMBOLS) {
            PruneExcessEntriesLocked();
        }
    }
}

void AdaptiveCodeEngine::RecordTokenTransition(
    const std::wstring& prevToken,
    const std::wstring& currentToken,
    const std::wstring& lang
) {
    if (prevToken.empty() || currentToken.empty()) return;

    std::wstring key = lang + L":" + prevToken;
    uint64_t nowSec = GetCurrentTimestampSec();

    std::lock_guard<std::mutex> lock(m_mutex);
    auto& list = m_transitions[key];

    bool found = false;
    for (auto& cand : list) {
        if (cand.nextToken == currentToken) {
            cand.frequency++;
            cand.lastUsedTimestampSec = nowSec;
            found = true;
            break;
        }
    }

    if (!found) {
        if (list.size() < 8) { // Maximum 8 branching targets per state to prevent memory bloat
            MarkovTransition trans;
            trans.nextToken = currentToken;
            trans.frequency = 1;
            trans.lastUsedTimestampSec = nowSec;
            list.push_back(std::move(trans));
        }
    }

    if (m_transitions.size() > MAX_TRANSITIONS) {
        PruneExcessEntriesLocked();
    }
}

void AdaptiveCodeEngine::RecordLineStyle(const std::wstring& line, const std::wstring& lang) {
    if (line.empty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. Indentation detection
    if (line[0] == L'\t') {
        if (m_style.countTabs < 10000) m_style.countTabs++;
    } else if (line[0] == L' ') {
        size_t spCount = 0;
        while (spCount < line.length() && line[spCount] == L' ') {
            spCount++;
        }
        if (spCount > 0) {
            if (spCount % 4 == 0) {
                if (m_style.count4Spaces < 10000) m_style.count4Spaces++;
            } else if (spCount % 2 == 0) {
                if (m_style.count2Spaces < 10000) m_style.count2Spaces++;
            }
        }
    }

    // 2. Quote preferences
    for (wchar_t ch : line) {
        if (ch == L'\'') {
            if (m_style.countSingleQuotes < 10000) m_style.countSingleQuotes++;
        } else if (ch == L'"') {
            if (m_style.countDoubleQuotes < 10000) m_style.countDoubleQuotes++;
        }
    }

    // 3. Trailing semicolon preferences (in JS, TS, PHP, C++)
    std::wstring trimmed = line;
    while (!trimmed.empty() && iswspace(trimmed.back())) trimmed.pop_back();
    if (!trimmed.empty() && trimmed.length() > 3) {
        if (trimmed.back() == L';') {
            if (m_style.countSemicolons < 10000) m_style.countSemicolons++;
        } else if (trimmed.back() != L'{' && trimmed.back() != L'}' && trimmed.back() != L':' && trimmed.back() != L',') {
            if (m_style.countNoSemicolons < 10000) m_style.countNoSemicolons++;
        }
    }

    // 4. Brace placement
    if (trimmed.ends_with(L"{") && trimmed.length() > 1) {
        if (m_style.countSameLineBrace < 10000) m_style.countSameLineBrace++;
    } else if (trimmed == L"{") {
        if (m_style.countNextLineBrace < 10000) m_style.countNextLineBrace++;
    }

    // Adapt consolidated style preferences
    m_style.useTabs = (m_style.countTabs > (m_style.count2Spaces + m_style.count4Spaces));
    m_style.indentSpaces = (m_style.count2Spaces > m_style.count4Spaces) ? 2 : 4;
    m_style.preferredQuote = (m_style.countDoubleQuotes > m_style.countSingleQuotes) ? L'"' : L'\'';
    m_style.preferSemicolons = (m_style.countSemicolons >= m_style.countNoSemicolons);
    m_style.sameLineBraces = (m_style.countSameLineBrace >= m_style.countNextLineBrace);
}

int AdaptiveCodeEngine::GetAdaptiveScoreBonus(const std::wstring& symbol, const std::wstring& lang) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_symbolHabits.find(symbol);
    if (it == m_symbolHabits.end()) {
        return 0;
    }

    const auto& habit = it->second;
    // Base bonus from logarithmic frequency: bonus = 160 * log2(1 + weight)
    float rawBonus = 160.0f * std::log2(1.0f + habit.weight);

    // Recency boost: +80 if used within last 300 seconds
    uint64_t nowSec = GetCurrentTimestampSec();
    if (nowSec >= habit.lastUsedTimestampSec && (nowSec - habit.lastUsedTimestampSec) < 300) {
        rawBonus += 80.0f;
    }

    // Language affinity boost: +40 if matching current file language
    if (!lang.empty() && !habit.language.empty() && habit.language == lang) {
        rawBonus += 40.0f;
    }

    int score = static_cast<int>(std::round(rawBonus));
    return std::clamp(score, 0, 600);
}

std::wstring AdaptiveCodeEngine::PredictNextToken(
    const std::wstring& prevToken,
    const std::wstring& lang,
    float& outConfidence
) const {
    outConfidence = 0.0f;
    if (prevToken.empty()) return {};

    std::wstring key = lang + L":" + prevToken;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_transitions.find(key);
    if (it == m_transitions.end() || it->second.empty()) {
        return {};
    }

    uint32_t totalFreq = 0;
    const MarkovTransition* best = nullptr;
    for (const auto& trans : it->second) {
        totalFreq += trans.frequency;
        if (!best || trans.frequency > best->frequency) {
            best = &trans;
        }
    }

    if (best && totalFreq > 0) {
        float conf = static_cast<float>(best->frequency) / static_cast<float>(totalFreq);
        outConfidence = conf;
        // Require at least 45% probability and minimum 2 occurrences to suggest
        if (conf >= 0.45f && best->frequency >= 2) {
            return best->nextToken;
        }
    }

    return {};
}

void AdaptiveCodeEngine::RecordAssignmentPattern(
    const std::wstring& strValue,
    const std::wstring& followTemplate,
    const std::wstring& lang
) {
    if (strValue.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_actionCount.fetch_add(1, std::memory_order_relaxed);

    std::wstring key = lang.empty() ? L"all" : lang;
    for (auto& c : key) c = towlower(c);

    auto& list = m_codePatterns[key];
    uint64_t nowSec = GetCurrentTimestampSec();

    for (auto& item : list) {
        if (item.strValue == strValue) {
            item.frequency++;
            item.lastUsedTimestampSec = nowSec;
            if (!followTemplate.empty()) {
                item.followTemplate = followTemplate;
            }
            return;
        }
    }

    if (list.size() >= 256) {
        auto minIt = std::min_element(list.begin(), list.end(), [](const LearnedCodePattern& a, const LearnedCodePattern& b) {
            return a.frequency < b.frequency;
        });
        if (minIt != list.end()) list.erase(minIt);
    }

    LearnedCodePattern pat;
    pat.strValue = strValue;
    pat.followTemplate = followTemplate;
    pat.frequency = 1;
    pat.lastUsedTimestampSec = nowSec;
    list.push_back(std::move(pat));
}

bool AdaptiveCodeEngine::PredictAdaptivePattern(
    const std::wstring& prefix,
    const std::wstring& lang,
    std::wstring& outStrValue,
    std::wstring& outFollowTemplate,
    float& outConfidence
) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::wstring key = lang.empty() ? L"all" : lang;
    for (auto& c : key) c = towlower(c);

    auto checkList = [&](const std::vector<LearnedCodePattern>& list) -> bool {
        const LearnedCodePattern* best = nullptr;
        uint32_t totalFreq = 0;
        for (const auto& item : list) {
            if (prefix.empty() || item.strValue.rfind(prefix, 0) == 0) {
                totalFreq += item.frequency;
                if (!best || item.frequency > best->frequency) {
                    best = &item;
                }
            }
        }
        if (best && totalFreq > 0) {
            outStrValue = best->strValue;
            outFollowTemplate = best->followTemplate;
            outConfidence = static_cast<float>(best->frequency) / static_cast<float>(totalFreq);
            return true;
        }
        return false;
    };

    auto it = m_codePatterns.find(key);
    if (it != m_codePatterns.end() && checkList(it->second)) {
        return true;
    }
    if (key != L"all") {
        auto itAll = m_codePatterns.find(L"all");
        if (itAll != m_codePatterns.end() && checkList(itAll->second)) {
            return true;
        }
    }
    return false;
}

UserStylePreferences AdaptiveCodeEngine::GetStylePreferences() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_style;
}

size_t AdaptiveCodeEngine::GetMemoryFootprintBytes() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t bytes = sizeof(AdaptiveCodeEngine);
    bytes += m_symbolHabits.size() * (sizeof(std::wstring) + sizeof(SymbolHabit) + 32);
    for (const auto& [k, v] : m_transitions) {
        bytes += k.capacity() * sizeof(wchar_t);
        bytes += v.capacity() * sizeof(MarkovTransition) + 32;
    }
    for (const auto& [k, v] : m_codePatterns) {
        bytes += k.capacity() * sizeof(wchar_t);
        bytes += v.capacity() * sizeof(LearnedCodePattern) + 32;
    }
    return bytes;
}

size_t AdaptiveCodeEngine::GetLearnedSymbolCount() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_symbolHabits.size();
}

void AdaptiveCodeEngine::LoadProfile(const std::wstring& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return;

    std::string line;
    std::string jsonStr((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (jsonStr.empty()) return;

    // Convert UTF-8 to wide string
    int needed = MultiByteToWideChar(CP_UTF8, 0, jsonStr.data(), static_cast<int>(jsonStr.size()), nullptr, 0);
    if (needed <= 0) return;
    std::wstring wideJson(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, jsonStr.data(), static_cast<int>(jsonStr.size()), wideJson.data(), needed);

    std::lock_guard<std::mutex> lock(m_mutex);

    // Simple fast parser for our own serialized JSON schema
    size_t symPos = wideJson.find(L"\"symbols\":");
    if (symPos != std::wstring::npos) {
        size_t endSym = wideJson.find(L"]", symPos);
        if (endSym != std::wstring::npos) {
            std::wstring block = wideJson.substr(symPos, endSym - symPos);
            size_t entryStart = 0;
            while ((entryStart = block.find(L"{\"name\":\"", entryStart)) != std::wstring::npos) {
                entryStart += 9;
                size_t nameEnd = block.find(L"\"", entryStart);
                if (nameEnd == std::wstring::npos) break;
                std::wstring name = block.substr(entryStart, nameEnd - entryStart);

                float weight = 1.0f;
                size_t wPos = block.find(L"\"w\":", nameEnd);
                if (wPos != std::wstring::npos && wPos < entryStart + 80) {
                    try {
                        weight = std::stof(block.substr(wPos + 4));
                    } catch (...) {}
                }

                SymbolHabit h;
                h.weight = weight;
                h.lastUsedTimestampSec = GetCurrentTimestampSec();
                m_symbolHabits[name] = h;
                entryStart = nameEnd;
            }
        }
    }

    // Style preferences
    if (wideJson.find(L"\"useTabs\":true") != std::wstring::npos) m_style.useTabs = true;
    if (wideJson.find(L"\"indentSpaces\":2") != std::wstring::npos) m_style.indentSpaces = 2;
    if (wideJson.find(L"\"quote\":\"\\\"\"") != std::wstring::npos) m_style.preferredQuote = L'"';
}

void AdaptiveCodeEngine::SaveProfileAsync(const std::wstring& filePath) {
    if (m_savingInProgress.exchange(true)) {
        return; // Already saving in background
    }

    // Take an atomic snapshot of current state to write in background thread
    std::vector<std::pair<std::wstring, float>> symbolSnapshot;
    UserStylePreferences styleSnapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        symbolSnapshot.reserve((std::min)(m_symbolHabits.size(), size_t(500)));
        for (const auto& [k, v] : m_symbolHabits) {
            if (v.weight >= 0.5f) {
                symbolSnapshot.push_back({ k, v.weight });
            }
        }
        styleSnapshot = m_style;
    }

    std::thread([this, filePath, symbolSnapshot = std::move(symbolSnapshot), styleSnapshot]() {
        std::error_code ec;
        std::filesystem::path p(filePath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path(), ec);
        }

        std::ofstream ofs(p);
        if (ofs.is_open()) {
            ofs << "{\n  \"style\": {\n";
            ofs << "    \"indentSpaces\": " << styleSnapshot.indentSpaces << ",\n";
            ofs << "    \"useTabs\": " << (styleSnapshot.useTabs ? "true" : "false") << ",\n";
            ofs << "    \"quote\": \"" << (styleSnapshot.preferredQuote == L'"' ? "\\\"" : "'") << "\",\n";
            ofs << "    \"semicolons\": " << (styleSnapshot.preferSemicolons ? "true" : "false") << "\n";
            ofs << "  },\n  \"symbols\": [\n";

            for (size_t i = 0; i < symbolSnapshot.size(); ++i) {
                // Convert wide name to UTF-8
                std::string u8Name;
                const auto& wName = symbolSnapshot[i].first;
                int needed = WideCharToMultiByte(CP_UTF8, 0, wName.data(), static_cast<int>(wName.size()), nullptr, 0, nullptr, nullptr);
                if (needed > 0) {
                    u8Name.resize(needed);
                    WideCharToMultiByte(CP_UTF8, 0, wName.data(), static_cast<int>(wName.size()), u8Name.data(), needed, nullptr, nullptr);
                }

                ofs << "    {\"name\":\"" << u8Name << "\",\"w\":" << symbolSnapshot[i].second << "}";
                if (i + 1 < symbolSnapshot.size()) ofs << ",";
                ofs << "\n";
            }
            ofs << "  ]\n}\n";
        }
        m_savingInProgress.store(false);
    }).detach();
}

} // namespace phide::editor
