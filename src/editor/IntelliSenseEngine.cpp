#include "phide/editor/IntelliSenseEngine.hpp"
#include "phide/editor/SnippetManager.hpp"
#include <algorithm>
#include <cwctype>
#include <unordered_set>

namespace phide::editor {

IntelliSenseEngine::IntelliSenseEngine() {
    PopulateHtmlTags();
    PopulateHtmlAttributes();
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
        { L"class", L"Space-separated list of CSS class names" },
        { L"id", L"Unique document-wide identifier" },
        { L"style", L"Inline CSS styling rules" },
        { L"href", L"Target URL or destination anchor" },
        { L"src", L"Source URL of external resource or image" },
        { L"type", L"Type of control, media or script" },
        { L"name", L"Form control name sent with HTTP request" },
        { L"value", L"Default or current value of form control" },
        { L"placeholder", L"Hint text displayed when input is empty" },
        { L"alt", L"Alternative text representation for image" },
        { L"title", L"Advisory information displayed as tooltip" },
        { L"target", L"Browsing context (_blank, _self, _parent)" },
        { L"rel", L"Relationship between current and linked resource" },
        { L"disabled", L"Disables user interaction with form control" },
        { L"required", L"Specifies that input field must be filled" },
        { L"readonly", L"Specifies that input cannot be modified" },
        { L"checked", L"Pre-selects radio button or checkbox" },
        { L"selected", L"Pre-selects option in select dropdown" },
        { L"multiple", L"Allows multiple selections or file uploads" },
        { L"autofocus", L"Automatically focuses control on page load" },
        { L"autocomplete", L"Controls browser autofill behavior" },
        { L"width", L"Horizontal dimension in pixels or ratio" },
        { L"height", L"Vertical dimension in pixels or ratio" },
        { L"loading", L"Image/iframe loading strategy (lazy, eager)" },
        { L"role", L"WAI-ARIA accessible component role" },
        { L"tabindex", L"Keyboard focus tab navigation order" },
        { L"hidden", L"Hides element from rendering and accessibility" },
        { L"draggable", L"Enables drag-and-drop interaction" },
        { L"contenteditable", L"Allows user to edit element contents" },
        { L"onclick", L"Executed when mouse clicks the element" },
        { L"onchange", L"Executed when form control value commits change" },
        { L"oninput", L"Executed immediately on user value input" },
        { L"onsubmit", L"Executed when form is submitted" },
        { L"onkeydown", L"Executed when keyboard key is depressed" },
        { L"onkeyup", L"Executed when keyboard key is released" },
        { L"onload", L"Executed after element finishes loading" },
        { L"onerror", L"Executed when resource fails to load" },
        { L"aria-label", L"Defines string value that labels current element" },
        { L"aria-hidden", L"Hides element from assistive screen readers" },
        { L"aria-expanded", L"Indicates if grouping element is expanded" }
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
        { L"stop", L"Halts execution or cancels running process", CompletionKind::Function }
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

std::vector<IntelliSenseItem> IntelliSenseEngine::QueryCompletions(
    const std::wstring& fileName,
    const std::wstring& currentLine,
    size_t cursorCol
) const {
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
    bool isJs = (ext == L".js" || ext == L".jsx" || ext == L".ts" || ext == L".tsx" || ext == L".mjs" || ext == L".cjs" || isHtml);
    bool isPy = (ext == L".py");

    // Check context on line: if typing after '<' or in tag
    size_t lastOpenTag = currentLine.rfind(L'<', cursorCol > 0 ? cursorCol - 1 : 0);
    size_t lastCloseTag = currentLine.rfind(L'>', cursorCol > 0 ? cursorCol - 1 : 0);
    bool insideTag = (isHtml && lastOpenTag != std::wstring::npos && (lastCloseTag == std::wstring::npos || lastOpenTag > lastCloseTag));

    if (insideTag) {
        // Tag name or attributes
        size_t spaceAfterOpen = currentLine.find_first_of(L" \t", lastOpenTag);
        if (spaceAfterOpen == std::wstring::npos || cursorCol <= spaceAfterOpen) {
            for (const auto& tag : m_htmlTags) rawCandidates.push_back(tag);
        } else {
            for (const auto& attr : m_htmlAttributes) rawCandidates.push_back(attr);
        }
    } else if (isHtml) {
        // In HTML outside of tags (e.g. typing "div" or "a" directly in document)
        // 1. Suggest HTML tags (div, dialog, header, head, p, span, etc.) with highest priority!
        for (const auto& tag : m_htmlTags) {
            IntelliSenseItem it = tag;
            it.insertText = std::wstring(L"<") + tag.label + L">$0</" + tag.label + L">";
            it.score += 300; // Prioritize tags in HTML so 'div' beats generic symbols
            rawCandidates.push_back(std::move(it));
        }
        // 2. Suggest JS globals & keywords if inside script or inline handlers
        for (const auto& g : m_jsGlobals) rawCandidates.push_back(g);
        for (const auto& kw : m_jsKeywords) rawCandidates.push_back(kw);
        for (const auto& d : m_jsDomApis) rawCandidates.push_back(d);
    } else if (isJs) {
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
    } else if (isPy) {
        const wchar_t* pyKeywords[] = {
            L"def", L"class", L"self", L"return", L"import", L"from", L"if", L"elif", L"else",
            L"for", L"while", L"try", L"except", L"finally", L"with", L"as", L"async", L"await",
            L"lambda", L"yield", L"pass", L"break", L"continue", L"None", L"True", L"False"
        };
        for (const auto* kw : pyKeywords) {
            rawCandidates.push_back({ kw, L"Python Keyword", kw, CompletionKind::Keyword, 120, 0, 0, L"keyword" });
        }
    } else if (ext == L".css" || ext == L".scss" || ext == L".less") {
        const wchar_t* cssProps[] = {
            L"display", L"position", L"flex", L"grid", L"margin", L"padding", L"width", L"height",
            L"color", L"background", L"border", L"font-size", L"font-family", L"font-weight",
            L"line-height", L"letter-spacing", L"text-align", L"justify-content", L"align-items",
            L"flex-direction", L"flex-wrap", L"gap", L"opacity", L"visibility", L"overflow",
            L"z-index", L"box-shadow", L"border-radius", L"transform", L"transition", L"animation",
            L"cursor", L"outline", L"box-sizing", L"max-width", L"min-width", L"max-height", L"min-height"
        };
        for (const auto* cp : cssProps) {
            rawCandidates.push_back({ cp, L"CSS Property", cp, CompletionKind::Property, 120, 0, 0, L"css" });
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

    // Common web and programming vocabulary (properties, tags, methods, identifiers)
    for (const auto& v : m_commonVocabulary) {
        rawCandidates.push_back(v);
    }

    // Matching & Scoring Engine: Exact prefix (1000), Substring match (500), Fuzzy match (100)
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
        if (p == 0) {
            // Exact prefix match: Highest priority (score ~ 1000)
            IntelliSenseItem scored = item;
            scored.score += 1000 - static_cast<int>(item.label.length());
            scored.matchStart = 0;
            scored.matchLen = prefix.length();
            seenLabels.insert(item.label);
            matches.push_back(std::move(scored));
        } else if (p != std::wstring::npos) {
            // Substring match: Medium priority (score ~ 500)
            IntelliSenseItem scored = item;
            scored.score += 500 - static_cast<int>(p * 10 + item.label.length());
            scored.matchStart = p;
            scored.matchLen = prefix.length();
            seenLabels.insert(item.label);
            matches.push_back(std::move(scored));
        } else {
            // Fuzzy match: all chars of prefix appear in order (score ~ 100)
            size_t pi = 0;
            size_t firstMatch = std::wstring::npos;
            for (size_t ci = 0; ci < candLower.length() && pi < prefixLower.length(); ++ci) {
                if (candLower[ci] == prefixLower[pi]) {
                    if (firstMatch == std::wstring::npos) firstMatch = ci;
                    pi++;
                }
            }
            if (pi == prefixLower.length()) {
                IntelliSenseItem scored = item;
                scored.score += 100 - static_cast<int>(item.label.length());
                scored.matchStart = (firstMatch != std::wstring::npos) ? firstMatch : 0;
                scored.matchLen = prefix.length();
                seenLabels.insert(item.label);
                matches.push_back(std::move(scored));
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
