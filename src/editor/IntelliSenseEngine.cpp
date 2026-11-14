#include "phide/editor/IntelliSenseEngine.hpp"
#include "phide/editor/PhpIntelliSenseData.hpp"
#include "phide/editor/PythonIntelliSenseData.hpp"
#include "phide/editor/PythonEnvironment.hpp"
#include "phide/editor/ReactIntelliSenseData.hpp"
#include "phide/editor/WebIntelliSenseData.hpp"
#include "phide/editor/EmmetEngine.hpp"
#include "phide/editor/SnippetManager.hpp"
#include "phide/editor/TailwindHelper.hpp"
#include "phide/editor/AdaptiveCodeEngine.hpp"
#include "phide/plugin/PluginManager.hpp"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace phide::editor {

IntelliSenseEngine::IntelliSenseEngine() {
    PopulateHtmlTags();
    PopulateHtmlAttributes();
    PopulateCssProperties();
    PopulateCssValues();
    PopulateJavaScriptKeywords();
    PopulateJavaScriptGlobals();
    PopulateJavaScriptDomApis();
    PopulateJavaScriptPrototypeMethods();
    PopulatePhpVocabulary();
    PopulatePythonVocabulary();
    PopulateReactAndNodeVocabulary();
    PopulateCommonVocabulary();
}

void IntelliSenseEngine::SetWorkspaceEnvVars(std::vector<EnvVarEntry> vars) {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    m_envVars = std::move(vars);
}

void IntelliSenseEngine::SetDatabaseSchema(std::vector<DbSchemaColumn> columns, std::wstring dbName) {
    std::lock_guard<std::mutex> lock(m_contextMutex);
    m_dbSchema = std::move(columns);
    m_dbName = std::move(dbName);
}

