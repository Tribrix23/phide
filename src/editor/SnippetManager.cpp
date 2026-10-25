#include "phide/editor/SnippetManager.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cwctype>

namespace phide::editor {

void SnippetManager::Initialize(const std::wstring& snippetsFilePath) {
    m_snippetsFilePath = snippetsFilePath;
    LoadDefaults();
    EnsureDefaultSnippetsFile();
    LoadSnippets();
}

void SnippetManager::LoadDefaults() {
    m_snippets.clear();

    // 1. HTML5 Boilerplate & HTML Templates (Image 3 request: "like you know ! in vs code create html template")
    m_snippets.push_back({
        L"HTML5 Boilerplate Template (!)",
        L"!",
        {
            L"<!DOCTYPE html>",
            L"<html lang=\"en\">",
            L"<head>",
            L"    <meta charset=\"UTF-8\">",
            L"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">",
            L"    <title>${1:Document}</title>",
            L"</head>",
            L"<body>",
            L"    $0",
            L"</body>",
            L"</html>"
        },
        L"Complete HTML5 boilerplate skeleton with DOCTYPE, meta charset, viewport, title, and body",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"HTML5 Boilerplate (html:5)",
        L"html:5",
        {
            L"<!DOCTYPE html>",
            L"<html lang=\"en\">",
            L"<head>",
            L"    <meta charset=\"UTF-8\">",
            L"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">",
            L"    <title>${1:Document}</title>",
            L"</head>",
            L"<body>",
            L"    $0",
            L"</body>",
            L"</html>"
        },
        L"Complete HTML5 boilerplate skeleton with DOCTYPE, meta charset, viewport, title, and body",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Link External CSS Stylesheet",
        L"link:css",
        { L"<link rel=\"stylesheet\" href=\"${1:style.css}\" />" },
        L"Link external stylesheet link element",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"External JavaScript Script Tag",
        L"script:src",
        { L"<script src=\"${1:app.js}\"></script>" },
        L"Include external JavaScript source script element",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Meta Viewport Tag",
        L"meta:vp",
        { L"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />" },
        L"Responsive viewport meta tag",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Meta Charset UTF-8",
        L"meta:utf",
        { L"<meta charset=\"UTF-8\" />" },
        L"Standard UTF-8 encoding meta tag",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Anchor Hyperlink",
        L"a",
        { L"<a href=\"${1:#}\">${2:Link}</a>" },
        L"HTML anchor hyperlink tag",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Image Element",
        L"img",
        { L"<img src=\"${1:image.png}\" alt=\"${2:description}\" />" },
        L"HTML image tag with src and alt",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Button Element",
        L"btn",
        { L"<button type=\"${1:button}\">${2:Click}</button>" },
        L"HTML button element with type and text",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Form Post Element",
        L"form:post",
        {
            L"<form action=\"${1:#}\" method=\"post\">",
            L"    $0",
            L"</form>"
        },
        L"HTML form element with POST action",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Text Field",
        L"input:text",
        { L"<input type=\"text\" name=\"${1:name}\" placeholder=\"${2:Enter text...}\" />" },
        L"HTML text input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Password Field",
        L"input:password",
        { L"<input type=\"password\" name=\"${1:password}\" />" },
        L"HTML password input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Email Field",
        L"input:email",
        { L"<input type=\"email\" name=\"${1:email}\" placeholder=\"${2:name@example.com}\" />" },
        L"HTML email input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Submit Button",
        L"input:submit",
        { L"<input type=\"submit\" value=\"${1:Submit}\" />" },
        L"HTML form submit button",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"HTML Table Structure",
        L"table",
        {
            L"<table>",
            L"    <thead>",
            L"        <tr>",
            L"            <th>${1:Header}</th>",
            L"        </tr>",
            L"    </thead>",
            L"    <tbody>",
            L"        <tr>",
            L"            <td>$0</td>",
            L"        </tr>",
            L"    </tbody>",
            L"</table>"
        },
        L"Complete HTML table skeleton structure",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Unordered List with Item",
        L"ul>li",
        {
            L"<ul>",
            L"    <li>$0</li>",
            L"</ul>"
        },
        L"HTML unordered list with list item",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Ordered List with Item",
        L"ol>li",
        {
            L"<ol>",
            L"    <li>$0</li>",
            L"</ol>"
        },
        L"HTML ordered list with list item",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Container Div Element",
        L"div:container",
        {
            L"<div class=\"${1:container}\">",
            L"    $0",
            L"</div>"
        },
        L"HTML div container element",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Embedded Style Block",
        L"style",
        {
            L"<style>",
            L"    $0",
            L"</style>"
        },
        L"HTML embedded CSS style block",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Embedded Script Block",
        L"script",
        {
            L"<script>",
            L"    $0",
            L"</script>"
        },
        L"HTML embedded JavaScript script block",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Favicon Link",
        L"link:favicon",
        { L"<link rel=\"icon\" type=\"image/x-icon\" href=\"${1:favicon.ico}\">" },
        L"HTML link favicon tag",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Google Fonts Link Preconnect",
        L"link:font",
        {
            L"<link rel=\"preconnect\" href=\"https://fonts.googleapis.com\">",
            L"<link rel=\"preconnect\" href=\"https://fonts.gstatic.com\" crossorigin>",
            L"<link href=\"${1:https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap}\" rel=\"stylesheet\">"
        },
        L"Google Fonts preconnect and stylesheet link tags",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Canonical Link",
        L"link:canonical",
        { L"<link rel=\"canonical\" href=\"${1:https://example.com/}\" />" },
        L"SEO canonical URL link tag",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"ES Module Script Tag",
        L"script:module",
        { L"<script type=\"module\" src=\"${1:main.js}\"></script>" },
        L"JavaScript ES module script tag",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"DOM Ready Script Block",
        L"script:inline",
        {
            L"<script>",
            L"    document.addEventListener('DOMContentLoaded', () => {",
            L"        $0",
            L"    });",
            L"</script>"
        },
        L"Inline script block with DOMContentLoaded event listener",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Meta Description Tag",
        L"meta:desc",
        { L"<meta name=\"description\" content=\"${1:Page description here}\" />" },
        L"SEO meta description tag",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"OpenGraph Social Meta Tags",
        L"meta:og",
        {
            L"<meta property=\"og:title\" content=\"${1:Title}\" />",
            L"<meta property=\"og:description\" content=\"${2:Description}\" />",
            L"<meta property=\"og:image\" content=\"${3:https://example.com/og-image.png}\" />",
            L"<meta property=\"og:url\" content=\"${4:https://example.com/}\" />",
            L"<meta property=\"og:type\" content=\"website\" />"
        },
        L"OpenGraph metadata tags for social sharing previews",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Twitter Card Meta Tags",
        L"meta:twitter",
        {
            L"<meta name=\"twitter:card\" content=\"summary_large_image\" />",
            L"<meta name=\"twitter:title\" content=\"${1:Title}\" />",
            L"<meta name=\"twitter:description\" content=\"${2:Description}\" />",
            L"<meta name=\"twitter:image\" content=\"${3:https://example.com/og-image.png}\" />"
        },
        L"Twitter Card metadata tags",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Theme Color Meta Tag",
        L"meta:theme",
        { L"<meta name=\"theme-color\" content=\"${1:#0f172a}\" />" },
        L"Browser mobile theme color meta tag",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Form GET Method",
        L"form:get",
        {
            L"<form action=\"${1:#}\" method=\"get\">",
            L"    $0",
            L"</form>"
        },
        L"HTML form element with GET method",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Form Multipart File Upload",
        L"form:upload",
        {
            L"<form action=\"${1:#}\" method=\"post\" enctype=\"multipart/form-data\">",
            L"    <input type=\"file\" name=\"${2:file}\" />",
            L"    <button type=\"submit\">${3:Upload}</button>",
            L"</form>"
        },
        L"HTML form with multipart file upload support",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Number Field",
        L"input:number",
        { L"<input type=\"number\" name=\"${1:quantity}\" min=\"${2:1}\" max=\"${3:100}\" value=\"${4:1}\" />" },
        L"HTML numeric input field with min and max bounds",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Telephone Field",
        L"input:tel",
        { L"<input type=\"tel\" name=\"${1:phone}\" placeholder=\"${2:+1 (555) 000-0000}\" autocomplete=\"tel\" />" },
        L"HTML telephone number input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input URL Field",
        L"input:url",
        { L"<input type=\"url\" name=\"${1:url}\" placeholder=\"${2:https://example.com}\" />" },
        L"HTML URL input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Search Field",
        L"input:search",
        { L"<input type=\"search\" name=\"${1:q}\" placeholder=\"${2:Search...}\" />" },
        L"HTML search query input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Date Field",
        L"input:date",
        { L"<input type=\"date\" name=\"${1:date}\" />" },
        L"HTML date picker input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Time Field",
        L"input:time",
        { L"<input type=\"time\" name=\"${1:time}\" />" },
        L"HTML time picker input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Color Picker Field",
        L"input:color",
        { L"<input type=\"color\" name=\"${1:color}\" value=\"${2:#3b82f6}\" />" },
        L"HTML color picker input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Range Slider Field",
        L"input:range",
        { L"<input type=\"range\" name=\"${1:range}\" min=\"${2:0}\" max=\"${3:100}\" value=\"${4:50}\" />" },
        L"HTML range slider input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Checkbox with Label",
        L"input:checkbox",
        { L"<label><input type=\"checkbox\" name=\"${1:remember}\" value=\"${2:1}\" /> ${3:Remember me}</label>" },
        L"HTML checkbox with wrapping label",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Radio Button with Label",
        L"input:radio",
        { L"<label><input type=\"radio\" name=\"${1:option}\" value=\"${2:val}\" checked /> ${3:Option}</label>" },
        L"HTML radio button with wrapping label",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input File Picker",
        L"input:file",
        { L"<input type=\"file\" name=\"${1:attachment}\" accept=\"${2:image/*}\" />" },
        L"HTML file chooser input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Hidden Field",
        L"input:hidden",
        { L"<input type=\"hidden\" name=\"${1:token}\" value=\"${2:value}\" />" },
        L"HTML hidden input field",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input Reset Button",
        L"input:reset",
        { L"<input type=\"reset\" value=\"${1:Reset}\" />" },
        L"HTML form reset button",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Textarea Multi-line Control",
        L"textarea:full",
        { L"<textarea name=\"${1:message}\" rows=\"${2:4}\" cols=\"${3:50}\" placeholder=\"${4:Enter your message...}\"></textarea>" },
        L"HTML multi-line textarea input with rows and cols",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Select Dropdown with Options",
        L"select:options",
        {
            L"<select name=\"${1:selectName}\" id=\"${1:selectName}\">",
            L"    <option value=\"\">${2:Select an option...}</option>",
            L"    <option value=\"${3:val1}\">${4:Option 1}</option>",
            L"    <option value=\"${5:val2}\">${6:Option 2}</option>",
            L"</select>"
        },
        L"HTML select dropdown list with placeholder and option elements",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Input with Datalist Autocomplete",
        L"datalist:options",
        {
            L"<input list=\"${1:suggestions}\" name=\"${2:item}\" placeholder=\"${3:Choose or type...}\" />",
            L"<datalist id=\"${1:suggestions}\">",
            L"    <option value=\"${4:Choice 1}\">",
            L"    <option value=\"${5:Choice 2}\">",
            L"    <option value=\"${6:Choice 3}\">",
            L"</datalist>"
        },
        L"HTML datalist autocomplete suggestion component",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Submit Button Element",
        L"btn:submit",
        { L"<button type=\"submit\" class=\"${1:btn btn-primary}\">${2:Submit}</button>" },
        L"HTML form submit button",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Reset Button Element",
        L"btn:reset",
        { L"<button type=\"reset\" class=\"${1:btn btn-secondary}\">${2:Reset}</button>" },
        L"HTML form reset button",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Navigation Bar Component",
        L"nav:bar",
        {
            L"<nav class=\"navbar\">",
            L"    <div class=\"nav-brand\"><a href=\"${1:#}\">${2:Brand}</a></div>",
            L"    <ul class=\"nav-links\">",
            L"        <li><a href=\"${3:#}\">${4:Home}</a></li>",
            L"        <li><a href=\"${5:#}\">${6:About}</a></li>",
            L"        <li><a href=\"${7:#}\">${8:Contact}</a></li>",
            L"    </ul>",
            L"</nav>"
        },
        L"Complete responsive navigation bar component",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Header Hero Section",
        L"header:hero",
        {
            L"<header class=\"hero-section\">",
            L"    <h1>${1:Welcome to Our Platform}</h1>",
            L"    <p class=\"hero-lead\">${2:Build fast, modern, and high-performance web applications.}</p>",
            L"    <div class=\"hero-actions\">",
            L"        <a href=\"${3:#}\" class=\"btn btn-primary\">${4:Get Started}</a>",
            L"        <a href=\"${5:#}\" class=\"btn btn-outline\">${6:Learn More}</a>",
            L"    </div>",
            L"</header>"
        },
        L"Hero header landing banner section",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Footer Copyright Section",
        L"footer:copy",
        {
            L"<footer class=\"site-footer\">",
            L"    <p>&copy; ${1:2026} ${2:Your Name or Company}. All rights reserved.</p>",
            L"</footer>"
        },
        L"Standard copyright page footer",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Main Document Content Wrapper",
        L"main:content",
        {
            L"<main role=\"main\" class=\"container mx-auto px-4 py-8\">",
            L"    $0",
            L"</main>"
        },
        L"HTML5 main landmark wrapper",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Section with Heading Container",
        L"section:card",
        {
            L"<section class=\"py-12\">",
            L"    <div class=\"container\">",
            L"        <h2 class=\"text-2xl font-bold mb-4\">${1:Section Title}</h2>",
            L"        <p class=\"text-gray-600 mb-6\">${2:Section subtitle or introduction.}</p>",
            L"        $0",
            L"    </div>",
            L"</section>"
        },
        L"Semantic section block with container",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Blog Article Post Component",
        L"article:post",
        {
            L"<article class=\"post\">",
            L"    <header class=\"post-header\">",
            L"        <h1>${1:Article Title}</h1>",
            L"        <div class=\"post-meta\">",
            L"            <span>By <a href=\"${2:#}\">${3:Author}</a></span>",
            L"            <time datetime=\"${4:2026-01-01}\">${5:Jan 1, 2026}</time>",
            L"        </div>",
            L"    </header>",
            L"    <div class=\"post-content\">",
            L"        <p>$0</p>",
            L"    </div>",
            L"</article>"
        },
        L"Complete blog article post markup",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Sidebar Aside Component",
        L"aside:sidebar",
        {
            L"<aside class=\"sidebar\" aria-label=\"${1:Sidebar}\">",
            L"    <div class=\"widget\">",
            L"        <h3>${2:Related Topics}</h3>",
            L"        <ul>",
            L"            <li><a href=\"${3:#}\">${4:Topic 1}</a></li>",
            L"            <li><a href=\"${5:#}\">${6:Topic 2}</a></li>",
            L"        </ul>",
            L"    </div>",
            L"</aside>"
        },
        L"Semantic aside sidebar component",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Figure with Caption",
        L"figure:img",
        {
            L"<figure>",
            L"    <img src=\"${1:image.jpg}\" alt=\"${2:Description}\" />",
            L"    <figcaption>${3:Caption describing the image}</figcaption>",
            L"</figure>"
        },
        L"HTML figure with nested image and figcaption",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Responsive Picture with WebP Fallback",
        L"picture:webp",
        {
            L"<picture>",
            L"    <source srcset=\"${1:image.webp}\" type=\"image/webp\">",
            L"    <source srcset=\"${2:image.jpg}\" type=\"image/jpeg\">",
            L"    <img src=\"${2:image.jpg}\" alt=\"${3:Description}\" loading=\"lazy\">",
            L"</picture>"
        },
        L"Responsive picture element with WebP next-gen format support",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"HTML5 Video Player",
        L"video:controls",
        {
            L"<video src=\"${1:video.mp4}\" controls poster=\"${2:poster.jpg}\" width=\"${3:640}\" height=\"${4:360}\">",
            L"    <track kind=\"subtitles\" src=\"${5:subtitles.vtt}\" srclang=\"en\" label=\"English\">",
            L"    Your browser does not support the video tag.",
            L"</video>"
        },
        L"HTML5 video player element with controls and subtitles",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"HTML5 Audio Player",
        L"audio:controls",
        {
            L"<audio controls src=\"${1:audio.mp3}\">",
            L"    Your browser does not support the audio element.",
            L"</audio>"
        },
        L"HTML5 audio player element with controls",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"HTML5 Native Dialog Modal",
        L"dialog:modal",
        {
            L"<dialog id=\"${1:myDialog}\" class=\"dialog-modal\">",
            L"    <form method=\"dialog\">",
            L"        <h2>${2:Dialog Title}</h2>",
            L"        <p>${3:Dialog body text content goes here.}</p>",
            L"        <button value=\"cancel\">${4:Cancel}</button>",
            L"        <button value=\"confirm\" class=\"btn-primary\">${5:Confirm}</button>",
            L"    </form>",
            L"</dialog>"
        },
        L"HTML5 native dialog modal element with form method dialog",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Details Accordion Disclosure",
        L"details:summary",
        {
            L"<details>",
            L"    <summary>${1:Click to reveal details}</summary>",
            L"    <p>${2:Hidden details content that unfolds on user click.}</p>",
            L"</details>"
        },
        L"HTML5 collapsible details and summary disclosure widget",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"SVG Feather/Lucide Icon Template",
        L"svg:icon",
        {
            L"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"${1:24}\" height=\"${1:24}\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">",
            L"    $0",
            L"</svg>"
        },
        L"SVG vector icon template container",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Canvas 2D Context Setup",
        L"canvas:2d",
        {
            L"<canvas id=\"${1:myCanvas}\" width=\"${2:600}\" height=\"${3:400}\"></canvas>",
            L"<script>",
            L"    const canvas = document.getElementById('${1:myCanvas}');",
            L"    const ctx = canvas.getContext('2d');",
            L"    $0",
            L"</script>"
        },
        L"HTML5 canvas element with 2D rendering context boilerplate",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Definition Description List",
        L"dl:terms",
        {
            L"<dl>",
            L"    <dt>${1:Term 1}</dt>",
            L"    <dd>${2:Definition of term 1}</dd>",
            L"    <dt>${3:Term 2}</dt>",
            L"    <dd>${4:Definition of term 2}</dd>",
            L"</dl>"
        },
        L"HTML definition list with terms and descriptions",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Table Striped with Thead and Tbody",
        L"table:striped",
        {
            L"<table class=\"table table-striped\">",
            L"    <thead>",
            L"        <tr>",
            L"            <th>${1:#}</th>",
            L"            <th>${2:Name}</th>",
            L"            <th>${3:Email}</th>",
            L"            <th>${4:Status}</th>",
            L"        </tr>",
            L"    </thead>",
            L"    <tbody>",
            L"        <tr>",
            L"            <td>1</td>",
            L"            <td>${5:John Doe}</td>",
            L"            <td>${6:john@example.com}</td>",
            L"            <td><span class=\"badge badge-success\">${7:Active}</span></td>",
            L"        </tr>",
            L"    </tbody>",
            L"</table>"
        },
        L"Complete formatted table component with headers, rows and badges",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Card Component",
        L"div:card",
        {
            L"<div class=\"card\">",
            L"    <div class=\"card-header\">",
            L"        <h3 class=\"card-title\">${1:Card Title}</h3>",
            L"    </div>",
            L"    <div class=\"card-body\">",
            L"        <p>${2:Card description text and details.}</p>",
            L"    </div>",
            L"    <div class=\"card-footer\">",
            L"        <button class=\"btn btn-primary\">${3:Action}</button>",
            L"    </div>",
            L"</div>"
        },
        L"Standard UI card container with header, body and footer",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"CSS Grid Container",
        L"div:grid",
        {
            L"<div class=\"grid grid-cols-${1:3} gap-6\">",
            L"    <div class=\"col-item\">$0</div>",
            L"    <div class=\"col-item\"></div>",
            L"    <div class=\"col-item\"></div>",
            L"</div>"
        },
        L"Responsive CSS grid layout container",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"CSS Flexbox Row Container",
        L"div:flex",
        {
            L"<div class=\"flex items-center justify-between\">",
            L"    <div>$1</div>",
            L"    <div>$0</div>",
            L"</div>"
        },
        L"Flexbox row layout with space-between alignment",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Badge Status Pill",
        L"div:badge",
        { L"<span class=\"badge badge-${1:primary}\">${2:New}</span>" },
        L"UI status badge pill component",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Alert Notification Box",
        L"div:alert",
        {
            L"<div class=\"alert alert-${1:info}\" role=\"alert\">",
            L"    <strong>${2:Notice:}</strong> ${3:Operation completed successfully.}",
            L"</div>"
        },
        L"Accessible alert notification component",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Breadcrumb Navigation Component",
        L"div:breadcrumb",
        {
            L"<nav aria-label=\"Breadcrumb\">",
            L"    <ol class=\"breadcrumb\">",
            L"        <li><a href=\"${1:/}\">Home</a></li>",
            L"        <li><a href=\"${2:/section}\">${3:Section}</a></li>",
            L"        <li aria-current=\"page\">${4:Current Page}</li>",
            L"    </ol>",
            L"</nav>"
        },
        L"Accessible breadcrumb trail navigation component",
        L"html,htm,php"
    });

    m_snippets.push_back({
        L"Iframe Embedded Frame",
        L"iframe:embed",
        { L"<iframe src=\"${1:https://example.com}\" width=\"${2:100%}\" height=\"${3:450}\" style=\"border:0;\" allowfullscreen=\"\" loading=\"lazy\"></iframe>" },
        L"Responsive embedded iframe element",
        L"html,htm,php"
    });

    // 2. CSS Snippets
    m_snippets.push_back({
        L"Flexbox Center Alignment",
        L"flexcenter",
        {
            L"display: flex;",
            L"justify-content: center;",
            L"align-items: center;"
        },
        L"CSS flexbox centering layout",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Flexbox Column Layout",
        L"flexcol",
        {
            L"display: flex;",
            L"flex-direction: column;",
            L"gap: ${1:12px};"
        },
        L"CSS flexbox vertical column layout",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"CSS Grid Columns Layout",
        L"grid",
        {
            L"display: grid;",
            L"grid-template-columns: repeat(${1:3}, 1fr);",
            L"gap: ${2:16px};"
        },
        L"CSS grid responsive columns layout",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Universal CSS Reset",
        L"reset",
        {
            L"*, *::before, *::after {",
            L"    box-sizing: border-box;",
            L"    margin: 0;",
            L"    padding: 0;",
            L"}"
        },
        L"Universal CSS box-sizing and margin reset",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"CSS Media Query",
        L"media",
        {
            L"@media (max-width: ${1:768px}) {",
            L"    $0",
            L"}"
        },
        L"CSS responsive media query",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"CSS Animation Keyframes",
        L"keyframes",
        {
            L"@keyframes ${1:fadeIn} {",
            L"    from { opacity: 0; }",
            L"    to   { opacity: 1; }",
            L"}"
        },
        L"CSS keyframe animation definition",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Glassmorphism Card Effect",
        L"glass",
        {
            L"background: rgba(255, 255, 255, 0.08);",
            L"backdrop-filter: blur(12px);",
            L"border: 1px solid rgba(255, 255, 255, 0.15);",
            L"border-radius: 8px;"
        },
        L"Modern glassmorphism translucent card styling",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"CSS Custom Properties Root Variables",
        L":root",
        {
            L":root {",
            L"    --primary: ${1:#3b82f6};",
            L"    --background: ${2:#0f172a};",
            L"    --text: ${3:#f8fafc};",
            L"    --radius: ${4:8px};",
            L"}"
        },
        L"CSS custom variables declarations in :root selector",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"CSS Custom Properties Root Variables",
        L"root",
        {
            L":root {",
            L"    --primary: ${1:#3b82f6};",
            L"    --background: ${2:#0f172a};",
            L"    --text: ${3:#f8fafc};",
            L"    --radius: ${4:8px};",
            L"}"
        },
        L"CSS custom variables declarations in :root selector",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"CSS Responsive Auto-fit Grid",
        L"grid:auto",
        {
            L"display: grid;",
            L"grid-template-columns: repeat(auto-fit, minmax(${1:250px}, 1fr));",
            L"gap: ${2:1.5rem};"
        },
        L"Responsive auto-fit CSS grid template",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Modern Drop Shadow",
        L"shadow",
        { L"box-shadow: 0 10px 15px -3px rgba(0, 0, 0, 0.1), 0 4px 6px -4px rgba(0, 0, 0, 0.1);" },
        L"Modern layered box shadow",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Linear Gradient Background",
        L"gradient",
        { L"background: linear-gradient(${1:135deg}, ${2:#3b82f6} 0%, ${3:#8b5cf6} 100%);" },
        L"CSS linear gradient background",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"CSS Card Container Styling",
        L"card",
        {
            L"background: ${1:#ffffff};",
            L"border: 1px solid ${2:#e2e8f0};",
            L"border-radius: ${3:8px};",
            L"padding: ${4:1.5rem};",
            L"box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1);"
        },
        L"Complete CSS card component styling rules",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"CSS Interactive Button",
        L"btn",
        {
            L"display: inline-flex;",
            L"align-items: center;",
            L"justify-content: center;",
            L"padding: 0.5rem 1rem;",
            L"border-radius: 6px;",
            L"font-weight: 500;",
            L"cursor: pointer;",
            L"transition: all 0.2s ease;"
        },
        L"Modern flexible button component styling",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Absolute Centering Transform",
        L"center:abs",
        {
            L"position: absolute;",
            L"top: 50%;",
            L"left: 50%;",
            L"transform: translate(-50%, -50%);"
        },
        L"Exact centering using absolute positioning and translate",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Single-line Text Truncation Ellipsis",
        L"truncate",
        {
            L"white-space: nowrap;",
            L"overflow: hidden;",
            L"text-overflow: ellipsis;"
        },
        L"CSS single-line ellipsis text truncation",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Multi-line Text Line Clamp",
        L"clamp",
        {
            L"display: -webkit-box;",
            L"-webkit-line-clamp: ${1:3};",
            L"-webkit-box-orient: vertical;",
            L"overflow: hidden;"
        },
        L"Multi-line text clamping with ellipsis",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Smooth Page Scroll Behavior",
        L"scroll",
        { L"scroll-behavior: smooth;" },
        L"Smooth scrolling behavior for html root",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Dark Mode Prefers Color Scheme",
        L"dark",
        {
            L"@media (prefers-color-scheme: dark) {",
            L"    $0",
            L"}"
        },
        L"Media query for user dark color scheme preference",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Custom Webkit Scrollbar Styling",
        L"scrollbar",
        {
            L"::-webkit-scrollbar {",
            L"    width: ${1:8px};",
            L"}",
            L"::-webkit-scrollbar-track {",
            L"    background: ${2:#f1f5f9};",
            L"}",
            L"::-webkit-scrollbar-thumb {",
            L"    background: ${3:#cbd5e1};",
            L"    border-radius: 4px;",
            L"}"
        },
        L"Custom scrollbar track and thumb styling",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Smooth Transition Declaration",
        L"transition",
        { L"transition: ${1:all} ${2:0.3s} ${3:ease};" },
        L"CSS transition timing shorthand",
        L"css,scss,less,html"
    });

    m_snippets.push_back({
        L"Aspect Ratio Box",
        L"aspect",
        { L"aspect-ratio: ${1:16 / 9};" },
        L"Modern CSS aspect-ratio declaration",
        L"css,scss,less,html"
    });

    // 3. TypeScript / JavaScript Snippets
    m_snippets.push_back({
        L"Console Log (JS/TS/HTML)",
        L"log",
        { L"console.log($1);" },
        L"Log output to developer console",
        L"html,htm,js,ts,jsx,tsx,php"
    });

    m_snippets.push_back({
        L"Console Log (clg)",
        L"clg",
        { L"console.log($1);" },
        L"Log output to developer console",
        L"ts,js,tsx,jsx,html,htm,php"
    });

    m_snippets.push_back({
        L"Console Error",
        L"cerr",
        { L"console.error($1);" },
        L"Log error diagnostic to developer console",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Console Warn",
        L"cwarn",
        { L"console.warn($1);" },
        L"Log warning alert to developer console",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Console Table",
        L"ctable",
        { L"console.table($1);" },
        L"Log tabular dataset to developer console",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Arrow Function",
        L"afn",
        {
            L"const ${1:name} = ($2) => {",
            L"    $0",
            L"};"
        },
        L"ES6 Arrow Function declaration",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Standard Function",
        L"fn",
        {
            L"function ${1:name}($2) {",
            L"    $0",
            L"}"
        },
        L"Standard function declaration",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Async Function",
        L"asyncfn",
        {
            L"async function ${1:name}($2) {",
            L"    $0",
            L"}"
        },
        L"Asynchronous function declaration",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"New Promise",
        L"prom",
        {
            L"new Promise((resolve, reject) => {",
            L"    $0",
            L"});"
        },
        L"Construct new ES6 Promise",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Fetch API Request",
        L"fetch",
        {
            L"fetch('${1:url}')",
            L"    .then(res => res.json())",
            L"    .then(data => {",
            L"        $0",
            L"    });"
        },
        L"Standard fetch JSON request promise chain",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Try Catch Block",
        L"trycatch",
        {
            L"try {",
            L"    $1",
            L"} catch (error) {",
            L"    console.error(error);",
            L"    $0",
            L"}"
        },
        L"Try Catch error handling block",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"For Loop (JS)",
        L"fori",
        {
            L"for (let ${1:i} = 0; ${1:i} < ${2:array}.length; ${1:i}++) {",
            L"    $0",
            L"}"
        },
        L"Standard indexed for loop",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"For Of Loop (JS)",
        L"forof",
        {
            L"for (const ${1:item} of ${2:iterable}) {",
            L"    $0",
            L"}"
        },
        L"ES6 for..of iterable loop",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Add Event Listener",
        L"ael",
        {
            L"${1:element}.addEventListener('${2:click}', (e) => {",
            L"    $0",
            L"});"
        },
        L"DOM addEventListener callback binding",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Document Query Selector",
        L"qs",
        { L"document.querySelector('${1:selector}')" },
        L"Query single DOM element",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Document Query Selector All",
        L"qsa",
        { L"document.querySelectorAll('${1:selector}')" },
        L"Query all matching DOM elements",
        L"ts,js,tsx,jsx,html"
    });

    m_snippets.push_back({
        L"Import Statement",
        L"imp",
        { L"import { $1 } from '$2';" },
        L"ES6 import statement",
        L"ts,js,tsx,jsx"
    });

    m_snippets.push_back({
        L"CommonJS Require",
        L"req",
        { L"const $1 = require('$2');" },
        L"CommonJS module require statement",
        L"ts,js"
    });

    m_snippets.push_back({
        L"Export Default",
        L"ed",
        { L"export default $1;" },
        L"ES6 export default statement",
        L"ts,js,tsx,jsx"
    });

    m_snippets.push_back({
        L"React Functional Component",
        L"rfc",
        {
            L"export const ${1:ComponentName} = () => {",
            L"    return (",
            L"        <div>",
            L"            $0",
            L"        </div>",
            L"    );",
            L"};"
        },
        L"React functional component export",
        L"tsx,jsx"
    });

    m_snippets.push_back({
        L"React useState Hook",
        L"us",
        { L"const [${1:state}, set${1:state}] = useState($2);" },
        L"React useState hook",
        L"tsx,jsx,ts,js"
    });

    m_snippets.push_back({
        L"React useEffect Hook",
        L"ue",
        {
            L"useEffect(() => {",
            L"    $0",
            L"}, []);"
        },
        L"React useEffect hook",
        L"tsx,jsx,ts,js"
    });

    // 4. C / C++ Snippets
    m_snippets.push_back({
        L"Include Header",
        L"inc",
        { L"#include <$1>" },
        L"Include standard library header",
        L"cpp,hpp,c,h,cc,cxx"
    });

    m_snippets.push_back({
        L"Include Local Header",
        L"incl",
        { L"#include \"$1\"" },
        L"Include project local header",
        L"cpp,hpp,c,h,cc,cxx"
    });

    m_snippets.push_back({
        L"Main Function",
        L"main",
        {
            L"int main(int argc, char* argv[]) {",
            L"    $0",
            L"    return 0;",
            L"}"
        },
        L"Standard main entry function",
        L"cpp,c,cc,cxx"
    });

    m_snippets.push_back({
        L"Print to Console (C++ log)",
        L"log",
        { L"std::cout << \"$1\" << std::endl;" },
        L"Log output to console in C++",
        L"cpp,hpp,c,h,cc,cxx"
    });

    m_snippets.push_back({
        L"Print to Console (C++ cout)",
        L"cout",
        { L"std::cout << $1 << std::endl;" },
        L"Print expression to standard console output",
        L"cpp,hpp,c,h,cc,cxx"
    });

    m_snippets.push_back({
        L"For Loop (C++)",
        L"fori",
        {
            L"for (size_t ${1:i} = 0; ${1:i} < ${2:count}; ++${1:i}) {",
            L"    $0",
            L"}"
        },
        L"Standard indexed for loop",
        L"cpp,c,cc,cxx"
    });

    m_snippets.push_back({
        L"Class Definition",
        L"class",
        {
            L"class ${1:MyClass} {",
            L"public:",
            L"    ${1:MyClass}();",
            L"    virtual ~${1:MyClass}();",
            L"private:",
            L"    $0",
            L"};"
        },
        L"C++ Class boilerplate",
        L"cpp,hpp,cc,h"
    });

    m_snippets.push_back({
        L"Struct Definition",
        L"struct",
        {
            L"struct ${1:MyStruct} {",
            L"    $0",
            L"};"
        },
        L"C++ Struct boilerplate",
        L"cpp,hpp,cc,h"
    });

    m_snippets.push_back({
        L"Std Vector Declaration",
        L"vec",
        { L"std::vector<${1:int}> ${2:items};" },
        L"Standard vector container declaration",
        L"cpp,hpp,cc,h"
    });

    // 5. Python Snippets
    m_snippets.push_back({
        L"Python Function",
        L"def",
        {
            L"def ${1:function_name}($2):",
            L"    \"\"\"$3\"\"\"",
            L"    $0"
        },
        L"Python function definition",
        L"py"
    });

    m_snippets.push_back({
        L"Python If Main",
        L"ifmain",
        {
            L"if __name__ == '__main__':",
            L"    $0"
        },
        L"Python entrypoint boilerplate",
        L"py"
    });

    m_snippets.push_back({
        L"Python Class",
        L"class",
        {
            L"class ${1:ClassName}:",
            L"    def __init__(self$2):",
            L"        $0"
        },
        L"Python class definition",
        L"py"
    });

    m_snippets.push_back({
        L"Python Print",
        L"print",
        { L"print($1)" },
        L"Python print statement",
        L"py"
    });

    m_snippets.push_back({
        L"Print to Console (Python)",
        L"log",
        { L"print($1)" },
        L"Print expression to standard console output in Python",
        L"py"
    });
}

static std::wstring EscapeJsonString(const std::wstring& input) {
    std::wstring out;
    out.reserve(input.length() * 2);
    for (wchar_t c : input) {
        if (c == L'\\') out += L"\\\\";
        else if (c == L'"') out += L"\\\"";
        else if (c == L'\n') out += L"\\n";
        else if (c == L'\r') out += L"\\r";
        else if (c == L'\t') out += L"\\t";
        else out += c;
    }
    return out;
}

void SnippetManager::SaveSnippets(const std::wstring& filePath) const {
    std::wofstream out(filePath);
    if (!out.is_open()) return;

    out << L"{\n";
    out << L"    // User Code Snippets for phide IDE\n";
    out << L"    // Each snippet is defined under a snippet name and has a prefix, body, description, and scope.\n";
    out << L"    // Use $1, $2 for tab stops, $0 for final cursor position, and ${1:label} for placeholders.\n\n";

    for (size_t sIdx = 0; sIdx < m_snippets.size(); ++sIdx) {
        const auto& sn = m_snippets[sIdx];
        out << L"    \"" << EscapeJsonString(sn.name) << L"\": {\n";
        out << L"        \"prefix\": \"" << EscapeJsonString(sn.prefix) << L"\",\n";
        out << L"        \"body\": [\n";
        for (size_t bIdx = 0; bIdx < sn.body.size(); ++bIdx) {
            out << L"            \"" << EscapeJsonString(sn.body[bIdx]) << L"\"";
            if (bIdx + 1 < sn.body.size()) out << L",";
            out << L"\n";
        }
        out << L"        ],\n";
        out << L"        \"description\": \"" << EscapeJsonString(sn.description) << L"\",\n";
        out << L"        \"scope\": \"" << EscapeJsonString(sn.scope) << L"\"\n";
        out << L"    }";
        if (sIdx + 1 < m_snippets.size()) out << L",";
        out << L"\n\n";
    }

    out << L"}\n";
    out.close();
}

void SnippetManager::EnsureDefaultSnippetsFile() {
    if (m_snippetsFilePath.empty()) return;

    std::filesystem::path p(m_snippetsFilePath);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);

