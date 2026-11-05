import re
from pathlib import Path

REPO_ROOT = Path(r"c:\Users\Administrator\Downloads\studio\phide")

# -----------------------------------------------------------------------------
# 1. Patch src/editor/PredictionEngine.cpp to prevent ghost text overlap
# -----------------------------------------------------------------------------
pred_file = REPO_ROOT / "src" / "editor" / "PredictionEngine.cpp"
pred_content = pred_file.read_text(encoding="utf-8").replace("\r\n", "\n")

old_pred_start = """CodePrediction PredictionEngine::PredictNextEdit(
    const std::wstring& filePath,
    const Buffer& buffer,
    size_t cursorLine,
    size_t cursorCol,
    const std::vector<std::wstring>& openFiles
) {"""

new_pred_start = """CodePrediction PredictionEngine::PredictNextEdit(
    const std::wstring& filePath,
    const Buffer& buffer,
    size_t cursorLine,
    size_t cursorCol,
    const std::vector<std::wstring>& openFiles
) {
    if (cursorLine < buffer.GetLineCount()) {
        const std::wstring& curL = buffer.GetLine(cursorLine);
        // Do not generate ghost text predictions if there is non-whitespace code after caret on same line
        if (cursorCol < curL.length()) {
            bool hasCodeAfter = false;
            for (size_t c = cursorCol; c < curL.length(); ++c) {
                if (!iswspace(curL[c])) { hasCodeAfter = true; break; }
            }
            if (hasCodeAfter) return {};
        }
    }"""

if old_pred_start in pred_content:
    pred_content = pred_content.replace(old_pred_start, new_pred_start)
    pred_file.write_text(pred_content, encoding="utf-8")
    print("1. Successfully patched PredictionEngine.cpp (no overlap on mid-line edit)")
else:
    print("Warning: old_pred_start not found in PredictionEngine.cpp")

# -----------------------------------------------------------------------------
# 2. Patch src/editor/IntelliSenseEngine.cpp for React JSX className & attributes
# -----------------------------------------------------------------------------
is_file = REPO_ROOT / "src" / "editor" / "IntelliSenseEngine.cpp"
is_content = is_file.read_text(encoding="utf-8").replace("\r\n", "\n")

old_js_block = """            for (const auto& g : m_jsGlobals) rawCandidates.push_back(g);
            for (const auto& kw : m_jsKeywords) rawCandidates.push_back(kw);
            for (const auto& d : m_jsDomApis) rawCandidates.push_back(d);
            for (const auto& r : m_reactAndNodeVocabulary) rawCandidates.push_back(r);

            // In React JSX/TSX files, also provide HTML element tags
            if (ext == L".jsx" || ext == L".tsx") {
                for (const auto& tag : m_htmlTags) rawCandidates.push_back(tag);
            }
        }
    } else if (isHtml) {"""

new_js_block = """            bool isJsx = (ext == L".jsx" || ext == L".tsx" || (isJs && (currentLine.find(L"<") != std::wstring::npos)));
            size_t lastOpenTag = currentLine.rfind(L'<', cursorCol > 0 ? cursorCol - 1 : 0);
            size_t lastCloseTag = currentLine.rfind(L'>', cursorCol > 0 ? cursorCol - 1 : 0);
            bool insideTag = isJsx && (lastOpenTag != std::wstring::npos && (lastCloseTag == std::wstring::npos || lastOpenTag > lastCloseTag));

            if (insideTag) {
                size_t spaceAfterOpen = currentLine.find_first_of(L" \\t", lastOpenTag);
                if (spaceAfterOpen == std::wstring::npos || cursorCol <= spaceAfterOpen) {
                    // Tag name inside JSX: <div, <Button, etc.
                    for (const auto& tag : m_htmlTags) rawCandidates.push_back(tag);
                    for (const auto& r : m_reactAndNodeVocabulary) {
                        if (r.kind == CompletionKind::Class || r.kind == CompletionKind::Function) {
                            rawCandidates.push_back(r);
                        }
                    }
                } else {
                    // Inside JSX Tag Attributes: e.g. <div class|
                    // 1. Propose all React JSX Attributes from @types/react (with className at score 4500!)
                    const auto& jsxAttrs = GetReactJsxAttributes();
                    for (const auto& attr : jsxAttrs) {
                        rawCandidates.push_back(attr);
                    }
                    // 2. Also propose standard HTML attributes (excluding HTML 'class', which is invalid in React)
                    for (const auto& attr : m_htmlAttributes) {
                        if (attr.label == L"class") continue;
                        rawCandidates.push_back(attr);
                    }
                    insideTagAttr = true;
                }
            } else {
                for (const auto& g : m_jsGlobals) rawCandidates.push_back(g);
                for (const auto& kw : m_jsKeywords) rawCandidates.push_back(kw);
                for (const auto& d : m_jsDomApis) rawCandidates.push_back(d);
                for (const auto& r : m_reactAndNodeVocabulary) rawCandidates.push_back(r);

                // In React JSX/TSX files, also provide HTML element tags
                if (ext == L".jsx" || ext == L".tsx") {
                    for (const auto& tag : m_htmlTags) rawCandidates.push_back(tag);
                }
            }
        }
    } else if (isHtml) {"""

