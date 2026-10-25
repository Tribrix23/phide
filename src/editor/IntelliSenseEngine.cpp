#include "phide/editor/IntelliSenseEngine.hpp"
#include "phide/editor/SnippetManager.hpp"
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <functional>
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
    PopulateCommonVocabulary();
}

void IntelliSenseEngine::PopulateHtmlTags() {
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
    if (line.empty() || cursorCol == 0) return L"";
    size_t col = (std::min)(cursorCol, line.length());
    size_t start = col;
    while (start > 0) {
        wchar_t c = line[start - 1];
        if (c == L' ' || c == L'\t' || c == L'<' || c == L';' || c == L'\"' || c == L'\'') {
            break;
        }
        if (c == L'>') {
            // '>' can be child operator in Emmet like ul>li, but if preceded by '/' or tag close '</tag>', stop
            if (start >= 2 && line[start - 2] == L'/') {
                break;
            }
        }
        start--;
    }
    while (start < col && (line[start] == L'>' || line[start] == L' ' || line[start] == L'\t')) {
        start++;
    }
    if (start >= col) return L"";
    return line.substr(start, col - start);
}

std::wstring IntelliSenseEngine::ExpandEmmet(const std::wstring& abbr) {
    if (abbr.empty()) return L"";

    // Quick syntax validation: must look like an Emmet expression
    bool hasEmmetSyntax = (abbr.find_first_of(L">*+^$") != std::wstring::npos) ||
                          (abbr.find(L'.') != std::wstring::npos && abbr.length() > 2) ||
                          (abbr.find(L'#') != std::wstring::npos && abbr.length() > 2);
    if (!hasEmmetSyntax) return L"";

    struct Node {
        std::wstring tag = L"div";
        std::wstring id;
        std::vector<std::wstring> classes;
        std::wstring text;
        int count = 1;
        std::vector<Node> children;
    };

    auto parseSingle = [](const std::wstring& s) -> Node {
        Node n;
        size_t len = s.length();
        if (len == 0) return n;

        // Check multiplier *N at end
        size_t star = s.rfind(L'*');
        if (star != std::wstring::npos && star + 1 < len) {
            bool allDig = true;
            for (size_t d = star + 1; d < len; ++d) {
                if (!iswdigit(s[d])) { allDig = false; break; }
            }
            if (allDig) {
                n.count = (std::max)(1, _wtoi(s.substr(star + 1).c_str()));
                len = star;
            }
        }

        size_t tagEnd = 0;
        while (tagEnd < len && s[tagEnd] != L'.' && s[tagEnd] != L'#' && s[tagEnd] != L'{') {
            tagEnd++;
        }
        if (tagEnd > 0) {
            n.tag = s.substr(0, tagEnd);
        } else {
            n.tag = L"div";
        }

        size_t i = tagEnd;
        while (i < len) {
            if (s[i] == L'#') {
                i++;
                size_t idEnd = i;
                while (idEnd < len && s[idEnd] != L'.' && s[idEnd] != L'#' && s[idEnd] != L'{') idEnd++;
                n.id = s.substr(i, idEnd - i);
                i = idEnd;
            } else if (s[i] == L'.') {
                i++;
                size_t clsEnd = i;
                while (clsEnd < len && s[clsEnd] != L'.' && s[clsEnd] != L'#' && s[clsEnd] != L'{') clsEnd++;
                n.classes.push_back(s.substr(i, clsEnd - i));
                i = clsEnd;
            } else if (s[i] == L'{') {
                i++;
                size_t txtEnd = s.find(L'}', i);
                if (txtEnd != std::wstring::npos && txtEnd <= len) {
                    n.text = s.substr(i, txtEnd - i);
                    i = txtEnd + 1;
                } else {
                    n.text = s.substr(i, len - i);
                    break;
                }
            } else {
                i++;
            }
        }
        return n;
    };

    // Parse hierarchy by splitting along '>'
    std::vector<std::wstring> levels;
    size_t prev = 0;
    for (size_t i = 0; i < abbr.length(); ++i) {
        if (abbr[i] == L'>') {
            levels.push_back(abbr.substr(prev, i - prev));
            prev = i + 1;
        }
    }
    levels.push_back(abbr.substr(prev));
    if (levels.empty()) return L"";

    // Build tree
    std::function<Node(size_t)> buildTree = [&](size_t idx) -> Node {
        Node curr = parseSingle(levels[idx]);
        if (idx + 1 < levels.size()) {
            curr.children.push_back(buildTree(idx + 1));
        }
        return curr;
    };

    Node root = buildTree(0);

    // Render tree with indentation and $ numbering replacement
    auto isVoidTag = [](const std::wstring& t) {
        return t == L"img" || t == L"input" || t == L"br" || t == L"hr" || t == L"meta" || t == L"link" || t == L"source";
    };

    auto replaceNum = [](const std::wstring& inStr, int num) -> std::wstring {
        std::wstring out = inStr;
        std::wstring numStr = std::to_wstring(num);
        size_t pos = 0;
        while ((pos = out.find(L'$', pos)) != std::wstring::npos) {
            out.replace(pos, 1, numStr);
            pos += numStr.length();
        }
        return out;
    };

    std::wstring result;
    std::function<void(const Node&, int, int)> render = [&](const Node& n, int depth, int num) {
        std::wstring indent(depth * 4, L' ');
        result += indent + L"<" + n.tag;
        if (!n.id.empty()) {
            result += L" id=\"" + replaceNum(n.id, num) + L"\"";
        }
        if (!n.classes.empty()) {
            result += L" class=\"";
            for (size_t c = 0; c < n.classes.size(); ++c) {
                if (c > 0) result += L" ";
                result += replaceNum(n.classes[c], num);
            }
            result += L"\"";
        }

        if (isVoidTag(n.tag)) {
            result += L" />\n";
            return;
        }

        result += L">";
        if (!n.text.empty()) {
            result += replaceNum(n.text, num);
        }

        if (!n.children.empty()) {
            result += L"\n";
            for (const auto& child : n.children) {
                for (int k = 1; k <= child.count; ++k) {
                    render(child, depth + 1, k);
                }
            }
            result += indent;
        }

        result += L"</" + n.tag + L">\n";
    };

    for (int k = 1; k <= root.count; ++k) {
        render(root, 0, k);
    }

    if (!result.empty() && result.back() == L'\n') {
        result.pop_back();
    }
    return result;
}