    bool needsExport = !std::filesystem::exists(p, ec);
    if (!needsExport) {
        // If file exists, check if it's the old stub or lacks newly expanded HTML snippets like picture:webp
        std::wifstream in(m_snippetsFilePath);
        if (in.is_open()) {
            std::wstring content((std::istreambuf_iterator<wchar_t>(in)), std::istreambuf_iterator<wchar_t>());
            in.close();
            // If missing "!" or "picture:webp" or "css:gradient" or file is too small to contain the full expanded catalog
            if (content.find(L"\"prefix\": \"!\"") == std::wstring::npos || 
                content.find(L"\"prefix\": \"picture:webp\"") == std::wstring::npos ||
                content.find(L"\"prefix\": \"css:gradient\"") == std::wstring::npos ||
                content.length() < 15000) {
                needsExport = true;
            }
        } else {
            needsExport = true;
        }
    }

    if (needsExport) {
        SaveSnippets(m_snippetsFilePath);
    }
}

void SnippetManager::LoadSnippets() {
    if (m_snippetsFilePath.empty() || !std::filesystem::exists(m_snippetsFilePath)) return;

    std::wifstream in(m_snippetsFilePath);
    if (!in.is_open()) return;

    std::wstring content((std::istreambuf_iterator<wchar_t>(in)), std::istreambuf_iterator<wchar_t>());
    in.close();

    // Custom lightweight JSON parser for user snippets
    size_t i = 0;
    size_t len = content.length();

    auto skipWs = [&]() {
        while (i < len) {
            if (content[i] == L' ' || content[i] == L'\t' || content[i] == L'\r' || content[i] == L'\n') {
                i++;
            } else if (i + 1 < len && content[i] == L'/' && content[i + 1] == L'/') {
                while (i < len && content[i] != L'\n') i++;
            } else {
                break;
            }
        }
    };

    auto readString = [&]() -> std::wstring {
        if (i >= len || content[i] != L'"') return L"";
        i++; // skip open quote
        std::wstring str;
        while (i < len && content[i] != L'"') {
            if (content[i] == L'\\' && i + 1 < len) {
                i++;
                if (content[i] == L'n') str += L'\n';
                else if (content[i] == L't') str += L'\t';
                else if (content[i] == L'"') str += L'"';
                else if (content[i] == L'\\') str += L'\\';
                else str += content[i];
            } else {
                str += content[i];
            }
            i++;
        }
        if (i < len && content[i] == L'"') i++; // skip close quote
        return str;
    };

    skipWs();
    if (i >= len || content[i] != L'{') return;
    i++; // skip root {

    while (i < len) {
        skipWs();
        if (i >= len || content[i] == L'}') break;

        std::wstring snippetName = readString();
        skipWs();
        if (i >= len || content[i] != L':') break;
        i++; // skip :
        skipWs();
        if (i >= len || content[i] != L'{') break;
        i++; // skip {

        Snippet sn;
        sn.name = snippetName;

        while (i < len) {
            skipWs();
            if (i >= len || content[i] == L'}') {
                i++;
                break;
            }

            std::wstring key = readString();
            skipWs();
            if (i < len && content[i] == L':') i++;
            skipWs();

            if (key == L"prefix") {
                sn.prefix = readString();
            } else if (key == L"description") {
                sn.description = readString();
            } else if (key == L"scope") {
                sn.scope = readString();
            } else if (key == L"body") {
                if (i < len && content[i] == L'[') {
                    i++; // skip [
                    while (i < len) {
                        skipWs();
                        if (i >= len || content[i] == L']') {
                            i++;
                            break;
                        }
                        sn.body.push_back(readString());
                        skipWs();
                        if (i < len && content[i] == L',') i++;
                    }
                } else if (i < len && content[i] == L'"') {
                    sn.body.push_back(readString());
                }
            } else {
                // Unknown field - skip string or array
                if (i < len && content[i] == L'"') readString();
                else if (i < len && content[i] == L'[') {
                    while (i < len && content[i] != L']') i++;
                    if (i < len) i++;
                }
            }

            skipWs();
            if (i < len && content[i] == L',') i++;
        }

        if (!sn.prefix.empty() && !sn.body.empty()) {
            // Replace existing or add
            auto it = std::find_if(m_snippets.begin(), m_snippets.end(), [&](const Snippet& s) {
                return (s.name == sn.name && s.prefix == sn.prefix) || (s.prefix == sn.prefix && s.scope == sn.scope);
            });
            if (it != m_snippets.end()) {
                *it = sn;
            } else {
                m_snippets.push_back(sn);
            }
        }

        skipWs();
        if (i < len && content[i] == L',') i++;
    }
}