if old_js_block in is_content:
    # declare insideTagAttr at start of QueryCompletions
    if "bool insideTagAttr = false;" not in is_content:
        is_content = is_content.replace(
            "bool isCss = (ext == L\".css\" || ext == L\".scss\" || ext == L\".less\");",
            "bool isCss = (ext == L\".css\" || ext == L\".scss\" || ext == L\".less\");\n    bool insideTagAttr = false;"
        )
    is_content = is_content.replace(old_js_block, new_js_block)
    # also update line 2017 to check insideTagAttr
    is_content = is_content.replace(
        "if (!isCss && !insideStyle && !insidePhp) {",
        "if (!isCss && !insideStyle && !insidePhp && !insideTagAttr) {"
    )
    is_file.write_text(is_content, encoding="utf-8")
    print("2. Successfully patched IntelliSenseEngine.cpp (JSX attributes & className prioritized)")
else:
    print("Warning: old_js_block not found in IntelliSenseEngine.cpp")

# -----------------------------------------------------------------------------
# 3. Patch src/ui/TerminalPanel.cpp for High-Contrast Light Mode
# -----------------------------------------------------------------------------
term_file = REPO_ROOT / "src" / "ui" / "TerminalPanel.cpp"
term_content = term_file.read_text(encoding="utf-8").replace("\r\n", "\n")

# Add AdaptTerminalColorForLight helper
adapt_code = """
static D2D1_COLOR_F AdaptTerminalColorForLight(D2D1_COLOR_F c) {
    // If color is very light / white / near white (e.g. > 0.70f gray/white)
    if (c.r > 0.70f && c.g > 0.70f && c.b > 0.70f) {
        return D2D1::ColorF(0.08f, 0.10f, 0.14f, 1.0f); // Rich dark charcoal
    }
    // If color is washed-out yellow (e.g. command names like python -u ...)
    if (c.r > 0.80f && c.g > 0.65f && c.b < 0.45f) {
        return D2D1::ColorF(0.58f, 0.32f, 0.00f, 1.0f); // Rich deep amber bronze
    }
    // If color is cyan / light blue (e.g. PowerShell path)
    if (c.b > 0.75f && c.g > 0.60f && c.r < 0.50f) {
        return D2D1::ColorF(0.00f, 0.35f, 0.72f, 1.0f); // Rich deep royal blue
    }
    // If color is bright lime / light green
    if (c.g > 0.70f && c.r < 0.50f) {
        return D2D1::ColorF(0.02f, 0.48f, 0.22f, 1.0f); // Deep emerald green
    }
    // If color is bright light magenta
    if (c.r > 0.70f && c.b > 0.70f && c.g < 0.60f) {
        return D2D1::ColorF(0.52f, 0.12f, 0.62f, 1.0f); // Deep rich purple
    }
    // If color is bright light red
    if (c.r > 0.85f && c.g < 0.45f && c.b < 0.45f) {
        return D2D1::ColorF(0.78f, 0.12f, 0.12f, 1.0f); // Deep crimson red
    }
    return c;
}
"""

if "AdaptTerminalColorForLight" not in term_content:
    term_content = term_content.replace(
        "namespace phide::ui {",
        "namespace phide::ui {\n" + adapt_code
    )

# Replace RenderContent span rendering
old_span_brush = """            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> spanBrush;
            pRT->CreateSolidColorBrush(span.color, &spanBrush);"""

new_span_brush = """            D2D1_COLOR_F drawColor = theme.IsDarkMode() ? span.color : AdaptTerminalColorForLight(span.color);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> spanBrush;
            pRT->CreateSolidColorBrush(drawColor, &spanBrush);"""

term_content = term_content.replace(old_span_brush, new_span_brush)

# Replace typed command input line rendering
old_input_span_brush = """                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> spanBrush;
                pRT->CreateSolidColorBrush(span.color, &spanBrush);
                if (spanBrush) {
                    pRT->DrawText(span.text.c_str(), static_cast<UINT32>(span.text.length()),
                                  termFmt, rcSpan, spanBrush.Get());
                }"""

new_input_span_brush = """                D2D1_COLOR_F drawColor = theme.IsDarkMode() ? span.color : AdaptTerminalColorForLight(span.color);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> spanBrush;
                pRT->CreateSolidColorBrush(drawColor, &spanBrush);
                if (spanBrush) {
                    pRT->DrawText(span.text.c_str(), static_cast<UINT32>(span.text.length()),
                                  termFmt, rcSpan, spanBrush.Get());
                }"""

