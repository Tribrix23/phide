/**
 * @file WebIntelliSenseData.hpp
 * @brief Standard W3C HTML, CSS, and JavaScript/DOM IntelliSense definitions.
 */

#pragma once

#include "phide/editor/IntelliSenseEngine.hpp"
#include <vector>

namespace phide::editor {

class WebIntelliSenseData {
public:
    static const std::vector<IntelliSenseItem>& GetHtmlTags();
    static const std::vector<IntelliSenseItem>& GetHtmlGlobalAttributes();
    static const std::vector<IntelliSenseItem>& GetHtmlTagAttributes(const std::wstring& tag);
    static const std::vector<IntelliSenseItem>& GetCssProperties();
    static const std::vector<IntelliSenseItem>& GetCssValues();
    static const std::vector<IntelliSenseItem>& GetCssPseudo();
    static const std::vector<IntelliSenseItem>& GetJavaScriptGlobals();
    static const std::vector<IntelliSenseItem>& GetJavaScriptDomMethods();
    static const std::vector<IntelliSenseItem>& GetJavaScriptPrototypeMethods();
    static const std::vector<IntelliSenseItem>& GetCanvasRenderingContext2DMethods();
    static const std::vector<IntelliSenseItem>& GetTypeScriptGlobalsAndUtilityTypes();
};

} // namespace phide::editor