bool SnippetManager::MatchesScope(const std::wstring& scope, const std::wstring& fileExt) {
    if (scope.empty()) return true;
    if (fileExt.empty()) return true;

    std::wstring lowerExt = fileExt;
    for (auto& c : lowerExt) c = towlower(c);
    if (!lowerExt.empty() && lowerExt[0] == L'.') {
        lowerExt = lowerExt.substr(1);
    }

    std::wstringstream ss(scope);
    std::wstring item;
    while (std::getline(ss, item, L',')) {
        size_t f = item.find_first_not_of(L" ");
        size_t l = item.find_last_not_of(L" ");
        if (f != std::wstring::npos) item = item.substr(f, l - f + 1);
        for (auto& c : item) c = towlower(c);
        if (!item.empty() && item[0] == L'.') {
            item = item.substr(1);
        }
        if (item == lowerExt) return true;
    }
    return false;
}

const Snippet* SnippetManager::FindSnippetByPrefix(const std::wstring& prefix, const std::wstring& fileExt) const {
    for (const auto& sn : m_snippets) {
        if (sn.prefix == prefix && MatchesScope(sn.scope, fileExt)) {
            return &sn;
        }
    }
    for (const auto& sn : m_snippets) {
        if (sn.prefix == prefix && sn.scope.empty()) {
            return &sn;
        }
    }
    return nullptr;
}