std::wstring IntelliSenseEngine::ExtractQueryPrefix(const std::wstring& line, size_t cursorCol) {
    if (line.empty() || cursorCol == 0) return L"";
    size_t col = (std::min)(cursorCol, line.length());
    size_t start = col;
    while (start > 0) {
        wchar_t c = line[start - 1];
        if (iswalnum(c) || c == L'_' || c == L'$' || c == L'-' || c == L'!' || c == L':') {
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

        // 1. PHP style variables: $port = 3306;
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
                }

                std::wstring detailStr = inferredType.empty() ? L"Variable" : (L"Variable (" + inferredType + L")");

                if (seenVars.insert(fullVar).second) {
                    IntelliSenseItem item;
                    item.label = fullVar;
                    item.detail = detailStr;
                    item.insertText = fullVar;
                    item.kind = CompletionKind::Variable;
                    item.score = 550;
                    item.origin = L"variable";
                    results.push_back(std::move(item));
                }
                if (seenVars.insert(varName).second) {
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

std::vector<IntelliSenseItem> IntelliSenseEngine::QueryCompletions(
    const std::wstring& fileName,
    const std::wstring& currentLine,
    size_t cursorCol,
    const std::vector<std::wstring>* pDocLines
) const {
    // 0. Check if cursor is in a File / Folder Path context (Images 2 & 3)
    // E.g.: <a href="./", <script src="./", import ... from "./", require("./"), preload("./"), url("./"), etc.
    size_t lastDouble = currentLine.rfind(L'"', cursorCol > 0 ? cursorCol - 1 : 0);
    size_t lastSingle = currentLine.rfind(L'\'', cursorCol > 0 ? cursorCol - 1 : 0);
    size_t quotePos = std::wstring::npos;
    if (lastDouble != std::wstring::npos && lastSingle != std::wstring::npos) {
        quotePos = (std::max)(lastDouble, lastSingle);
    } else if (lastDouble != std::wstring::npos) {
        quotePos = lastDouble;
    } else if (lastSingle != std::wstring::npos) {
        quotePos = lastSingle;
    }

    bool isPathContext = false;
    std::wstring pathTyped;
    if (quotePos != std::wstring::npos && quotePos < cursorCol) {
        std::wstring insideQuote = currentLine.substr(quotePos + 1, cursorCol - (quotePos + 1));
        std::wstring beforeQuote = currentLine.substr(0, quotePos);
        while (!beforeQuote.empty() && iswspace(beforeQuote.back())) beforeQuote.pop_back();

        bool isAttrOrImport = false;
        static const std::vector<std::wstring> triggers = {
            L"href=", L"src=", L"action=", L"poster=", L"data=", L"content=",
            L"from", L"require(", L"import(", L"import", L"include", L"url(",
            L"preload(", L"load(", L"open("
        };
        for (const auto& trig : triggers) {
            if (beforeQuote.length() >= trig.length()) {
                std::wstring tail = beforeQuote.substr(beforeQuote.length() - trig.length());
                for (auto& c : tail) c = static_cast<wchar_t>(towlower(c));
                if (tail == trig) {
                    isAttrOrImport = true;
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
    }

    if (isPathContext) {
        std::filesystem::path baseDir;
        std::error_code ec;
        if (!fileName.empty()) {
            std::filesystem::path docPath(fileName);
            if (std::filesystem::exists(docPath, ec) && !std::filesystem::is_directory(docPath, ec)) {
                baseDir = docPath.parent_path();
            } else if (docPath.has_parent_path()) {
                baseDir = docPath.parent_path();
            }
        }
        if (baseDir.empty() && !m_workspaceRoot.empty() && std::filesystem::exists(m_workspaceRoot, ec)) {
            baseDir = std::filesystem::path(m_workspaceRoot);
        }
        if (baseDir.empty()) {
            baseDir = std::filesystem::current_path(ec);
        }

        std::wstring normPath = pathTyped;
        for (auto& ch : normPath) {
            if (ch == L'\\') ch = L'/';
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
                else searchDir = baseDir / dirPart.substr(1);
            } else {
                searchDir = baseDir / dirPart;
            }
        }
        searchDir = searchDir.lexically_normal();

        std::wstring filePrefixLower = filePrefix;
        for (auto& c : filePrefixLower) c = static_cast<wchar_t>(towlower(c));

        std::vector<IntelliSenseItem> pathItems;
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
                    score = isDir ? 1200 : 1000;
                } else {
                    size_t p = candLower.find(filePrefixLower);
                    if (p == 0) {
                        score = (isDir ? 2000 : 1800) - static_cast<int>(candName.length());
                        matchStart = 0;
                        matchLen = filePrefix.length();
                    } else if (p != std::wstring::npos) {
                        score = (isDir ? 1500 : 1300) - static_cast<int>(p * 10 + candName.length());
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

        // Return EXCLUSIVELY path items in path context (no HTML snippets/tags polluting href="./"!)
        std::sort(pathItems.begin(), pathItems.end(), [](const IntelliSenseItem& a, const IntelliSenseItem& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.label < b.label;
        });
        if (pathItems.size() > 50) pathItems.resize(50);
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

    std::vector<IntelliSenseItem> rawCandidates;

    // 0. Include matching user & built-in snippets (Image 3 request: "like you know ! in vs code create html template")
    std::wstring extNoDot = ext.empty() ? L"" : (ext[0] == L'.' ? ext.substr(1) : ext);
    if (m_pSnippetManager) {
        for (const auto& snip : m_pSnippetManager->GetSnippets()) {
            if (SnippetManager::MatchesScope(snip.scope, extNoDot)) {
                IntelliSenseItem sItem;
                sItem.label = snip.prefix;
                sItem.detail = snip.name + (snip.description.empty() ? L"" : (L" - " + snip.description));
                sItem.insertText = snip.prefix;
                sItem.kind = CompletionKind::Snippet;
                sItem.origin = L"snippets";
                sItem.score = 600; // Baseline high snippet score so user shortcuts appear prominently
                rawCandidates.push_back(std::move(sItem));
            }
        }
    }

    bool isHtml = (ext == L".html" || ext == L".htm" || ext == L".php");
    bool isCss = (ext == L".css" || ext == L".scss" || ext == L".less");
    bool isJs = (ext == L".js" || ext == L".jsx" || ext == L".ts" || ext == L".tsx" || ext == L".mjs" || ext == L".cjs");
    bool isPy = (ext == L".py");

    // Detect contextual block (embedded <style> or <script>) in HTML/PHP files
    bool insideStyle = isCss;
    bool insideScript = isJs;

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
            for (const auto& v : m_cssValues) rawCandidates.push_back(v);
        } else {
            for (const auto& p : m_cssProperties) rawCandidates.push_back(p);

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
        size_t dotPos = (cursorCol > prefix.length() && cursorCol > 0) ? cursorCol - prefix.length() - 1 : std::wstring::npos;
        bool afterDot = (dotPos != std::wstring::npos && dotPos < currentLine.length() && currentLine[dotPos] == L'.');

        if (afterDot) {
            for (const auto& p : m_jsPrototypes) rawCandidates.push_back(p);
            for (const auto& d : m_jsDomApis) rawCandidates.push_back(d);
        } else {
            for (const auto& g : m_jsGlobals) rawCandidates.push_back(g);
            for (const auto& kw : m_jsKeywords) rawCandidates.push_back(kw);
            for (const auto& d : m_jsDomApis) rawCandidates.push_back(d);
        }
    } else if (isHtml) {
        // Check context on line: if typing after '<' or in tag
        size_t lastOpenTag = currentLine.rfind(L'<', cursorCol > 0 ? cursorCol - 1 : 0);
        size_t lastCloseTag = currentLine.rfind(L'>', cursorCol > 0 ? cursorCol - 1 : 0);
        bool insideTag = (lastOpenTag != std::wstring::npos && (lastCloseTag == std::wstring::npos || lastOpenTag > lastCloseTag));

        if (insideTag) {
            // Tag name or attributes
            size_t spaceAfterOpen = currentLine.find_first_of(L" \t", lastOpenTag);
            if (spaceAfterOpen == std::wstring::npos || cursorCol <= spaceAfterOpen) {
                for (const auto& tag : m_htmlTags) rawCandidates.push_back(tag);
            } else {
                for (const auto& attr : m_htmlAttributes) rawCandidates.push_back(attr);
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

            // 2. Check for Emmet Abbreviation (Image 4: ul>li*3>span.hello$)
            std::wstring emmetPrefix = ExtractEmmetPrefix(currentLine, cursorCol);
            if (!emmetPrefix.empty() && emmetPrefix.length() >= 3) {
                std::wstring expanded = ExpandEmmet(emmetPrefix);
                if (!expanded.empty()) {
                    IntelliSenseItem eItem;
                    eItem.label = emmetPrefix;
                    eItem.detail = L"Emmet Abbreviation\n" + expanded;
                    eItem.insertText = expanded;
                    eItem.kind = CompletionKind::Snippet;
                    eItem.origin = L"emmet";
                    eItem.score = 3500; // Highest priority at top of list!
                    rawCandidates.push_back(std::move(eItem));
                }
            }
        }
    } else if (isPy) {
        const wchar_t* pyKeywords[] = {
            L"def", L"class", L"self", L"return", L"import", L"from", L"if", L"elif", L"else",
            L"for", L"while", L"try", L"except", L"finally", L"with", L"as", L"async", L"await",
            L"lambda", L"yield", L"pass", L"break", L"continue", L"None", L"True", L"False"
        };
        for (const auto* kw : pyKeywords) {
            rawCandidates.push_back({ kw, L"Python Keyword", kw, CompletionKind::Keyword, 120, 0, 0, L"keyword" });
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

    // Dynamic Document Variables & Typed Symbols (Image 3 request: "when i type port as variable then when i go to another line the port same case is in the intelisense... saying its variable if like another lang that they specifies then it say datatype and variable")
    if (pDocLines && !pDocLines->empty()) {
        auto docSymbols = ExtractDocumentSymbols(*pDocLines, ext);
        for (auto& sym : docSymbols) {
            rawCandidates.push_back(std::move(sym));
        }
    }

    // Common web and programming vocabulary (properties, tags, methods, identifiers)
    // In CSS files, keep completion list clean and strictly scoped to CSS
    if (!isCss && !insideStyle) {
        for (const auto& v : m_commonVocabulary) {
            rawCandidates.push_back(v);
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
            // Check if this match starts at a word or delimiter boundary (':', '-', '_', '.', '>')
            wchar_t prevChar = candLower[p - 1];
            bool isBoundary = (prevChar == L':' || prevChar == L'-' || prevChar == L'_' || prevChar == L'.' || prevChar == L' ' || prevChar == L'>');

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