void IntelliSenseEngine::PopulateHtmlTags() {
    const auto& officialTags = WebIntelliSenseData::GetHtmlTags();
    if (!officialTags.empty()) {
        m_htmlTags = officialTags;
        return;
    }
    // Standard HTML5 tags (all 120+ elements)
    struct TagDef { const wchar_t* name; const wchar_t* desc; };
    TagDef tags[] = {
        { L"a", L"Anchor hyperlink element" },
        { L"abbr", L"Abbreviation or acronym" },
        { L"address", L"Contact information for author/owner" },
        { L"area", L"Hyperlink area within an image map" },
        { L"article", L"Self-contained composition in document" },
        { L"aside", L"Indirectly related sidebar content" },
        { L"audio", L"Sound or audio stream container" },
        { L"b", L"Bring attention to text (bold)" },
        { L"base", L"Base URL for all relative URLs" },
        { L"bdi", L"Bidirectional isolation element" },
        { L"bdo", L"Bidirectional override element" },
        { L"blockquote", L"Extended block quotation" },
        { L"body", L"Document body content" },
        { L"br", L"Line break" },
        { L"button", L"Clickable button control" },
        { L"canvas", L"Bitmap graphics canvas API" },
        { L"caption", L"Table title or caption" },
        { L"cite", L"Title of a work or reference" },
        { L"code", L"Inline code fragment" },
        { L"col", L"Column within a table column group" },
        { L"colgroup", L"Group of columns within a table" },
        { L"data", L"Machine-readable translation of content" },
        { L"datalist", L"Set of predefined options for input" },
        { L"dd", L"Description details element" },
        { L"del", L"Deleted text representation" },
        { L"details", L"Disclosure widget for extra info" },
        { L"dfn", L"Defining instance of a term" },
        { L"dialog", L"Dialog box or subwindow modal" },
        { L"div", L"Generic block-level division container" },
        { L"dl", L"Description list container" },
        { L"dt", L"Description term element" },
        { L"em", L"Emphasized text" },
        { L"embed", L"External application integration container" },
        { L"fieldset", L"Group of form controls with legend" },
        { L"figcaption", L"Caption or legend for a figure" },
        { L"figure", L"Self-contained illustrative figure" },
        { L"footer", L"Footer for nearest section or page" },
        { L"form", L"Interactive form container for user input" },
        { L"h1", L"Top level section heading" },
        { L"h2", L"Second level section heading" },
        { L"h3", L"Third level section heading" },
        { L"h4", L"Fourth level section heading" },
        { L"h5", L"Fifth level section heading" },
        { L"h6", L"Sixth level section heading" },
        { L"head", L"Machine-readable metadata container" },
        { L"header", L"Introductory content or navigation banner" },
        { L"hgroup", L"Heading and subheading multi-level group" },
        { L"hr", L"Thematic break or horizontal rule" },
        { L"html", L"Root element of an HTML document" },
        { L"i", L"Idiomatic text or icon container" },
        { L"iframe", L"Nested browsing context / inline frame" },
        { L"img", L"Image embed element" },
        { L"input", L"Interactive data input control" },
        { L"ins", L"Inserted text representation" },
        { L"kbd", L"User keyboard input representation" },
        { L"label", L"Caption for an input form control" },
        { L"legend", L"Caption for a fieldset control" },
        { L"li", L"List item element" },
        { L"link", L"External resource stylesheet / icon link" },
        { L"main", L"Dominant central content of document body" },
        { L"map", L"Client-side image map container" },
        { L"mark", L"Highlighted or marked text for reference" },
        { L"meta", L"Document-level metadata representation" },
        { L"meter", L"Scalar measurement within known range" },
        { L"nav", L"Navigation section containing links" },
        { L"noscript", L"Fallback content when script is disabled" },
        { L"object", L"External multi-media or plugin resource" },
        { L"ol", L"Ordered enumerated list container" },
        { L"optgroup", L"Group of options within select dropdown" },
        { L"option", L"Item in a select dropdown or datalist" },
        { L"output", L"Result of calculation or user action" },
        { L"p", L"Paragraph text block" },
        { L"picture", L"Responsive image source container" },
        { L"pre", L"Preformatted text with preserved whitespace" },
        { L"progress", L"Completion progress indicator bar" },
        { L"q", L"Inline quotation" },
        { L"rp", L"Parentheses fallback for ruby annotation" },
        { L"rt", L"Ruby pronunciation annotation text" },
        { L"ruby", L"Ruby pronunciation container" },
        { L"s", L"Strikethrough or non-accurate text" },
        { L"samp", L"Sample output from computer program" },
        { L"script", L"Embedded or external client-side script" },
        { L"search", L"Search and filtering section container" },
        { L"section", L"Generic standalone thematic section" },
        { L"select", L"Dropdown selection control menu" },
        { L"slot", L"Web Component placeholder slot" },
        { L"small", L"Side comment or fine print" },
        { L"source", L"Media resource for picture, audio or video" },
        { L"span", L"Generic inline styling container" },
        { L"strong", L"Strong importance or urgency" },
        { L"style", L"Document-level CSS stylesheet rules" },
        { L"sub", L"Subscript typography" },
        { L"summary", L"Summary or disclosure heading for details" },
        { L"sup", L"Superscript typography" },
        { L"svg", L"Scalable Vector Graphics container" },
        { L"table", L"Tabular data grid table representation" },
        { L"tbody", L"Body content row group within a table" },
        { L"td", L"Table data cell" },
        { L"template", L"Declared HTML fragment for client cloning" },
        { L"textarea", L"Multi-line plain text editing control" },
        { L"tfoot", L"Footer row group within a table" },
        { L"th", L"Table header cell" },
        { L"thead", L"Header row group within a table" },
        { L"time", L"Specific date, time, or duration value" },
        { L"title", L"Document title displayed in browser tab" },
        { L"tr", L"Table row container" },
        { L"track", L"Timed text track subtitles for media" },
        { L"u", L"Unarticulated annotation or underline" },
        { L"ul", L"Unordered bulleted list container" },
        { L"var", L"Mathematical or programming variable" },
        { L"video", L"Movie clip or video stream playback" },
        { L"wbr", L"Word break opportunity" }
    };

    for (const auto& t : tags) {
        IntelliSenseItem item;
        item.label = t.name;
        item.detail = t.desc;
        item.insertText = t.name;
        item.kind = CompletionKind::Tag;
        item.origin = L"html";
        m_htmlTags.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulateHtmlAttributes() {
    const auto& officialAttrs = WebIntelliSenseData::GetHtmlGlobalAttributes();
    if (!officialAttrs.empty()) {
        m_htmlAttributes = officialAttrs;
        return;
    }
    struct AttrDef { const wchar_t* name; const wchar_t* desc; };
    AttrDef attrs[] = {
        // Core & Global Attributes
        { L"class", L"Space-separated list of CSS class names" },
        { L"id", L"Unique document-wide identifier" },
        { L"style", L"Inline CSS styling rules" },
        { L"title", L"Advisory information displayed as tooltip" },
        { L"lang", L"Specifies language of element content (e.g. en, es)" },
        { L"dir", L"Text directionality (ltr, rtl, auto)" },
        { L"translate", L"Whether element content should be translated (yes, no)" },
        { L"tabindex", L"Keyboard focus tab navigation order" },
        { L"hidden", L"Hides element from rendering and accessibility" },
        { L"accesskey", L"Keyboard shortcut to activate or focus element" },
        { L"contenteditable", L"Allows user to edit element contents (true, false)" },
        { L"draggable", L"Enables drag-and-drop interaction (true, false)" },
        { L"spellcheck", L"Controls browser spellchecking (true, false)" },
        { L"inputmode", L"Virtual keyboard layout hint (numeric, decimal, email, tel, search)" },
        { L"enterkeyhint", L"Action label for the enter key on virtual keyboards" },
        { L"autocapitalize", L"Controls automatic text capitalization on mobile devices" },
        { L"nonce", L"Cryptographic nonce used by Content Security Policy" },
        { L"slot", L"Assigns slot name in Shadow DOM web components" },
        { L"part", L"Exposes element to CSS ::part() styling across shadow boundary" },
        { L"is", L"Custom built-in HTML element specification" },

        // Links, Navigation, Resources & Media
        { L"href", L"Target URL or destination anchor" },
        { L"src", L"Source URL of external resource or image" },
        { L"alt", L"Alternative text representation for image" },
        { L"target", L"Browsing context (_blank, _self, _parent, _top)" },
        { L"rel", L"Relationship between current and linked resource" },
        { L"download", L"Instructs browser to download linked target file" },
        { L"ping", L"URLs to ping via POST when link is clicked" },
        { L"referrerpolicy", L"Referrer information policy for HTTP requests" },
        { L"crossorigin", L"CORS request setting (anonymous, use-credentials)" },
        { L"integrity", L"Subresource Integrity cryptographic hash (e.g. sha384-...)" },
        { L"media", L"Media query specifying which medium resource applies to" },
        { L"type", L"MIME type or button/input control behavior type" },
        { L"srcset", L"Comma-separated list of image candidate URLs and pixel ratios/widths" },
        { L"sizes", L"Source sizes condition strings for responsive image selection" },
        { L"loading", L"Loading strategy (lazy, eager)" },
        { L"decoding", L"Image decoding mode (async, sync, auto)" },
        { L"fetchpriority", L"Resource fetch relative priority hint (high, low, auto)" },
        { L"poster", L"URL of preview image displayed while video is downloading" },
        { L"preload", L"Media preloading hint (auto, metadata, none)" },
        { L"autoplay", L"Automatically starts media playback when ready" },
        { L"loop", L"Causes media to restart automatically upon finishing" },
        { L"muted", L"Mutes audio playback of media stream by default" },
        { L"controls", L"Displays browser native playback UI controls" },
        { L"playsinline", L"Allows inline video playback on mobile devices without fullscreen" },
        { L"default", L"Enables track subtitle as default for media player" },
        { L"kind", L"Kind of text track (subtitles, captions, descriptions, chapters)" },
        { L"srclang", L"Language of timed text track subtitles (e.g. en, fr)" },

        // Form Controls & Inputs
        { L"name", L"Form control name sent with HTTP request" },
        { L"value", L"Default or current value of form control" },
        { L"placeholder", L"Hint text displayed when input is empty" },
        { L"disabled", L"Disables user interaction with form control" },
        { L"required", L"Specifies that input field must be filled" },
        { L"readonly", L"Specifies that input cannot be modified" },
        { L"checked", L"Pre-selects radio button or checkbox" },
        { L"selected", L"Pre-selects option in select dropdown" },
        { L"multiple", L"Allows multiple selections or file uploads" },
        { L"autofocus", L"Automatically focuses control on page load" },
        { L"autocomplete", L"Controls browser autofill behavior (on, off, email, username)" },
        { L"pattern", L"Regular expression regex pattern input value must match" },
        { L"min", L"Minimum allowable numeric, date or range value" },
        { L"max", L"Maximum allowable numeric, date or range value" },
        { L"step", L"Granularity interval for numeric or date/time stepper" },
        { L"maxlength", L"Maximum number of characters allowed in text input" },
        { L"minlength", L"Minimum number of characters required in text input" },
        { L"size", L"Visual width in characters of input or visible options in select" },
        { L"rows", L"Visible number of text lines in textarea" },
        { L"cols", L"Visible width in average character widths of textarea" },
        { L"wrap", L"Text wrapping control in textarea (soft, hard)" },
        { L"for", L"Associates label with ID of form control element" },
        { L"form", L"Associates input element with ID of parent form" },
        { L"action", L"Target URL where form data is submitted" },
        { L"method", L"HTTP method used to send form data (get, post, dialog)" },
        { L"enctype", L"MIME encoding for submitted form (application/x-www-form-urlencoded, multipart/form-data)" },
        { L"novalidate", L"Bypasses browser built-in form constraint validation on submit" },
        { L"formaction", L"Overrides form action URL on submit button click" },
        { L"formenctype", L"Overrides form encoding type on submit button click" },
        { L"formmethod", L"Overrides form HTTP method on submit button click" },
        { L"formnovalidate", L"Overrides form validation behavior on submit button click" },
        { L"formtarget", L"Overrides form browsing target context on submit button click" },
        { L"list", L"ID of datalist element providing autocomplete suggestions" },
        { L"accept", L"Comma-separated list of accepted file types (e.g. image/*, .pdf)" },
        { L"capture", L"Media capture mechanism for file input (user, environment)" },

        // Layout, Table & Sizing
        { L"width", L"Horizontal dimension in pixels or ratio" },
        { L"height", L"Vertical dimension in pixels or ratio" },
        { L"span", L"Number of columns a col element spans" },
        { L"colspan", L"Number of table columns cell spans across" },
        { L"rowspan", L"Number of table rows cell spans across" },
        { L"headers", L"IDs of table header cells associated with this cell" },
        { L"scope", L"Defines whether table header cell applies to row, col, or group" },
        { L"border", L"Specifies border width around table elements" },

        // Modern HTML5 & Interactive Components
        { L"open", L"Indicates whether details accordion or dialog modal is open" },
        { L"popover", L"Designates element as popover overlay (auto, manual)" },
        { L"popovertarget", L"ID of popover element toggled or controlled by button" },
        { L"popovertargetaction", L"Popover action triggered by button (toggle, show, hide)" },
        { L"datetime", L"Machine-readable ISO-8601 date, time or duration string" },
        { L"cite", L"URL citing source document or message for blockquote or del" },
        { L"sandbox", L"Enforces security sandbox restrictions on iframe content" },
        { L"srcdoc", L"Inline HTML document content rendered inside iframe" },
        { L"allow", L"Feature Policy / Permissions Policy permissions for iframe" },
        { L"allowfullscreen", L"Allows iframe to trigger fullscreen mode" },
        { L"reversed", L"Reverses numbering order of ordered list (ol)" },
        { L"start", L"Starting integer value of ordered list (ol)" },
        { L"optimum", L"Optimum numeric value for gauge meter element" },
        { L"low", L"Low range upper boundary for meter element" },
        { L"high", L"High range lower boundary for meter element" },

        // Event Handlers
        { L"onclick", L"Executed when mouse clicks the element" },
        { L"ondblclick", L"Executed when mouse double-clicks the element" },
        { L"onmousedown", L"Executed when mouse button is pressed down" },
        { L"onmouseup", L"Executed when mouse button is released" },
        { L"onmouseover", L"Executed when mouse moves onto the element" },
        { L"onmouseout", L"Executed when mouse moves out of the element" },
        { L"onmouseenter", L"Executed when pointer enters element boundaries" },
        { L"onmouseleave", L"Executed when pointer leaves element boundaries" },
        { L"onmousemove", L"Executed continuously as mouse moves over element" },
        { L"oncontextmenu", L"Executed when right-click context menu is triggered" },
        { L"onkeydown", L"Executed when keyboard key is depressed" },
        { L"onkeyup", L"Executed when keyboard key is released" },
        { L"onkeypress", L"Executed when printable character key is pressed" },
        { L"onfocus", L"Executed when element receives keyboard focus" },
        { L"onblur", L"Executed when element loses keyboard focus" },
        { L"onchange", L"Executed when form control value commits change" },
        { L"oninput", L"Executed immediately on user value input" },
        { L"onsubmit", L"Executed when form is submitted" },
        { L"onreset", L"Executed when form inputs are reset" },
        { L"onload", L"Executed after element finishes loading" },
        { L"onerror", L"Executed when resource fails to load" },
        { L"onscroll", L"Executed when element scrollbar moves" },
        { L"onresize", L"Executed when element or window dimensions change" },
        { L"oncopy", L"Executed when user copies content to clipboard" },
        { L"oncut", L"Executed when user cuts content to clipboard" },
        { L"onpaste", L"Executed when user pastes content from clipboard" },
        { L"ondrag", L"Executed periodically while element is being dragged" },
        { L"ondrop", L"Executed when dragged item is dropped onto element" },
        { L"ondragover", L"Executed when dragged item is hovered over drop target" },
        { L"ontouchstart", L"Executed when finger touches mobile touchscreen" },
        { L"ontouchend", L"Executed when finger releases from mobile touchscreen" },

        // WAI-ARIA Attributes & Accessibility
        { L"role", L"WAI-ARIA accessible component role (button, navigation, dialog, etc.)" },
        { L"aria-label", L"Defines string value that labels current element" },
        { L"aria-labelledby", L"Identifies element IDs that label current element" },
        { L"aria-describedby", L"Identifies element IDs that describe current element" },
        { L"aria-hidden", L"Hides element from assistive screen readers (true, false)" },
        { L"aria-expanded", L"Indicates if grouping element is expanded (true, false)" },
        { L"aria-selected", L"Indicates selection state of tab or option (true, false)" },
        { L"aria-checked", L"Indicates checked state of checkbox/radio (true, false, mixed)" },
        { L"aria-disabled", L"Indicates element is perceivable but disabled (true, false)" },
        { L"aria-current", L"Indicates current item in set (page, step, location, date, time)" },
        { L"aria-controls", L"Identifies element IDs controlled or grouped by this widget" },
        { L"aria-live", L"Indicates dynamic content region (polite, assertive, off)" },
        { L"aria-atomic", L"Presents entire live region as a whole on change (true, false)" },
        { L"aria-busy", L"Indicates element is being modified or updating (true, false)" },
        { L"aria-modal", L"Indicates window modal behavior for screen readers (true, false)" },
        { L"aria-haspopup", L"Indicates element has popup context (menu, listbox, tree, dialog)" },
        { L"aria-invalid", L"Indicates entered value does not conform to format (true, false)" },
        { L"aria-required", L"Indicates user input is required before form submission" },
        { L"aria-valuenow", L"Current numeric value for range slider or progressbar" },
        { L"aria-valuemin", L"Minimum numeric value for range widget" },
        { L"aria-valuemax", L"Maximum numeric value for range widget" },
        { L"aria-valuetext", L"Human-readable alternative text representation for value" },

        // Microdata
        { L"itemscope", L"Creates a new Microdata item scope" },
        { L"itemtype", L"Specifies URL of Schema.org vocabulary defining item" },
        { L"itemprop", L"Adds a property to a Microdata item" },
        { L"itemid", L"Global unique identifier for a Microdata item" },
        { L"itemref", L"List of element IDs outside parent providing item properties" },
        { L"data-", L"Custom data attribute prefix for arbitrary application metadata" }
    };

    for (const auto& a : attrs) {
        IntelliSenseItem item;
        item.label = a.name;
        item.detail = a.desc;
        item.insertText = std::wstring(a.name) + L"=\"$0\"";
        item.kind = CompletionKind::Attribute;
        item.origin = L"html";
        m_htmlAttributes.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulateCssProperties() {
    const auto& officialProps = WebIntelliSenseData::GetCssProperties();
    if (!officialProps.empty()) {
        m_cssProperties = officialProps;
        return;
    }
    struct PropDef { const wchar_t* name; const wchar_t* desc; };
    static const PropDef cssProps[] = {
        // Layout & Display
        { L"display", L"In combination with 'float' and 'position', determines the type of box generated for an element (block, flex, grid, inline, none)." },
        { L"position", L"Specifies how an element is positioned in a document (static, relative, absolute, fixed, sticky)." },
        { L"top", L"Participates in specifying the vertical position of a positioned element." },
        { L"right", L"Participates in specifying the horizontal position of a positioned element." },
        { L"bottom", L"Participates in specifying the bottom position of a positioned element." },
        { L"left", L"Participates in specifying the left position of a positioned element." },
        { L"inset", L"Shorthand property corresponding to the top, right, bottom, and/or left properties." },
        { L"inset-block", L"Defines the logical block start and block end offsets of an element." },
        { L"inset-inline", L"Defines the logical inline start and inline end offsets of an element." },
        { L"z-index", L"Sets the z-order of a positioned element and its descendants or flex/grid items." },
        { L"float", L"Places an element on the left or right side of its container, allowing text and inline elements to wrap around it." },
        { L"clear", L"Sets whether an element must be moved below preceding floating elements." },
        { L"visibility", L"Shows or hides an element without changing the layout of a document (visible, hidden, collapse)." },
        { L"box-sizing", L"Sets how the total width and height of an element is calculated (content-box, border-box)." },
        { L"overflow", L"Sets what happens if content overflows an element's box (visible, hidden, scroll, auto, clip)." },
        { L"overflow-x", L"Sets what happens if content overflows the left and right edges of an element's content area." },
        { L"overflow-y", L"Sets what happens if content overflows the top and bottom edges of an element's content area." },
        { L"clip-path", L"Creates a clipping region that sets what part of an element should be shown." },

        // Flexbox Layout
        { L"flex", L"Shorthand property for flex-grow, flex-shrink, and flex-basis combined." },
        { L"flex-direction", L"Specifies how flex items are placed in the flex container defining the main axis (row, column, row-reverse, column-reverse)." },
        { L"flex-wrap", L"Sets whether flex items are forced onto one line or can wrap onto multiple lines (nowrap, wrap, wrap-reverse)." },
        { L"flex-flow", L"Shorthand property for flex-direction and flex-wrap." },
        { L"justify-content", L"Defines the alignment along the main axis of the current line of the flex or grid container." },
        { L"align-items", L"Defines the default behavior for how flex items are laid out along the cross axis on the current line." },
        { L"align-content", L"Aligns a flex container's lines within the flexible container when there is extra space in the cross-axis." },
        { L"align-self", L"Overrides a flex or grid item's align-items value." },
        { L"justify-items", L"Defines the default justify-self for all items of the box, giving them the default way of justifying each box along the appropriate axis." },
        { L"justify-self", L"Sets the way a box is justified inside its alignment container along the appropriate axis." },
        { L"place-items", L"Shorthand property for align-items and justify-items combined (e.g. place-items: center)." },
        { L"place-content", L"Shorthand property for align-content and justify-content combined." },
        { L"place-self", L"Shorthand property for align-self and justify-self combined." },
        { L"flex-grow", L"Sets the flex grow factor of a flex item main size." },
        { L"flex-shrink", L"Sets the flex shrink factor of a flex item." },
        { L"flex-basis", L"Sets the initial main size of a flex item." },
        { L"order", L"Sets the order to lay out an item in a flex or grid container." },
        { L"gap", L"Sets the gaps (gutters) between rows and columns in flex and grid layouts." },
        { L"row-gap", L"Sets the size of the gap (gutter) between an element's grid rows." },
        { L"column-gap", L"Sets the size of the gap (gutter) between an element's columns." },

        // Grid Layout
        { L"grid", L"Shorthand property that sets all explicit and implicit grid properties." },
        { L"grid-template-columns", L"Defines the line names and track sizing functions of the grid columns." },
        { L"grid-template-rows", L"Defines the line names and track sizing functions of the grid rows." },
        { L"grid-template-areas", L"Specifies named grid areas, establishing the cells in the grid and assigning them names." },
        { L"grid-column", L"Shorthand property for grid-column-start and grid-column-end." },
        { L"grid-row", L"Shorthand property for grid-row-start and grid-row-end." },
        { L"grid-area", L"Shorthand for grid-row-start, grid-column-start, grid-row-end, and grid-column-end." },
        { L"grid-auto-flow", L"Controls how the auto-placement algorithm works, specifying exactly how auto-placed items get flowed into the grid." },
        { L"grid-auto-columns", L"Specifies the size of an implicitly-created grid column track or pattern of tracks." },
        { L"grid-auto-rows", L"Specifies the size of an implicitly-created grid row track or pattern of tracks." },

        // Box Model & Dimensions
        { L"width", L"Sets an element's width." },
        { L"min-width", L"Sets the minimum width of an element." },
        { L"max-width", L"Sets the maximum width of an element." },
        { L"height", L"Sets an element's height." },
        { L"min-height", L"Sets the minimum height of an element." },
        { L"max-height", L"Sets the maximum height of an element." },
        { L"aspect-ratio", L"Sets a preferred aspect ratio for the box, which will be used in the calculation of auto sizes." },
        { L"margin", L"Shorthand property sets the margin area on all four sides of an element." },
        { L"margin-top", L"Sets the margin area on the top of an element." },
        { L"margin-right", L"Sets the margin area on the right of an element." },
        { L"margin-bottom", L"Sets the margin area on the bottom of an element." },
        { L"margin-left", L"Sets the margin area on the left of an element." },
        { L"padding", L"Shorthand property sets the padding area on all four sides of an element." },
        { L"padding-top", L"Sets the padding area on the top of an element." },
        { L"padding-right", L"Sets the padding area on the right of an element." },
        { L"padding-bottom", L"Sets the padding area on the bottom of an element." },
        { L"padding-left", L"Sets the padding area on the left of an element." },

        // Colors & Backgrounds
        { L"color", L"Sets the foreground color value of an element's text and text decorations." },
        { L"background", L"Shorthand for setting background properties (color, image, position, size, repeat)." },
        { L"background-color", L"Sets the background color of an element." },
        { L"background-image", L"Sets one or more background images on an element." },
        { L"background-position", L"Sets the initial position for each background image." },
        { L"background-size", L"Sets the size of the element's background images (cover, contain, auto)." },
        { L"background-repeat", L"Sets how background images are repeated (repeat, no-repeat, repeat-x, repeat-y)." },
        { L"background-attachment", L"Sets whether a background image's position is fixed within the viewport, or scrolls with its containing block." },
        { L"background-clip", L"Sets whether an element's background extends beneath its border box, padding box, or content box." },
        { L"opacity", L"Sets the opacity of an element (0.0 fully transparent to 1.0 fully opaque)." },

        // Borders & Outlines
        { L"border", L"Shorthand property for setting border width, line style, and color." },
        { L"border-width", L"Sets the line width of an element's four borders." },
        { L"border-style", L"Sets the line style for all four sides of an element's border (solid, dashed, dotted, none)." },
        { L"border-color", L"Sets the color of an element's four borders." },
        { L"border-radius", L"Rounds the corners of an element's outer border edge." },
        { L"border-top", L"Shorthand for border-top-width, border-top-style, and border-top-color." },
        { L"border-right", L"Shorthand for border-right-width, border-right-style, and border-right-color." },
        { L"border-bottom", L"Shorthand for border-bottom-width, border-bottom-style, and border-bottom-color." },
        { L"border-left", L"Shorthand for border-left-width, border-left-style, and border-left-color." },
        { L"outline", L"Shorthand for setting outline width, style, and color in a single declaration." },
        { L"outline-offset", L"Sets the amount of space between an outline and the edge or border of an element." },
        { L"box-shadow", L"Adds shadow effects around an element's frame. You can set multiple effects separated by commas." },

        // Typography & Text
        { L"font", L"Shorthand property for font-style, font-variant, font-weight, font-size/line-height, and font-family." },
        { L"font-family", L"Specifies a prioritized list of one or more font family names and/or generic family names." },
        { L"font-size", L"Sets the size of the font." },
        { L"font-weight", L"Sets the weight (or boldness) of the font (bold, normal, 100-900)." },
        { L"font-style", L"Sets whether a font should be styled with a normal, italic, or oblique face." },
        { L"line-height", L"Sets the height of a line box. Commonly used to set distance between lines of text." },
        { L"letter-spacing", L"Sets the horizontal spacing behavior between text characters." },
        { L"word-spacing", L"Sets the spacing behavior between words." },
        { L"text-align", L"Sets the horizontal alignment of the inline-level content inside a block element." },
        { L"text-decoration", L"Sets the appearance of decorative lines on text (underline, line-through, none)." },
        { L"text-transform", L"Specifies how to capitalize an element's text (uppercase, lowercase, capitalize)." },
        { L"text-overflow", L"Sets how hidden overflow content is signaled to users (clip, ellipsis)." },
        { L"text-shadow", L"Adds shadows to text. It accepts a comma-separated list of shadows to be applied." },
        { L"white-space", L"Sets how white space inside an element is handled (nowrap, pre, pre-wrap, normal)." },
        { L"word-break", L"Sets whether line breaks appear wherever the text would otherwise overflow its content box." },

        // Transforms, Transitions & Animations
        { L"transform", L"Applies 2D or 3D transformation to an element (rotate, scale, translate, skew)." },
        { L"transform-origin", L"Sets the origin for an element's transformations." },
        { L"transition", L"Shorthand property for transition-property, transition-duration, transition-timing-function, and transition-delay." },
        { L"transition-duration", L"Sets the length of time a transition animation should take to complete." },
        { L"transition-property", L"Sets the CSS properties to which a transition effect should be applied." },
        { L"transition-timing-function", L"Sets how intermediate values are calculated for CSS properties being affected by a transition effect (ease, linear, ease-in-out)." },
        { L"animation", L"Shorthand property applies an animation between styles." },
        { L"animation-name", L"Specifies the names of one or more @keyframes at-rules describing the animation." },
        { L"animation-duration", L"Sets the length of time that an animation takes to complete one cycle." },
        { L"animation-iteration-count", L"Sets the number of times an animation sequence should be played before stopping (infinite, 1, 2)." },

        // UI & Interactivity
        { L"cursor", L"Sets the mouse cursor to show when the mouse pointer is over an element (pointer, default, not-allowed, grab)." },
        { L"pointer-events", L"Sets whether an element reacts to pointer events (auto, none)." },
        { L"user-select", L"Controls whether the user can select text (none, auto, text, all)." },
        { L"resize", L"Sets whether an element is resizable, and if so, in which directions (none, both, horizontal, vertical)." },
        { L"filter", L"Applies graphical effects like blur, brightness, contrast, drop-shadow to an element." },
        { L"backdrop-filter", L"Applies graphical effects to the area behind an element (e.g. blur(10px) for glassmorphism)." },
        { L"object-fit", L"Sets how the content of a replaced element, such as an <img>, should be resized to fit its container (cover, contain, fill)." },
        { L"object-position", L"Specifies the alignment of the selected replaced element's contents within the element's box." },
        { L"content", L"Replaces an element with a generated value (used with ::before and ::after pseudo-elements)." },
        { L"will-change", L"Hints to browsers how an element is expected to change, allowing optimizations before the element is animated." }
    };

    for (const auto& p : cssProps) {
        IntelliSenseItem item;
        item.label = p.name;
        item.detail = p.desc;
        item.insertText = std::wstring(p.name) + L": $0;";
        item.kind = CompletionKind::Property;
        item.origin = L"css";
        m_cssProperties.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulateCssValues() {
    const auto& officialVals = WebIntelliSenseData::GetCssValues();
    if (!officialVals.empty()) {
        m_cssValues = officialVals;
        const auto& pseudo = WebIntelliSenseData::GetCssPseudo();
        m_cssValues.insert(m_cssValues.end(), pseudo.begin(), pseudo.end());
        return;
    }
    struct ValDef { const wchar_t* name; const wchar_t* desc; };
    static const ValDef cssVals[] = {
        // Display values
        { L"flex", L"Displays an element as a block-level flex container" },
        { L"grid", L"Displays an element as a block-level grid container" },
        { L"block", L"Displays an element as a block element" },
        { L"inline-block", L"Displays an element as an inline-level block container" },
        { L"inline", L"Displays an element as an inline element" },
        { L"none", L"Turns off the display of an element" },
        { L"inline-flex", L"Displays an element as an inline-level flex container" },
        { L"inline-grid", L"Displays an element as an inline-level grid container" },

        // Position values
        { L"relative", L"Element is positioned according to normal flow, then offset relative to itself" },
        { L"absolute", L"Element is removed from normal flow and positioned relative to nearest positioned ancestor" },
        { L"fixed", L"Element is removed from normal flow and positioned relative to the viewport" },
        { L"sticky", L"Element is positioned based on the user's scroll position (hybrid relative/fixed)" },
        { L"static", L"Default value. Elements render in order as they appear in document flow" },

        // Flex & Grid alignment values
        { L"center", L"Centers items along the alignment axis" },
        { L"space-between", L"Items are evenly distributed; first item on start line, last on end line" },
        { L"space-around", L"Items are evenly distributed with equal space around them" },
        { L"space-evenly", L"Items are distributed so spacing between any two adjacent items is equal" },
        { L"flex-start", L"Items are packed toward the start line of the container" },
        { L"flex-end", L"Items are packed toward the end line of the container" },
        { L"stretch", L"Stretches items to fill the container along the cross axis" },
        { L"baseline", L"Items are aligned such that their baselines align" },

        // Cursor values
        { L"pointer", L"The cursor is a pointer that indicates a link" },
        { L"default", L"The platform-dependent default cursor (arrow)" },
        { L"not-allowed", L"Indicates that the requested action will not be carried out" },
        { L"grab", L"Indicates that something can be grabbed" },
        { L"grabbing", L"Indicates that something is being grabbed" },
        { L"text", L"Indicates text that can be selected (I-beam)" },
        { L"move", L"Indicates something is to be moved" },
        { L"crosshair", L"A simple crosshair cursor" },
        { L"wait", L"Indicates that the program is busy (hourglass or watch)" },

        // Box sizing & overflow
        { L"border-box", L"Width and height properties include content, padding, and border" },
        { L"content-box", L"Default. Width and height properties only include content" },
        { L"hidden", L"Content is clipped and no scrollbars are provided" },
        { L"auto", L"Browser automatically determines sizing or provides scrollbars if needed" },
        { L"scroll", L"Content is clipped and browser always displays scrollbars" },
        { L"visible", L"Default. Content is not clipped and renders outside the element's box" },

        // Object fit
        { L"cover", L"Replaced content is sized to maintain its aspect ratio while filling the element box" },
        { L"contain", L"Replaced content is scaled to maintain its aspect ratio while fitting within the box" },

        // Global CSS keywords
        { L"inherit", L"Inherits this property from its parent element" },
        { L"initial", L"Sets this property to its default initial value" },
        { L"unset", L"Resets the property to its natural value (inherited if inherited, initial if not)" },
        { L"currentColor", L"Uses the value of the color property on this element" },
        { L"transparent", L"Fully transparent color value rgba(0, 0, 0, 0)" }
    };

    for (const auto& v : cssVals) {
        IntelliSenseItem item;
        item.label = v.name;
        item.detail = v.desc;
        item.insertText = v.name;
        item.kind = CompletionKind::Keyword;
        item.origin = L"css";
        m_cssValues.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulateJavaScriptKeywords() {
    const wchar_t* keywords[] = {
        L"async", L"await", L"break", L"case", L"catch", L"class", L"const", L"continue",
        L"debugger", L"default", L"delete", L"do", L"else", L"export", L"extends",
        L"finally", L"for", L"function", L"if", L"import", L"in", L"instanceof",
        L"let", L"new", L"return", L"super", L"switch", L"this", L"throw", L"try",
        L"typeof", L"var", L"void", L"while", L"with", L"yield"
    };
    for (const auto* kw : keywords) {
        IntelliSenseItem item;
        item.label = kw;
        item.detail = L"JavaScript Keyword";
        item.insertText = kw;
        item.kind = CompletionKind::Keyword;
        item.origin = L"keyword";
        m_jsKeywords.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulateJavaScriptGlobals() {
    const auto& officialGlobals = WebIntelliSenseData::GetJavaScriptGlobals();
    if (!officialGlobals.empty()) {
        m_jsGlobals = officialGlobals;
        return;
    }
    struct GlobalDef { const wchar_t* name; const wchar_t* desc; };
    GlobalDef globals[] = {
        { L"document", L"DOM Document object interface for page structure" },
        { L"window", L"Global browser window and execution context" },
        { L"console", L"Console debugging logging utility" },
        { L"fetch", L"Promise-based asynchronous HTTP network request" },
        { L"JSON", L"JavaScript Object Notation serialization utility" },
        { L"Math", L"Mathematical constants and functions library" },
        { L"Promise", L"Asynchronous operation eventual result representation" },
        { L"Object", L"JavaScript foundational object constructor" },
        { L"Array", L"Ordered collection constructor and static methods" },
        { L"String", L"String primitive wrapper and utility methods" },
        { L"Number", L"Numeric representation and precision methods" },
        { L"Boolean", L"Boolean truth value constructor" },
        { L"Date", L"Date and time calendar timestamp constructor" },
        { L"RegExp", L"Regular expression pattern matching constructor" },
        { L"Map", L"Key-value collection supporting arbitrary keys" },
        { L"Set", L"Collection of unique primitive values or references" },
        { L"setTimeout", L"Schedules callback execution after millisecond delay" },
        { L"setInterval", L"Repeats callback execution at fixed intervals" },
        { L"clearTimeout", L"Cancels previously scheduled timeout timer" },
        { L"clearInterval", L"Cancels previously scheduled interval timer" },
        { L"localStorage", L"Persistent web storage surviving browser restart" },
        { L"sessionStorage", L"Session web storage cleared on tab close" },
        { L"navigator", L"User agent, browser state, and platform info" },
        { L"location", L"Current document URL and navigation interface" },
        { L"history", L"Browser session history navigation stack" },
        { L"URL", L"URL parsing, resolution and formatting constructor" },
        { L"URLSearchParams", L"Query string utility parser and manipulator" },
        { L"FormData", L"Constructs multipart/form-data for network upload" },
        { L"AbortController", L"Aborts asynchronous signals including fetch" },
        { L"Event", L"DOM event interface constructor" },
        { L"CustomEvent", L"User-defined synthetic DOM event with detail" },
        { L"requestAnimationFrame", L"Requests repaint callback before next screen frame" },
        { L"self", L"Current global window/worker or class instance" }
    };

    for (const auto& g : globals) {
        IntelliSenseItem item;
        item.label = g.name;
        item.detail = g.desc;
        item.insertText = g.name;
        item.kind = CompletionKind::Variable;
        item.origin = L"builtins";
        m_jsGlobals.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulateJavaScriptDomApis() {
    const auto& officialDom = WebIntelliSenseData::GetJavaScriptDomMethods();
    if (!officialDom.empty()) {
        m_jsDomApis = officialDom;
        return;
    }
    struct DomDef { const wchar_t* name; const wchar_t* signature; CompletionKind kind; };
    DomDef doms[] = {
        { L"getElementById", L"(elementId: string): HTMLElement | null", CompletionKind::Method },
        { L"querySelector", L"(selectors: string): Element | null", CompletionKind::Method },
        { L"querySelectorAll", L"(selectors: string): NodeListOf<Element>", CompletionKind::Method },
        { L"getElementsByClassName", L"(classNames: string): HTMLCollectionOf<Element>", CompletionKind::Method },
        { L"getElementsByTagName", L"(qualifiedName: string): HTMLCollectionOf<Element>", CompletionKind::Method },
        { L"createElement", L"(tagName: string): HTMLElement", CompletionKind::Method },
        { L"createTextNode", L"(data: string): Text", CompletionKind::Method },
        { L"createDocumentFragment", L"(): DocumentFragment", CompletionKind::Method },
        { L"appendChild", L"(node: Node): Node", CompletionKind::Method },
        { L"removeChild", L"(child: Node): Node", CompletionKind::Method },
        { L"replaceChild", L"(newChild: Node, oldChild: Node): Node", CompletionKind::Method },
        { L"insertBefore", L"(newNode: Node, referenceNode: Node | null): Node", CompletionKind::Method },
        { L"cloneNode", L"(deep?: boolean): Node", CompletionKind::Method },
        { L"addEventListener", L"(type: string, listener: EventListener): void", CompletionKind::Method },
        { L"removeEventListener", L"(type: string, listener: EventListener): void", CompletionKind::Method },
        { L"dispatchEvent", L"(event: Event): boolean", CompletionKind::Method },
        { L"setAttribute", L"(qualifiedName: string, value: string): void", CompletionKind::Method },
        { L"getAttribute", L"(qualifiedName: string): string | null", CompletionKind::Method },
        { L"removeAttribute", L"(qualifiedName: string): void", CompletionKind::Method },
        { L"hasAttribute", L"(qualifiedName: string): boolean", CompletionKind::Method },
        { L"getBoundingClientRect", L"(): DOMRect", CompletionKind::Method },
        { L"scrollIntoView", L"(arg?: boolean | ScrollIntoViewOptions): void", CompletionKind::Method },
        { L"focus", L"(options?: FocusOptions): void", CompletionKind::Method },
        { L"blur", L"(): void", CompletionKind::Method },
        { L"click", L"(): void", CompletionKind::Method },
        { L"innerHTML", L"string: HTML markup of element children", CompletionKind::Property },
        { L"innerText", L"string: Rendered text content of element", CompletionKind::Property },
        { L"textContent", L"string: Textual content of node and descendants", CompletionKind::Property },
        { L"classList", L"DOMTokenList: Token list of element CSS classes", CompletionKind::Property },
        { L"style", L"CSSStyleDeclaration: Inline CSS styling attributes", CompletionKind::Property },
        { L"dataset", L"DOMStringMap: Custom data-* attributes map", CompletionKind::Property },
        { L"parentElement", L"HTMLElement | null: Parent element node", CompletionKind::Property },
        { L"children", L"HTMLCollection: Child element nodes collection", CompletionKind::Property }
    };

    for (const auto& d : doms) {
        IntelliSenseItem item;
        item.label = d.name;
        item.detail = d.signature;
        item.insertText = d.name;
        item.kind = d.kind;
        item.origin = L"dom";
        m_jsDomApis.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulateJavaScriptPrototypeMethods() {
    const auto& officialProto = WebIntelliSenseData::GetJavaScriptPrototypeMethods();
    if (!officialProto.empty()) {
        m_jsPrototypes = officialProto;
        return;
    }
    struct ProtoDef { const wchar_t* name; const wchar_t* sig; };
    ProtoDef protos[] = {
        // Array methods
        { L"map", L"<U>(callback: (value: T, index: number) => U): U[]" },
        { L"filter", L"(predicate: (value: T, index: number) => boolean): T[]" },
        { L"reduce", L"<U>(callback: (acc: U, cur: T) => U, initialValue: U): U" },
        { L"forEach", L"(callback: (value: T, index: number) => void): void" },
        { L"find", L"(predicate: (value: T) => boolean): T | undefined" },
        { L"findIndex", L"(predicate: (value: T) => boolean): number" },
        { L"includes", L"(searchElement: T, fromIndex?: number): boolean" },
        { L"indexOf", L"(searchElement: T, fromIndex?: number): number" },
        { L"push", L"(...items: T[]): number" },
        { L"pop", L"(): T | undefined" },
        { L"shift", L"(): T | undefined" },
        { L"unshift", L"(...items: T[]): number" },
        { L"splice", L"(start: number, deleteCount?: number, ...items: T[]): T[]" },
        { L"slice", L"(start?: number, end?: number): T[]" },
        { L"concat", L"(...items: ConcatArray<T>[]): T[]" },
        { L"join", L"(separator?: string): string" },
        { L"sort", L"(compareFn?: (a: T, b: T) => number): this" },
        { L"reverse", L"(): this" },
        { L"some", L"(predicate: (value: T) => boolean): boolean" },
        { L"every", L"(predicate: (value: T) => boolean): boolean" },
        { L"flat", L"<A>(depth?: number): A[]" },
        { L"flatMap", L"<U>(callback: (value: T) => U[]): U[]" },
        // String methods
        { L"split", L"(separator: string | RegExp, limit?: number): string[]" },
        { L"trim", L"(): string" },
        { L"trimStart", L"(): string" },
        { L"trimEnd", L"(): string" },
        { L"toLowerCase", L"(): string" },
        { L"toUpperCase", L"(): string" },
        { L"startsWith", L"(searchString: string, position?: number): boolean" },
        { L"endsWith", L"(searchString: string, endPosition?: number): boolean" },
        { L"replace", L"(searchValue: string | RegExp, replaceValue: string): string" },
        { L"replaceAll", L"(searchValue: string | RegExp, replaceValue: string): string" },
        { L"substring", L"(start: number, end?: number): string" },
        { L"charAt", L"(pos: number): string" },
        { L"charCodeAt", L"(index: number): number" },
        { L"padStart", L"(targetLength: number, padString?: string): string" },
        { L"padEnd", L"(targetLength: number, padString?: string): string" },
        { L"match", L"(matcher: RegExp): RegExpMatchArray | null" },
        // Promise methods
        { L"then", L"<TResult>(onfulfilled: (value: T) => TResult): Promise<TResult>" },
        { L"catch", L"<TResult>(onrejected: (reason: any) => TResult): Promise<TResult>" },
        { L"finally", L"(onfinally?: () => void): Promise<T>" },
        // JSON methods
        { L"stringify", L"(value: any, replacer?: any, space?: any): string" },
        { L"parse", L"(text: string, reviver?: any): any" },
        // Console methods
        { L"log", L"(...data: any[]): void" },
        { L"error", L"(...data: any[]): void" },
        { L"warn", L"(...data: any[]): void" },
        { L"info", L"(...data: any[]): void" },
        { L"debug", L"(...data: any[]): void" },
        { L"table", L"(tabularData?: any, properties?: string[]): void" }
    };

    for (const auto& p : protos) {
        IntelliSenseItem item;
        item.label = p.name;
        item.detail = p.sig;
        item.insertText = p.name;
        item.kind = CompletionKind::Method;
        item.origin = L"prototype";
        m_jsPrototypes.push_back(std::move(item));
    }
}

void IntelliSenseEngine::PopulatePhpVocabulary() {
    m_phpSymbols.clear();
    PopulateBuiltInPhpIntelliSense(m_phpSymbols);
}

void IntelliSenseEngine::PopulatePythonVocabulary() {
    m_pySymbols.clear();
    const auto& globalPy = GetPythonGlobalSymbols();
    m_pySymbols.reserve(globalPy.size());
    for (const auto& item : globalPy) {
        m_pySymbols.push_back(item);
    }
}

void IntelliSenseEngine::PopulateReactAndNodeVocabulary() {
    m_reactAndNodeVocabulary.clear();
    const auto& reactSyms = GetReactGlobalSymbols();
    m_reactAndNodeVocabulary.reserve(reactSyms.size() + 50);
    for (const auto& item : reactSyms) {
        m_reactAndNodeVocabulary.push_back(item);
    }

    struct ItemDef { const wchar_t* name; const wchar_t* desc; CompletionKind kind; };
    static const ItemDef items[] = {
        // Vite & Node.js Vocabulary
        { L"import.meta.env", L"Vite environment variables container (VITE_APP_*, MODE, DEV, PROD)", CompletionKind::Variable },
        { L"import.meta.glob", L"import.meta.glob<T>(pattern: string): Record<string, () => Promise<T>> (Vite module globber)", CompletionKind::Function },
        { L"defineConfig", L"defineConfig(config: UserConfig | Promise<UserConfig>): UserConfig (Vite config helper)", CompletionKind::Function },
        { L"loadEnv", L"loadEnv(mode: string, envDir: string, prefixes?: string | string[]): Record<string, string>", CompletionKind::Function },
        { L"process.env", L"NodeJS.ProcessEnv: Global process environment variables object", CompletionKind::Variable },
        { L"require", L"(id: string) => any: Synchronous CommonJS module loader function", CompletionKind::Function },
        { L"module.exports", L"any: CommonJS module exports object", CompletionKind::Property },
        { L"exports", L"any: CommonJS exports shorthand reference", CompletionKind::Variable },
        { L"__dirname", L"string: Directory path of currently executing CommonJS module", CompletionKind::Variable },
        { L"__filename", L"string: Absolute file path of currently executing CommonJS module", CompletionKind::Variable },
        { L"Buffer", L"class Buffer: Node.js binary octet stream buffer class", CompletionKind::Class },
        { L"path", L"Node.js path module for working with file and directory paths", CompletionKind::Module },
        { L"fs", L"Node.js file system module for reading and writing files", CompletionKind::Module },
        { L"http", L"Node.js HTTP server and client network module", CompletionKind::Module },
        { L"express", L"express(): Express: Fast, unopinionated Node.js web application framework", CompletionKind::Function }
    };

    for (const auto& item : items) {
        IntelliSenseItem it;
        it.label = item.name;
        it.detail = item.desc;
        it.insertText = item.name;
        it.kind = item.kind;
        it.origin = L"react-node";
        it.score = 250;
        m_reactAndNodeVocabulary.push_back(std::move(it));
    }
}

void IntelliSenseEngine::PopulateCommonVocabulary() {
    struct WordDef { const wchar_t* word; const wchar_t* desc; CompletionKind kind; };
    WordDef words[] = {
        // Common web & programming vocabulary (requested by user: "hello", "log", "header", "href", etc.)
        { L"hello", L"Greeting / sample greeting string", CompletionKind::Variable },
        { L"help", L"Help documentation / usage assistant", CompletionKind::Function },
        { L"header", L"Page or section header block / HTTP headers", CompletionKind::Tag },
        { L"heading", L"Document heading typography style", CompletionKind::Property },
        { L"height", L"Vertical dimension in pixels or viewport units", CompletionKind::Property },
        { L"hidden", L"Specifies that element is hidden from view", CompletionKind::Attribute },
        { L"href", L"Target URL or destination hyperlink", CompletionKind::Attribute },
        { L"hover", L"Hover interaction pseudo-class or state", CompletionKind::Property },
        { L"handle", L"Event handler or resource handle reference", CompletionKind::Function },
        { L"handler", L"Callback function invoked upon event trigger", CompletionKind::Function },
        { L"helper", L"Utility helper function or class", CompletionKind::Function },
        { L"host", L"Host name or server address", CompletionKind::Variable },
        { L"hostname", L"Domain host name without port", CompletionKind::Property },
        { L"html", L"HyperText Markup Language root document", CompletionKind::Tag },
        { L"http", L"Hypertext Transfer Protocol client or URL", CompletionKind::Variable },
        { L"https", L"Secure Hypertext Transfer Protocol", CompletionKind::Variable },

        { L"login", L"Authentication login operation or form", CompletionKind::Function },
        { L"logout", L"De-authentication session termination", CompletionKind::Function },
        { L"layout", L"CSS grid or flexbox layout container", CompletionKind::Property },
        { L"load", L"Loads resource, module, or document", CompletionKind::Function },
        { L"loader", L"Resource loader or loading spinner component", CompletionKind::Variable },
        { L"loading", L"Specifies lazy or eager resource loading", CompletionKind::Attribute },
        { L"local", L"Local scope variable or storage access", CompletionKind::Variable },
        { L"location", L"Window location URL interface", CompletionKind::Variable },
        { L"localStorage", L"Persistent web browser key-value storage", CompletionKind::Variable },
        { L"loop", L"Loop iteration or continuous media playback", CompletionKind::Keyword },
        { L"length", L"Count of elements in array or string", CompletionKind::Property },
        { L"log", L"Outputs message or debugging info to console", CompletionKind::Method },

        { L"data", L"Arbitrary data payload or attributes", CompletionKind::Variable },
        { L"date", L"Calendar date representation", CompletionKind::Variable },
        { L"default", L"Default fallback value or case", CompletionKind::Keyword },
        { L"define", L"Defines new symbol, macro, or module", CompletionKind::Keyword },
        { L"delete", L"Deletes property or removes record", CompletionKind::Keyword },
        { L"description", L"Detailed textual description or metadata", CompletionKind::Property },
        { L"detail", L"Event detail object or expanded view", CompletionKind::Property },
        { L"details", L"Disclosure widget container for details", CompletionKind::Tag },
        { L"dialog", L"Modal or non-modal popup dialog box", CompletionKind::Tag },
        { L"direction", L"Layout flow direction (row, column)", CompletionKind::Property },
        { L"disabled", L"Disables user interaction on element", CompletionKind::Attribute },
        { L"display", L"CSS display layout mode (flex, grid, block)", CompletionKind::Property },
        { L"document", L"DOM document global object", CompletionKind::Variable },

        { L"title", L"Document or element title tooltip", CompletionKind::Property },
        { L"type", L"Type definition or element MIME/input type", CompletionKind::Property },
        { L"text", L"Textual content or text alignment property", CompletionKind::Property },
        { L"target", L"Event target or hyperlink target window", CompletionKind::Property },
        { L"value", L"Current form control or object property value", CompletionKind::Property },
        { L"name", L"Identifier or form control submission name", CompletionKind::Property },
        { L"id", L"Unique element identifier attribute", CompletionKind::Attribute },
        { L"class", L"CSS class name attribute or OOP class definition", CompletionKind::Keyword },
        { L"style", L"Inline CSS styling attribute or rule", CompletionKind::Attribute },
        { L"src", L"Media resource source URI attribute", CompletionKind::Attribute },
        { L"alt", L"Alternative text for images and media", CompletionKind::Attribute },
        { L"width", L"Horizontal dimension in pixels or viewport units", CompletionKind::Property },
        { L"color", L"Foreground typography color property", CompletionKind::Property },
        { L"background", L"Background styling, color, or image property", CompletionKind::Property },
        { L"border", L"Element border thickness, style, and color", CompletionKind::Property },
        { L"padding", L"Inner element spacing between content and border", CompletionKind::Property },
        { L"margin", L"Outer element spacing between border and adjacent", CompletionKind::Property },
        { L"font", L"Font family, size, weight, and style shorthand", CompletionKind::Property },
        { L"size", L"Dimension size or form control character width", CompletionKind::Property },
        { L"action", L"Form submission endpoint or dispatch action", CompletionKind::Property },
        { L"method", L"HTTP request method (GET, POST) or function", CompletionKind::Property },
        { L"success", L"Success status indicator or callback handler", CompletionKind::Variable },
        { L"error", L"Error object or failure status callback", CompletionKind::Variable },
        { L"warning", L"Warning notification or alert message", CompletionKind::Variable },
        { L"info", L"Informational payload or logging level", CompletionKind::Variable },
        { L"result", L"Execution output or computation result", CompletionKind::Variable },
        { L"response", L"HTTP network response object", CompletionKind::Variable },
        { L"request", L"HTTP network request configuration or stream", CompletionKind::Variable },
        { L"status", L"HTTP response code or operational state", CompletionKind::Property },
        { L"message", L"Diagnostic or user-facing notification string", CompletionKind::Property },
        { L"index", L"Numeric positional index or entry file name", CompletionKind::Property },
        { L"item", L"Individual element in list, array, or set", CompletionKind::Variable },
        { L"items", L"Collection of entries or flex child items", CompletionKind::Variable },
        { L"key", L"Unique dictionary lookup key or React map prop", CompletionKind::Property },
        { L"keys", L"List of dictionary or object properties", CompletionKind::Function },
        { L"values", L"List of dictionary or object property values", CompletionKind::Function },
        { L"entries", L"Key-value pairs iterator for array or map", CompletionKind::Function },
        { L"count", L"Total quantity or tally of matching elements", CompletionKind::Property },
        { L"total", L"Aggregate sum or cumulative count metric", CompletionKind::Property },
        { L"active", L"Current active state or selection flag", CompletionKind::Property },
        { L"enabled", L"Feature enablement toggle or status", CompletionKind::Property },
        { L"visible", L"Visibility flag or rendering toggle", CompletionKind::Property },
        { L"content", L"Inner element content or metadata payload", CompletionKind::Property },
        { L"container", L"Parent container component or layout wrapper", CompletionKind::Variable },
        { L"wrapper", L"Outer wrapper element for layout grouping", CompletionKind::Variable },
        { L"button", L"Interactive button element or role", CompletionKind::Tag },
        { L"input", L"Interactive user input form control", CompletionKind::Tag },
        { L"form", L"Interactive HTML form container", CompletionKind::Tag },
        { L"image", L"Graphic image element or asset object", CompletionKind::Tag },
        { L"video", L"Video playback element or media stream", CompletionKind::Tag },
        { L"audio", L"Audio sound playback element or asset", CompletionKind::Tag },
        { L"canvas", L"2D/3D immediate-mode bitmap drawing surface", CompletionKind::Tag },
        { L"svg", L"Scalable Vector Graphics container", CompletionKind::Tag },
        { L"path", L"File system path string or SVG vector path", CompletionKind::Property },
        { L"file", L"File descriptor or uploaded binary payload", CompletionKind::Variable },
        { L"folder", L"Directory or storage folder descriptor", CompletionKind::Variable },
        { L"directory", L"File system directory container", CompletionKind::Variable },
        { L"url", L"Uniform Resource Locator string", CompletionKind::Property },
        { L"uri", L"Uniform Resource Identifier string", CompletionKind::Property },
        { L"query", L"Search query string or database selector", CompletionKind::Property },
        { L"filter", L"Filtering predicate function or CSS effect", CompletionKind::Function },
        { L"sort", L"Sorting comparator function or ordering rule", CompletionKind::Function },
        { L"order", L"Flexbox or grid display ordering index", CompletionKind::Property },
        { L"user", L"User profile or session account entity", CompletionKind::Variable },
        { L"config", L"Configuration options dictionary or instance", CompletionKind::Variable },
        { L"options", L"Optional parameter flags or dropdown choices", CompletionKind::Variable },
        { L"settings", L"Application or component preference settings", CompletionKind::Variable },
        { L"context", L"Execution context or state provider wrapper", CompletionKind::Variable },
        { L"props", L"Component input properties dictionary", CompletionKind::Variable },
        { L"state", L"Component reactive state container", CompletionKind::Variable },
        { L"update", L"Updates entity state or refreshes database", CompletionKind::Function },
        { L"create", L"Instantiates new object or resource record", CompletionKind::Function },
        { L"remove", L"Removes entity or deletes node from DOM", CompletionKind::Function },
        { L"clear", L"Clears collection or resets canvas buffer", CompletionKind::Function },
        { L"reset", L"Restores defaults or resets form inputs", CompletionKind::Function },
        { L"init", L"Initializes subsystem or lifecycle hook", CompletionKind::Function },
        { L"render", L"Paints visual elements or outputs HTML markup", CompletionKind::Function },
        { L"refresh", L"Reloads data or triggers layout recalculation", CompletionKind::Function },
        { L"close", L"Closes modal, dialog, or network socket", CompletionKind::Function },
        { L"open", L"Opens modal, dialog, or network stream", CompletionKind::Function },
        { L"start", L"Initiates background task or timer loop", CompletionKind::Function },
        { L"stop", L"Halts execution or cancels running process", CompletionKind::Function },

        // Network, Database, Web Server, and PHP vocabulary (Image 3 request: "when i type port theres no recommended auto complete")
        { L"port", L"Network listening port number (e.g. 80, 8080, 3306)", CompletionKind::Variable },
        { L"host", L"Host name or server address (e.g. localhost, 127.0.0.1)", CompletionKind::Variable },
        { L"hostname", L"Domain host name or server address without port", CompletionKind::Property },
        { L"database", L"Database name or connection schema", CompletionKind::Variable },
        { L"dbname", L"Database name parameter or connection string", CompletionKind::Variable },
        { L"db", L"Database handle or instance reference", CompletionKind::Variable },
        { L"username", L"User login name or authentication credential", CompletionKind::Variable },
        { L"password", L"User authentication secret or credential", CompletionKind::Variable },
        { L"pass", L"Password credential parameter or variable", CompletionKind::Variable },
        { L"user", L"User profile or session account entity", CompletionKind::Variable },
        { L"charset", L"Character encoding set (e.g. utf8mb4, utf8)", CompletionKind::Property },
        { L"pdo", L"PHP Data Objects database abstraction layer", CompletionKind::Variable },
        { L"mysqli", L"MySQL Improved Extension client instance", CompletionKind::Variable },
        { L"conn", L"Database connection handle or socket", CompletionKind::Variable },
        { L"connection", L"Active client-server database connection stream", CompletionKind::Variable },
        { L"stmt", L"Prepared SQL statement handle or cursor", CompletionKind::Variable },
        { L"statement", L"Prepared SQL query or execution statement", CompletionKind::Variable },
        { L"table", L"Relational database table entity", CompletionKind::Variable },
        { L"column", L"Relational database column field", CompletionKind::Property },
        { L"row", L"Single record row in database result set", CompletionKind::Variable },
        { L"rows", L"Collection of record rows retrieved from database", CompletionKind::Variable },
        { L"execute", L"Executes prepared query or SQL statement", CompletionKind::Method },
        { L"prepare", L"Prepares parameterized SQL query statement", CompletionKind::Method },
        { L"fetch", L"Fetches next record row from result set cursor", CompletionKind::Method },
        { L"fetchAll", L"Fetches all remaining rows in query result set", CompletionKind::Method },
        { L"fetch_assoc", L"Fetches result row as associative array", CompletionKind::Method },
        { L"fetch_array", L"Fetches result row as indexed or associative array", CompletionKind::Method },
        { L"rowCount", L"Returns number of rows affected or retrieved", CompletionKind::Method },
        { L"connect", L"Establishes network or database connection", CompletionKind::Function },
        { L"disconnect", L"Closes active network or database connection", CompletionKind::Function },
        { L"query", L"Executes raw SQL query statement or CSS selector", CompletionKind::Method },
        { L"server", L"Web or application server instance / daemon", CompletionKind::Variable },
        { L"nginx", L"High-performance Nginx HTTP web server reverse proxy", CompletionKind::Variable },
        { L"apache", L"Apache HTTP Server daemon", CompletionKind::Variable },
        { L"mysql", L"MySQL relational database management system", CompletionKind::Variable },
        { L"mariadb", L"MariaDB community database management server", CompletionKind::Variable },
        { L"phpmyadmin", L"Web-based MySQL and MariaDB administration tool", CompletionKind::Variable },
        { L"localhost", L"Local loopback network address (127.0.0.1)", CompletionKind::Variable },
        { L"socket", L"Network socket or UNIX domain socket descriptor", CompletionKind::Variable },
        { L"endpoint", L"API or service network endpoint URI", CompletionKind::Property },
        { L"session", L"User stateful session instance or cookie store", CompletionKind::Variable },
        { L"cookie", L"HTTP cookie header or browser state token", CompletionKind::Property },
        { L"token", L"Authentication bearer token or security credential", CompletionKind::Variable },
        { L"params", L"URL query parameters or routing arguments", CompletionKind::Variable },
        { L"route", L"Application URL route definition or handler", CompletionKind::Variable },
        { L"router", L"HTTP routing dispatcher or gateway", CompletionKind::Variable },
        { L"middleware", L"HTTP request-response pipeline middleware interceptor", CompletionKind::Function },
        { L"controller", L"Application MVC controller business logic class", CompletionKind::Variable },
        { L"model", L"Application data model or ORM entity", CompletionKind::Variable },
        { L"view", L"Application UI view template or presentation component", CompletionKind::Variable },
        { L"service", L"Background or business logic service provider", CompletionKind::Variable },
        { L"repository", L"Data access repository pattern interface", CompletionKind::Variable },
        { L"entity", L"Persistent database domain model entity", CompletionKind::Variable },
        { L"schema", L"Database or validation data schema definition", CompletionKind::Variable },
        { L"migration", L"Database structure migration version script", CompletionKind::Variable },
        { L"env", L"Environment variable configuration map (.env)", CompletionKind::Variable },
        { L"dotenv", L"Loads environment variables from .env file", CompletionKind::Function },

        // PHP Superglobals (Image 3 request: "say its varuiable")
        { L"$_GET", L"PHP Superglobal: HTTP GET query parameters array", CompletionKind::Variable },
        { L"$_POST", L"PHP Superglobal: HTTP POST form data array", CompletionKind::Variable },
        { L"$_SESSION", L"PHP Superglobal: Current session variables array", CompletionKind::Variable },
        { L"$_COOKIE", L"PHP Superglobal: HTTP cookies array", CompletionKind::Variable },
        { L"$_SERVER", L"PHP Superglobal: Web server and execution environment headers", CompletionKind::Variable },
        { L"$_REQUEST", L"PHP Superglobal: Combined GET, POST, and COOKIE input data", CompletionKind::Variable },
        { L"$_FILES", L"PHP Superglobal: HTTP file upload items array", CompletionKind::Variable },
        { L"$_ENV", L"PHP Superglobal: Environment variables array", CompletionKind::Variable },
        { L"$GLOBALS", L"PHP Superglobal: References all global variables", CompletionKind::Variable },

        // PHP Built-in Functions & Keywords
        { L"echo", L"Outputs one or more strings to the output buffer", CompletionKind::Keyword },
        { L"print", L"Outputs a string expression", CompletionKind::Keyword },
        { L"var_dump", L"Dumps structured information about a variable", CompletionKind::Function },
        { L"print_r", L"Prints human-readable information about a variable", CompletionKind::Function },
        { L"die", L"Equivalent to exit(); prints message and terminates script", CompletionKind::Function },
        { L"exit", L"Terminates current script execution with status code", CompletionKind::Function },
        { L"isset", L"Determines if a variable is declared and different from null", CompletionKind::Function },
        { L"empty", L"Determines whether a variable is empty", CompletionKind::Function },
        { L"unset", L"Destroys specified variables", CompletionKind::Function },
        { L"header", L"Sends a raw HTTP header to client", CompletionKind::Function },
        { L"json_encode", L"Returns JSON representation of a value", CompletionKind::Function },
        { L"json_decode", L"Decodes a JSON string into PHP object/array", CompletionKind::Function },
        { L"include_once", L"Includes and evaluates specified file once during execution", CompletionKind::Keyword },
        { L"require_once", L"Requires specified file once; produces fatal error if not found", CompletionKind::Keyword },
        { L"session_start", L"Initializes or resumes an existing PHP session", CompletionKind::Function }
    };

    m_commonVocabulary.reserve(sizeof(words) / sizeof(words[0]));
    for (const auto& w : words) {
        IntelliSenseItem item;
        item.label = w.word;
        item.detail = w.desc;
        item.insertText = w.word;
        item.kind = w.kind;
        item.score = 250;
        switch (w.kind) {
            case CompletionKind::Tag: item.origin = L"html"; break;
            case CompletionKind::Attribute: item.origin = L"html"; break;
            case CompletionKind::Property: item.origin = L"css"; break;
            case CompletionKind::Method:
            case CompletionKind::Function: item.origin = L"builtins"; break;
            case CompletionKind::Variable: item.origin = L"builtins"; break;
            case CompletionKind::Snippet: item.origin = L"snippets"; break;
            default: item.origin = L"keyword"; break;
        }
        m_commonVocabulary.push_back(std::move(item));
    }
}

std::wstring IntelliSenseEngine::ExtractEmmetPrefix(const std::wstring& line, size_t cursorCol) {
    return EmmetEngine::ExtractAbbreviation(line, cursorCol, false, nullptr);
}

std::wstring IntelliSenseEngine::ExpandEmmet(const std::wstring& abbr) {
    return EmmetEngine::Expand(abbr, L"    ", L"", false, nullptr);
}

std::wstring IntelliSenseEngine::ExtractQueryPrefix(const std::wstring& line, size_t cursorCol) {
    if (line.empty() || cursorCol == 0) return L"";
    size_t col = (std::min)(cursorCol, line.length());
    size_t start = col;
    while (start > 0) {
        wchar_t c = line[start - 1];
        if (c == L':') {
            // Stop at '::' (PHP/C++ scope resolution operator) so "PDO::ATTR_ERR" extracts "ATTR_ERR"
            if ((start >= 2 && line[start - 2] == L':') || (start < line.length() && line[start] == L':')) {
                break;
            }
            start--;
        } else if (c == L'.') {
            // Allow decimal point inside numeric CSS values (e.g. "1.5" or "1.5rem"), but stop at member access "."
            if (start >= 2 && iswdigit(line[start - 2])) {
                start--;
            } else {
                break;
            }
        } else if (iswalnum(c) || c == L'_' || c == L'$' || c == L'-' || c == L'!' || c == L'%') {
            start--;
        } else {
            break;
        }
    }
    return line.substr(start, col - start);
}

std::vector<IntelliSenseItem> IntelliSenseEngine::ExtractDocumentSymbols(
    const std::vector<std::wstring>& docLines,
    const std::wstring& fileExt
) const {
    std::vector<IntelliSenseItem> results;
    std::unordered_set<std::wstring> seenVars;

    auto trim = [](const std::wstring& s) -> std::wstring {
        size_t start = 0;
        while (start < s.length() && iswspace(s[start])) start++;
        size_t end = s.length();
        while (end > start && iswspace(s[end - 1])) end--;
        return s.substr(start, end - start);
    };

    auto isIdentChar = [](wchar_t c) {
        return iswalnum(c) || c == L'_';
    };

    auto inferTypeFromVal = [](const std::wstring& val) -> std::wstring {
        if (val.empty()) return L"";
        if (val[0] == L'"' || val[0] == L'\'') return L"string";
        if (val == L"true" || val == L"false" || val == L"TRUE" || val == L"FALSE") return L"bool";
        if (val[0] == L'[' || val.rfind(L"array(", 0) == 0) return L"array";
        if (val[0] == L'{') return L"object";
        if (val.rfind(L"new ", 0) == 0) {
            size_t p = 4;
            while (p < val.length() && iswspace(val[p])) p++;
            size_t q = p;
            while (q < val.length() && (iswalnum(val[q]) || val[q] == L'_' || val[q] == L'\\')) q++;
            if (q > p) return val.substr(p, q - p);
            return L"object";
        }
        if (val.find(L"->prepare(") != std::wstring::npos || val.find(L"->query(") != std::wstring::npos) {
            return L"PDOStatement";
        }
        if (val.find(L"->fetchAll(") != std::wstring::npos || val.find(L"->fetch(") != std::wstring::npos) {
            return L"array";
        }
        if (val.rfind(L"trim(", 0) == 0 || val.rfind(L"htmlspecialchars(", 0) == 0 ||
            val.rfind(L"strtolower(", 0) == 0 || val.rfind(L"strtoupper(", 0) == 0 ||
            val.rfind(L"substr(", 0) == 0 || val.rfind(L"str_replace(", 0) == 0) {
            return L"string";
        }
        if (val.rfind(L"count(", 0) == 0 || val.rfind(L"intval(", 0) == 0 || val.rfind(L"strlen(", 0) == 0) {
            return L"int";
        }
        size_t startIdx = (val[0] == L'-' || val[0] == L'+') ? 1 : 0;
        if (startIdx < val.length() && iswdigit(val[startIdx])) {
            if (val.find(L'.') != std::wstring::npos) return L"float";
            return L"int";
        }
        return L"";
    };

    for (const auto& line : docLines) {
        std::wstring trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (trimmed.rfind(L"//", 0) == 0 || trimmed.rfind(L"#", 0) == 0 || trimmed.rfind(L"/*", 0) == 0 || trimmed.rfind(L"*", 0) == 0) {
            continue;
        }

        // 1. PHP style variables: $port = 3306; or catch (PDOException $e)
        size_t pDollar = 0;
        while ((pDollar = line.find(L'$', pDollar)) != std::wstring::npos) {
            size_t pStart = pDollar + 1;
            if (pStart < line.length() && (iswalpha(line[pStart]) || line[pStart] == L'_')) {
                size_t pEnd = pStart;
                while (pEnd < line.length() && isIdentChar(line[pEnd])) {
                    pEnd++;
                }
                std::wstring varName = line.substr(pStart, pEnd - pStart);
                std::wstring fullVar = L"$" + varName;

                // Skip superglobals so we don't duplicate them with generic "Variable" detail
                if (fullVar == L"$_GET" || fullVar == L"$_POST" || fullVar == L"$_REQUEST" ||
                    fullVar == L"$_SESSION" || fullVar == L"$_COOKIE" || fullVar == L"$_SERVER" ||
                    fullVar == L"$_FILES" || fullVar == L"$_ENV" || fullVar == L"$GLOBALS" || fullVar == L"$this") {
                    pDollar = pEnd;
                    continue;
                }

                std::wstring inferredType;
                size_t pEq = line.find(L'=', pEnd);
                if (pEq != std::wstring::npos) {
                    bool isAssign = true;
                    if (pEq + 1 < line.length() && (line[pEq + 1] == L'=' || line[pEq + 1] == L'>')) isAssign = false;
                    if (pEq > 0 && (line[pEq - 1] == L'=' || line[pEq - 1] == L'!' || line[pEq - 1] == L'<' || line[pEq - 1] == L'>')) isAssign = false;
                    if (isAssign) {
                        std::wstring rightSide = trim(line.substr(pEq + 1));
                        if (!rightSide.empty() && rightSide.back() == L';') rightSide.pop_back();
                        rightSide = trim(rightSide);
                        inferredType = inferTypeFromVal(rightSide);
                    }
                } else if (pDollar >= 2) {
                    // Check for typed parameter or catch (PDOException $e)
                    size_t tEnd = pDollar;
                    while (tEnd > 0 && iswspace(line[tEnd - 1])) tEnd--;
                    size_t tStart = tEnd;
                    while (tStart > 0 && (iswalnum(line[tStart - 1]) || line[tStart - 1] == L'_' || line[tStart - 1] == L'\\' || line[tStart - 1] == L'?')) tStart--;
                    if (tEnd > tStart) {
                        std::wstring candidateType = line.substr(tStart, tEnd - tStart);
                        if (candidateType != L"as" && candidateType != L"echo" && candidateType != L"print" &&
                            candidateType != L"return" && candidateType != L"global" && candidateType != L"static" &&
                            candidateType != L"new" && candidateType != L"throw" && candidateType != L"unset") {
                            inferredType = candidateType;
                        }
                    }
                }

                std::wstring detailStr = inferredType.empty() ? L"Variable" : (L"Variable (" + inferredType + L")");

                if (seenVars.insert(fullVar).second) {
                    IntelliSenseItem item;
                    item.label = fullVar;
                    item.detail = detailStr;
                    item.insertText = fullVar;
                    item.kind = CompletionKind::Variable;
                    item.score = 650;
                    item.origin = L"variable";
                    results.push_back(std::move(item));
                } else if (!inferredType.empty()) {
                    // Upgrade existing variable entry if we later found a concrete type
                    for (auto& existing : results) {
                        if (existing.label == fullVar && existing.detail == L"Variable") {
                            existing.detail = detailStr;
                            break;
                        }
                    }
                }
                if (fileExt != L".php" && seenVars.insert(varName).second) {
                    IntelliSenseItem item;
                    item.label = varName;
                    item.detail = detailStr;
                    item.insertText = varName;
                    item.kind = CompletionKind::Variable;
                    item.score = 540;
                    item.origin = L"variable";
                    results.push_back(std::move(item));
                }
                pDollar = pEnd;
            } else {
                pDollar++;
            }
        }

        // 1b. CSS Custom Properties: --var-name: #hex;
        size_t pDash = 0;
        while ((pDash = line.find(L"--", pDash)) != std::wstring::npos) {
            size_t pStart = pDash;
            size_t pEnd = pStart + 2;
            while (pEnd < line.length() && (iswalnum(line[pEnd]) || line[pEnd] == L'-' || line[pEnd] == L'_')) {
                pEnd++;
            }
            if (pEnd > pStart + 2) {
                std::wstring varName = line.substr(pStart, pEnd - pStart);
                if (seenVars.insert(varName).second) {
                    IntelliSenseItem item;
                    item.label = varName;
                    item.detail = L"CSS Custom Property";
                    item.insertText = varName;
                    item.kind = CompletionKind::Variable;
                    item.score = 550;
                    item.origin = L"css-var";
                    results.push_back(std::move(item));
                }
            }
            pDash = pEnd;
        }

        // 2. JS/TS declarations: const port = 3000; or let port = ...
        static const std::vector<std::wstring> jsVarKws = { L"const", L"let", L"var" };
        for (const auto& kw : jsVarKws) {
            if (trimmed.rfind(kw + L" ", 0) == 0) {
                size_t pStart = kw.length();
                while (pStart < trimmed.length() && iswspace(trimmed[pStart])) pStart++;
                if (pStart < trimmed.length() && (iswalpha(trimmed[pStart]) || trimmed[pStart] == L'_')) {
                    size_t pEnd = pStart;
                    while (pEnd < trimmed.length() && isIdentChar(trimmed[pEnd])) pEnd++;
                    std::wstring varName = trimmed.substr(pStart, pEnd - pStart);

                    std::wstring inferredType;
                    size_t pColon = trimmed.find(L':', pEnd);
                    size_t pEq = trimmed.find(L'=', pEnd);
                    if (pColon != std::wstring::npos && (pEq == std::wstring::npos || pColon < pEq)) {
                        size_t tStart = pColon + 1;
                        while (tStart < trimmed.length() && iswspace(trimmed[tStart])) tStart++;
                        size_t tEnd = (pEq != std::wstring::npos) ? pEq : trimmed.length();
                        inferredType = trim(trimmed.substr(tStart, tEnd - tStart));
                        if (!inferredType.empty() && inferredType.back() == L';') inferredType.pop_back();
                        inferredType = trim(inferredType);
                    } else if (pEq != std::wstring::npos) {
                        std::wstring rightSide = trim(trimmed.substr(pEq + 1));
                        if (!rightSide.empty() && rightSide.back() == L';') rightSide.pop_back();
                        rightSide = trim(rightSide);
                        inferredType = inferTypeFromVal(rightSide);
                    }

                    std::wstring detailStr = inferredType.empty() ? L"Variable" : (L"Variable (" + inferredType + L")");
                    if (seenVars.insert(varName).second) {
                        IntelliSenseItem item;
                        item.label = varName;
                        item.detail = detailStr;
                        item.insertText = varName;
                        item.kind = CompletionKind::Variable;
                        item.score = 550;
                        item.origin = L"variable";
                        results.push_back(std::move(item));
                    }
                }
                break;
            }
        }

        // 3. Typed language declarations (C, C++, Java, C#):
        static const std::vector<std::wstring> cTypes = {
            L"int", L"float", L"double", L"bool", L"char", L"string", L"std::string",
            L"auto", L"size_t", L"uint32_t", L"int32_t", L"uint64_t", L"int64_t",
            L"long", L"short", L"unsigned", L"void*", L"DWORD", L"HANDLE", L"UINT", L"BOOL"
        };
        for (const auto& cType : cTypes) {
            std::wstring prefix = cType + L" ";
            std::wstring constPrefix = L"const " + prefix;
            size_t pTypeStart = std::wstring::npos;
            if (trimmed.rfind(constPrefix, 0) == 0) {
                pTypeStart = constPrefix.length();
            } else if (trimmed.rfind(prefix, 0) == 0) {
                pTypeStart = prefix.length();
            }

            if (pTypeStart != std::wstring::npos) {
                while (pTypeStart < trimmed.length() && iswspace(trimmed[pTypeStart])) pTypeStart++;
                if (pTypeStart < trimmed.length() && (iswalpha(trimmed[pTypeStart]) || trimmed[pTypeStart] == L'_')) {
                    size_t pEnd = pTypeStart;
                    while (pEnd < trimmed.length() && isIdentChar(trimmed[pEnd])) pEnd++;
                    std::wstring varName = trimmed.substr(pTypeStart, pEnd - pTypeStart);

                    std::wstring detailStr = L"Variable (" + cType + L")";
                    if (seenVars.insert(varName).second) {
                        IntelliSenseItem item;
                        item.label = varName;
                        item.detail = detailStr;
                        item.insertText = varName;
                        item.kind = CompletionKind::Variable;
                        item.score = 550;
                        item.origin = L"variable";
                        results.push_back(std::move(item));
                    }
                }
                break;
            }
        }

        // 4. Python / Generic assignment: port = 8080
        size_t eqPos = trimmed.find(L'=');
        if (eqPos != std::wstring::npos && eqPos > 0) {
            bool isAssign = true;
            if (eqPos + 1 < trimmed.length() && (trimmed[eqPos + 1] == L'=' || trimmed[eqPos + 1] == L'>')) isAssign = false;
            if (trimmed[eqPos - 1] == L'=' || trimmed[eqPos - 1] == L'!' || trimmed[eqPos - 1] == L'<' || trimmed[eqPos - 1] == L'>' || trimmed[eqPos - 1] == L'+' || trimmed[eqPos - 1] == L'-') isAssign = false;
            if (isAssign) {
                std::wstring leftSide = trim(trimmed.substr(0, eqPos));
                bool validIdent = !leftSide.empty() && (iswalpha(leftSide[0]) || leftSide[0] == L'_');
                for (size_t i = 1; i < leftSide.length(); ++i) {
                    if (!isIdentChar(leftSide[i])) { validIdent = false; break; }
                }
                if (validIdent && leftSide != L"if" && leftSide != L"while" && leftSide != L"return" && leftSide != L"case") {
                    std::wstring rightSide = trim(trimmed.substr(eqPos + 1));
                    if (!rightSide.empty() && rightSide.back() == L';') rightSide.pop_back();
                    rightSide = trim(rightSide);
                    std::wstring inferred = inferTypeFromVal(rightSide);
                    std::wstring detailStr = inferred.empty() ? L"Variable" : (L"Variable (" + inferred + L")");
                    if (seenVars.insert(leftSide).second) {
                        IntelliSenseItem item;
                        item.label = leftSide;
                        item.detail = detailStr;
                        item.insertText = leftSide;
                        item.kind = CompletionKind::Variable;
                        item.score = 550;
                        item.origin = L"variable";
                        results.push_back(std::move(item));
                    }
                }
            }
        }
    }

    return results;
}

namespace {

struct NpmPackageCacheEntry {
    std::filesystem::file_time_type pkgJsonMtime{};
    std::filesystem::file_time_type nodeModulesMtime{};
    std::vector<std::pair<std::wstring, std::wstring>> packages; // { name, version/detail }
};

struct ModuleExportsCacheEntry {
    std::filesystem::file_time_type fileMtime{};
    std::vector<IntelliSenseItem> exports;
};

static std::unordered_map<std::wstring, NpmPackageCacheEntry> g_npmPackageCache;
static std::unordered_map<std::wstring, ModuleExportsCacheEntry> g_moduleExportsCache;

static std::wstring TrimWs(const std::wstring& s) {
    size_t start = 0;
    while (start < s.length() && iswspace(s[start])) start++;
    size_t end = s.length();
    while (end > start && iswspace(s[end - 1])) end--;
    return s.substr(start, end - start);
}

static bool IsJsIdentChar(wchar_t c) {
    return iswalnum(c) || c == L'_' || c == L'$';
}

static bool IsValidJsIdent(const std::wstring& s) {
    if (s.empty()) return false;
    if (!(iswalpha(s[0]) || s[0] == L'_' || s[0] == L'$')) return false;
    for (size_t i = 1; i < s.length(); ++i) {
        if (!IsJsIdentChar(s[i])) return false;
    }
    return true;
}

static std::filesystem::path ResolveBaseDir(const std::wstring& fileName, const std::wstring& workspaceRoot) {
    std::error_code ec;
    if (!fileName.empty()) {
        std::filesystem::path docPath(fileName);
        if (std::filesystem::exists(docPath, ec) && !std::filesystem::is_directory(docPath, ec)) {
            return docPath.parent_path();
        } else if (docPath.has_parent_path() && std::filesystem::exists(docPath.parent_path(), ec)) {
            return docPath.parent_path();
        }
    }
    if (!workspaceRoot.empty() && std::filesystem::exists(workspaceRoot, ec)) {
        return std::filesystem::path(workspaceRoot);
    }
    return std::filesystem::current_path(ec);
}

static bool IsPathInsideRoot(const std::filesystem::path& path, const std::filesystem::path& root) {
    if (root.empty() || path.empty()) return false;
    std::error_code ec;
    auto normRoot = std::filesystem::weakly_canonical(root, ec);
    auto normPath = std::filesystem::weakly_canonical(path, ec);
    if (normRoot.empty() || normPath.empty()) {
        normRoot = root.lexically_normal();
        normPath = path.lexically_normal();
    }
    auto itRoot = normRoot.begin();
    auto itPath = normPath.begin();
    for (; itRoot != normRoot.end(); ++itRoot, ++itPath) {
        if (itRoot->empty() || *itRoot == L".") continue;
        if (itPath == normPath.end()) return false;
        std::wstring a = itRoot->wstring();
        std::wstring b = itPath->wstring();
        for (auto& c : a) c = static_cast<wchar_t>(towlower(c));
        for (auto& c : b) c = static_cast<wchar_t>(towlower(c));
        if (a != b) return false;
    }
    return true;
}

static bool HasProjectMarker(const std::filesystem::path& dir, std::error_code& ec) {
    return std::filesystem::exists(dir / L"package.json", ec) ||
           std::filesystem::exists(dir / L"node_modules", ec) ||
           std::filesystem::exists(dir / L"composer.json", ec) ||
           std::filesystem::exists(dir / L"pyproject.toml", ec) ||
           std::filesystem::exists(dir / L".venv", ec);
}

static std::filesystem::path FindProjectRoot(const std::filesystem::path& baseDir, const std::wstring& workspaceRoot) {
    std::error_code ec;
    std::filesystem::path wsPath = workspaceRoot;
    bool hasWs = !wsPath.empty() && std::filesystem::exists(wsPath, ec);
    if (hasWs && !baseDir.empty() && IsPathInsideRoot(baseDir, wsPath)) {
        std::filesystem::path cur = baseDir;
        for (int depth = 0; depth < 7 && !cur.empty(); ++depth) {
            if (HasProjectMarker(cur, ec)) {
                return cur;
            }
            if (std::filesystem::equivalent(cur, wsPath, ec)) {
                break;
            }
            auto parent = cur.parent_path();
            if (parent == cur || !IsPathInsideRoot(parent, wsPath)) break;
            cur = parent;
        }
        return wsPath;
    }
    // For loose files or empty workspaceRoot, check only baseDir directly without
    // climbing up into ancestor directories (e.g. %USERPROFILE%\package.json).
    if (!baseDir.empty() && HasProjectMarker(baseDir, ec)) {
        return baseDir;
    }
    if (hasWs) {
        return wsPath;
    }
    return baseDir;
}

static std::string ReadFileUtf8Bounded(const std::filesystem::path& p, size_t maxBytes = 2500000) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return {};
    ifs.seekg(0, std::ios::end);
    std::streamoff sz = ifs.tellg();
    if (sz <= 0) return {};
    size_t toRead = static_cast<size_t>((std::min)(static_cast<std::streamoff>(maxBytes), sz));
    std::string buf(toRead, '\0');
    ifs.seekg(0, std::ios::beg);
    ifs.read(buf.data(), static_cast<std::streamsize>(toRead));
    return buf;
}

static std::wstring Utf8ToWideAsciiFast(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (unsigned char c : s) {
        if (c < 128) w.push_back(static_cast<wchar_t>(c));
    }
    return w;
}

// Dynamically discovers all installed NPM packages from package.json and node_modules/
static const std::vector<std::pair<std::wstring, std::wstring>>& GetInstalledNpmPackages(const std::filesystem::path& projectRoot) {
    std::error_code ec;
    std::wstring key = projectRoot.wstring();
    auto pkgJsonPath = projectRoot / L"package.json";
    auto nodeModulesPath = projectRoot / L"node_modules";

    auto pkgMtime = std::filesystem::exists(pkgJsonPath, ec) ? std::filesystem::last_write_time(pkgJsonPath, ec) : std::filesystem::file_time_type{};
    auto nmMtime = std::filesystem::exists(nodeModulesPath, ec) ? std::filesystem::last_write_time(nodeModulesPath, ec) : std::filesystem::file_time_type{};

    auto it = g_npmPackageCache.find(key);
    if (it != g_npmPackageCache.end() && it->second.pkgJsonMtime == pkgMtime && it->second.nodeModulesMtime == nmMtime) {
        return it->second.packages;
    }

    NpmPackageCacheEntry entry;
    entry.pkgJsonMtime = pkgMtime;
    entry.nodeModulesMtime = nmMtime;
    std::unordered_map<std::wstring, std::wstring> foundMap;

    // 1. Parse dependencies, devDependencies, peerDependencies from package.json
    if (std::filesystem::exists(pkgJsonPath, ec)) {
        std::wstring content = Utf8ToWideAsciiFast(ReadFileUtf8Bounded(pkgJsonPath, 262144));
        static const wchar_t* sections[] = { L"\"dependencies\"", L"\"devDependencies\"", L"\"peerDependencies\"" };
        for (const auto* sec : sections) {
            size_t secPos = content.find(sec);
            if (secPos == std::wstring::npos) continue;
            size_t openBrace = content.find(L'{', secPos);
            if (openBrace == std::wstring::npos) continue;
            size_t closeBrace = content.find(L'}', openBrace);
            if (closeBrace == std::wstring::npos) continue;
            std::wstring block = content.substr(openBrace + 1, closeBrace - openBrace - 1);
            size_t pos = 0;
            while (pos < block.length()) {
                size_t q1 = block.find(L'"', pos);
                if (q1 == std::wstring::npos) break;
                size_t q2 = block.find(L'"', q1 + 1);
                if (q2 == std::wstring::npos) break;
                std::wstring pkgName = block.substr(q1 + 1, q2 - q1 - 1);
                size_t colon = block.find(L':', q2 + 1);
                if (colon == std::wstring::npos) break;
                size_t vq1 = block.find(L'"', colon + 1);
                if (vq1 == std::wstring::npos) break;
                size_t vq2 = block.find(L'"', vq1 + 1);
                if (vq2 == std::wstring::npos) break;
                std::wstring ver = block.substr(vq1 + 1, vq2 - vq1 - 1);
                if (!pkgName.empty() && pkgName.rfind(L"@types/", 0) != 0) {
                    foundMap[pkgName] = L"NPM Package (" + ver + L")";
                }
                pos = vq2 + 1;
            }
        }
    }

    // 2. Scan node_modules/ directory for any installed packages (including scoped @org/pkg)
    if (std::filesystem::exists(nodeModulesPath, ec) && std::filesystem::is_directory(nodeModulesPath, ec)) {
        for (std::filesystem::directory_iterator dit(nodeModulesPath, ec); !ec && dit != std::filesystem::directory_iterator(); dit.increment(ec)) {
            if (!dit->is_directory(ec)) continue;
            std::wstring dname = dit->path().filename().wstring();
            if (dname.empty() || dname[0] == L'.') continue;
            if (dname == L"@types") {
                for (std::filesystem::directory_iterator tit(dit->path(), ec); !ec && tit != std::filesystem::directory_iterator(); tit.increment(ec)) {
                    if (!tit->is_directory(ec)) continue;
                    std::wstring tname = tit->path().filename().wstring();
                    if (tname.empty() || tname[0] == L'.') continue;
                    size_t dunder = tname.find(L"__");
                    if (dunder != std::wstring::npos) {
                        tname = L"@" + tname.substr(0, dunder) + L"/" + tname.substr(dunder + 2);
                    }
                    if (foundMap.find(tname) == foundMap.end()) {
                        foundMap[tname] = L"NPM Package (@types)";
                    }
                }
            } else if (dname[0] == L'@') {
                for (std::filesystem::directory_iterator sit(dit->path(), ec); !ec && sit != std::filesystem::directory_iterator(); sit.increment(ec)) {
                    if (!sit->is_directory(ec)) continue;
                    std::wstring sname = sit->path().filename().wstring();
                    if (sname.empty() || sname[0] == L'.') continue;
                    std::wstring scoped = dname + L"/" + sname;
                    if (foundMap.find(scoped) == foundMap.end()) {
                        foundMap[scoped] = L"NPM Package (node_modules)";
                    }
                }
            } else {
                if (foundMap.find(dname) == foundMap.end()) {
                    foundMap[dname] = L"NPM Package (node_modules)";
                }
            }
        }
    }

    for (auto& kv : foundMap) {
        entry.packages.push_back(std::move(kv));
    }
    std::sort(entry.packages.begin(), entry.packages.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto& stored = g_npmPackageCache[key];
    stored = std::move(entry);
    return stored.packages;
}

// Extracts JSON string property value for key (e.g. "types", "typings", "module", "main")
static std::wstring ExtractJsonFieldValue(const std::wstring& json, const std::wstring& fieldName) {
    std::wstring needle = L"\"" + fieldName + L"\"";
    size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::wstring::npos) {
        size_t colon = json.find(L':', pos + needle.length());
        if (colon != std::wstring::npos) {
            size_t p = colon + 1;
            while (p < json.length() && iswspace(json[p])) p++;
            if (p < json.length() && json[p] == L'"') {
                size_t qEnd = json.find(L'"', p + 1);
                if (qEnd != std::wstring::npos) {
                    return json.substr(p + 1, qEnd - p - 1);
                }
            }
        }
        pos += needle.length();
    }
    return L"";
}

// Resolves a module specifier (relative path "./foo" or NPM package "lucide-react") to its .d.ts or .js/.jsx/.ts/.tsx entry file
static std::filesystem::path ResolveModuleEntryFile(
    const std::wstring& moduleSpec,
    const std::filesystem::path& baseDir,
    const std::filesystem::path& projectRoot
) {
    std::error_code ec;
    if (moduleSpec.empty()) return {};

    auto tryResolveFileWithExts = [&](const std::filesystem::path& target) -> std::filesystem::path {
        if (std::filesystem::exists(target, ec) && std::filesystem::is_regular_file(target, ec)) {
            return target;
        }
        static const wchar_t* exts[] = { L".d.ts", L".ts", L".tsx", L".js", L".jsx", L".mjs", L".cjs" };
        for (const auto* ext : exts) {
            std::filesystem::path cand = target;
            cand += ext;
            if (std::filesystem::exists(cand, ec) && std::filesystem::is_regular_file(cand, ec)) {
                return cand;
            }
        }
        if (std::filesystem::exists(target, ec) && std::filesystem::is_directory(target, ec)) {
            static const wchar_t* idxFiles[] = {
                L"index.d.ts", L"index.ts", L"index.tsx", L"index.js", L"index.jsx", L"index.mjs"
            };
            for (const auto* idx : idxFiles) {
                auto cand = target / idx;
                if (std::filesystem::exists(cand, ec) && std::filesystem::is_regular_file(cand, ec)) {
                    return cand;
                }
            }
        }
        return {};
    };

    // Case A: Relative or absolute file path in workspace (./foo, ../bar, /baz)
    if (moduleSpec[0] == L'.' || moduleSpec[0] == L'/') {
        std::filesystem::path relTarget = (moduleSpec[0] == L'/')
            ? (projectRoot / moduleSpec.substr(1))
            : (baseDir / moduleSpec);
        return tryResolveFileWithExts(relTarget.lexically_normal());
    }

    // Case B: NPM package in node_modules/ (or @types/)
    std::vector<std::filesystem::path> pkgDirs;
    std::filesystem::path cur = baseDir;
    for (int d = 0; d < 7 && !cur.empty(); ++d) {
        auto nm = cur / L"node_modules";
        if (std::filesystem::exists(nm, ec)) {
            pkgDirs.push_back(nm / moduleSpec);
            std::wstring typesName = moduleSpec;
            if (!typesName.empty() && typesName[0] == L'@') {
                size_t slash = typesName.find(L'/');
                if (slash != std::wstring::npos) {
                    typesName = typesName.substr(1, slash - 1) + L"__" + typesName.substr(slash + 1);
                }
            }
            pkgDirs.push_back(nm / L"@types" / typesName);
        }
        auto parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
    if (!projectRoot.empty()) {
        pkgDirs.push_back(projectRoot / L"node_modules" / moduleSpec);
        std::wstring typesName = moduleSpec;
        if (!typesName.empty() && typesName[0] == L'@') {
            size_t slash = typesName.find(L'/');
            if (slash != std::wstring::npos) {
                typesName = typesName.substr(1, slash - 1) + L"__" + typesName.substr(slash + 1);
            }
        }
        pkgDirs.push_back(projectRoot / L"node_modules" / L"@types" / typesName);
    }

    for (const auto& pkgDir : pkgDirs) {
        if (!std::filesystem::exists(pkgDir, ec)) continue;
        if (std::filesystem::is_regular_file(pkgDir, ec)) return pkgDir;

        auto pkgJson = pkgDir / L"package.json";
        if (std::filesystem::exists(pkgJson, ec)) {
            std::wstring json = Utf8ToWideAsciiFast(ReadFileUtf8Bounded(pkgJson, 131072));

            // 1. Check "types" and "typings"
            for (const auto* f : { L"types", L"typings" }) {
                std::wstring val = ExtractJsonFieldValue(json, f);
                if (!val.empty()) {
                    auto resolved = tryResolveFileWithExts((pkgDir / val).lexically_normal());
                    if (!resolved.empty()) return resolved;
                }
            }

            // 2. Scan for any ".d.ts" referenced in package.json (e.g. exports["."].types = "./dist/lucide-react.d.ts")
            size_t dtsPos = json.find(L".d.ts\"");
            if (dtsPos == std::wstring::npos) dtsPos = json.find(L".d.mts\"");
            if (dtsPos != std::wstring::npos) {
                size_t qStart = json.rfind(L'"', dtsPos);
                size_t qEnd = json.find(L'"', dtsPos);
                if (qStart != std::wstring::npos && qEnd != std::wstring::npos && qEnd > qStart) {
                    std::wstring relDts = json.substr(qStart + 1, qEnd - qStart - 1);
                    auto resolved = tryResolveFileWithExts((pkgDir / relDts).lexically_normal());
                    if (!resolved.empty()) return resolved;
                }
            }

            // 3. Check index.d.ts or dist/*.d.ts before falling back to JS bundle
            std::wstring shortName = pkgDir.filename().wstring();
            for (const auto& candRel : {
                std::wstring(L"index.d.ts"),
                L"dist/" + shortName + L".d.ts",
                std::wstring(L"dist/index.d.ts"),
                std::wstring(L"dist/esm/index.d.ts"),
                std::wstring(L"dist/cjs/index.d.ts")
            }) {
                auto cand = pkgDir / candRel;
                if (std::filesystem::exists(cand, ec) && std::filesystem::is_regular_file(cand, ec)) {
                    return cand;
                }
            }

            // 4. Fallback to "module" or "main" JS/MJS entry
            for (const auto* f : { L"module", L"main" }) {
                std::wstring val = ExtractJsonFieldValue(json, f);
                if (!val.empty()) {
                    auto resolved = tryResolveFileWithExts((pkgDir / val).lexically_normal());
                    if (!resolved.empty()) return resolved;
                }
            }
        }

        auto resolvedIdx = tryResolveFileWithExts(pkgDir);
        if (!resolvedIdx.empty()) return resolvedIdx;
    }

    return {};
}

// Dynamically parses all exported symbols (components, icons, functions, classes, types, constants) from a .d.ts / .js / .jsx / .ts / .tsx file
static std::vector<IntelliSenseItem> ParseModuleExportsFromFile(
    const std::filesystem::path& entryFile,
    const std::wstring& moduleLabel,
    const std::filesystem::path& projectRoot,
    int depth = 0
) {
    std::error_code ec;
    if (entryFile.empty() || !std::filesystem::exists(entryFile, ec) || depth > 2) return {};

    std::wstring cacheKey = entryFile.wstring();
    auto mtime = std::filesystem::last_write_time(entryFile, ec);
    if (depth == 0) {
        auto it = g_moduleExportsCache.find(cacheKey);
        if (it != g_moduleExportsCache.end() && it->second.fileMtime == mtime) {
            return it->second.exports;
        }
    }

    std::string rawU8 = ReadFileUtf8Bounded(entryFile, 2500000);
    if (rawU8.empty()) return {};

    // Strip block comments /* ... */ and line comments // ... while preserving newlines
    std::wstring clean;
    clean.reserve(rawU8.size());
    for (size_t i = 0; i < rawU8.size(); ++i) {
        char c = rawU8[i];
        if (c == '/' && i + 1 < rawU8.size() && rawU8[i + 1] == '*') {
            i += 2;
            while (i + 1 < rawU8.size() && !(rawU8[i] == '*' && rawU8[i + 1] == '/')) {
                if (rawU8[i] == '\n') clean.push_back(L'\n');
                i++;
            }
            i++; // skip '/'
        } else if (c == '/' && i + 1 < rawU8.size() && rawU8[i + 1] == '/') {
            i += 2;
            while (i < rawU8.size() && rawU8[i] != '\n') i++;
            clean.push_back(L'\n');
        } else if (static_cast<unsigned char>(c) < 128) {
            clean.push_back(static_cast<wchar_t>(c));
        }
    }

    // Map of top-level declared identifier -> { CompletionKind, detailHint }
    std::unordered_map<std::wstring, std::pair<CompletionKind, std::wstring>> declKinds;
    std::vector<IntelliSenseItem> exports;
    std::unordered_set<std::wstring> seenExports;

    auto inferKindForIdent = [&](const std::wstring& ident, CompletionKind fallbackKind) -> std::pair<CompletionKind, std::wstring> {
        auto it = declKinds.find(ident);
        if (it != declKinds.end()) return it->second;
        if (!ident.empty() && iswupper(ident[0])) {
            return { CompletionKind::Class, L"Component / Type from '" + moduleLabel + L"'" };
        }
        return { fallbackKind, L"Export from '" + moduleLabel + L"'" };
    };

    auto addExport = [&](const std::wstring& ident, CompletionKind kind, const std::wstring& detailOverride = L"") {
        if (!IsValidJsIdent(ident) || ident == L"default" || ident == L"type" || ident == L"from") return;
        if (!seenExports.insert(ident).second) return;
        auto [resolvedKind, resolvedDetail] = inferKindForIdent(ident, kind);
        IntelliSenseItem item;
        item.label = ident;
        item.detail = detailOverride.empty() ? resolvedDetail : detailOverride;
        item.insertText = ident;
        item.kind = resolvedKind;
        item.score = (!ident.empty() && iswupper(ident[0])) ? 950 : 900;
        item.origin = L"module";
        item.documentation = L"auto-import:" + moduleLabel;
        exports.push_back(std::move(item));
    };

    // Pass 1: Line-by-line scan for declarations, direct exports, and export * from '...'
    std::vector<std::wstring> wildcardReexports;
    size_t lineStart = 0;
    while (lineStart < clean.length()) {
        size_t lineEnd = clean.find(L'\n', lineStart);
        if (lineEnd == std::wstring::npos) lineEnd = clean.length();
        std::wstring line = TrimWs(clean.substr(lineStart, lineEnd - lineStart));
        lineStart = lineEnd + 1;
        if (line.empty()) continue;

        bool isExported = false;
        if (line.rfind(L"export ", 0) == 0) {
            isExported = true;
            line = TrimWs(line.substr(7));
            if (line.rfind(L"default ", 0) == 0) {
                line = TrimWs(line.substr(8));
            }
        }
        if (line.rfind(L"declare ", 0) == 0) {
            line = TrimWs(line.substr(8));
        }

        // Check declaration keywords
        struct DeclRule { const wchar_t* kw; CompletionKind kind; const wchar_t* label; };
        static const DeclRule rules[] = {
            { L"function ",   CompletionKind::Function, L"Function" },
            { L"function*",  CompletionKind::Function, L"Generator" },
            { L"class ",      CompletionKind::Class,    L"Class" },
            { L"interface ",  CompletionKind::Class,    L"Interface" },
            { L"type ",       CompletionKind::Class,    L"Type" },
            { L"enum ",       CompletionKind::Class,    L"Enum" },
            { L"const enum ", CompletionKind::Class,    L"Enum" },
            { L"const ",      CompletionKind::Variable, L"Const" },
            { L"let ",        CompletionKind::Variable, L"Variable" },
            { L"var ",        CompletionKind::Variable, L"Variable" }
        };

        for (const auto& r : rules) {
            size_t kwLen = std::wcslen(r.kw);
            if (line.rfind(r.kw, 0) == 0) {
                size_t p = kwLen;
                while (p < line.length() && iswspace(line[p])) p++;
                size_t idEnd = p;
                while (idEnd < line.length() && IsJsIdentChar(line[idEnd])) idEnd++;
                if (idEnd > p) {
                    std::wstring ident = line.substr(p, idEnd - p);
                    CompletionKind k = r.kind;
                    std::wstring desc = std::wstring(r.label) + L" from '" + moduleLabel + L"'";
                    if (k == CompletionKind::Variable) {
                        if (line.find(L"ForwardRefExoticComponent") != std::wstring::npos ||
                            line.find(L"React.FC") != std::wstring::npos ||
                            line.find(L"ComponentType") != std::wstring::npos ||
                            line.find(L"LucideIcon") != std::wstring::npos ||
                            (!ident.empty() && iswupper(ident[0]))) {
                            k = CompletionKind::Class;
                            desc = L"Component from '" + moduleLabel + L"'";
                        } else if (line.find(L"=>") != std::wstring::npos || line.find(L": (") != std::wstring::npos) {
                            k = CompletionKind::Function;
                            desc = L"Function from '" + moduleLabel + L"'";
                        }
                    }
                    declKinds[ident] = { k, desc };
                    if (isExported) {
                        addExport(ident, k, desc);
                    }
                }
                break;
            }
        }

        // Check `export * from './...'`
        if (isExported && line.rfind(L"*", 0) == 0) {
            size_t fromPos = line.find(L"from");
            if (fromPos != std::wstring::npos) {
                size_t q1 = line.find_first_of(L"\"'", fromPos + 4);
                if (q1 != std::wstring::npos) {
                    wchar_t qCh = line[q1];
                    size_t q2 = line.find(qCh, q1 + 1);
                    if (q2 != std::wstring::npos && q2 > q1 + 1) {
                        wildcardReexports.push_back(line.substr(q1 + 1, q2 - q1 - 1));
                    }
                }
            }
        }
    }

    // Pass 2: Scan multi-line `export { ... }` and `export type { ... }` blocks across the entire file
    size_t searchPos = 0;
    while ((searchPos = clean.find(L"export", searchPos)) != std::wstring::npos) {
        if (searchPos > 0 && IsJsIdentChar(clean[searchPos - 1])) {
            searchPos += 6;
            continue;
        }
        size_t p = searchPos + 6;
        while (p < clean.length() && iswspace(clean[p])) p++;
        bool isTypeExport = false;
        if (clean.compare(p, 4, L"type") == 0 && p + 4 < clean.length() && (iswspace(clean[p + 4]) || clean[p + 4] == L'{')) {
            isTypeExport = true;
            p += 4;
            while (p < clean.length() && iswspace(clean[p])) p++;
        }
        if (p < clean.length() && clean[p] == L'{') {
            size_t closeBrace = clean.find(L'}', p + 1);
            if (closeBrace != std::wstring::npos) {
                std::wstring inside = clean.substr(p + 1, closeBrace - p - 1);
                size_t itemStart = 0;
                while (itemStart < inside.length()) {
                    size_t comma = inside.find(L',', itemStart);
                    if (comma == std::wstring::npos) comma = inside.length();
                    std::wstring token = TrimWs(inside.substr(itemStart, comma - itemStart));
                    itemStart = comma + 1;
                    if (token.empty()) continue;

                    if (token.rfind(L"type ", 0) == 0) {
                        token = TrimWs(token.substr(5));
                    }

                    std::wstring origName = token;
                    std::wstring exportedName = token;
                    size_t asPos = token.find(L" as ");
                    if (asPos == std::wstring::npos) asPos = token.find(L"\tas ");
                    if (asPos != std::wstring::npos) {
                        origName = TrimWs(token.substr(0, asPos));
                        exportedName = TrimWs(token.substr(asPos + 4));
                    }

                    if (exportedName == L"default") {
                        if (IsValidJsIdent(origName) && origName != L"default") {
                            addExport(origName, isTypeExport ? CompletionKind::Class : CompletionKind::Function);
                        }
                    } else if (IsValidJsIdent(exportedName)) {
                        CompletionKind k = isTypeExport ? CompletionKind::Class : CompletionKind::Function;
                        std::wstring detailStr;
                        auto origIt = declKinds.find(origName);
                        if (origIt != declKinds.end()) {
                            k = origIt->second.first;
                            detailStr = origIt->second.second;
                        } else if (!exportedName.empty() && iswupper(exportedName[0])) {
                            k = CompletionKind::Class;
                            detailStr = L"Component / Icon from '" + moduleLabel + L"'";
                        }
                        addExport(exportedName, k, detailStr);
                    }
                }
                searchPos = closeBrace + 1;
                continue;
            }
        }
        searchPos += 6;
    }

    // Pass 3: Follow `export * from './...'` re-exports (up to 40 submodules)
    if (depth < 2 && !wildcardReexports.empty()) {
        std::filesystem::path entryDir = entryFile.parent_path();
        size_t followed = 0;
        for (const auto& subSpec : wildcardReexports) {
            if (++followed > 40) break;
            auto subFile = ResolveModuleEntryFile(subSpec, entryDir, projectRoot);
            if (!subFile.empty() && subFile != entryFile) {
                auto subItems = ParseModuleExportsFromFile(subFile, moduleLabel, projectRoot, depth + 1);
                for (auto& si : subItems) {
                    if (seenExports.insert(si.label).second) {
                        exports.push_back(std::move(si));
                    }
                }
            }
        }
    }

    if (depth == 0) {
        ModuleExportsCacheEntry ce;
        ce.fileMtime = mtime;
        ce.exports = exports;
        g_moduleExportsCache[cacheKey] = std::move(ce);
    }
    return exports;
}

static void ProvideIdCompletions(
    std::vector<IntelliSenseItem>& outCandidates,
    const std::vector<std::wstring>* pDocLines,
    const std::filesystem::path& baseDir,
    const std::filesystem::path& projectRoot
) {
    std::unordered_set<std::wstring> seenIds;

    auto addId = [&](const std::wstring& idStr, const std::wstring& detail, int score = 1600) {
        if (idStr.empty()) return;
        if (seenIds.insert(idStr).second) {
            IntelliSenseItem it;
            it.label = idStr;
            it.detail = detail;
            it.insertText = idStr;
            it.kind = CompletionKind::Variable;
            it.origin = L"html-id";
            it.score = score;
            outCandidates.push_back(std::move(it));
        }
    };

    // 1. Scan current document lines for existing id="..." or id='...' declarations and CSS #id selectors
    if (pDocLines) {
        for (const auto& line : *pDocLines) {
            // Find id="..." or id='...'
            size_t idPos = 0;
            while ((idPos = line.find(L"id=", idPos)) != std::wstring::npos) {
                size_t qPos = idPos + 3;
                while (qPos < line.length() && iswspace(line[qPos])) qPos++;
                if (qPos < line.length() && (line[qPos] == L'"' || line[qPos] == L'\'')) {
                    wchar_t qChar = line[qPos];
                    size_t qEnd = line.find(qChar, qPos + 1);
                    if (qEnd != std::wstring::npos && qEnd > qPos + 1) {
                        std::wstring idVal = line.substr(qPos + 1, qEnd - qPos - 1);
                        addId(idVal, L"Element ID (from document)", 1700);
                    }
                }
                idPos += 3;
            }

            // Find #ident CSS selectors
            size_t hashPos = 0;
            while ((hashPos = line.find(L'#', hashPos)) != std::wstring::npos) {
                bool isSelectorStart = (hashPos == 0 || iswspace(line[hashPos - 1]) || line[hashPos - 1] == L',' || line[hashPos - 1] == L'>' || line[hashPos - 1] == L'+' || line[hashPos - 1] == L'~');
                if (isSelectorStart && hashPos + 1 < line.length() && (iswalpha(line[hashPos + 1]) || line[hashPos + 1] == L'_')) {
                    size_t idEnd = hashPos + 1;
                    while (idEnd < line.length() && (iswalnum(line[idEnd]) || line[idEnd] == L'-' || line[idEnd] == L'_')) idEnd++;
                    std::wstring idVal = line.substr(hashPos + 1, idEnd - hashPos - 1);
                    addId(idVal, L"CSS ID Selector (#" + idVal + L")", 1800);
                    hashPos = idEnd;
                    continue;
                }
                hashPos++;
            }
        }
    }

    // 2. Scan peer .css files in baseDir or projectRoot for CSS #id selectors
    std::error_code ec;
    std::vector<std::filesystem::path> searchDirs;
    if (!baseDir.empty() && std::filesystem::exists(baseDir, ec)) searchDirs.push_back(baseDir);
    if (!projectRoot.empty() && projectRoot != baseDir && std::filesystem::exists(projectRoot, ec)) searchDirs.push_back(projectRoot);

    for (const auto& sDir : searchDirs) {
        int scanned = 0;
        int visited = 0;
        for (std::filesystem::directory_iterator it(sDir, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::directory_iterator() && scanned < 10 && visited < 60;
             it.increment(ec)) {
            ++visited;
            if (!it->is_regular_file(ec)) continue;
            auto p = it->path();
            if (p.extension() != L".css") continue;
            scanned++;
            std::ifstream ifs(p, std::ios::binary);
            if (!ifs) continue;
            std::string u8Line;
            size_t lineCount = 0;
            while (std::getline(ifs, u8Line) && lineCount++ < 300) {
                std::wstring wLine(u8Line.begin(), u8Line.end());
                size_t hashPos = 0;
                while ((hashPos = wLine.find(L'#', hashPos)) != std::wstring::npos) {
                    bool isSelectorStart = (hashPos == 0 || iswspace(wLine[hashPos - 1]) || wLine[hashPos - 1] == L',' || wLine[hashPos - 1] == L'>' || wLine[hashPos - 1] == L'+' || wLine[hashPos - 1] == L'~');
                    if (isSelectorStart && hashPos + 1 < wLine.length() && (iswalpha(wLine[hashPos + 1]) || wLine[hashPos + 1] == L'_')) {
                        size_t idEnd = hashPos + 1;
                        while (idEnd < wLine.length() && (iswalnum(wLine[idEnd]) || wLine[idEnd] == L'-' || wLine[idEnd] == L'_')) idEnd++;
                        std::wstring idVal = wLine.substr(hashPos + 1, idEnd - hashPos - 1);
                        addId(idVal, L"CSS ID Selector (#" + idVal + L") from " + p.filename().wstring(), 1850);
                        hashPos = idEnd;
                        continue;
                    }
                    hashPos++;
                }
            }
        }
    }

    // 3. Standard semantic element IDs matching web conventions
    static const struct SemanticIdDef { const wchar_t* name; const wchar_t* desc; } s_semanticIds[] = {
        { L"app", L"Root application mount container" },
        { L"root", L"Root application mount element" },
        { L"main", L"Primary content container" },
        { L"main-content", L"Primary page content section" },
        { L"content", L"Main body content area" },
        { L"header", L"Page or site top header section" },
        { L"footer", L"Page or site bottom footer section" },
        { L"navbar", L"Navigation bar container" },
        { L"nav", L"Primary navigation container" },
        { L"sidebar", L"Sidebar navigation or drawer panel" },
        { L"sidebar-nav", L"Sidebar navigation links menu" },
        { L"container", L"Centered layout wrapper container" },
        { L"wrapper", L"Outer document or component wrapper" },
        { L"modal", L"Modal popup dialog container" },
        { L"dialog", L"Accessible modal dialog element" },
        { L"backdrop", L"Modal or drawer overlay backdrop" },
        { L"card", L"Content card container" },
        { L"banner", L"Hero or promotional header banner" },
        { L"hero", L"Hero section container" },
        { L"logo", L"Brand logo image or link container" },
        { L"menu", L"Navigation dropdown or context menu" },
        { L"search", L"Search bar input form container" },
        { L"toolbar", L"Interactive controls action toolbar" },
        { L"panel", L"Collapsible or fixed content panel" },
        { L"form", L"Primary submission form" },
        { L"title", L"Main heading title element" },
        { L"heading", L"Section title or heading wrapper" },
        { L"btn-submit", L"Primary form submit action button" },
        { L"btn-close", L"Modal or panel close button" },
        { L"bg-overlay", L"Background overlay container" },
        { L"bg-image", L"Background image element" },
        { L"bg-video", L"Background video element" },
        { L"bg-canvas", L"Background animated canvas" },
        { L"bg-container", L"Background wrapper container" },
        { L"table-data", L"Main data table container" },
        { L"list-items", L"Dynamic list items container" }
    };
    for (const auto& sid : s_semanticIds) {
        addId(sid.name, std::wstring(L"Semantic ID — ") + sid.desc, 1400);
    }
}

static bool IsStandardCssExcludedTagOrKeyword(const std::wstring& ident) {
    if (ident.empty()) return true;
    std::wstring low = ident;
    for (auto& c : low) c = static_cast<wchar_t>(towlower(c));
    static const std::unordered_set<std::wstring> s_excluded = {
        // Universal / Root / Keyframes / Pseudo keywords
        L"root", L"host", L"scope", L"from", L"to", L"after", L"before",
        L"placeholder", L"selection", L"backdrop", L"marker", L"first-line", L"first-letter",
        L"hover", L"focus", L"focus-visible", L"focus-within", L"active", L"visited",
        L"disabled", L"checked", L"first-child", L"last-child", L"nth-child", L"not", L"is", L"where", L"has",
        L"keyframes", L"media", L"supports", L"layer", L"import", L"font-face", L"charset", L"namespace", L"page", L"property",
        // Standard HTML / SVG Element Tags (excluded when written without a leading dot, e.g. body, main, html, *)
        L"html", L"head", L"title", L"base", L"link", L"meta", L"style", L"script", L"noscript", L"template", L"slot",
        L"body", L"main", L"header", L"footer", L"nav", L"section", L"article", L"aside", L"address", L"hgroup", L"search",
        L"h1", L"h2", L"h3", L"h4", L"h5", L"h6", L"p", L"hr", L"pre", L"blockquote",
        L"ol", L"ul", L"menu", L"li", L"dl", L"dt", L"dd", L"figure", L"figcaption", L"div",
        L"a", L"em", L"strong", L"small", L"s", L"cite", L"q", L"dfn", L"abbr", L"ruby", L"rt", L"rp",
        L"data", L"time", L"code", L"var", L"samp", L"kbd", L"sub", L"sup", L"i", L"b", L"u", L"mark",
        L"bdi", L"bdo", L"span", L"br", L"wbr", L"ins", L"del",
        L"picture", L"source", L"img", L"iframe", L"embed", L"object", L"video", L"audio", L"track", L"map", L"area",
        L"table", L"caption", L"colgroup", L"col", L"tbody", L"thead", L"tfoot", L"tr", L"td", L"th",
        L"form", L"label", L"input", L"button", L"select", L"datalist", L"optgroup", L"option", L"textarea",
        L"output", L"progress", L"meter", L"fieldset", L"legend", L"details", L"summary", L"dialog",
        L"canvas", L"svg", L"path", L"g", L"rect", L"circle", L"ellipse", L"line", L"polyline", L"polygon",
        L"text", L"tspan", L"defs", L"use", L"symbol", L"lineargradient", L"radialgradient", L"stop", L"clippath", L"mask", L"pattern"
    };
    return s_excluded.count(low) > 0;
}

struct CachedCssClassEntry {
    std::wstring className;
    std::wstring rulePreview;
};

struct CachedCssFileClasses {
    std::filesystem::file_time_type mtime{};
    std::vector<CachedCssClassEntry> classes;
};

static std::vector<CachedCssClassEntry> ParseCssClassesFromText(const std::wstring& cssText) {
    std::vector<CachedCssClassEntry> out;
    std::unordered_set<std::wstring> seen;

    // 1. Strip /* ... */ comments while preserving structure
    std::wstring clean;
    clean.reserve(cssText.size());
    for (size_t i = 0; i < cssText.size(); ) {
        if (i + 1 < cssText.size() && cssText[i] == L'/' && cssText[i + 1] == L'*') {
            size_t endC = cssText.find(L"*/", i + 2);
            if (endC == std::wstring::npos) break;
            i = endC + 2;
            clean.push_back(L' ');
            continue;
        }
        clean.push_back(cssText[i++]);
    }

    // 2. Walk CSS blocks to extract selectors at rule level (skipping @keyframes bodies and declaration blocks)
    size_t n = clean.size();
    size_t pos = 0;
    while (pos < n) {
        while (pos < n && iswspace(clean[pos])) pos++;
        if (pos >= n) break;

        if (clean[pos] == L'}' || clean[pos] == L';') {
            pos++;
            continue;
        }

        size_t nextBrace = clean.find(L'{', pos);
        size_t nextSemi = clean.find(L';', pos);
        size_t nextClose = clean.find(L'}', pos);

        // If a closing brace comes before '{', step past it (e.g. closing an @media or @layer block)
        if (nextClose != std::wstring::npos && (nextBrace == std::wstring::npos || nextClose < nextBrace)) {
            pos = nextClose + 1;
            continue;
        }
        // If a semicolon comes before '{', it's a top-level @import / @charset / CSS variable / declaration
        if (nextSemi != std::wstring::npos && (nextBrace == std::wstring::npos || nextSemi < nextBrace)) {
            pos = nextSemi + 1;
            continue;
        }
        if (nextBrace == std::wstring::npos) break;

        std::wstring header = clean.substr(pos, nextBrace - pos);
        while (!header.empty() && iswspace(header.front())) header.erase(header.begin());
        while (!header.empty() && iswspace(header.back())) header.pop_back();

        // Find matching '}' for this '{'
        int depth = 1;
        size_t bodyEnd = nextBrace + 1;
        bool hasNestedBraces = false;
        while (bodyEnd < n && depth > 0) {
            if (clean[bodyEnd] == L'{') {
                depth++;
                hasNestedBraces = true;
            } else if (clean[bodyEnd] == L'}') {
                depth--;
            }
            bodyEnd++;
        }
        size_t closeBracePos = (bodyEnd > 0) ? (bodyEnd - 1) : n;

        if (!header.empty() && header[0] == L'@') {
            std::wstring headerLow = header;
            for (auto& c : headerLow) c = static_cast<wchar_t>(towlower(c));
            if (headerLow.rfind(L"@keyframes", 0) == 0 ||
                headerLow.rfind(L"@-webkit-keyframes", 0) == 0 ||
                headerLow.rfind(L"@font-face", 0) == 0 ||
                headerLow.rfind(L"@property", 0) == 0) {
                // Skip entire @keyframes / @font-face block
                pos = bodyEnd;
                continue;
            }
            // For @media, @supports, @layer, @container: step inside the opening brace to parse inner rules
            pos = nextBrace + 1;
            continue;
        }

        // If this block contains nested rules (e.g. CSS nesting), still extract selectors from `header`,
        // and extract preview from non-nested declarations.
        std::wstring bodyStr = (closeBracePos > nextBrace + 1) ? clean.substr(nextBrace + 1, closeBracePos - nextBrace - 1) : L"";
        std::wstring preview;
        for (size_t bi = 0; bi < bodyStr.size() && preview.size() < 64; ++bi) {
            wchar_t bc = bodyStr[bi];
            if (bc == L'{') break;
            if (bc == L'\r' || bc == L'\n' || bc == L'\t') bc = L' ';
            if (bc == L' ' && (preview.empty() || preview.back() == L' ')) continue;
            preview.push_back(bc);
        }
        while (!preview.empty() && iswspace(preview.back())) preview.pop_back();
        if (preview.size() > 60) preview = preview.substr(0, 57) + L"...";

        // Parse selectors in `header` (comma-separated)
        // A) Extract all explicit `.className` selectors anywhere in `header`
        for (size_t i = 0; i < header.size(); ) {
            if (header[i] == L'.') {
                // Ensure '.' is not inside a number like 0.5s
                bool afterDigit = (i > 0 && iswdigit(header[i - 1]));
                if (!afterDigit && i + 1 < header.size() && (iswalpha(header[i + 1]) || header[i + 1] == L'_' || header[i + 1] == L'-')) {
                    size_t cStart = i + 1;
                    size_t cEnd = cStart;
                    while (cEnd < header.size() && (iswalnum(header[cEnd]) || header[cEnd] == L'-' || header[cEnd] == L'_')) {
                        cEnd++;
                    }
                    std::wstring clsName = header.substr(cStart, cEnd - cStart);
                    // Exclude standard root/universal keywords if someone wrote .root by mistake? Wait: `.root` is a valid class if prefixed with `.`, only exclude empty!
                    if (!clsName.empty() && seen.insert(clsName).second) {
                        out.push_back({ clsName, preview });
                    }
                    i = cEnd;
                    continue;
                }
            }
            i++;
        }

        // B) Also check each comma-separated selector part for a bare custom identifier (e.g. `hello { ... }` in style.css)
        // Exclude standard HTML tags (body, main, html, div, etc.), universal `*`, `:root`, `#id`, `[attr]`, etc.
        size_t sPos = 0;
        while (sPos < header.size()) {
            size_t comma = header.find(L',', sPos);
            if (comma == std::wstring::npos) comma = header.size();
            std::wstring part = header.substr(sPos, comma - sPos);
            sPos = comma + 1;
            while (!part.empty() && iswspace(part.front())) part.erase(part.begin());
            while (!part.empty() && iswspace(part.back())) part.pop_back();
            if (part.empty()) continue;

            // Check if `part` starts with an identifier (no leading '.', '#', ':', '[', '*', '@')
            if (iswalpha(part[0]) || part[0] == L'_') {
                size_t idEnd = 0;
                while (idEnd < part.size() && (iswalnum(part[idEnd]) || part[idEnd] == L'-' || part[idEnd] == L'_')) {
                    idEnd++;
                }
                // Only treat bare identifier as a custom selector if the rest of `part` is empty or pseudo-class (e.g. `hello` or `hello:hover`)
                std::wstring rest = part.substr(idEnd);
                while (!rest.empty() && iswspace(rest.front())) rest.erase(rest.begin());
                if (rest.empty() || rest[0] == L':') {
                    std::wstring bareIdent = part.substr(0, idEnd);
                    if (!IsStandardCssExcludedTagOrKeyword(bareIdent) && seen.insert(bareIdent).second) {
                        out.push_back({ bareIdent, preview });
                    }
                }
            }
        }

        pos = hasNestedBraces ? (nextBrace + 1) : bodyEnd;
    }

    return out;
}

static const std::vector<CachedCssClassEntry>& GetCachedCssClassesForFile(const std::filesystem::path& cssPath) {
    static std::unordered_map<std::wstring, CachedCssFileClasses> s_cssClassCache;
    static const std::vector<CachedCssClassEntry> s_empty;

    std::error_code ec;
    if (!std::filesystem::exists(cssPath, ec) || !std::filesystem::is_regular_file(cssPath, ec)) {
        return s_empty;
    }

    auto mtime = std::filesystem::last_write_time(cssPath, ec);
    std::wstring key = cssPath.wstring();
    auto it = s_cssClassCache.find(key);
    if (it != s_cssClassCache.end() && it->second.mtime == mtime) {
        return it->second.classes;
    }

    std::ifstream ifs(cssPath, std::ios::binary);
    if (!ifs) return s_empty;
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    std::wstring wText;
    if (!raw.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
        if (wlen > 0) {
            wText.resize(wlen);
            MultiByteToWideChar(CP_UTF8, 0, raw.data(), static_cast<int>(raw.size()), wText.data(), wlen);
        } else {
            wText.assign(raw.begin(), raw.end());
        }
    }

    CachedCssFileClasses entry;
    entry.mtime = mtime;
    entry.classes = ParseCssClassesFromText(wText);
    auto& stored = s_cssClassCache[key];
    stored = std::move(entry);
    return stored.classes;
}

static void ProvideClassCompletions(
    std::vector<IntelliSenseItem>& outCandidates,
    const std::vector<std::wstring>* pDocLines,
    const std::filesystem::path& baseDir,
    const std::filesystem::path& projectRoot,
    const std::wstring& currentLineText = L""
) {
    std::unordered_set<std::wstring> seenClasses;

    auto addClass = [&](const std::wstring& clsStr, const std::wstring& detail, int score = 2600) {
        if (clsStr.empty()) return;
        // Never suggest universal/root/element tag names like body, main, html, *, root
        if (clsStr == L"*" || clsStr[0] == L':' || clsStr[0] == L'@') return;
        if (IsStandardCssExcludedTagOrKeyword(clsStr)) return;
        if (seenClasses.insert(clsStr).second) {
            IntelliSenseItem it;
            it.label = clsStr;
            it.detail = detail;
            it.insertText = clsStr;
            it.kind = CompletionKind::Class;
            it.origin = L"css-class";
            it.score = score;
            outCandidates.push_back(std::move(it));
        }
    };

    std::error_code ec;
    std::vector<std::pair<std::filesystem::path, int>> cssFilesToScan; // (path, priorityScore)
    std::unordered_set<std::wstring> queuedPaths;

    auto queueCssFile = [&](const std::filesystem::path& p, int priorityScore) {
        if (p.empty()) return;
        std::error_code localEc;
        if (!std::filesystem::exists(p, localEc) || !std::filesystem::is_regular_file(p, localEc)) return;
        auto canon = std::filesystem::weakly_canonical(p, localEc);
        std::wstring key = localEc ? p.wstring() : canon.wstring();
        for (auto& c : key) c = static_cast<wchar_t>(towlower(c));
        if (queuedPaths.insert(key).second) {
            cssFilesToScan.push_back({ p, priorityScore });
        }
    };

    // 1. Scan current document for:
    //    - Linked CSS files (<link ... href="style.css"> or import "./globals.css")
    //    - Inline <style> CSS rules
    if (pDocLines) {
        std::wstring inlineCss;
        bool inStyleBlock = false;
        for (const auto& line : *pDocLines) {
            // Check <link ... href="...css">
            if (line.find(L"<link") != std::wstring::npos || line.find(L"href=") != std::wstring::npos) {
                size_t hp = line.find(L"href=");
                if (hp != std::wstring::npos) {
                    size_t qStart = hp + 5;
                    while (qStart < line.size() && iswspace(line[qStart])) qStart++;
                    if (qStart < line.size() && (line[qStart] == L'"' || line[qStart] == L'\'')) {
                        wchar_t qCh = line[qStart];
                        size_t qEnd = line.find(qCh, qStart + 1);
                        if (qEnd != std::wstring::npos && qEnd > qStart + 1) {
                            std::wstring hrefVal = line.substr(qStart + 1, qEnd - qStart - 1);
                            if (hrefVal.find(L".css") != std::wstring::npos) {
                                // Strip query string if any
                                size_t qm = hrefVal.find(L'?');
                                if (qm != std::wstring::npos) hrefVal = hrefVal.substr(0, qm);
                                if (!hrefVal.empty() && hrefVal[0] == L'/' && !projectRoot.empty()) {
                                    queueCssFile(projectRoot / hrefVal.substr(1), 3500);
                                    queueCssFile(projectRoot / L"public" / hrefVal.substr(1), 3500);
                                }
                                if (!baseDir.empty()) queueCssFile(baseDir / hrefVal, 3500);
                                if (!projectRoot.empty()) queueCssFile(projectRoot / hrefVal, 3450);
                            }
                        }
                    }
                }
            }

            // Check JS/TS CSS imports: import "./globals.css", import '@/app/globals.css', import styles from './landing.module.css'
            if (line.find(L".css") != std::wstring::npos && (line.find(L"import") != std::wstring::npos || line.find(L"require") != std::wstring::npos)) {
                size_t q1 = line.find_first_of(L"\"'");
                if (q1 != std::wstring::npos) {
                    wchar_t qCh = line[q1];
                    size_t q2 = line.find(qCh, q1 + 1);
                    if (q2 != std::wstring::npos && q2 > q1 + 1) {
                        std::wstring impSpec = line.substr(q1 + 1, q2 - q1 - 1);
                        if (impSpec.rfind(L"@/", 0) == 0 && !projectRoot.empty()) {
                            queueCssFile(projectRoot / impSpec.substr(2), 3500);
                            queueCssFile(projectRoot / L"src" / impSpec.substr(2), 3500);
                        } else {
                            if (!baseDir.empty()) queueCssFile(baseDir / impSpec, 3500);
                            if (!projectRoot.empty()) queueCssFile(projectRoot / impSpec, 3450);
                        }
                    }
                }
            }

            // Collect inline <style>...</style> blocks
            if (line.find(L"<style") != std::wstring::npos) inStyleBlock = true;
            if (inStyleBlock) {
                inlineCss += line + L"\n";
            }
            if (line.find(L"</style>") != std::wstring::npos) inStyleBlock = false;
        }

        if (!inlineCss.empty()) {
            auto inlineClasses = ParseCssClassesFromText(inlineCss);
            for (const auto& entry : inlineClasses) {
                std::wstring det = entry.rulePreview.empty()
                    ? L"CSS Class (inline <style>)"
                    : (L"." + entry.className + L" { " + entry.rulePreview + L" } — inline <style>");
                addClass(entry.className, det, 3500);
            }
        }
    }

    // 2. Priority CSS files in baseDir and projectRoot (style.css, globals.css, global.css, app/globals.css, src/app/globals.css, etc.)
    static const wchar_t* s_priorityCssNames[] = {
        L"style.css", L"styles.css", L"globals.css", L"global.css", L"index.css", L"app.css", L"main.css",
        L"app/globals.css", L"app/global.css", L"src/app/globals.css", L"src/app/global.css",
        L"src/index.css", L"src/App.css", L"src/styles.css", L"src/style.css", L"styles/globals.css", L"styles/global.css"
    };
    for (const wchar_t* relName : s_priorityCssNames) {
        if (!baseDir.empty()) queueCssFile(baseDir / relName, 3400);
        if (!projectRoot.empty()) queueCssFile(projectRoot / relName, 3350);
    }

    // 3. All .css files in baseDir (same directory as the HTML/React file)
    if (!baseDir.empty() && std::filesystem::exists(baseDir, ec)) {
        int localCount = 0;
        int localVisited = 0;
        for (std::filesystem::directory_iterator it(baseDir, std::filesystem::directory_options::skip_permission_denied, ec);
             !ec && it != std::filesystem::directory_iterator() && localCount < 25 && localVisited < 120;
             it.increment(ec)) {
            ++localVisited;
            if (!it->is_regular_file(ec)) continue;
            auto p = it->path();
            std::wstring ext = p.extension().wstring();
            for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
            if (ext == L".css") {
                localCount++;
                queueCssFile(p, 3380);
            }
        }
    }

    // 4. All .css files across the workspace (cached 5s per root, bounded entries & depth)
    if (!projectRoot.empty() && std::filesystem::exists(projectRoot, ec)) {
        struct WsCssScanCacheEntry {
            std::chrono::steady_clock::time_point scannedAt;
            std::vector<std::filesystem::path> cssFiles;
        };
        static std::mutex s_wsCssCacheMutex;
        static std::unordered_map<std::wstring, WsCssScanCacheEntry> s_wsCssCache;

        std::wstring rootKey = projectRoot.lexically_normal().wstring();
        for (auto& c : rootKey) c = static_cast<wchar_t>(towlower(c));

        auto now = std::chrono::steady_clock::now();
        std::vector<std::filesystem::path> discoveredCss;
        bool cacheHit = false;
        {
            std::lock_guard<std::mutex> lock(s_wsCssCacheMutex);
            auto itCache = s_wsCssCache.find(rootKey);
            if (itCache != s_wsCssCache.end() && (now - itCache->second.scannedAt) < std::chrono::seconds(5)) {
                discoveredCss = itCache->second.cssFiles;
                cacheHit = true;
            }
        }

        if (!cacheHit) {
            try {
                std::error_code ecIter;
                std::filesystem::recursive_directory_iterator it(
                    projectRoot, std::filesystem::directory_options::skip_permission_denied, ecIter);
                std::filesystem::recursive_directory_iterator endIt;
                int wsCssCount = 0;
                int visitedEntries = 0;
                for (; it != endIt && !ecIter && wsCssCount < 40 && visitedEntries < 500; it.increment(ecIter)) {
                    ++visitedEntries;
                    if (it.depth() > 4) {
                        it.disable_recursion_pending();
                        continue;
                    }
                    if (it->is_directory(ecIter)) {
                        std::wstring dName = it->path().filename().wstring();
                        for (auto& c : dName) c = static_cast<wchar_t>(towlower(c));
                        if ((!dName.empty() && dName[0] == L'.') ||
                            dName == L"node_modules" || dName == L"dist" || dName == L"build" ||
                            dName == L"out" || dName == L"vendor" || dName == L"venv" ||
                            dName == L"__pycache__" || dName == L"appdata" || dName == L"coverage" ||
                            dName == L"tmp" || dName == L"temp" || dName == L"target" ||
                            dName == L"bin" || dName == L"obj") {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }
                    if (it->is_regular_file(ecIter)) {
                        auto p = it->path();
                        std::wstring ext = p.extension().wstring();
                        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
                        if (ext == L".css") {
                            wsCssCount++;
                            discoveredCss.push_back(std::move(p));
                        }
                    }
                }
            } catch (...) {}

            std::lock_guard<std::mutex> lock(s_wsCssCacheMutex);
            if (s_wsCssCache.size() > 32) s_wsCssCache.clear();
            s_wsCssCache[rootKey] = { std::chrono::steady_clock::now(), discoveredCss };
        }

        for (const auto& p : discoveredCss) {
            queueCssFile(p, 3250);
        }
    }

    // 5. Populate classes from all queued CSS files using mtime cache
    for (const auto& [cssPath, prioScore] : cssFilesToScan) {
        const auto& cssEntries = GetCachedCssClassesForFile(cssPath);
        std::wstring fileNameStr = cssPath.filename().wstring();
        for (const auto& entry : cssEntries) {
            std::wstring det = entry.rulePreview.empty()
                ? (L"CSS Class from " + fileNameStr)
                : (L"." + entry.className + L" { " + entry.rulePreview + L" } — " + fileNameStr);
            addClass(entry.className, det, prioScore);
        }
    }

    // 6. Also scan existing class="..." and className="..." in the current document (skipping the line currently being edited)
    if (pDocLines) {
        for (const auto& line : *pDocLines) {
            if (!currentLineText.empty() && line == currentLineText) continue;
            for (const wchar_t* attrKey : { L"class=", L"className=" }) {
                size_t keyLen = std::wcslen(attrKey);
                size_t cPos = 0;
                while ((cPos = line.find(attrKey, cPos)) != std::wstring::npos) {
                    size_t qPos = cPos + keyLen;
                    while (qPos < line.length() && iswspace(line[qPos])) qPos++;
                    if (qPos < line.length() && (line[qPos] == L'"' || line[qPos] == L'\'')) {
                        wchar_t qChar = line[qPos];
                        size_t qEnd = line.find(qChar, qPos + 1);
                        if (qEnd != std::wstring::npos && qEnd > qPos + 1) {
                            std::wstring classesStr = line.substr(qPos + 1, qEnd - qPos - 1);
                            size_t ws = 0;
                            while (ws < classesStr.length()) {
                                while (ws < classesStr.length() && iswspace(classesStr[ws])) ws++;
                                size_t we = ws;
                                while (we < classesStr.length() && !iswspace(classesStr[we])) we++;
                                if (we > ws) {
                                    addClass(classesStr.substr(ws, we - ws), L"CSS Class (from document)", 2400);
                                }
                                ws = we;
                            }
                        }
                    }
                    cPos += keyLen;
                }
            }
        }
    }
}

static void ProvideHtmlTypeCompletions(std::vector<IntelliSenseItem>& outCandidates) {
    static const struct TypeDef { const wchar_t* val; const wchar_t* desc; } s_types[] = {
        { L"text", L"Single-line text input control" },
        { L"password", L"Obscured single-line text input control" },
        { L"email", L"Input control for editing an email address" },
        { L"number", L"Input control for entering a numeric value" },
        { L"checkbox", L"Two-state checkbox toggle control" },
        { L"radio", L"Radio button option within a radio group" },
        { L"submit", L"Button that submits form data" },
        { L"button", L"Generic clickable button with no default behavior" },
        { L"reset", L"Button that resets form inputs to default values" },
        { L"file", L"Control that lets the user select one or more files" },
        { L"hidden", L"Hidden form input value not displayed to the user" },
        { L"date", L"Input control for entering a calendar date" },
        { L"time", L"Input control for entering a time value" },
        { L"datetime-local", L"Input control for entering a date and time without timezone" },
        { L"month", L"Input control for entering a month and year" },
        { L"week", L"Input control for entering a week and year" },
        { L"color", L"Color picker control" },
        { L"range", L"Slider range input control" },
        { L"search", L"Single-line text input control formatted for search queries" },
        { L"tel", L"Input control for entering a telephone number" },
        { L"url", L"Input control for entering a web URL" },
        { L"image", L"Graphical submit button referencing an image source" },
        { L"module", L"JavaScript ES module script execution type" },
        { L"text/css", L"Standard CSS stylesheet MIME type" },
        { L"text/javascript", L"Standard JavaScript MIME type" },
        { L"application/json", L"JSON data format MIME type" }
    };
    for (const auto& td : s_types) {
        IntelliSenseItem it;
        it.label = td.val;
        it.detail = std::wstring(L"Type — ") + td.desc;
        it.insertText = td.val;
        it.kind = CompletionKind::Constant;
        it.origin = L"html";
        it.score = 1600;
        outCandidates.push_back(std::move(it));
    }
}

static void ProvideHtmlRelCompletions(std::vector<IntelliSenseItem>& outCandidates) {
    static const struct RelDef { const wchar_t* val; const wchar_t* desc; } s_rels[] = {
        { L"stylesheet", L"Imports an external CSS stylesheet into the document" },
        { L"icon", L"Specifies site favicon icon resource" },
        { L"apple-touch-icon", L"Icon for Apple iOS home screen bookmarks" },
        { L"shortcut icon", L"Legacy shortcut icon format" },
        { L"preload", L"Forces browser to download and cache resource high priority" },
        { L"prefetch", L"Suggests browser fetch resource in idle time for future navigation" },
        { L"preconnect", L"Initiates an early connection (DNS lookup, TCP, TLS handshake)" },
        { L"dns-prefetch", L"Resolves DNS domain name in background before link is clicked" },
        { L"modulepreload", L"Preemptively fetches and parses ES module scripts" },
        { L"manifest", L"Web application manifest file (.webmanifest or manifest.json)" },
        { L"alternate", L"Alternate representation of the current document (e.g. RSS, print)" },
        { L"author", L"Links to author information for the current document" },
        { L"canonical", L"Specifies preferred canonical URL for search engine indexing" },
        { L"license", L"Links to the copyright license governing the document" },
        { L"noopener", L"Instructs browser to open link without granting window.opener access" },
        { L"noreferrer", L"Prevents browser from sending HTTP Referer header to target page" }
    };
    for (const auto& rd : s_rels) {
        IntelliSenseItem it;
        it.label = rd.val;
        it.detail = std::wstring(L"Rel — ") + rd.desc;
        it.insertText = rd.val;
        it.kind = CompletionKind::Constant;
        it.origin = L"html";
        it.score = 1600;
        outCandidates.push_back(std::move(it));
    }
}

static void ProvideHtmlTargetCompletions(std::vector<IntelliSenseItem>& outCandidates) {
    static const struct TargetDef { const wchar_t* val; const wchar_t* desc; } s_targets[] = {
        { L"_blank", L"Opens the linked document in a new tab or window" },
        { L"_self", L"Opens the linked document in the same browsing frame (default)" },
        { L"_parent", L"Opens the linked document in the parent browsing frame" },
        { L"_top", L"Opens the linked document in the full body of the current window" }
    };
    for (const auto& td : s_targets) {
        IntelliSenseItem it;
        it.label = td.val;
        it.detail = std::wstring(L"Target — ") + td.desc;
        it.insertText = td.val;
        it.kind = CompletionKind::Constant;
        it.origin = L"html";
        it.score = 1600;
        outCandidates.push_back(std::move(it));
    }
}

static void ProvideHtmlMethodCompletions(std::vector<IntelliSenseItem>& outCandidates) {
    static const struct MethodDef { const wchar_t* val; const wchar_t* desc; } s_methods[] = {
        { L"get", L"HTTP GET: Appends form data to the URL as query parameters" },
        { L"post", L"HTTP POST: Sends form data in the HTTP request transaction body" },
        { L"dialog", L"Submits the form to close an enclosing <dialog> element without sending a network request" }
    };
    for (const auto& md : s_methods) {
        IntelliSenseItem it;
        it.label = md.val;
        it.detail = std::wstring(L"Method — ") + md.desc;
        it.insertText = md.val;
        it.kind = CompletionKind::Constant;
        it.origin = L"html";
        it.score = 1600;
        outCandidates.push_back(std::move(it));
    }
}

} // namespace

std::vector<IntelliSenseItem> IntelliSenseEngine::QueryCompletions(
    const std::wstring& fileName,
    const std::wstring& currentLine,
    size_t cursorCol,
    const std::vector<std::wstring>* pDocLines
) const {
    // Suppress all completions when cursor is inside a line comment or comment block
    std::wstring lowerFile = fileName;
    for (auto& c : lowerFile) c = static_cast<wchar_t>(towlower(c));
    bool isPyFile = (lowerFile.ends_with(L".py") || lowerFile.ends_with(L".pyw"));
    bool isPhpFile = lowerFile.ends_with(L".php");
    bool isShellOrYaml = (lowerFile.ends_with(L".sh") || lowerFile.ends_with(L".bash") || lowerFile.ends_with(L".zsh") || lowerFile.ends_with(L".yaml") || lowerFile.ends_with(L".yml") || lowerFile.ends_with(L".toml") || lowerFile.ends_with(L".ini") || lowerFile.ends_with(L".env"));

    bool inDblQuote = false;
    bool inSglQuote = false;
    for (size_t i = 0; i < currentLine.length() && i < cursorCol; ++i) {
        wchar_t c = currentLine[i];
        if (c == L'\\' && (inDblQuote || inSglQuote) && i + 1 < currentLine.length()) { ++i; continue; }
        if (c == L'"' && !inSglQuote) inDblQuote = !inDblQuote;
        else if (c == L'\'' && !inDblQuote) inSglQuote = !inSglQuote;
        else if (!inDblQuote && !inSglQuote) {
            if (isPyFile || isShellOrYaml) {
                if (c == L'#') return {};
            } else if (isPhpFile) {
                if (c == L'#' || (c == L'/' && i + 1 < currentLine.length() && currentLine[i + 1] == L'/')) return {};
            } else {
                if (c == L'/' && i + 1 < currentLine.length() && currentLine[i + 1] == L'/') return {};
            }
        }
    }

    size_t firstNonWs = currentLine.find_first_not_of(L" \t");
    if (firstNonWs != std::wstring::npos && firstNonWs < cursorCol) {
        if (isPyFile || isShellOrYaml) {
            if (currentLine[firstNonWs] == L'#') return {};
        } else if (isPhpFile) {
            if (currentLine[firstNonWs] == L'#' || currentLine.substr(firstNonWs, 2) == L"//" || currentLine.substr(firstNonWs, 2) == L"/*" || currentLine[firstNonWs] == L'*') return {};
        } else {
            if (currentLine.substr(firstNonWs, 2) == L"//" || currentLine.substr(firstNonWs, 2) == L"/*" || currentLine[firstNonWs] == L'*') return {};
        }
    }

    std::filesystem::path baseDir = ResolveBaseDir(fileName, m_workspaceRoot);
    std::filesystem::path projectRoot = FindProjectRoot(baseDir, m_workspaceRoot);

    // 0. Check if cursor is in a File / Folder Path or Module Specifier context
    // Supports quoted strings ("./", './', `./`, from "lucide-react") AND unquoted ./ or ../ typed directly!
    size_t lastDouble = currentLine.rfind(L'"', cursorCol > 0 ? cursorCol - 1 : 0);
    size_t lastSingle = currentLine.rfind(L'\'', cursorCol > 0 ? cursorCol - 1 : 0);
    size_t lastBacktick = currentLine.rfind(L'`', cursorCol > 0 ? cursorCol - 1 : 0);
    size_t quotePos = std::wstring::npos;
    if (lastDouble != std::wstring::npos) quotePos = lastDouble;
    if (lastSingle != std::wstring::npos && (quotePos == std::wstring::npos || lastSingle > quotePos)) quotePos = lastSingle;
    if (lastBacktick != std::wstring::npos && (quotePos == std::wstring::npos || lastBacktick > quotePos)) quotePos = lastBacktick;

    bool isPathContext = false;
    bool isModuleSpecifierContext = false;
    std::wstring pathTyped;
    if (quotePos != std::wstring::npos && quotePos < cursorCol) {
        std::wstring insideQuote = currentLine.substr(quotePos + 1, cursorCol - (quotePos + 1));
        std::wstring beforeQuote = currentLine.substr(0, quotePos);
        while (!beforeQuote.empty() && iswspace(beforeQuote.back())) beforeQuote.pop_back();

        bool isAttrOrImport = false;
        static const std::vector<std::wstring> triggers = {
            L"href=", L"src=", L"action=", L"poster=", L"data=", L"content=",
            L"from", L"require(", L"import(", L"import", L"include", L"include_once",
            L"require", L"require_once", L"url(", L"preload(", L"load(", L"open("
        };
        static const std::vector<std::wstring> moduleTriggers = {
            L"from", L"require(", L"import(", L"import"
        };
        for (const auto& trig : triggers) {
            if (beforeQuote.length() >= trig.length()) {
                std::wstring tail = beforeQuote.substr(beforeQuote.length() - trig.length());
                for (auto& c : tail) c = static_cast<wchar_t>(towlower(c));
                if (tail == trig) {
                    isAttrOrImport = true;
                    for (const auto& mtrig : moduleTriggers) {
                        if (tail == mtrig) {
                            isModuleSpecifierContext = true;
                            break;
                        }
                    }
                    break;
                }
            }
        }

        bool hasPathMarker = (insideQuote.rfind(L"./", 0) == 0 ||
                              insideQuote.rfind(L"../", 0) == 0 ||
                              insideQuote.rfind(L"/", 0) == 0 ||
                              insideQuote.rfind(L"\\", 0) == 0 ||
                              insideQuote.find(L'/') != std::wstring::npos ||
                              insideQuote.find(L'\\') != std::wstring::npos);

        if (isAttrOrImport || hasPathMarker) {
            isPathContext = true;
            pathTyped = insideQuote;
        }
    } else if (cursorCol >= 2) {
        // Check for unquoted ./ or ../ path reference typed directly in the buffer
        size_t tokenStart = cursorCol;
        while (tokenStart > 0) {
            wchar_t c = currentLine[tokenStart - 1];
            if (iswalnum(c) || c == L'.' || c == L'/' || c == L'\\' || c == L'_' || c == L'-') {
                tokenStart--;
            } else {
                break;
            }
        }
        std::wstring rawToken = currentLine.substr(tokenStart, cursorCol - tokenStart);
        if (rawToken.rfind(L"./", 0) == 0 || rawToken.rfind(L"../", 0) == 0 ||
            rawToken.rfind(L".\\", 0) == 0 || rawToken.rfind(L"..\\", 0) == 0) {
            isPathContext = true;
            pathTyped = rawToken;
        }
    }

    if (isPathContext) {
        std::error_code ec;
        std::wstring normPath = pathTyped;
        for (auto& ch : normPath) {
            if (ch == L'\\') ch = L'/';
        }

        std::vector<IntelliSenseItem> pathItems;

        // If inside `import ... from '|'` or `require('|')` and NOT starting with `.` or `/`,
        // dynamically suggest all installed NPM packages from package.json & node_modules!
        bool startsWithDotOrSlash = (!normPath.empty() && (normPath[0] == L'.' || normPath[0] == L'/'));
        if (isModuleSpecifierContext && !startsWithDotOrSlash) {
            std::wstring modQueryLower = normPath;
            for (auto& c : modQueryLower) c = static_cast<wchar_t>(towlower(c));

            const auto& npmPkgs = GetInstalledNpmPackages(projectRoot);
            for (const auto& [pkgName, pkgDetail] : npmPkgs) {
                std::wstring pkgLower = pkgName;
                for (auto& c : pkgLower) c = static_cast<wchar_t>(towlower(c));

                int score = 0;
                size_t matchStart = 0;
                size_t matchLen = 0;
                if (modQueryLower.empty()) {
                    score = 2200;
                } else {
                    size_t p = pkgLower.find(modQueryLower);
                    if (p == 0) {
                        score = 3000 - static_cast<int>(pkgName.length());
                        matchStart = 0;
                        matchLen = normPath.length();
                    } else if (p != std::wstring::npos) {
                        score = 2400 - static_cast<int>(p * 10 + pkgName.length());
                        matchStart = p;
                        matchLen = normPath.length();
                    } else {
                        continue;
                    }
                }

                IntelliSenseItem mi;
                mi.label = pkgName;
                mi.detail = pkgDetail;
                mi.insertText = pkgName;
                mi.kind = CompletionKind::Module;
                mi.score = score;
                mi.matchStart = matchStart;
                mi.matchLen = matchLen;
                mi.origin = L"npm-module";
                pathItems.push_back(std::move(mi));
            }

            // Also offer "./" and "../" shortcuts when starting a module specifier
            if (modQueryLower.empty()) {
                IntelliSenseItem curDirItem;
                curDirItem.label = L"./";
                curDirItem.detail = L"Current Directory";
                curDirItem.insertText = L"./";
                curDirItem.kind = CompletionKind::Folder;
                curDirItem.score = 2100;
                curDirItem.origin = L"path";
                pathItems.push_back(std::move(curDirItem));
            }
        }

        std::wstring dirPart;
        std::wstring filePrefix;
        size_t lastSlash = normPath.find_last_of(L'/');
        if (lastSlash != std::wstring::npos) {
            dirPart = normPath.substr(0, lastSlash);
            filePrefix = normPath.substr(lastSlash + 1);
        } else {
            dirPart = L"";
            filePrefix = normPath;
        }

        std::filesystem::path searchDir = baseDir;
        if (!dirPart.empty()) {
            if (dirPart == L".") {
                searchDir = baseDir;
            } else if (dirPart.rfind(L"./", 0) == 0) {
                searchDir = baseDir / dirPart.substr(2);
            } else if (dirPart.rfind(L"/", 0) == 0) {
                if (!m_workspaceRoot.empty()) searchDir = std::filesystem::path(m_workspaceRoot) / dirPart.substr(1);
                else searchDir = projectRoot / dirPart.substr(1);
            } else {
                searchDir = baseDir / dirPart;
            }
        }
        searchDir = searchDir.lexically_normal();

        std::wstring filePrefixLower = filePrefix;
        for (auto& c : filePrefixLower) c = static_cast<wchar_t>(towlower(c));

        if (std::filesystem::exists(searchDir, ec) && std::filesystem::is_directory(searchDir, ec)) {
            for (std::filesystem::directory_iterator it(searchDir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
                if (ec) break;
                const auto& entry = *it;
                std::wstring entryName = entry.path().filename().wstring();
                if (entryName.empty()) continue;
                if (entryName == L"." || entryName == L".." || entryName == L".git" || entryName == L".phide" || entryName == L".vs") continue;

                bool isDir = entry.is_directory(ec);
                bool isReg = entry.is_regular_file(ec);
                if (!isDir && !isReg) continue;

                std::wstring candName = entryName + (isDir ? L"/" : L"");
                std::wstring candLower = candName;
                for (auto& c : candLower) c = static_cast<wchar_t>(towlower(c));

                int score = 0;
                size_t matchStart = 0;
                size_t matchLen = 0;

                if (filePrefixLower.empty()) {
                    score = isDir ? 1500 : 1300;
                } else {
                    size_t p = candLower.find(filePrefixLower);
                    if (p == 0) {
                        score = (isDir ? 2500 : 2300) - static_cast<int>(candName.length());
                        matchStart = 0;
                        matchLen = filePrefix.length();
                    } else if (p != std::wstring::npos) {
                        score = (isDir ? 1800 : 1600) - static_cast<int>(p * 10 + candName.length());
                        matchStart = p;
                        matchLen = filePrefix.length();
                    } else {
                        continue;
                    }
                }

                IntelliSenseItem pi;
                pi.label = candName;
                pi.detail = isDir ? L"Folder" : L"File";
                pi.insertText = candName;
                pi.kind = isDir ? CompletionKind::Folder : CompletionKind::File;
                pi.score = score;
                pi.matchStart = matchStart;
                pi.matchLen = matchLen;
                pi.origin = L"path";
                pathItems.push_back(std::move(pi));
            }
        }

        // Return EXCLUSIVELY path/module items in path context
        std::sort(pathItems.begin(), pathItems.end(), [](const IntelliSenseItem& a, const IntelliSenseItem& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.label < b.label;
        });
        if (pathItems.size() > 60) pathItems.resize(60);
        return pathItems;
    }

    std::wstring ext;
    auto pos = fileName.rfind(L'.');
    if (pos != std::wstring::npos) {
        ext = fileName.substr(pos);
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    }

    std::wstring prefix = ExtractQueryPrefix(currentLine, cursorCol);
    std::wstring prefixLower = prefix;
    for (auto& c : prefixLower) c = static_cast<wchar_t>(towlower(c));

    bool isPhp = (ext == L".php");
    bool insidePhp = isPhp;
    if (isPhp && pDocLines && !pDocLines->empty()) {
        bool inTag = false;
        for (const auto& dLine : *pDocLines) {
            if (dLine.find(L"<?php") != std::wstring::npos || dLine.find(L"<?=") != std::wstring::npos) inTag = true;
            if (dLine.find(L"?>") != std::wstring::npos) inTag = false;
            if (&dLine == &currentLine) break;
        }
        insidePhp = inTag || (currentLine.find(L"<?") != std::wstring::npos) || (prefix.rfind(L"$", 0) == 0);
    }

    bool isHtml = (ext == L".html" || ext == L".htm" || (isPhp && !insidePhp));
    bool isCss = (ext == L".css" || ext == L".scss" || ext == L".less");
    bool insideTagAttr = false;
    bool isJs = (ext == L".js" || ext == L".jsx" || ext == L".ts" || ext == L".tsx" || ext == L".mjs" || ext == L".cjs" || ext == L".mts" || ext == L".cts");
    bool isTs = (ext == L".ts" || ext == L".tsx" || ext == L".mts" || ext == L".cts");
    bool isPy = (ext == L".py");
    bool pyExclusive = false;                        // Python import/member context: show ONLY these items
    std::vector<IntelliSenseItem> pyExclusiveItems;

    // Detect contextual triggers before prefix (e.g. ::, ->, ., <, $_SERVER["..."])
    size_t beforePos = (cursorCol >= prefix.length()) ? (cursorCol - prefix.length()) : 0;
    bool afterDoubleColon = (beforePos >= 2 && currentLine[beforePos - 1] == L':' && currentLine[beforePos - 2] == L':');
    bool afterArrow       = (beforePos >= 2 && currentLine[beforePos - 1] == L'>' && currentLine[beforePos - 2] == L'-');
    bool afterDot         = (beforePos >= 1 && currentLine[beforePos - 1] == L'.');
    bool afterOpenAngle   = (beforePos >= 1 && currentLine[beforePos - 1] == L'<');

    bool insideServerKey = false;
    if (insidePhp && quotePos != std::wstring::npos && quotePos < cursorCol) {
        std::wstring beforeQ = currentLine.substr(0, quotePos);
        if (beforeQ.find(L"$_SERVER[") != std::wstring::npos) {
            insideServerKey = true;
        }
    }

    // Detect if cursor is inside an ES `import { ... | ... } from 'module'` statement
    bool insideJsImportClause = false;
    std::wstring importLineModuleSpec;
    if (isJs) {
        std::wstring trimmedLine = TrimWs(currentLine);
        if (trimmedLine.rfind(L"import ", 0) == 0 || trimmedLine.rfind(L"export ", 0) == 0) {
            size_t fromIdx = currentLine.find(L"from");
            if (fromIdx != std::wstring::npos && cursorCol <= fromIdx) {
                insideJsImportClause = true;
                size_t q1 = currentLine.find_first_of(L"\"'`", fromIdx + 4);
                if (q1 != std::wstring::npos) {
                    wchar_t qCh = currentLine[q1];
                    size_t q2 = currentLine.find(qCh, q1 + 1);
                    if (q2 != std::wstring::npos && q2 > q1 + 1) {
                        importLineModuleSpec = currentLine.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
            } else if (currentLine.find(L'{') != std::wstring::npos && cursorCol > currentLine.find(L'{')) {
                size_t closeB = currentLine.find(L'}');
                if (closeB == std::wstring::npos || cursorCol <= closeB) {
                    insideJsImportClause = true;
                }
            }
        }
    }

    // Parse inline quotes and HTML/JSX attribute context on the current line
    bool isInsideQuotes = false;
    wchar_t activeQuote = L'\0';
    size_t openQuotePos = std::wstring::npos;
    for (size_t i = 0; i < cursorCol && i < currentLine.length(); ++i) {
        wchar_t c = currentLine[i];
        if (activeQuote != L'\0') {
            if (c == L'\\') {
                i++; // skip escaped char
                continue;
            }
            if (c == activeQuote) {
                activeQuote = L'\0';
                openQuotePos = std::wstring::npos;
            }
        } else {
            if (c == L'"' || c == L'\'' || (c == L'`' && isJs)) {
                activeQuote = c;
                openQuotePos = i;
            }
        }
    }
    isInsideQuotes = (activeQuote != L'\0');

    std::wstring enclosingAttrName;
    bool insideHtmlTag = false;
    if (isHtml || isJs || isPhp) {
        size_t openTag = currentLine.rfind(L'<', openQuotePos);
        size_t closeTag = currentLine.rfind(L'>', openQuotePos);
        if (openTag != std::wstring::npos && (closeTag == std::wstring::npos || openTag > closeTag)) {
            insideHtmlTag = true;
        }
    }
    if ((isHtml || isJs || isPhp) && isInsideQuotes && openQuotePos != std::wstring::npos) {
        size_t eqPos = openQuotePos;
        while (eqPos > 0 && iswspace(currentLine[eqPos - 1])) eqPos--;
        // Also support JSX className={"..."} or className={`...`}
        if (eqPos > 0 && currentLine[eqPos - 1] == L'{') {
            eqPos--;
            while (eqPos > 0 && iswspace(currentLine[eqPos - 1])) eqPos--;
        }
        if (eqPos > 0 && currentLine[eqPos - 1] == L'=') {
            eqPos--; // skip '='
            while (eqPos > 0 && iswspace(currentLine[eqPos - 1])) eqPos--;
            size_t nameEnd = eqPos;
            size_t nameStart = nameEnd;
            while (nameStart > 0 && (iswalnum(currentLine[nameStart - 1]) || currentLine[nameStart - 1] == L'-' || currentLine[nameStart - 1] == L'_' || currentLine[nameStart - 1] == L':')) {
                nameStart--;
            }
            if (nameEnd > nameStart) {
                std::wstring candAttr = currentLine.substr(nameStart, nameEnd - nameStart);
                for (auto& chA : candAttr) chA = static_cast<wchar_t>(towlower(chA));
                if (insideHtmlTag || candAttr == L"class" || candAttr == L"classname" || candAttr == L"id" ||
                    candAttr == L"style" || candAttr == L"href" || candAttr == L"src" || candAttr == L"type" ||
                    candAttr == L"rel" || candAttr == L"target" || candAttr == L"method" || candAttr == L"htmlfor") {
                    enclosingAttrName = candAttr;
                    insideHtmlTag = true;
                }
            }
        }
    }

    bool isClassAttrContext = (isInsideQuotes && (enclosingAttrName == L"class" || enclosingAttrName == L"classname"));
    bool isIdAttrContext = (isInsideQuotes && enclosingAttrName == L"id");
    bool quoteAttrTrigger = isInsideQuotes && !enclosingAttrName.empty();

    std::wstring fnameOnly = std::filesystem::path(fileName).filename().wstring();
    std::wstring fnameLower = fnameOnly;
    for (auto& chF : fnameLower) chF = static_cast<wchar_t>(towlower(chF));
    bool isEnvFile = (fnameLower == L".env" || fnameLower.rfind(L".env.", 0) == 0 || ext == L".env");
    bool isSqlFile = (ext == L".sql");

    // Detect .env variable lookup context: getenv('...'), $_ENV['...'], $_SERVER['...'], process.env.*, import.meta.env.*, os.environ[...], os.getenv(...)
    bool isEnvKeyContext = isEnvFile;
    if (!isEnvKeyContext) {
        size_t scanLimit = (std::min)(cursorCol, currentLine.length());
        std::wstring lineBefore = currentLine.substr(0, scanLimit);
        if (lineBefore.find(L"process.env.") != std::wstring::npos ||
            lineBefore.find(L"import.meta.env.") != std::wstring::npos) {
            isEnvKeyContext = true;
        } else if (isInsideQuotes && openQuotePos != std::wstring::npos) {
            std::wstring beforeCall = currentLine.substr(0, openQuotePos);
            std::wstring beforeCallLower = beforeCall;
            for (auto& chB : beforeCallLower) chB = static_cast<wchar_t>(towlower(chB));
            if (beforeCallLower.find(L"getenv(") != std::wstring::npos ||
                beforeCallLower.find(L"$_env[") != std::wstring::npos ||
                beforeCallLower.find(L"$_server[") != std::wstring::npos ||
                beforeCallLower.find(L"os.environ") != std::wstring::npos ||
                beforeCallLower.find(L"env(") != std::wstring::npos) {
                isEnvKeyContext = true;
            }
        }
    }

    // Detect SQL query context in .sql files OR SQL strings/calls in PHP, Python, JS, TS
    auto hasSqlToken = [](const std::wstring& lineText) -> bool {
        std::wstring up;
        up.reserve(lineText.size());
        for (wchar_t ch : lineText) up.push_back(static_cast<wchar_t>(towupper(ch)));
        static const wchar_t* kSqlWords[] = {
            L"SELECT ", L"FROM ", L"JOIN ", L"WHERE ", L"INSERT INTO ", L"UPDATE ",
            L"DELETE FROM ", L"ORDER BY ", L"GROUP BY ", L"ALTER TABLE ", L"CREATE TABLE ",
            L"HAVING ", L"->QUERY(", L"->PREPARE(", L"->EXEC(", L"CURSOR.EXECUTE(", L"DB::TABLE("
        };
        for (const auto* kw : kSqlWords) {
            if (up.find(kw) != std::wstring::npos) return true;
        }
        return false;
    };
    bool isSqlContext = isSqlFile || hasSqlToken(currentLine);
    if (!isSqlContext && isInsideQuotes && pDocLines && !pDocLines->empty()) {
        size_t checked = 0;
        for (const auto& dl : *pDocLines) {
            if (hasSqlToken(dl)) {
                // Only check up to cursor line
                isSqlContext = true;
            }
            if (&dl == &currentLine || ++checked >= 120) break;
        }
    }

    // If typing inside quotes and NOT inside an HTML tag attribute, PHP server key, JS import, .env lookup, SQL query, or file path prefix:
    if (isInsideQuotes && !insideHtmlTag && !insideServerKey && !insideJsImportClause && !isEnvKeyContext && !isSqlContext) {
        bool isPathPrefix = (!prefix.empty() && (prefix[0] == L'.' || prefix[0] == L'/' || prefix[0] == L'\\'));
        bool isImportLine = (currentLine.find(L"import ") != std::wstring::npos || currentLine.find(L"require(") != std::wstring::npos || currentLine.find(L"include") != std::wstring::npos);
        if (!isPathPrefix && !isImportLine) {
            // General string literal in source code: Do not pop up code completions or HTML attributes!
            return {};
        }
    }

    // If prefix is empty and cursor is NOT at an explicit member/tag/key/import/attribute/env/sql trigger, do not dump unfiltered completions!
    bool pyImportTrigger = isPy && (TrimWs(currentLine).rfind(L"import ", 0) == 0 || TrimWs(currentLine).rfind(L"from ", 0) == 0)
                           && cursorCol > 0 && cursorCol <= currentLine.length() && currentLine[cursorCol - 1] == L' ';
    bool envTrigger = isEnvKeyContext && (afterDot || (isInsideQuotes && openQuotePos != std::wstring::npos && cursorCol >= openQuotePos));
    bool sqlDotTrigger = isSqlContext && afterDot;
    if (prefixLower.empty() && !afterDoubleColon && !afterArrow && !afterDot && !afterOpenAngle && !insideServerKey && !insideJsImportClause && !pyImportTrigger && !quoteAttrTrigger && !envTrigger && !sqlDotTrigger) {
        return {};
    }

    std::vector<IntelliSenseItem> rawCandidates;

    // 0a. .env Variable Autocomplete & Live Local Database Schema IntelliSense
    {
        std::lock_guard<std::mutex> ctxLock(m_contextMutex);
        if (isEnvKeyContext && !m_envVars.empty()) {
            for (const auto& ev : m_envVars) {
                IntelliSenseItem item;
                item.label = ev.key;
                item.detail = ev.valuePreview.empty()
                    ? (L"Env key (" + ev.sourceFile + L")")
                    : (ev.valuePreview + L" [" + ev.sourceFile + L"]");
                item.insertText = isEnvFile ? (ev.key + L"=") : ev.key;
                item.kind = CompletionKind::Constant;
                item.origin = L"env";
                item.score = 3600;
                rawCandidates.push_back(std::move(item));
            }
            if (!isEnvFile && (isInsideQuotes || afterDot)) {
                pyExclusive = true;
                pyExclusiveItems = rawCandidates;
            }
        }

        if (isSqlContext) {
            std::wstring tableQualifier;
            if (afterDot && beforePos >= 2) {
                size_t qEnd = beforePos - 1;
                size_t qStart = qEnd;
                while (qStart > 0 && (iswalnum(currentLine[qStart - 1]) || currentLine[qStart - 1] == L'_')) {
                    qStart--;
                }
                if (qEnd > qStart) {
                    tableQualifier = currentLine.substr(qStart, qEnd - qStart);
                }
            }

            std::vector<IntelliSenseItem> sqlItems;
            if (!tableQualifier.empty()) {
                for (const auto& col : m_dbSchema) {
                    if (_wcsicmp(col.tableName.c_str(), tableQualifier.c_str()) == 0) {
                        IntelliSenseItem item;
                        item.label = col.columnName;
                        item.detail = col.tableName + L"." + col.columnName + L" (" + col.dataType + L")" +
                                      (col.keyType.empty() ? L"" : (L" [" + col.keyType + L"]"));
                        item.insertText = col.columnName;
                        item.kind = CompletionKind::Property;
                        item.origin = L"db-schema";
                        item.score = 4200;
                        sqlItems.push_back(std::move(item));
                    }
                }
                if (!sqlItems.empty()) {
                    pyExclusive = true;
                    pyExclusiveItems = std::move(sqlItems);
                }
            } else {
                std::unordered_set<std::wstring> addedTables;
                std::wstring dbLabel = m_dbName.empty() ? L"MySQL" : m_dbName;
                for (const auto& col : m_dbSchema) {
                    std::wstring tLow = col.tableName;
                    for (auto& cT : tLow) cT = static_cast<wchar_t>(towlower(cT));
                    if (addedTables.insert(tLow).second) {
                        IntelliSenseItem tItem;
                        tItem.label = col.tableName;
                        tItem.detail = L"Table in " + dbLabel;
                        tItem.insertText = col.tableName;
                        tItem.kind = CompletionKind::Class;
                        tItem.origin = L"db-schema";
                        tItem.score = 3400;
                        sqlItems.push_back(std::move(tItem));
                    }
                    IntelliSenseItem cItem;
                    cItem.label = col.columnName;
                    cItem.detail = col.tableName + L"." + col.columnName + L" (" + col.dataType + L")" +
                                   (col.keyType.empty() ? L"" : (L" [" + col.keyType + L"]"));
                    cItem.insertText = col.columnName;
                    cItem.kind = CompletionKind::Property;
                    cItem.origin = L"db-schema";
                    cItem.score = 3200;
                    sqlItems.push_back(std::move(cItem));
                }
                if (isSqlFile) {
                    static const wchar_t* kSqlKwList[] = {
                        L"SELECT", L"FROM", L"WHERE", L"JOIN", L"LEFT JOIN", L"INNER JOIN",
                        L"INSERT INTO", L"VALUES", L"UPDATE", L"SET", L"DELETE FROM",
                        L"ORDER BY", L"GROUP BY", L"HAVING", L"LIMIT", L"OFFSET",
                        L"CREATE TABLE", L"ALTER TABLE", L"DROP TABLE", L"PRIMARY KEY",
                        L"AUTO_INCREMENT", L"NOT NULL", L"VARCHAR", L"INT", L"BIGINT",
                        L"TEXT", L"DATETIME", L"TIMESTAMP", L"BOOLEAN"
                    };
                    for (const auto* sk : kSqlKwList) {
                        sqlItems.push_back({ sk, L"SQL Keyword", sk, CompletionKind::Keyword, 2200, 0, 0, L"sql" });
                    }
                }
                if (isSqlFile || (isInsideQuotes && !sqlItems.empty())) {
                    pyExclusive = true;
                    pyExclusiveItems = std::move(sqlItems);
                } else {
                    for (auto& si : sqlItems) rawCandidates.push_back(std::move(si));
                }
            }
        }
    }

    // 0. Include matching user & built-in snippets (exclude HTML markup snippets when inside <?php ... ?> code, inside quotes, or right after '<')
    std::wstring extNoDot = ext.empty() ? L"" : (ext[0] == L'.' ? ext.substr(1) : ext);
    if (m_pSnippetManager && !isInsideQuotes && !afterDoubleColon && !afterArrow && !afterDot && !insideServerKey && !afterOpenAngle) {
        for (const auto& snip : m_pSnippetManager->GetSnippets()) {
            if (insidePhp && snip.scope != L"php") continue;
            if (SnippetManager::MatchesScope(snip.scope, extNoDot)) {
                IntelliSenseItem sItem;
                sItem.label = snip.prefix;
                sItem.detail = snip.name + (snip.description.empty() ? L"" : (L" - " + snip.description));
                sItem.insertText = snip.prefix;
                sItem.kind = CompletionKind::Snippet;
                sItem.origin = L"snippets";
                sItem.score = 600;
                rawCandidates.push_back(std::move(sItem));
            }
        }
    }

    // Detect contextual block (embedded <style> or <script>) in HTML/PHP files
    bool insideStyle = isCss || (enclosingAttrName == L"style");
    bool insideScript = isJs || (enclosingAttrName.rfind(L"on", 0) == 0 && enclosingAttrName.length() > 2);

    if (isHtml) {
        // Check if cursor line has inline style="..." or on*="..." attributes
        size_t styleAttr = currentLine.rfind(L"style=\"", cursorCol > 0 ? cursorCol - 1 : 0);
        if (styleAttr != std::wstring::npos) {
            size_t closeQ = currentLine.find(L'"', styleAttr + 7);
            if (closeQ == std::wstring::npos || cursorCol <= closeQ) {
                insideStyle = true;
            }
        }
        if (!insideStyle && pDocLines && !pDocLines->empty()) {
            // Scan backwards from current line to find enclosing <style> or <script> tags
            size_t curL = 0;
            // Approximate line by finding matching text or looking at last lines
            for (size_t l = 0; l < pDocLines->size(); ++l) {
                const auto& dLine = (*pDocLines)[l];
                if (dLine.find(L"<style") != std::wstring::npos) insideStyle = true;
                if (dLine.find(L"</style>") != std::wstring::npos) insideStyle = false;
                if (dLine.find(L"<script") != std::wstring::npos) insideScript = true;
                if (dLine.find(L"</script>") != std::wstring::npos) insideScript = false;
            }
        }
    }

    // Collect document & workspace symbols first so we know variable types (e.g. $pdo -> PDO, $stmt -> PDOStatement)
    std::vector<IntelliSenseItem> workspaceVarSymbols;
    if (pDocLines && !pDocLines->empty()) {
        auto docSymbols = ExtractDocumentSymbols(*pDocLines, ext);
        for (auto& sym : docSymbols) {
            workspaceVarSymbols.push_back(std::move(sym));
        }
    }
    if (isPhp) {
        std::error_code ec;
        std::filesystem::path scanDir;
        if (!fileName.empty()) {
            std::filesystem::path fp(fileName);
            if (fp.has_parent_path() && std::filesystem::exists(fp.parent_path(), ec)) {
                scanDir = fp.parent_path();
            }
        }
        if (scanDir.empty() && !m_workspaceRoot.empty() && std::filesystem::exists(m_workspaceRoot, ec)) {
            scanDir = std::filesystem::path(m_workspaceRoot);
        }
        if (!scanDir.empty() && std::filesystem::is_directory(scanDir, ec)) {
            int filesScanned = 0;
            for (std::filesystem::directory_iterator it(scanDir, ec); !ec && it != std::filesystem::directory_iterator() && filesScanned < 12; it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                auto p = it->path();
                if (p.extension() != L".php") continue;
                if (!fileName.empty() && std::filesystem::equivalent(p, std::filesystem::path(fileName), ec)) continue;
                std::ifstream ifs(p, std::ios::binary);
                if (!ifs) continue;
                filesScanned++;
                std::vector<std::wstring> peerLines;
                std::string u8Line;
                while (std::getline(ifs, u8Line) && peerLines.size() < 400) {
                    std::wstring wLine(u8Line.begin(), u8Line.end());
                    peerLines.push_back(std::move(wLine));
                }
                auto peerSyms = ExtractDocumentSymbols(peerLines, L".php");
                for (auto& sym : peerSyms) {
                    workspaceVarSymbols.push_back(std::move(sym));
                }
            }
        }
    }

    if (insideStyle) {
        // In CSS context: Check if typing inside rule block { ... } or at selector level
        bool insideBraces = false;
        size_t lastOpenBrace = currentLine.rfind(L'{', cursorCol > 0 ? cursorCol - 1 : 0);
        size_t lastCloseBrace = currentLine.rfind(L'}', cursorCol > 0 ? cursorCol - 1 : 0);
        if (lastOpenBrace != std::wstring::npos && (lastCloseBrace == std::wstring::npos || lastOpenBrace > lastCloseBrace)) {
            insideBraces = true;
        } else if (pDocLines && !pDocLines->empty()) {
            int depth = 0;
            for (const auto& dLine : *pDocLines) {
                if (&dLine == &currentLine) {
                    for (size_t i = 0; i < cursorCol && i < dLine.length(); ++i) {
                        if (dLine[i] == L'{') depth++;
                        else if (dLine[i] == L'}') { if (depth > 0) depth--; }
                    }
                    break;
                }
                for (wchar_t ch : dLine) {
                    if (ch == L'{') depth++;
                    else if (ch == L'}') { if (depth > 0) depth--; }
                }
            }
            insideBraces = (depth > 0);
        }

        size_t lastColon = currentLine.rfind(L':', cursorCol > 0 ? cursorCol - 1 : 0);
        size_t lastSemi = currentLine.rfind(L';', cursorCol > 0 ? cursorCol - 1 : 0);
        bool colonPrecededByIdent = (lastColon != std::wstring::npos && lastColon > 0 && iswalnum(currentLine[lastColon - 1]));
        bool typingValue = insideBraces && (lastColon != std::wstring::npos && (lastSemi == std::wstring::npos || lastColon > lastSemi) && colonPrecededByIdent);

        if (typingValue) {
            // Extract property name before ':'
            size_t propEnd = lastColon;
            while (propEnd > 0 && iswspace(currentLine[propEnd - 1])) propEnd--;
            size_t propStart = propEnd;
            while (propStart > 0 && (iswalnum(currentLine[propStart - 1]) || currentLine[propStart - 1] == L'-' || currentLine[propStart - 1] == L'_')) {
                propStart--;
            }
            std::wstring propName = currentLine.substr(propStart, propEnd - propStart);
            for (auto& chP : propName) chP = static_cast<wchar_t>(towlower(chP));

            // Check if the user is typing a numeric CSS value (e.g. `20`, `1.5`, `-10`, `20p`, `100%`)
            bool isNumericPrefix = false;
            std::wstring numPart;
            std::wstring unitPart;
            if (!prefix.empty()) {
                size_t idx = 0;
                if (prefix[idx] == L'-' || prefix[idx] == L'+') idx++;
                size_t digitStart = idx;
                bool hasDigit = false;
                while (idx < prefix.length() && iswdigit(prefix[idx])) {
                    hasDigit = true;
                    idx++;
                }
                if (idx < prefix.length() && prefix[idx] == L'.') {
                    idx++;
                    while (idx < prefix.length() && iswdigit(prefix[idx])) {
                        hasDigit = true;
                        idx++;
                    }
                }
                if (hasDigit && idx > digitStart) {
                    numPart = prefix.substr(0, idx);
                    unitPart = prefix.substr(idx);
                    bool validUnitChars = true;
                    for (wchar_t uc : unitPart) {
                        if (!iswalpha(uc) && uc != L'%') {
                            validUnitChars = false;
                            break;
                        }
                    }
                    if (validUnitChars) {
                        isNumericPrefix = true;
                    }
                }
            }

            if (isNumericPrefix) {
                // Exclusively return CSS unit completions for the typed number (no unrelated identifier or U+... matches)
                pyExclusive = true;
                struct CssUnitDef {
                    const wchar_t* unit;
                    const wchar_t* desc;
                    int category; // 0 = length/sizing, 1 = grid flex, 2 = angle, 3 = time, 4 = resolution
                };
                static const CssUnitDef s_units[] = {
                    { L"px",    L"Pixels (1px = 1/96th of 1in)", 0 },
                    { L"rem",   L"Root em (relative to root <html> font-size)", 0 },
                    { L"em",    L"Relative to the font-size of the element", 0 },
                    { L"%",     L"Percentage relative to the parent element", 0 },
                    { L"vh",    L"Viewport height (1% of viewport height)", 0 },
                    { L"vw",    L"Viewport width (1% of viewport width)", 0 },
                    { L"dvh",   L"Dynamic viewport height (1% of dynamic viewport height)", 0 },
                    { L"dvw",   L"Dynamic viewport width (1% of dynamic viewport width)", 0 },
                    { L"svh",   L"Small viewport height (1% of small viewport height)", 0 },
                    { L"svw",   L"Small viewport width (1% of small viewport width)", 0 },
                    { L"lvh",   L"Large viewport height (1% of large viewport height)", 0 },
                    { L"lvw",   L"Large viewport width (1% of large viewport width)", 0 },
                    { L"vmin",  L"Viewport minimum (1% of smaller viewport dimension)", 0 },
                    { L"vmax",  L"Viewport maximum (1% of larger viewport dimension)", 0 },
                    { L"ch",    L"Width of the '0' (zero) glyph in the element's font", 0 },
                    { L"ex",    L"x-height of the element's font", 0 },
                    { L"lh",    L"Line height of the element", 0 },
                    { L"rlh",   L"Root line height of the <html> element", 0 },
                    { L"cqw",   L"1% of a query container's width", 0 },
                    { L"cqh",   L"1% of a query container's height", 0 },
                    { L"cqi",   L"1% of a query container's inline size", 0 },
                    { L"cqb",   L"1% of a query container's block size", 0 },
                    { L"fr",    L"Flexible fraction of available space in grid layout", 1 },
                    { L"pt",    L"Points (1pt = 1/72nd of 1in)", 0 },
                    { L"cm",    L"Centimeters", 0 },
                    { L"mm",    L"Millimeters", 0 },
                    { L"in",    L"Inches (1in = 96px = 2.54cm)", 0 },
                    { L"pc",    L"Picas (1pc = 12pt)", 0 },
                    { L"s",     L"Seconds (time duration/delay)", 3 },
                    { L"ms",    L"Milliseconds (1000ms = 1s)", 3 },
                    { L"deg",   L"Degrees (full circle = 360deg)", 2 },
                    { L"rad",   L"Radians (full circle = 2π rad)", 2 },
                    { L"turn",  L"Turns (full circle = 1turn)", 2 },
                    { L"grad",  L"Gradians (full circle = 400grad)", 2 },
                    { L"dpi",   L"Dots per inch (screen/print resolution)", 4 },
                    { L"dppx",  L"Dots per px unit (1dppx = 96dpi)", 4 }
                };

                bool isTimeProp = (propName.find(L"duration") != std::wstring::npos ||
                                   propName.find(L"delay") != std::wstring::npos ||
                                   propName == L"transition" || propName == L"animation");
                bool isAngleProp = (propName == L"rotate" || propName == L"transform" ||
                                    propName.find(L"skew") != std::wstring::npos ||
                                    propName == L"hue-rotate" || propName == L"offset-rotate");
                bool isGridProp = (propName.find(L"grid") != std::wstring::npos);

                // For font-weight, offer standard numeric weights (100..900) if matching
                if (propName == L"font-weight" || propName == L"font") {
                    static const struct WeightDef { const wchar_t* w; const wchar_t* d; } s_weights[] = {
                        { L"100", L"Thin (Hairline) font weight" },
                        { L"200", L"Extra Light (Ultra Light) font weight" },
                        { L"300", L"Light font weight" },
                        { L"400", L"Normal (Regular) font weight" },
                        { L"500", L"Medium font weight" },
                        { L"600", L"Semi Bold (Demi Bold) font weight" },
                        { L"700", L"Bold font weight" },
                        { L"800", L"Extra Bold (Ultra Bold) font weight" },
                        { L"900", L"Black (Heavy) font weight" }
                    };
                    for (int wi = 0; wi < 9; ++wi) {
                        IntelliSenseItem wItem;
                        wItem.label = s_weights[wi].w;
                        wItem.detail = s_weights[wi].d;
                        wItem.insertText = s_weights[wi].w;
                        wItem.kind = CompletionKind::Constant;
                        wItem.score = 2600 - wi * 5;
                        wItem.origin = L"css";
                        pyExclusiveItems.push_back(std::move(wItem));
                    }
                }

                int rank = 0;
                for (const auto& u : s_units) {
                    int baseScore = 2400 - rank * 8;
                    if (isTimeProp && u.category == 3) baseScore += 400;
                    else if (isAngleProp && u.category == 2) baseScore += 400;
                    else if (isGridProp && u.category == 1) baseScore += 350;
                    else if (!isTimeProp && !isAngleProp && (u.category == 2 || u.category == 3 || u.category == 4)) {
                        baseScore -= 300;
                    }

                    std::wstring fullVal = numPart + u.unit;
                    IntelliSenseItem uItem;
                    uItem.label = fullVal;
                    uItem.detail = std::wstring(L"CSS Unit — ") + u.desc;
                    uItem.insertText = fullVal;
                    uItem.kind = CompletionKind::Constant;
                    uItem.score = baseScore;
                    uItem.origin = L"css";
                    pyExclusiveItems.push_back(std::move(uItem));
                    rank++;
                }
            } else {
                // Curated property-specific CSS values & functions prioritized above generic values
                struct PropValDef { const wchar_t* val; const wchar_t* desc; const wchar_t* insert; };
                auto addPropVal = [&](const wchar_t* val, const wchar_t* desc, int bonus = 600, const wchar_t* customInsert = nullptr) {
                    IntelliSenseItem it;
                    it.label = val;
                    it.detail = desc;
                    it.insertText = customInsert ? customInsert : val;
                    it.kind = ( std::wcschr(val, L'(') != nullptr ) ? CompletionKind::Function : CompletionKind::Constant;
                    it.score = 1200 + bonus;
                    it.origin = L"css";
                    rawCandidates.push_back(std::move(it));
                };

                // Sizing & Dimension properties (width, height, min/max-width/height, flex-basis, inline-size, block-size)
                if (propName == L"width" || propName == L"height" ||
                    propName == L"min-width" || propName == L"max-width" ||
                    propName == L"min-height" || propName == L"max-height" ||
                    propName == L"inline-size" || propName == L"block-size" ||
                    propName == L"min-inline-size" || propName == L"max-inline-size" ||
                    propName == L"min-block-size" || propName == L"max-block-size" ||
                    propName == L"flex-basis") {
                    addPropVal(L"auto", L"Browser calculates and selects the dimension automatically", 700);
                    addPropVal(L"100%", L"Full size of parent containing block", 690);
                    addPropVal(L"fit-content", L"Uses available space, but never more than max-content", 680);
                    addPropVal(L"max-content", L"Intrinsic preferred dimension without wrapping", 670);
                    addPropVal(L"min-content", L"Intrinsic minimum dimension without overflowing", 660);
                    addPropVal(L"100vw", L"100% of the viewport width", 650);
                    addPropVal(L"100vh", L"100% of the viewport height", 645);
                    addPropVal(L"100dvh", L"100% of the dynamic viewport height", 640);
                    addPropVal(L"calc()", L"Performs mathematical calculations on CSS values", 635, L"calc($0)");
                    addPropVal(L"clamp()", L"Clamps a value between a minimum and maximum bound: clamp(min, val, max)", 630, L"clamp($0)");
                    addPropVal(L"min()", L"Uses the smallest value from a comma-separated list of expressions", 625, L"min($0)");
                    addPropVal(L"max()", L"Uses the largest value from a comma-separated list of expressions", 620, L"max($0)");
                    addPropVal(L"var()", L"Inserts the value of a CSS custom property (--variable)", 615, L"var(--$0)");
                    if (propName.rfind(L"max-", 0) == 0) {
                        addPropVal(L"none", L"No maximum size limit applied", 695);
                    }
                } else if (propName == L"display") {
                    addPropVal(L"flex", L"Block-level flex container", 700);
                    addPropVal(L"grid", L"Block-level grid container", 695);
                    addPropVal(L"block", L"Block-level element box", 690);
                    addPropVal(L"inline-block", L"Inline-level block container", 685);
                    addPropVal(L"inline-flex", L"Inline-level flex container", 680);
                    addPropVal(L"inline-grid", L"Inline-level grid container", 675);
                    addPropVal(L"inline", L"Inline element box", 670);
                    addPropVal(L"none", L"Hides element and removes it from document layout", 665);
                    addPropVal(L"contents", L"Element box is replaced by its children in layout", 660);
                    addPropVal(L"flow-root", L"Block container that establishes a new block formatting context", 655);
                    addPropVal(L"table", L"Behaves like an HTML <table> element", 650);
                } else if (propName == L"position") {
                    addPropVal(L"relative", L"Positioned relative to its normal flow position", 700);
                    addPropVal(L"absolute", L"Positioned relative to its nearest positioned ancestor", 695);
                    addPropVal(L"fixed", L"Positioned relative to the viewport", 690);
                    addPropVal(L"sticky", L"Toggles between relative and fixed depending on scroll position", 685);
                    addPropVal(L"static", L"Default normal document flow positioning", 680);
                } else if (propName == L"flex-direction") {
                    addPropVal(L"row", L"Main axis runs horizontally left-to-right", 700);
                    addPropVal(L"column", L"Main axis runs vertically top-to-bottom", 695);
                    addPropVal(L"row-reverse", L"Main axis runs horizontally right-to-left", 690);
                    addPropVal(L"column-reverse", L"Main axis runs vertically bottom-to-top", 685);
                } else if (propName == L"flex-wrap") {
                    addPropVal(L"nowrap", L"Flex items are laid out in a single line", 700);
                    addPropVal(L"wrap", L"Flex items wrap onto multiple lines", 695);
                    addPropVal(L"wrap-reverse", L"Flex items wrap onto multiple lines in reverse order", 690);
                } else if (propName == L"justify-content" || propName == L"align-content" || propName == L"place-content") {
                    addPropVal(L"center", L"Pack items around the center", 700);
                    addPropVal(L"space-between", L"Distribute items evenly; first at start, last at end", 695);
                    addPropVal(L"space-around", L"Distribute items evenly with half-size space on ends", 690);
                    addPropVal(L"space-evenly", L"Distribute items evenly with equal space around them", 685);
                    addPropVal(L"flex-start", L"Pack flex items from the start", 680);
                    addPropVal(L"flex-end", L"Pack flex items from the end", 675);
                    addPropVal(L"start", L"Pack items from the writing-mode start edge", 670);
                    addPropVal(L"end", L"Pack items from the writing-mode end edge", 665);
                    addPropVal(L"stretch", L"Stretch auto-sized items to fill the container", 660);
                } else if (propName == L"align-items" || propName == L"align-self" || propName == L"justify-items" || propName == L"justify-self" || propName == L"place-items" || propName == L"place-self") {
                    addPropVal(L"center", L"Centers items along the axis", 700);
                    addPropVal(L"stretch", L"Stretches items to fill the container", 695);
                    addPropVal(L"flex-start", L"Aligns items to the start of the cross axis", 690);
                    addPropVal(L"flex-end", L"Aligns items to the end of the cross axis", 685);
                    addPropVal(L"start", L"Aligns items to the start edge", 680);
                    addPropVal(L"end", L"Aligns items to the end edge", 675);
                    addPropVal(L"baseline", L"Aligns items along their text baselines", 670);
                } else if (propName == L"box-sizing") {
                    addPropVal(L"border-box", L"Width and height include content, padding, and border", 700);
                    addPropVal(L"content-box", L"Width and height apply only to the content box", 695);
                } else if (propName == L"overflow" || propName == L"overflow-x" || propName == L"overflow-y") {
                    addPropVal(L"hidden", L"Overflow content is clipped with no scrollbars", 700);
                    addPropVal(L"auto", L"Displays scrollbars only when content overflows", 695);
                    addPropVal(L"scroll", L"Always displays scrollbars for overflow", 690);
                    addPropVal(L"visible", L"Overflow content renders outside the element box", 685);
                    addPropVal(L"clip", L"Clips overflow at the overflow clip edge without scrolling", 680);
                } else if (propName == L"cursor") {
                    addPropVal(L"pointer", L"Hand pointer indicating a clickable link/button", 700);
                    addPropVal(L"default", L"Platform default arrow cursor", 695);
                    addPropVal(L"not-allowed", L"Indicates the requested action will not be executed", 690);
                    addPropVal(L"grab", L"Indicates an item can be grabbed/dragged", 685);
                    addPropVal(L"grabbing", L"Indicates an item is currently being grabbed", 680);
                    addPropVal(L"text", L"I-beam cursor indicating selectable text", 675);
                    addPropVal(L"wait", L"Indicates the program is busy", 670);
                    addPropVal(L"crosshair", L"Crosshair cursor", 665);
                    addPropVal(L"move", L"Indicates something is to be moved", 660);
                    addPropVal(L"none", L"No cursor is rendered", 655);
                } else if (propName == L"text-align") {
                    addPropVal(L"center", L"Centers inline content", 700);
                    addPropVal(L"left", L"Aligns inline content to the left edge", 695);
                    addPropVal(L"right", L"Aligns inline content to the right edge", 690);
                    addPropVal(L"justify", L"Spaces inline content to line up left and right edges", 685);
                    addPropVal(L"start", L"Aligns to start edge of writing mode", 680);
                    addPropVal(L"end", L"Aligns to end edge of writing mode", 675);
                } else if (propName == L"font-weight") {
                    addPropVal(L"bold", L"Bold font weight (700)", 700);
                    addPropVal(L"normal", L"Normal font weight (400)", 695);
                    addPropVal(L"600", L"Semi-Bold font weight", 690);
                    addPropVal(L"500", L"Medium font weight", 685);
                    addPropVal(L"700", L"Bold font weight", 680);
                    addPropVal(L"300", L"Light font weight", 675);
                    addPropVal(L"800", L"Extra-Bold font weight", 670);
                    addPropVal(L"900", L"Black font weight", 665);
                    addPropVal(L"bolder", L"One relative weight heavier than parent", 660);
                    addPropVal(L"lighter", L"One relative weight lighter than parent", 655);
                } else if (propName == L"object-fit") {
                    addPropVal(L"cover", L"Scales to fill container while preserving aspect ratio (clips excess)", 700);
                    addPropVal(L"contain", L"Scales to fit inside container while preserving aspect ratio", 695);
                    addPropVal(L"fill", L"Stretches to fill container (may distort aspect ratio)", 690);
                    addPropVal(L"none", L"Replaced content is not resized", 685);
                    addPropVal(L"scale-down", L"Sizes as if none or contain were specified, whichever is smaller", 680);
                } else {
                    // Common CSS functions & global keywords useful for any property
                    addPropVal(L"var()", L"Inserts the value of a CSS custom property (--variable)", 400, L"var(--$0)");
                    addPropVal(L"calc()", L"Performs mathematical calculations on CSS values", 390, L"calc($0)");
                }

                for (const auto& v : m_cssValues) rawCandidates.push_back(v);
            }
        } else {
            for (const auto& p : m_cssProperties) rawCandidates.push_back(p);

            // Check for CSS Emmet abbreviation
            std::wstring cssEmmet = EmmetEngine::ExtractAbbreviation(currentLine, cursorCol, true, nullptr);
            if (!cssEmmet.empty() && EmmetEngine::IsEmmetAbbreviation(cssEmmet, true)) {
                std::wstring expandedCss = EmmetEngine::ExpandCss(cssEmmet);
                if (!expandedCss.empty()) {
                    IntelliSenseItem cItem;
                    cItem.label = cssEmmet;
                    cItem.detail = L"CSS Emmet\n" + expandedCss;
                    cItem.insertText = expandedCss;
                    cItem.kind = CompletionKind::Snippet;
                    cItem.origin = L"emmet";
                    cItem.score = 4000;
                    rawCandidates.push_back(std::move(cItem));
                }
            }

            // Standard CSS Pseudo-Classes & Pseudo-Elements
            static const struct PseudoDef { const wchar_t* name; const wchar_t* desc; } cssPseudos[] = {
                { L":root", L"Matches the document root element (usually <html>). Primary selector for declaring global CSS variables." },
                { L":hover", L"Matches an element when the user hovers over it with a pointing device." },
                { L":focus", L"Matches an element when it has gained focus." },
                { L":focus-visible", L"Matches an element when focused and UA determines focus indication should be visible." },
                { L":focus-within", L"Matches an element if the element itself or any descendant has focus." },
                { L":active", L"Matches an element being activated by the user (e.g. mouse button down)." },
                { L":visited", L"Matches links that the user has already visited in browser history." },
                { L":checked", L"Matches radio buttons, checkboxes, or options that are checked/selected." },
                { L":disabled", L"Matches form elements that are in a disabled state." },
                { L":first-child", L"Matches an element that is the first child of its parent." },
                { L":last-child", L"Matches an element that is the last child of its parent." },
                { L":nth-child()", L"Matches elements based on their position in siblings (e.g. :nth-child(2n+1))." },
                { L":not()", L"Matches elements that do not match the selector list argument." },
                { L":is()", L"Takes a selector list and selects any element that matches any of the selectors in that list." },
                { L":where()", L"Takes a selector list and matches with 0 specificity." },
                { L":has()", L"Relational pseudo-class: matches an element if any selector in arguments matches relative to :scope." },
                { L"::before", L"Creates a pseudo-element that is the first child of the selected element (used with 'content')." },
                { L"::after", L"Creates a pseudo-element that is the last child of the selected element (used with 'content')." },
                { L"::placeholder", L"Represents the placeholder text in an <input> or <textarea> element." },
                { L"::selection", L"Applies styles to the part of a document that has been highlighted/selected by the user." },
                { L"::-webkit-scrollbar", L"Targets the entire scrollbar in WebKit-based rendering engines." }
            };

            for (const auto& ps : cssPseudos) {
                IntelliSenseItem it;
                it.label = ps.name;
                it.detail = ps.desc;
                it.insertText = ps.name;
                it.kind = CompletionKind::Keyword;
                it.score = 500;
                it.origin = L"css";
                rawCandidates.push_back(std::move(it));
            }

            if (!insideBraces) {
                static const struct AtRuleDef { const wchar_t* name; const wchar_t* desc; } atRules[] = {
                    { L"@media", L"Applies CSS styles conditionally based on media type and screen capabilities." },
                    { L"@keyframes", L"Controls the intermediate steps in a CSS animation sequence." },
                    { L"@import", L"Imports style rules from other style sheets." },
                    { L"@font-face", L"Specifies a custom font with which to display text." },
                    { L"@supports", L"Tests whether the browser supports a specific CSS property and value." }
                };
                for (const auto& ar : atRules) {
                    IntelliSenseItem it;
                    it.label = ar.name;
                    it.detail = ar.desc;
                    it.insertText = ar.name;
                    it.kind = CompletionKind::Keyword;
                    it.score = 500;
                    it.origin = L"css";
                    rawCandidates.push_back(std::move(it));
                }
            }
        }
    } else if (insideScript) {
        // 1. Parse all `import ... from '...'` lines in the current document to know imported symbols & namespaces
        std::unordered_map<std::wstring, std::wstring> jsModuleNamespaces; // e.g. Icons -> lucide-react, React -> react
        std::vector<IntelliSenseItem> importedDocSymbols;
        if (pDocLines && !pDocLines->empty()) {
            for (const auto& dLine : *pDocLines) {
                std::wstring tLine = TrimWs(dLine);
                if (tLine.rfind(L"import ", 0) != 0) continue;
                size_t fromIdx = tLine.find(L"from");
                if (fromIdx == std::wstring::npos) continue;
                size_t q1 = tLine.find_first_of(L"\"'`", fromIdx + 4);
                if (q1 == std::wstring::npos) continue;
                wchar_t qCh = tLine[q1];
                size_t q2 = tLine.find(qCh, q1 + 1);
                if (q2 == std::wstring::npos || q2 <= q1 + 1) continue;
                std::wstring modSpec = tLine.substr(q1 + 1, q2 - q1 - 1);

                std::wstring clause = TrimWs(tLine.substr(7, fromIdx - 7));
                if (clause.rfind(L"type ", 0) == 0) clause = TrimWs(clause.substr(5));

                // Check namespace import: `* as Ns`
                size_t starAs = clause.find(L"* as ");
                if (starAs != std::wstring::npos) {
                    size_t nsStart = starAs + 5;
                    while (nsStart < clause.length() && iswspace(clause[nsStart])) nsStart++;
                    size_t nsEnd = nsStart;
                    while (nsEnd < clause.length() && IsJsIdentChar(clause[nsEnd])) nsEnd++;
                    if (nsEnd > nsStart) {
                        std::wstring nsIdent = clause.substr(nsStart, nsEnd - nsStart);
                        jsModuleNamespaces[nsIdent] = modSpec;
                        IntelliSenseItem nsItem;
                        nsItem.label = nsIdent;
                        nsItem.detail = L"Namespace import from '" + modSpec + L"'";
                        nsItem.insertText = nsIdent;
                        nsItem.kind = CompletionKind::Module;
                        nsItem.score = 1350;
                        nsItem.origin = L"import";
                        importedDocSymbols.push_back(std::move(nsItem));
                    }
                }

                // Check named imports: `{ A, B as C }`
                size_t ob = clause.find(L'{');
                size_t cb = clause.find(L'}', ob != std::wstring::npos ? ob : 0);
                if (ob != std::wstring::npos && cb != std::wstring::npos && cb > ob) {
                    std::wstring inside = clause.substr(ob + 1, cb - ob - 1);
                    size_t pos = 0;
                    while (pos < inside.length()) {
                        size_t comma = inside.find(L',', pos);
                        if (comma == std::wstring::npos) comma = inside.length();
                        std::wstring tok = TrimWs(inside.substr(pos, comma - pos));
                        pos = comma + 1;
                        if (tok.rfind(L"type ", 0) == 0) tok = TrimWs(tok.substr(5));
                        size_t asP = tok.find(L" as ");
                        if (asP != std::wstring::npos) tok = TrimWs(tok.substr(asP + 4));
                        if (IsValidJsIdent(tok)) {
                            bool isComp = !tok.empty() && iswupper(tok[0]);
                            IntelliSenseItem impItem;
                            impItem.label = tok;
                            impItem.detail = (isComp ? L"Component / Type from '" : L"Imported from '") + modSpec + L"'";
                            impItem.insertText = tok;
                            impItem.kind = isComp ? CompletionKind::Class : CompletionKind::Function;
                            impItem.score = 1400;
                            impItem.origin = L"import";
                            importedDocSymbols.push_back(std::move(impItem));
                        }
                    }
                }

                // Check default import before '{' or ','
                std::wstring beforeBrace = (ob != std::wstring::npos) ? TrimWs(clause.substr(0, ob)) : clause;
                if (!beforeBrace.empty() && beforeBrace.back() == L',') {
                    beforeBrace.pop_back();
                    beforeBrace = TrimWs(beforeBrace);
                }
                if (IsValidJsIdent(beforeBrace)) {
                    jsModuleNamespaces[beforeBrace] = modSpec;
                    bool isComp = !beforeBrace.empty() && iswupper(beforeBrace[0]);
                    IntelliSenseItem defItem;
                    defItem.label = beforeBrace;
                    defItem.detail = L"Default import from '" + modSpec + L"'";
                    defItem.insertText = beforeBrace;
                    defItem.kind = isComp ? CompletionKind::Class : CompletionKind::Module;
                    defItem.score = 1400;
                    defItem.origin = L"import";
                    importedDocSymbols.push_back(std::move(defItem));
                }
            }
        }

        // Helper to gather dynamic exports from installed NPM dependencies (package.json) and workspace files
        auto collectDynamicProjectExports = [&](std::vector<IntelliSenseItem>& outList, bool onlyComponentsAndFunctions) {
            // A) Installed packages from package.json / node_modules (prioritize direct dependencies in package.json)
            const auto& npmPkgs = GetInstalledNpmPackages(projectRoot);
            size_t pkgCount = 0;
            for (const auto& [pkgName, pkgDetail] : npmPkgs) {
                // Prioritize direct dependencies (which have version numbers) and non-@types packages
                bool isDirectDep = (pkgDetail.find(L"node_modules") == std::wstring::npos && pkgDetail.find(L"@types") == std::wstring::npos);
                if (!isDirectDep && npmPkgs.size() > 35) continue;
                if (++pkgCount > 35) break;
                auto entryFile = ResolveModuleEntryFile(pkgName, baseDir, projectRoot);
                if (!entryFile.empty()) {
                    auto modExps = ParseModuleExportsFromFile(entryFile, pkgName, projectRoot);
                    for (auto& exp : modExps) {
                        if (onlyComponentsAndFunctions && exp.kind != CompletionKind::Class && exp.kind != CompletionKind::Function) {
                            continue;
                        }
                        outList.push_back(std::move(exp));
                    }
                }
            }

            // B) Peer workspace files (.js, .jsx, .ts, .tsx) in baseDir and immediate subfolders (e.g. ./components, ./utils)
            std::error_code ec;
            int wsFilesScanned = 0;
            auto scanDirForExports = [&](const std::filesystem::path& dirToScan, const std::wstring& relPrefix) {
                if (!std::filesystem::exists(dirToScan, ec) || !std::filesystem::is_directory(dirToScan, ec)) return;
                for (std::filesystem::directory_iterator it(dirToScan, ec); !ec && it != std::filesystem::directory_iterator() && wsFilesScanned < 30; it.increment(ec)) {
                    if (it->is_regular_file(ec)) {
                        auto p = it->path();
                        std::wstring fext = p.extension().wstring();
                        for (auto& c : fext) c = static_cast<wchar_t>(towlower(c));
                        if (fext != L".js" && fext != L".jsx" && fext != L".ts" && fext != L".tsx" && fext != L".mjs") continue;
                        if (!fileName.empty() && std::filesystem::equivalent(p, std::filesystem::path(fileName), ec)) continue;
                        wsFilesScanned++;
                        std::wstring stem = p.stem().wstring();
                        std::wstring relSpec = relPrefix + stem;
                        auto fileExps = ParseModuleExportsFromFile(p, relSpec, projectRoot);
                        for (auto& exp : fileExps) {
                            if (onlyComponentsAndFunctions && exp.kind != CompletionKind::Class && exp.kind != CompletionKind::Function) {
                                continue;
                            }
                            exp.score += 120; // Boost local workspace exports
                            outList.push_back(std::move(exp));
                        }
                    }
                }
            };
            scanDirForExports(baseDir, L"./");
            if (std::filesystem::exists(baseDir, ec) && std::filesystem::is_directory(baseDir, ec)) {
                for (std::filesystem::directory_iterator dit(baseDir, ec); !ec && dit != std::filesystem::directory_iterator() && wsFilesScanned < 30; dit.increment(ec)) {
                    if (!dit->is_directory(ec)) continue;
                    std::wstring dname = dit->path().filename().wstring();
                    if (dname.empty() || dname[0] == L'.' || dname == L"node_modules" || dname == L"dist" || dname == L"build") continue;
                    scanDirForExports(dit->path(), L"./" + dname + L"/");
                }
            }
        };

        // 2. If cursor is inside `import { ... | ... } from 'moduleSpec'`, suggest exports from that module EXCLUSIVELY!
        if (insideJsImportClause) {
            rawCandidates.clear();
            if (!importLineModuleSpec.empty()) {
                auto entryFile = ResolveModuleEntryFile(importLineModuleSpec, baseDir, projectRoot);
                if (!entryFile.empty()) {
                    auto modExps = ParseModuleExportsFromFile(entryFile, importLineModuleSpec, projectRoot);
                    for (auto& exp : modExps) {
                        exp.score += 1500;
                        exp.documentation.clear(); // Already on the import line, no need to auto-insert another import line
                        rawCandidates.push_back(std::move(exp));
                    }
                }
            } else {
                // User typed `import { Cam|` before typing `from '...'`: suggest exports across all installed packages & workspace files!
                collectDynamicProjectExports(rawCandidates, false);
            }
            insideTagAttr = true; // Suppress generic vocabulary from polluting import { ... }
        } else {
            size_t dotPos = (cursorCol > prefix.length() && cursorCol > 0) ? cursorCol - prefix.length() - 1 : std::wstring::npos;
            bool afterDot = (dotPos != std::wstring::npos && dotPos < currentLine.length() && currentLine[dotPos] == L'.');

            if (afterDot) {
                // Check if receiver before '.' is an imported module namespace (e.g. `Icons.` or `React.`)
                size_t recEnd = dotPos;
                size_t recStart = recEnd;
                while (recStart > 0 && IsJsIdentChar(currentLine[recStart - 1])) recStart--;
                std::wstring receiver = currentLine.substr(recStart, recEnd - recStart);
                auto nsIt = jsModuleNamespaces.find(receiver);
                if (nsIt != jsModuleNamespaces.end()) {
                    auto entryFile = ResolveModuleEntryFile(nsIt->second, baseDir, projectRoot);
                    if (!entryFile.empty()) {
                        auto modExps = ParseModuleExportsFromFile(entryFile, nsIt->second, projectRoot);
                        for (auto& exp : modExps) {
                            exp.score += 1200;
                            exp.documentation.clear();
                            rawCandidates.push_back(std::move(exp));
                        }
                    }
                }

                bool isCanvasContext = (receiver == L"context" || receiver == L"ctx" || receiver == L"c2d");
                if (!isCanvasContext && pDocLines) {
                    for (const auto& dLine : *pDocLines) {
                        if (dLine.find(receiver) != std::wstring::npos && dLine.find(L"getContext") != std::wstring::npos) {
                            isCanvasContext = true;
                            break;
                        }
                    }
                }

                if (isCanvasContext) {
                    const auto& canvasMethods = WebIntelliSenseData::GetCanvasRenderingContext2DMethods();
                    for (auto item : canvasMethods) {
                        item.score = 5000;
                        rawCandidates.push_back(std::move(item));
                    }
                } else {
                    for (const auto& p : m_jsPrototypes) rawCandidates.push_back(p);
                    for (const auto& d : m_jsDomApis) rawCandidates.push_back(d);
                    for (const auto& r : m_reactAndNodeVocabulary) {
                        if (r.kind == CompletionKind::Property || r.kind == CompletionKind::Function || r.kind == CompletionKind::Variable) {
                            rawCandidates.push_back(r);
                        }
                    }
                }
            } else {
                bool isJsx = (ext == L".jsx" || ext == L".tsx" || (isJs && (currentLine.find(L"<") != std::wstring::npos || insideHtmlTag)));
                size_t lastOpenTag = currentLine.rfind(L'<', cursorCol > 0 ? cursorCol - 1 : 0);
                size_t lastCloseTag = currentLine.rfind(L'>', cursorCol > 0 ? cursorCol - 1 : 0);
                bool insideTag = isJsx && ((lastOpenTag != std::wstring::npos && (lastCloseTag == std::wstring::npos || lastOpenTag > lastCloseTag)) || insideHtmlTag);

                if (insideTag) {
                    size_t spaceAfterOpen = (lastOpenTag != std::wstring::npos) ? currentLine.find_first_of(L" \t", lastOpenTag) : 0;
                    if (lastOpenTag != std::wstring::npos && (spaceAfterOpen == std::wstring::npos || cursorCol <= spaceAfterOpen)) {
                        // Tag name inside JSX: <div, <Camera, <Button, etc.
                        for (const auto& imp : importedDocSymbols) {
                            if (imp.kind == CompletionKind::Class || imp.kind == CompletionKind::Function) {
                                rawCandidates.push_back(imp);
                            }
                        }
                        collectDynamicProjectExports(rawCandidates, true);
                        for (const auto& tag : m_htmlTags) rawCandidates.push_back(tag);
                        for (const auto& r : m_reactAndNodeVocabulary) {
                            if (r.kind == CompletionKind::Class || r.kind == CompletionKind::Function) {
                                rawCandidates.push_back(r);
                            }
                        }
                    } else if (!isInsideQuotes) {
                        // Inside JSX Tag Attributes (attribute name context): e.g. <div class|
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
                    } else {
                        // Inside JSX Attribute Value: e.g. <div className="|" or <div id="|"
                        insideTagAttr = true;
                        if (isIdAttrContext) {
                            ProvideIdCompletions(rawCandidates, pDocLines, baseDir, projectRoot);
                        } else if (isClassAttrContext) {
                            ProvideClassCompletions(rawCandidates, pDocLines, baseDir, projectRoot, currentLine);
                        }
                    }
                } else {
                    for (const auto& imp : importedDocSymbols) rawCandidates.push_back(imp);
                    collectDynamicProjectExports(rawCandidates, false);
                    for (const auto& g : m_jsGlobals) rawCandidates.push_back(g);
                    for (const auto& kw : m_jsKeywords) rawCandidates.push_back(kw);
                    for (const auto& d : m_jsDomApis) rawCandidates.push_back(d);
                    for (const auto& r : m_reactAndNodeVocabulary) rawCandidates.push_back(r);
                    if (isTs) {
                        for (const auto& tsItem : WebIntelliSenseData::GetTypeScriptGlobalsAndUtilityTypes()) {
                            rawCandidates.push_back(tsItem);
                        }
                    }

                    // In React JSX/TSX files, also provide HTML element tags
                    if (ext == L".jsx" || ext == L".tsx") {
                        for (const auto& tag : m_htmlTags) rawCandidates.push_back(tag);
                    }
                }
            }
        }
    } else if (isHtml) {
        // Check context on line: if typing after '<' or in tag
        size_t lastOpenTag = currentLine.rfind(L'<', cursorCol > 0 ? cursorCol - 1 : 0);
        size_t lastCloseTag = currentLine.rfind(L'>', cursorCol > 0 ? cursorCol - 1 : 0);
        bool insideTag = (lastOpenTag != std::wstring::npos && (lastCloseTag == std::wstring::npos || lastOpenTag > lastCloseTag)) || insideHtmlTag;

        if (insideTag) {
            insideTagAttr = true;
            // Tag name or attributes
            size_t spaceAfterOpen = (lastOpenTag != std::wstring::npos) ? currentLine.find_first_of(L" \t", lastOpenTag) : 0;
            if (lastOpenTag != std::wstring::npos && (spaceAfterOpen == std::wstring::npos || cursorCol <= spaceAfterOpen)) {
                for (const auto& tag : m_htmlTags) {
                    IntelliSenseItem it = tag;
                    it.score += 1200;
                    rawCandidates.push_back(std::move(it));
                }
            } else if (!isInsideQuotes) {
                for (const auto& attr : m_htmlAttributes) {
                    IntelliSenseItem it = attr;
                    it.score += 1000;
                    rawCandidates.push_back(std::move(it));
                }
            } else {
                if (isIdAttrContext) {
                    ProvideIdCompletions(rawCandidates, pDocLines, baseDir, projectRoot);
                } else if (isClassAttrContext) {
                    ProvideClassCompletions(rawCandidates, pDocLines, baseDir, projectRoot, currentLine);
                } else if (enclosingAttrName == L"type") {
                    ProvideHtmlTypeCompletions(rawCandidates);
                } else if (enclosingAttrName == L"rel") {
                    ProvideHtmlRelCompletions(rawCandidates);
                } else if (enclosingAttrName == L"target") {
                    ProvideHtmlTargetCompletions(rawCandidates);
                } else if (enclosingAttrName == L"method") {
                    ProvideHtmlMethodCompletions(rawCandidates);
                }
            }
        } else {
            // In HTML markup outside of tags (e.g. typing "div", "header", or Emmet abbreviations)
            // 1. Suggest HTML tags with priority
            for (const auto& tag : m_htmlTags) {
                IntelliSenseItem it = tag;
                it.insertText = std::wstring(L"<") + tag.label + L">$0</" + tag.label + L">";
                it.score += 300;
                rawCandidates.push_back(std::move(it));
            }

            // 2. Check for Emmet Abbreviation
            std::wstring emmetPrefix = ExtractEmmetPrefix(currentLine, cursorCol);
            if (!emmetPrefix.empty() && EmmetEngine::IsEmmetAbbreviation(emmetPrefix, false)) {
                std::wstring expanded = ExpandEmmet(emmetPrefix);
                if (!expanded.empty()) {
                    IntelliSenseItem eItem;
                    eItem.label = emmetPrefix;
                    eItem.detail = L"Emmet Abbreviation\n" + expanded;
                    eItem.insertText = expanded;
                    eItem.kind = CompletionKind::Snippet;
                    eItem.origin = L"emmet";
                    eItem.score = 4000; // Highest priority at top of list!
                    rawCandidates.push_back(std::move(eItem));
                }
            }
        }
    } else if (insidePhp) {
        if (insideServerKey) {
            static const struct SrvKey { const wchar_t* key; const wchar_t* desc; } srvKeys[] = {
                { L"REQUEST_METHOD", L"Request method used to access the page (e.g. 'GET', 'POST', 'PUT', 'DELETE')" },
                { L"REQUEST_URI", L"The URI which was given in order to access this page (e.g. '/index.php?id=1')" },
                { L"HTTP_HOST", L"Contents of the Host: header from the current request" },
                { L"SERVER_NAME", L"The name of the server host under which the current script is executing" },
                { L"SERVER_PORT", L"The port on the server machine being used by the web server for communication" },
                { L"SCRIPT_NAME", L"Contains the current script's path" },
                { L"DOCUMENT_ROOT", L"The document root directory under which the current script is executing" },
                { L"REMOTE_ADDR", L"The IP address from which the user is viewing the current page" },
                { L"HTTP_USER_AGENT", L"Contents of the User-Agent: header from the current request" },
                { L"HTTP_REFERER", L"The address of the page (if any) which referred the user agent to the current page" },
                { L"QUERY_STRING", L"The query string, if any, via which the page was accessed" },
                { L"HTTPS", L"Set to a non-empty value if the script was queried through the HTTPS protocol" },
                { L"PHP_SELF", L"The filename of the currently executing script, relative to the document root" },
                { L"CONTENT_TYPE", L"Content-Type header of the HTTP request" }
            };
            for (const auto& sk : srvKeys) {
                IntelliSenseItem it;
                it.label = sk.key;
                it.detail = sk.desc;
                it.insertText = sk.key;
                it.kind = CompletionKind::Constant;
                it.score = 1500;
                it.origin = L"php";
                rawCandidates.push_back(std::move(it));
            }
        } else if (afterDoubleColon) {
            // Extract class name before '::' (e.g. "PDO", "DateTime", "ReflectionClass")
            size_t clsEnd = beforePos - 2;
            size_t clsStart = clsEnd;
            while (clsStart > 0 && (iswalnum(currentLine[clsStart - 1]) || currentLine[clsStart - 1] == L'_' || currentLine[clsStart - 1] == L'\\')) {
                clsStart--;
            }
            std::wstring className = currentLine.substr(clsStart, clsEnd - clsStart);
            if (!className.empty() && className[0] == L'\\') className = className.substr(1);
            auto staticMembers = QueryPhpClassStaticMembers(className);
            for (auto& sm : staticMembers) {
                rawCandidates.push_back(std::move(sm));
            }
        } else if (afterArrow) {
            // Extract receiver variable before '->' (e.g. "$pdo", "$stmt", "$e") and infer its class
            size_t varEnd = beforePos - 2;
            size_t varStart = varEnd;
            while (varStart > 0 && (iswalnum(currentLine[varStart - 1]) || currentLine[varStart - 1] == L'_' || currentLine[varStart - 1] == L'$')) {
                varStart--;
            }
            std::wstring receiverVar = currentLine.substr(varStart, varEnd - varStart);
            std::wstring receiverClass;
            for (const auto& sym : workspaceVarSymbols) {
                if (sym.label == receiverVar) {
                    // Extract class from "Variable (ClassName)"
                    size_t lp = sym.detail.find(L'(');
                    size_t rp = sym.detail.find(L')');
                    if (lp != std::wstring::npos && rp != std::wstring::npos && rp > lp + 1) {
                        receiverClass = sym.detail.substr(lp + 1, rp - lp - 1);
                        break;
                    }
                }
            }
            auto instanceMethods = QueryPhpClassInstanceMethods(receiverClass);
            for (auto& im : instanceMethods) {
                rawCandidates.push_back(std::move(im));
            }
        } else {
            for (const auto& phpSym : m_phpSymbols) {
                IntelliSenseItem it = phpSym;
                if (prefix.rfind(L"$", 0) == 0 && it.label.rfind(L"$", 0) == 0) {
                    it.score += 400; // Boost superglobals and variables when user types $
                }
                rawCandidates.push_back(std::move(it));
            }
        }
    } else if (isPy) {
        auto scanPyFileExports = [&](const std::filesystem::path& pyFile, const std::wstring& modLabel, std::vector<IntelliSenseItem>& outItems) {
            std::string raw = ReadFileUtf8Bounded(pyFile, 262144);
            if (raw.empty()) return;
            std::wstring content = Utf8ToWideAsciiFast(raw);
            std::unordered_set<std::wstring> seenNames;
            size_t ls = 0;
            bool inAllList = false;
            while (ls < content.length()) {
                size_t le = content.find(L'\n', ls);
                if (le == std::wstring::npos) le = content.length();
                std::wstring ln = content.substr(ls, le - ls);
                ls = le + 1;
                std::wstring tln = TrimWs(ln);
                if (tln.rfind(L"__all__", 0) == 0) inAllList = true;
                if (inAllList) {
                    size_t q = 0;
                    while (q < tln.length()) {
                        wchar_t qc = tln[q];
                        if (qc == L'\'' || qc == L'"') {
                            size_t qe = tln.find(qc, q + 1);
                            if (qe != std::wstring::npos && qe > q + 1) {
                                std::wstring sym = tln.substr(q + 1, qe - q - 1);
                                if (!sym.empty() && sym[0] != L'_' && seenNames.insert(sym).second) {
                                    outItems.push_back({ sym, L"Export in " + modLabel, sym, CompletionKind::Function, 1120, 0, 0, L"python" });
                                }
                                q = qe + 1;
                                continue;
                            }
                        }
                        q++;
                    }
                    if (tln.find(L']') != std::wstring::npos) inAllList = false;
                }
                if (ln.rfind(L"def ", 0) == 0 || ln.rfind(L"async def ", 0) == 0) {
                    size_t p = (ln.rfind(L"async ", 0) == 0) ? 10 : 4;
                    while (p < ln.length() && iswspace(ln[p])) p++;
                    size_t e = p;
                    while (e < ln.length() && (iswalnum(ln[e]) || ln[e] == L'_')) e++;
                    if (e > p && ln[p] != L'_') {
                        std::wstring fn = ln.substr(p, e - p);
                        if (seenNames.insert(fn).second) {
                            outItems.push_back({ fn, L"Function in " + modLabel, fn, CompletionKind::Function, 1100, 0, 0, L"python" });
                        }
                    }
                } else if (ln.rfind(L"class ", 0) == 0) {
                    size_t p = 6;
                    while (p < ln.length() && iswspace(ln[p])) p++;
                    size_t e = p;
                    while (e < ln.length() && (iswalnum(ln[e]) || ln[e] == L'_')) e++;
                    if (e > p && ln[p] != L'_') {
                        std::wstring cn = ln.substr(p, e - p);
                        if (seenNames.insert(cn).second) {
                            outItems.push_back({ cn, L"Class in " + modLabel, cn, CompletionKind::Class, 1150, 0, 0, L"python" });
                        }
                    }
                } else if (!ln.empty() && iswalpha(ln[0])) {
                    // Top-level variable / callable stub annotation in .pyi (e.g. `randint: ...` or `uniform = ...`)
                    size_t e = 0;
                    while (e < ln.length() && (iswalnum(ln[e]) || ln[e] == L'_')) e++;
                    size_t after = e;
                    while (after < ln.length() && (ln[after] == L' ' || ln[after] == L'\t')) after++;
                    if (after < ln.length() && (ln[after] == L':' || ln[after] == L'=')) {
                        std::wstring vn = ln.substr(0, e);
                        if (vn != L"import" && vn != L"from" && vn != L"type" && seenNames.insert(vn).second) {
                            outItems.push_back({ vn, modLabel + L"." + vn, vn, CompletionKind::Function, 1090, 0, 0, L"python" });
                        }
                    }
                }
            }
        };

        std::error_code ec;

        // ---------------------------------------------------------------------------------
        // 1. Names imported by THIS document (alias -> dotted module). Nothing is hardcoded:
        //    import numpy as np        -> np  : numpy
        //    import os.path            -> os  : os
        //    from pkg import mod as m  -> m   : pkg.mod
        // ---------------------------------------------------------------------------------
        std::unordered_map<std::wstring, std::wstring> pyImports;
        auto splitCommaList = [](const std::wstring& s) {
            std::vector<std::wstring> parts;
            size_t a = 0;
            while (a <= s.size()) {
                size_t b = s.find(L',', a);
                if (b == std::wstring::npos) b = s.size();
                std::wstring part = TrimWs(s.substr(a, b - a));
                if (!part.empty()) parts.push_back(part);
                a = b + 1;
            }
            return parts;
        };
        if (pDocLines) {
            for (const auto& dLine : *pDocLines) {
                std::wstring t = TrimWs(dLine);
                size_t hashPos = t.find(L'#');
                if (hashPos != std::wstring::npos) t = TrimWs(t.substr(0, hashPos));
                if (t.rfind(L"import ", 0) == 0) {
                    for (const auto& part : splitCommaList(t.substr(7))) {
                        size_t asPos = part.find(L" as ");
                        if (asPos != std::wstring::npos) {
                            pyImports[TrimWs(part.substr(asPos + 4))] = TrimWs(part.substr(0, asPos));
                        } else {
                            std::wstring top = part.substr(0, part.find(L'.'));
                            pyImports[top] = top;
                        }
                    }
                } else if (t.rfind(L"from ", 0) == 0) {
                    size_t ip = t.find(L" import ");
                    if (ip == std::wstring::npos) continue;
                    std::wstring base = TrimWs(t.substr(5, ip - 5));
                    if (base.empty() || base[0] == L'.') continue; // relative imports are resolved from workspace files
                    std::wstring names = t.substr(ip + 8);
                    names.erase(std::remove(names.begin(), names.end(), L'('), names.end());
                    names.erase(std::remove(names.begin(), names.end(), L')'), names.end());
                    for (const auto& part : splitCommaList(names)) {
                        if (part == L"*") continue;
                        size_t asPos = part.find(L" as ");
                        std::wstring orig = (asPos != std::wstring::npos) ? TrimWs(part.substr(0, asPos)) : part;
                        std::wstring alias = (asPos != std::wstring::npos) ? TrimWs(part.substr(asPos + 4)) : part;
                        pyImports[alias] = base + L"." + orig;
                    }
                }
            }
            // Proactively pre-warm background module introspection for every imported module
            // while the user is editing the file, so typing `alias.` is an instant O(1) cache hit!
            for (const auto& [alias, targetMod] : pyImports) {
                pyenv::PrefetchModule(projectRoot, targetMod);
            }
        }

        // ---------------------------------------------------------------------------------
        // 2. Context: are we typing a module name (`import x` / `from x`), a name after
        //    `from x import`, or an attribute after `alias.` ?
        // ---------------------------------------------------------------------------------
        std::wstring lineBeforeCursor = currentLine.substr(0, (std::min)(cursorCol, currentLine.length()));
        std::wstring trimmedBefore = TrimWs(lineBeforeCursor);
        if (!lineBeforeCursor.empty() && iswspace(lineBeforeCursor.back())) trimmedBefore += L" ";
        bool pyModuleNameCtx = false;
        bool pyAliasNameCtx = false;     // `import numpy as n|` -> user is inventing a name, suggest nothing
        std::wstring pyFromModule;       // `from X import |`
        if (trimmedBefore.rfind(L"import ", 0) == 0) {
            std::wstring lastPart = trimmedBefore.substr(trimmedBefore.rfind(L',') == std::wstring::npos ? 7 : trimmedBefore.rfind(L',') + 1);
            if (lastPart.find(L" as ") != std::wstring::npos) pyAliasNameCtx = true;
            else pyModuleNameCtx = true;
        } else if (trimmedBefore.rfind(L"from ", 0) == 0) {
            size_t ip = trimmedBefore.find(L" import ");
            if (ip == std::wstring::npos) {
                pyModuleNameCtx = true;
            } else {
                std::wstring lastPart = trimmedBefore.substr(ip + 8);
                size_t lastComma = lastPart.rfind(L',');
                if (lastComma != std::wstring::npos) lastPart = lastPart.substr(lastComma + 1);
                if (lastPart.find(L" as ") != std::wstring::npos) pyAliasNameCtx = true;
                else pyFromModule = TrimWs(trimmedBefore.substr(5, ip - 5));
            }
        }

        // Dotted chain right before the prefix, e.g. "np.linalg." -> "np.linalg"
        std::wstring pyChain;
        if (afterDot && beforePos >= 1) {
            size_t chainEnd = beforePos - 1; // position of the '.'
            size_t chainStart = chainEnd;
            while (chainStart > 0 && (iswalnum(currentLine[chainStart - 1]) || currentLine[chainStart - 1] == L'_' || currentLine[chainStart - 1] == L'.')) {
                chainStart--;
            }
            pyChain = currentLine.substr(chainStart, chainEnd - chainStart);
        }

        auto describeSource = [](const pyenv::ModuleEntry& m) -> std::wstring {
            switch (m.source) {
                case pyenv::PackageSource::ProjectEnv: return L"Installed package \u00B7 " + m.label;
                case pyenv::PackageSource::StdLib:     return L"Standard library \u00B7 " + m.label;
                default:                               return L"External Library \u00B7 " + m.label;
            }
        };
        auto memberToItem = [](const pyenv::ModuleMember& m, const std::wstring& owner) {
            IntelliSenseItem it;
            it.label = m.name;
            it.insertText = m.name;
            it.origin = L"python";
            it.score = 1200;
            switch (m.kind) {
                case L'm': it.kind = CompletionKind::Module;   it.detail = L"module \u00B7 " + owner; break;
                case L'c': it.kind = CompletionKind::Class;    it.detail = L"class " + m.name + m.signature + L" \u00B7 " + owner; break;
                case L'f': it.kind = CompletionKind::Function; it.detail = m.name + m.signature + L" \u00B7 " + owner; break;
                default:   it.kind = CompletionKind::Variable; it.detail = owner + L"." + m.name; break;
            }
            return it;
        };
        // Static fallback while the interpreter introspection is still running: parse .pyi stub and .py source files
        auto scanModuleSource = [&](const std::wstring& dotted, std::vector<IntelliSenseItem>& outItems) {
            std::filesystem::path rel;
            for (const auto& seg : splitCommaList([&] { std::wstring d = dotted; std::replace(d.begin(), d.end(), L'.', L','); return d; }())) rel /= seg;
            std::vector<std::filesystem::path> roots = { baseDir, projectRoot };
            for (const auto& sp : pyenv::GetSearchPaths(projectRoot)) roots.push_back(sp.path);
            for (const auto& r : roots) {
                bool foundAny = false;
                auto initPyi = r / rel / L"__init__.pyi";
                if (std::filesystem::exists(initPyi, ec)) { scanPyFileExports(initPyi, dotted, outItems); foundAny = true; }
                auto initPy = r / rel / L"__init__.py";
                if (std::filesystem::exists(initPy, ec)) { scanPyFileExports(initPy, dotted, outItems); foundAny = true; }
                auto modPyi = r / rel;
                modPyi += L".pyi";
                if (std::filesystem::exists(modPyi, ec)) { scanPyFileExports(modPyi, dotted, outItems); foundAny = true; }
                auto modPy = r / rel;
                modPy += L".py";
                if (std::filesystem::exists(modPy, ec)) { scanPyFileExports(modPy, dotted, outItems); foundAny = true; }
                if (foundAny) return;
            }
        };
        auto addModuleMembers = [&](const std::wstring& dotted, std::vector<IntelliSenseItem>& outItems) {
            std::vector<pyenv::ModuleMember> members;
            if (pyenv::GetModuleMembers(projectRoot, dotted, members)) {
                for (const auto& m : members) outItems.push_back(memberToItem(m, dotted));
            } else {
                for (auto& mm : QueryPythonModuleMembers(dotted)) outItems.push_back(std::move(mm));
                scanModuleSource(dotted, outItems);
            }
            for (const auto& sub : pyenv::ListSubModules(projectRoot, dotted)) {
                outItems.push_back({ sub.name, L"module \u00B7 " + dotted, sub.name, CompletionKind::Module, 1150, 0, 0, L"python" });
            }
        };

        if (pyAliasNameCtx) {
            pyExclusive = true; // naming an alias: no suggestions
        } else if (pyModuleNameCtx) {
            pyExclusive = true;
            if (!pyChain.empty()) {
                // `import numpy.li|` -> sub-packages of numpy
                for (const auto& sub : pyenv::ListSubModules(projectRoot, pyChain)) {
                    pyExclusiveItems.push_back({ sub.name, L"module \u00B7 " + pyChain, sub.name, CompletionKind::Module, 1300, 0, 0, L"python" });
                }
                scanModuleSource(pyChain, pyExclusiveItems);
            } else {
                // Workspace modules first (files next to the current file and local packages)
                if (std::filesystem::is_directory(baseDir, ec)) {
                    for (std::filesystem::directory_iterator it(baseDir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
                        auto p = it->path();
                        std::wstring stem;
                        if (it->is_regular_file(ec) && p.extension() == L".py") stem = p.stem().wstring();
                        else if (it->is_directory(ec) && std::filesystem::exists(p / L"__init__.py", ec)) stem = p.filename().wstring();
                        if (stem.empty() || (!fileName.empty() && std::filesystem::equivalent(p, std::filesystem::path(fileName), ec))) continue;
                        pyExclusiveItems.push_back({ stem, L"Workspace module", stem, CompletionKind::Module, 1400, 0, 0, L"python" });
                    }
                }
                for (const auto& m : pyenv::ListTopLevelModules(projectRoot)) {
                    int score = (m.source == pyenv::PackageSource::ProjectEnv) ? 1350 : (m.source == pyenv::PackageSource::External ? 1300 : 1250);
                    pyExclusiveItems.push_back({ m.name, describeSource(m), m.name, CompletionKind::Module, score, 0, 0, L"python" });
                }
            }
        } else if (!pyFromModule.empty()) {
            // `from numpy import ar|` -> real members of numpy
            pyExclusive = true;
            addModuleMembers(pyFromModule, pyExclusiveItems);
        } else if (afterDot) {
            std::wstring firstSeg = pyChain.substr(0, pyChain.find(L'.'));
            auto imp = pyImports.find(firstSeg);
            if (imp != pyImports.end()) {
                // `np.` / `np.linalg.` -> members of the module the alias points to
                std::wstring dotted = imp->second + pyChain.substr(firstSeg.length());
                pyExclusive = true;
                addModuleMembers(dotted, pyExclusiveItems);
            } else {
                // Not an imported module (e.g. a local object): keep the generic member vocabulary
                std::wstring lastSeg = pyChain.substr(pyChain.rfind(L'.') == std::wstring::npos ? 0 : pyChain.rfind(L'.') + 1);
                for (auto& mm : QueryPythonModuleMembers(lastSeg)) rawCandidates.push_back(std::move(mm));
                auto localPy = baseDir / (lastSeg + L".py");
                if (std::filesystem::exists(localPy, ec)) scanPyFileExports(localPy, lastSeg + L".py", rawCandidates);
            }
        } else {
            for (const auto& pySym : m_pySymbols) {
                rawCandidates.push_back(pySym);
            }
            // Names this document imported (np, pd, os, ...) are real identifiers in scope
            for (const auto& [alias, target] : pyImports) {
                rawCandidates.push_back({ alias, L"import " + target + (alias != target ? L" as " + alias : L""), alias, CompletionKind::Module, 1250, 0, 0, L"python" });
            }
            // Peer .py modules & their functions/classes in the workspace
            if (std::filesystem::exists(baseDir, ec) && std::filesystem::is_directory(baseDir, ec)) {
                int pyScanned = 0;
                for (std::filesystem::directory_iterator it(baseDir, ec); !ec && it != std::filesystem::directory_iterator() && pyScanned < 20; it.increment(ec)) {
                    if (!it->is_regular_file(ec)) continue;
                    auto p = it->path();
                    if (p.extension() != L".py") continue;
                    if (!fileName.empty() && std::filesystem::equivalent(p, std::filesystem::path(fileName), ec)) continue;
                    pyScanned++;
                    std::wstring stem = p.stem().wstring();
                    rawCandidates.push_back({ stem, L"Workspace Python Module (" + p.filename().wstring() + L")", stem, CompletionKind::Module, 950, 0, 0, L"python" });
                    scanPyFileExports(p, p.filename().wstring(), rawCandidates);
                }
            }
        }
    } else if (ext == L".cpp" || ext == L".h" || ext == L".hpp" || ext == L".c") {
        const wchar_t* cppKeywords[] = {
            L"int", L"char", L"float", L"double", L"bool", L"void", L"auto", L"const", L"constexpr",
            L"class", L"struct", L"enum", L"public", L"private", L"protected", L"virtual", L"override",
            L"namespace", L"template", L"typename", L"using", L"return", L"if", L"else", L"for",
            L"while", L"switch", L"case", L"break", L"continue", L"new", L"delete", L"sizeof",
            L"nullptr", L"static_cast", L"dynamic_cast", L"reinterpret_cast", L"const_cast"
        };
        for (const auto* kw : cppKeywords) {
            rawCandidates.push_back({ kw, L"C++ Keyword", kw, CompletionKind::Keyword, 120, 0, 0, L"c++" });
        }
    }

    // Dynamic Document & Workspace Variables
    if (!afterDoubleColon && !afterArrow && !insideServerKey && !insideJsImportClause && !insideTagAttr) {
        for (auto& sym : workspaceVarSymbols) {
            if (prefix.rfind(L"$", 0) == 0 && sym.label.rfind(L"$", 0) == 0) {
                sym.score += 450;
            }
            rawCandidates.push_back(std::move(sym));
        }
    }

    // Common web and programming vocabulary (properties, tags, methods, identifiers)
    // Keep HTML markup, CSS, Python, C++, and PHP code completions strictly scoped and clean
    if (!isCss && !insideStyle && (!isHtml || insideScript) && !insidePhp && !insideTagAttr && !insideJsImportClause && !isPy && !isInsideQuotes && ext != L".cpp" && ext != L".h" && ext != L".hpp" && ext != L".c") {
        for (const auto& v : m_commonVocabulary) {
            rawCandidates.push_back(v);
        }
    }

    // Tailwind CSS IntelliSense completions: ONLY inside class="..." / className="..." attributes or CSS @apply
    // AND only when Tailwind is connected to the workspace/project or current document
    bool isCssApplyContext = (insideStyle && currentLine.find(L"@apply") != std::wstring::npos);
    if ((isClassAttrContext || isCssApplyContext) && !insidePhp && !insideJsImportClause) {
        bool isTailwind = TailwindHelper::IsTailwindConnected(m_workspaceRoot, fileName);
        if (!isTailwind && pDocLines && !pDocLines->empty()) {
            size_t checkLines = (std::min)(pDocLines->size(), static_cast<size_t>(50));
            for (size_t li = 0; li < checkLines; ++li) {
                const auto& dL = (*pDocLines)[li];
                if (dL.find(L"tailwindcss") != std::wstring::npos || dL.find(L"@tailwind") != std::wstring::npos) {
                    isTailwind = true;
                    break;
                }
            }
        }
        if (isTailwind) {
            static const auto allTailwindCandidates = TailwindHelper::GetAllCandidates();
            for (const auto& tw : allTailwindCandidates) {
                IntelliSenseItem item;
                item.label = tw.name;
                item.detail = tw.detail;
                item.documentation = tw.doc;
                item.isColor = tw.isColor;
                item.colorHex = tw.hex;
                item.kind = tw.isColor ? CompletionKind::Color : CompletionKind::Snippet;
                item.origin = L"tailwind";
                item.score = 2500;
                rawCandidates.push_back(std::move(item));
            }
        }
    }

    if (pyExclusive) {
        rawCandidates = std::move(pyExclusiveItems);
    }

    // 7. Merge plugin/extension/mod contributed completions & snippets
    {
        auto pluginItems = phide::plugin::PluginManager::Instance().QueryCompletions(ext, fileName, currentLine, cursorCol, prefix);
        for (auto& pi : pluginItems) {
            rawCandidates.push_back(std::move(pi));
        }
        if (!isInsideQuotes) {
            auto pluginSnippets = phide::plugin::PluginManager::Instance().GetContributedSnippets();
            for (const auto& sn : pluginSnippets) {
                if (SnippetManager::MatchesScope(sn.scope, ext)) {
                    IntelliSenseItem sItem;
                    sItem.label = sn.prefix;
                    sItem.detail = sn.description.empty() ? sn.name : sn.description;
                    std::wstring joined;
                    for (size_t i = 0; i < sn.body.size(); ++i) {
                        if (i > 0) joined += L"\n";
                        joined += sn.body[i];
                    }
                    sItem.insertText = joined;
                    sItem.kind = CompletionKind::Snippet;
                    sItem.origin = L"plugin-snippet";
                    sItem.score = 120;
                    rawCandidates.push_back(std::move(sItem));
                }
            }
        }
    }

    // Matching & Scoring Engine: Exact prefix (1000), Boundary match (750), Substring match (100)
    std::vector<IntelliSenseItem> matches;
    std::unordered_set<std::wstring> seenLabels;

    for (const auto& item : rawCandidates) {
        if (seenLabels.count(item.label)) continue;

        if (prefixLower.empty()) {
            IntelliSenseItem scored = item;
            scored.score += 50;
            scored.matchStart = 0;
            scored.matchLen = 0;
            seenLabels.insert(item.label);
            matches.push_back(std::move(scored));
            continue;
        }

        std::wstring candLower = item.label;
        for (auto& c : candLower) c = static_cast<wchar_t>(towlower(c));

        size_t p = candLower.find(prefixLower);
        bool isSnippet = (item.kind == CompletionKind::Snippet);

        if (p == 0) {
            // Exact prefix match: Highest priority (score ~ 1000)
            IntelliSenseItem scored = item;
            scored.score += 1000 - static_cast<int>(item.label.length());
            scored.matchStart = 0;
            scored.matchLen = prefix.length();
            seenLabels.insert(item.label);
            matches.push_back(std::move(scored));
        } else if (p != std::wstring::npos) {
            // Check if this match starts at a word or delimiter boundary (':', '-', '_', '.', '>', '$')
            wchar_t prevChar = candLower[p - 1];
            bool isBoundary = (prevChar == L':' || prevChar == L'-' || prevChar == L'_' || prevChar == L'.' || prevChar == L' ' || prevChar == L'>' || prevChar == L'$');

            if (isSnippet) {
                // Snippets MUST only match at prefix start (p == 0) or at a delimiter boundary (e.g. typing "bread" matches "div:breadcrumb")!
                // Middle-of-word substrings like typing "read" inside "breadcrumb" MUST NEVER MATCH!
                if (isBoundary) {
                    IntelliSenseItem scored = item;
                    scored.score += 700 - static_cast<int>(p * 5 + item.label.length());
                    scored.matchStart = p;
                    scored.matchLen = prefix.length();
                    seenLabels.insert(item.label);
                    matches.push_back(std::move(scored));
                }
            } else {
                if (isBoundary) {
                    // Word boundary match: High priority (score ~ 750)
                    IntelliSenseItem scored = item;
                    scored.score += 750 - static_cast<int>(p * 5 + item.label.length());
                    scored.matchStart = p;
                    scored.matchLen = prefix.length();
                    seenLabels.insert(item.label);
                    matches.push_back(std::move(scored));
                } else if (prefix.length() >= 3) {
                    // Substring in middle of word: Low priority (score ~ 100), only for longer queries
                    IntelliSenseItem scored = item;
                    scored.score += 100 - static_cast<int>(p * 15 + item.label.length());
                    scored.matchStart = p;
                    scored.matchLen = prefix.length();
                    seenLabels.insert(item.label);
                    matches.push_back(std::move(scored));
                }
            }
        } else if (!isSnippet) {
            // Fuzzy match for non-snippets only: require match to start at 0 or at word boundary,
            // or have adjacent character matches (avoids matching 'root' into 'rowCount'!)
            if (prefixLower.length() >= 2) {
                size_t pi = 0;
                size_t firstMatch = std::wstring::npos;
                size_t maxGap = 0;
                size_t lastMatchIdx = 0;
                for (size_t ci = 0; ci < candLower.length() && pi < prefixLower.length(); ++ci) {
                    if (candLower[ci] == prefixLower[pi]) {
                        if (firstMatch == std::wstring::npos) firstMatch = ci;
                        else {
                            size_t gap = ci - lastMatchIdx;
                            if (gap > maxGap) maxGap = gap;
                        }
                        lastMatchIdx = ci;
                        pi++;
                    }
                }
                // Accept fuzzy match only if all characters matched AND either:
                // 1) starts at index 0 or word boundary with small gap (<= 2), OR
                // 2) no large gaps scattering letters across unrelated words
                if (pi == prefixLower.length() && (firstMatch == 0 || maxGap <= 2)) {
                    IntelliSenseItem scored = item;
                    scored.score += 50 - static_cast<int>(item.label.length() + firstMatch * 10);
                    scored.matchStart = (firstMatch != std::wstring::npos) ? firstMatch : 0;
                    scored.matchLen = prefix.length();
                    seenLabels.insert(item.label);
                    matches.push_back(std::move(scored));
                }
            }
        }
    }

    // Apply user adaptive learning boost (habits learned overtime without freezing/memory bloat)
    if (m_pAdaptiveEngine) {
        for (auto& item : matches) {
            item.score += m_pAdaptiveEngine->GetAdaptiveScoreBonus(item.label, ext);
        }
    }

    // Sort descending by score
    std::sort(matches.begin(), matches.end(), [](const IntelliSenseItem& a, const IntelliSenseItem& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.label < b.label;
    });

    if (matches.size() > 50) {
        matches.resize(50);
    }

    return matches;
}

} // namespace phide::editor