bool SnippetManager::ExpandSnippet(const Snippet& snippet, Buffer& buffer, size_t prefixLen) {
    std::vector<std::wstring> processedLines;
    int targetCursorLineOffset = -1;
    int targetCursorCol = -1;
    bool foundPlaceholder1 = false;
    bool foundPlaceholder0 = false;

    for (size_t lIdx = 0; lIdx < snippet.body.size(); ++lIdx) {
        std::wstring raw = snippet.body[lIdx];
        std::wstring clean;

        for (size_t c = 0; c < raw.length(); ++c) {
            if (raw[c] == L'$') {
                if (c + 1 < raw.length() && raw[c + 1] == L'{') {
                    // ${1:name}
                    size_t colon = raw.find(L':', c + 2);
                    size_t close = raw.find(L'}', c + 2);
                    if (colon != std::wstring::npos && close != std::wstring::npos && colon < close) {
                        std::wstring defVal = raw.substr(colon + 1, close - colon - 1);
                        if (!foundPlaceholder1) {
                            targetCursorLineOffset = static_cast<int>(lIdx);
                            targetCursorCol = static_cast<int>(clean.length());
                            foundPlaceholder1 = true;
                        }
                        clean += defVal;
                        c = close;
                        continue;
                    }
                } else if (c + 1 < raw.length() && raw[c + 1] == L'1') {
                    if (!foundPlaceholder1) {
                        targetCursorLineOffset = static_cast<int>(lIdx);
                        targetCursorCol = static_cast<int>(clean.length());
                        foundPlaceholder1 = true;
                    }
                    c++; // skip 1
                    continue;
                } else if (c + 1 < raw.length() && raw[c + 1] == L'0') {
                    if (!foundPlaceholder0 && !foundPlaceholder1) {
                        targetCursorLineOffset = static_cast<int>(lIdx);
                        targetCursorCol = static_cast<int>(clean.length());
                        foundPlaceholder0 = true;
                    }
                    c++; // skip 0
                    continue;
                } else if (c + 1 < raw.length() && iswdigit(raw[c + 1])) {
                    c++; // skip other digits $2, $3...
                    continue;
                }
            }
            clean += raw[c];
        }
        processedLines.push_back(clean);
    }

    return buffer.ReplacePrefixWithSnippet(prefixLen, processedLines, targetCursorLineOffset, targetCursorCol);
}

bool SnippetManager::TryExpand(Buffer& buffer, const std::wstring& fileExt) {
    auto cursor = buffer.GetCursor();
    std::wstring line = buffer.GetLine(cursor.line);
    if (cursor.col == 0 || line.empty()) return false;

    size_t startCol = cursor.col;
    while (startCol > 0) {
        wchar_t ch = line[startCol - 1];
        if (iswalnum(ch) || ch == L'_' || ch == L'!' || ch == L':') {
            --startCol;
        } else {
            break;
        }
    }
    if (startCol == cursor.col) return false;

    std::wstring prefix = line.substr(startCol, cursor.col - startCol);
    const Snippet* pMatch = FindSnippetByPrefix(prefix, fileExt);
    if (!pMatch) return false;

    return ExpandSnippet(*pMatch, buffer, prefix.length());
}

} // namespace phide::editor
