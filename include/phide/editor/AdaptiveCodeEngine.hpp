#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <chrono>

namespace phide::editor {

/**
 * @brief User's learned stylistic formatting preferences.
 */
struct UserStylePreferences {
    int indentSpaces = 4;           ///< Observed indent width (typically 2 or 4)
    bool useTabs = false;           ///< Whether user prefers tabs over spaces
    wchar_t preferredQuote = L'\''; ///< Preferred quote character (' or ")
    bool preferSemicolons = true;   ///< Whether user prefers explicit semicolons
    bool sameLineBraces = true;     ///< Whether user places open brace on same line

    // Bayesian evidence tallies (strictly bounded)
    uint32_t count2Spaces = 0;
    uint32_t count4Spaces = 0;
    uint32_t countTabs = 0;
    uint32_t countSingleQuotes = 0;
    uint32_t countDoubleQuotes = 0;
    uint32_t countSemicolons = 0;
    uint32_t countNoSemicolons = 0;
    uint32_t countSameLineBrace = 0;
    uint32_t countNextLineBrace = 0;
};

/**
 * @brief Usage statistics for a specific token or symbol with exponential decay.
 */
struct SymbolHabit {
    float weight = 1.0f;              ///< Decayed frequency score
    uint64_t lastUsedTimestampSec = 0;///< Last used timestamp (seconds since epoch)
    uint32_t rawUsageCount = 1;       ///< Total lifetime selections
    std::wstring language;            ///< Language tag (e.g. "ts", "py", "php", "cpp")
};

/**
 * @brief Next-token transition candidate in order-2 Markov predictor.
 */
struct MarkovTransition {
    std::wstring nextToken;           ///< Predicted subsequent token/phrase
    uint32_t frequency = 1;           ///< Frequency count of this transition
    uint64_t lastUsedTimestampSec = 0;///< Recency
};

/**
 * @brief Learned repeated assignment and follow-up template pattern.
 */
struct LearnedCodePattern {
    std::wstring strValue;            ///< Learned string literal e.g. "hello world"
    std::wstring followTemplate;      ///< Learned follow-up template e.g. "print($VAR)"
    uint32_t frequency = 1;           ///< Usage frequency
    uint64_t lastUsedTimestampSec = 0;///< Recency
};

/**
 * @brief High-performance, zero-LLM adaptive learning engine.
 * Learns how the user codes overtime:
 *  1. Frequency & recency symbol affinity with temporal half-life decay (MRU+MFU)
 *  2. Order-2 Markov next-token transition predictor for instant ghost-text & completion
 *  3. Dynamic coding style adaptation (indentation, quotes, semicolons, braces)
 * Guarantees:
 *  - ZERO UI THREAD FREEZING: all hot-path operations run in under 50 microseconds.
 *  - STRICTLY BOUNDED MEMORY: hard cap on entries (< 1.5 MB RAM total footprint).
 *  - ASYNCHRONOUS PERSISTENCE: background thread serialization.
 */
class AdaptiveCodeEngine {
public:
    static constexpr size_t MAX_SYMBOLS = 4096;
    static constexpr size_t MAX_TRANSITIONS = 8192;
    static constexpr float DECAY_HALF_LIFE_ACTIONS = 600.0f; // Half-life in actions

    AdaptiveCodeEngine();
    ~AdaptiveCodeEngine();

    /**
     * @brief Record a symbol selected from autocomplete or typed by user.
     * @param symbol Identifier, method, property, or keyword name.
     * @param lang Language tag (e.g. "ts", "py", "php", "cpp").
     */
    void RecordSymbolUsage(const std::wstring& symbol, const std::wstring& lang = L"");

    /**
     * @brief Record a sequence transition (prevPrevToken, prevToken -> currentToken).
     */
    void RecordTokenTransition(
        const std::wstring& prevToken,
        const std::wstring& currentToken,
        const std::wstring& lang = L""
    );

    /**
     * @brief Record a line of code to analyze formatting style habits.
     */
    void RecordLineStyle(const std::wstring& line, const std::wstring& lang = L"");

    /**
     * @brief Query score boost for an autocomplete proposal based on user habits.
     * @param symbol Candidate label.
     * @param lang Current document language.
     * @return Adaptive score bonus (0 to 600).
     */
    [[nodiscard]] int GetAdaptiveScoreBonus(const std::wstring& symbol, const std::wstring& lang = L"") const;

    /**
     * @brief Predict the next token/construct based on learned Markov transitions.
     * @param prevToken Token preceding the caret.
     * @param lang Current document language.
     * @param outConfidence Confidence probability (0.0 to 1.0).
     * @return Predicted next string, or empty if confidence is insufficient.
     */
    [[nodiscard]] std::wstring PredictNextToken(
        const std::wstring& prevToken,
        const std::wstring& lang,
        float& outConfidence
    ) const;

    /**
     * @brief Record a learned assignment and follow-up template pattern.
     */
    void RecordAssignmentPattern(
        const std::wstring& strValue,
        const std::wstring& followTemplate,
        const std::wstring& lang = L""
    );

    /**
     * @brief Predict a learned assignment pattern (string literal and follow-up statement).
     */
    bool PredictAdaptivePattern(
        const std::wstring& prefix,
        const std::wstring& lang,
        std::wstring& outStrValue,
        std::wstring& outFollowTemplate,
        float& outConfidence
    ) const;

    /**
     * @brief Get current user stylistic preferences.
     */
    [[nodiscard]] UserStylePreferences GetStylePreferences() const;

    /**
     * @brief Load learned user profile from disk asynchronously or synchronously.
     */
    void LoadProfile(const std::wstring& filePath);

    /**
     * @brief Save learned user profile to disk asynchronously without freezing UI.
     */
    void SaveProfileAsync(const std::wstring& filePath);

    /**
     * @brief Clear all learned habits.
     */
    void Reset();

    /**
     * @brief Get memory footprint estimate in bytes.
     */
    [[nodiscard]] size_t GetMemoryFootprintBytes() const noexcept;

    /**
     * @brief Get count of learned symbols.
     */
    [[nodiscard]] size_t GetLearnedSymbolCount() const noexcept;

private:
    void ApplyDecayLocked();
    void PruneExcessEntriesLocked();

    mutable std::mutex m_mutex;

    // Symbol affinity map: symbol -> habit
    std::unordered_map<std::wstring, SymbolHabit> m_symbolHabits;

    // Markov transition map: "lang:prevToken" -> list of next candidates
    std::unordered_map<std::wstring, std::vector<MarkovTransition>> m_transitions;

    // Pattern map: "lang" -> list of learned assignment patterns
    std::unordered_map<std::wstring, std::vector<LearnedCodePattern>> m_codePatterns;

    // Formatting habits
    UserStylePreferences m_style;

    // Interaction counters for decay epochs
    std::atomic<uint64_t> m_actionCount{0};
    uint64_t m_lastDecayAction{0};

    // Background saver thread state
    std::atomic<bool> m_savingInProgress{false};
};

} // namespace phide::editor