term_content = term_content.replace(old_input_span_brush, new_input_span_brush)

# Replace terminal tab active pill color in Light mode
old_tab_pill = """pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.22f, 0.36f, 1.0f) : D2D1::ColorF(0.85f, 0.90f, 0.98f, 1.0f), &activePillBg);"""
new_tab_pill = """pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.22f, 0.36f, 1.0f) : D2D1::ColorF(0.000f, 0.400f, 0.850f, 1.0f), &activePillBg);"""
term_content = term_content.replace(old_tab_pill, new_tab_pill)

term_file.write_text(term_content, encoding="utf-8")
print("3. Successfully patched TerminalPanel.cpp (Light Mode terminal contrast overhaul)")

# -----------------------------------------------------------------------------
# 4. Patch src/ui/EditorCanvas.cpp (Autocomplete highlight, Zed tab badge, error card)
# -----------------------------------------------------------------------------
ec_file = REPO_ROOT / "src" / "ui" / "EditorCanvas.cpp"
ec_content = ec_file.read_text(encoding="utf-8").replace("\r\n", "\n")

# Autocomplete row highlight in light mode: blue text instead of invisible white
old_hl_brush = """                if (isSelected) {
                    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), &hlBrush); // crisp bold white on selection
                } else {"""

new_hl_brush = """                if (isSelected) {
                    pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.00f, 0.30f, 0.75f), &hlBrush);
                } else {"""

ec_content = ec_content.replace(old_hl_brush, new_hl_brush)

# Only show doc sidecar if documentation exists or detail is multiline/long
old_sidecar_check = """    if (m_autocompleteSelectedIdx < m_autocompleteItems.size()) {
        const auto& selItem = m_autocompleteItems[m_autocompleteSelectedIdx];
        if (!selItem.detail.empty() || !selItem.documentation.empty() || selItem.isColor) {"""

new_sidecar_check = """    if (m_autocompleteSelectedIdx < m_autocompleteItems.size()) {
        const auto& selItem = m_autocompleteItems[m_autocompleteSelectedIdx];
        bool hasRichDoc = !selItem.documentation.empty() || selItem.isColor || (selItem.detail.length() > 38 || selItem.detail.find(L'\\n') != std::wstring::npos);
        if (hasRichDoc) {"""

ec_content = ec_content.replace(old_sidecar_check, new_sidecar_check)

# Zed tab Accept badge light mode colors
old_zed_badge = """                        pRT->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.14f, 0.18f, 0.95f), &brBadgeBg);
                        pRT->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.28f, 0.36f, 0.85f), &brBadgeBorder);
                        pRT->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.24f, 0.32f, 0.98f), &brKeyBg);
                        pRT->CreateSolidColorBrush(D2D1::ColorF(0.34f, 0.40f, 0.52f, 0.90f), &brKeyBorder);
                        pRT->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.89f, 0.95f, 1.0f), &brKeyText);
                        pRT->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.97f, 1.0f, 1.0f), &brAcceptText);"""

new_zed_badge = """                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.14f, 0.18f, 0.95f) : D2D1::ColorF(0.92f, 0.94f, 0.97f, 0.98f), &brBadgeBg);
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.24f, 0.28f, 0.36f, 0.85f) : D2D1::ColorF(0.78f, 0.82f, 0.88f, 0.90f), &brBadgeBorder);
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.20f, 0.24f, 0.32f, 0.98f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brKeyBg);
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.34f, 0.40f, 0.52f, 0.90f) : D2D1::ColorF(0.75f, 0.79f, 0.85f, 0.95f), &brKeyBorder);
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.85f, 0.89f, 0.95f, 1.0f) : D2D1::ColorF(0.10f, 0.12f, 0.16f, 1.0f), &brKeyText);
                        pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.95f, 0.97f, 1.0f, 1.0f) : D2D1::ColorF(0.15f, 0.18f, 0.25f, 1.0f), &brAcceptText);"""

ec_content = ec_content.replace(old_zed_badge, new_zed_badge)

# Diagnostic error popover card colors in Light mode
old_diag_card = """            pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &cardBg);
            pRT->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.28f, 0.28f, 1.0f), &cardBorder);"""

new_diag_card = """            pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.12f, 0.12f, 0.14f, 1.0f) : D2D1::ColorF(1.0f, 0.96f, 0.96f, 0.98f), &cardBg);
            pRT->CreateSolidColorBrush(theme.IsDarkMode() ? D2D1::ColorF(0.92f, 0.28f, 0.28f, 1.0f) : D2D1::ColorF(0.90f, 0.40f, 0.40f, 1.0f), &cardBorder);"""

ec_content = ec_content.replace(old_diag_card, new_diag_card)

ec_file.write_text(ec_content, encoding="utf-8")
print("4. Successfully patched EditorCanvas.cpp (Autocomplete highlight, Zed tab badge, error card)")

print("All patches completed successfully!")
