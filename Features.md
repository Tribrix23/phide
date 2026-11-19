# Phide — 20 New Practical Developer Features (Gap-Verified Roadmap)

> Verified against `PLAN.md` (Features 1–50) and the current `src/` / `include/phide/` codebase. None of the items below are implemented yet—and all 20 are **100% deterministic, zero-LLM tools** built natively on Phide's C++20 / Direct2D / Win32 architecture.

---

## Core Editing & Productivity

### 1. AST/Bracket-Aware Expand & Shrink Selection (`Shift+Alt+Right` / `Left`)
- **Gap**: Phide supports word, line, multi-cursor, and rectangular box selection, but lacks structural selection expansion.
- **What It Does**: Incrementally grows or shrinks selection along syntax boundaries: `identifier` → `string interior` → `string + quotes` → `argument` → `argument list` → `statement` → `block { ... }` → `function / HTML tag` → `class`.

### 2. Multi-Cursor Column Alignment & Sequence / UUID Generator
- **Gap**: Phide has multi-cursor typing and case transforms (`UPPER`, `camelCase`, `snake_case`, etc.), but no alignment or sequence tools.
- **What It Does**:
  - **Align by Delimiter**: Align selected lines or multi-cursors on `=`, `:`, `=>`, `|`, or `,`.
  - **Generate Sequence**: Insert incrementing numbers (`1, 2, 3...` or `01, 02...`), letters (`a, b, c...`), or random UUID v4 values across active cursors.

### 3. Editor Keyboard Macro Recorder (`Ctrl+Shift+Q` / `Ctrl+Q`)
- **Gap**: Repetitive multi-step edits that don't fit a single regex find/replace currently require manual repetition.
- **What It Does**: Record a sequence of keystrokes, cursor motions, and editor commands, then replay once (`Ctrl+Q`), `N` times, or across every selected line / multi-cursor.

### 4. Interactive Regex & Cron Expression Playground (+ Inline Regex Hover)
- **Gap**: Phide supports regex in Find/Replace and Log Tail, but has no interactive regex/cron lab.
- **What It Does**:
  - Built-in offline **Regex & Cron Lab** tab with live match highlighting, capture group breakdown (`$1`, `(?<name>...)`), replacement preview, PCRE/ECMAScript flag toggles, and catastrophic-backtracking warnings.
  - **Cron Decoder**: Displays human-readable meaning and the next 5 execution timestamps for cron strings (both in the lab and on hover in code).

---

## Web, PHP, CSS & Frontend Tooling

### 5. Built-in Image, SVG & Font Asset Viewer Tab (Direct2D / DirectWrite)
- **Gap**: `QuickOpenModal` skips image binaries and Explorer cannot preview images, SVGs, or fonts inside editor tabs.
- **What It Does**: Open `.png`, `.jpg`, `.webp`, `.ico`, `.svg`, `.ttf`, `.otf`, `.woff2` directly in a native WIC/Direct2D viewer tab:
  - Inspect dimensions, bit depth, file size, and dominant colors.
  - **SVG Split View**: Live rendered preview alongside raw XML markup with instant updates as you type.
  - **Font Waterfall**: Render sample text across sizes (`12px–48px`) plus copy-ready `@font-face` / `<img>` / Base64 snippets.

### 6. In-Editor WCAG Contrast Checker & Visual `cubic-bezier` / CSS Grid Helper
- **Gap**: `ColorPickerModal` edits colors and CSS Peek shows rule text, but there is no accessibility contrast check or easing/grid visual editor.
- **What It Does**:
  - Compute **WCAG 2.1 AA/AAA** contrast ratios (`4.5:1`, `7:1`) when foreground `color` and `background-color` appear together in a CSS rule or hovered token.
  - Interactive visual curve editor for `cubic-bezier(...)` transitions and visual track preview for `grid-template-columns` / `gap`.

### 7. Dead Code, Unused Export & Unused CSS Selector Audit
- **Gap**: `WorkspaceNavigation` resolves symbols and CSS Peek searches CSS files, but nothing reports dead code across the workspace.
- **What It Does**: One-click workspace audit listing:
  - Exported JS/TS/PHP functions/classes with zero cross-file references.
  - CSS `.class` / `#id` selectors in `.css`/`.scss` never referenced in HTML/PHP/JSX templates.
  - Dependencies declared in `package.json` / `composer.json` that are never imported or required.

### 8. `.env` vs `.env.example` Drift Detector & Code Env-Key Validator
- **Gap**: Phide masks `.env` values (`Mask: ON`) and indexes `.env` keys for autocomplete, but does not validate missing keys.
- **What It Does**: Cross-check `.env` against `.env.example` and scan `$_ENV[...]`, `getenv(...)`, `process.env.*`, and `os.getenv(...)` references to surface missing keys in the Problems panel—with a 1-click **"Sync Keys to .env.example"** action (exporting key names only, never secrets).

---

## Database, Server & API Workflows

### 9. Per-Project Isolated Runtime & Database Profiles (`.phide/profile.json`)
- **Gap**: MySQL falls back to a shared `C:\xampp\mysql\data` or global `runtimes\mysql\data`, causing one project's databases to appear in every project.
- **What It Does**: Allow each workspace to pin an isolated MySQL `--datadir`, port, PHP binary/`php.ini` overrides, Node/Python runtime path, and nginx document root so projects never leak state or port conflicts into each other.

### 10. Database ER Diagram & Schema Diff / Migration Generator
- **Gap**: `SqlEngine` inspects `SqlTableInfo::columns` and `SqlTableInfo::foreignKeys`, and `GraphView` renders node graphs, but there is no visual ERD or schema diff tool.
- **What It Does**:
  - Render an interactive **Entity-Relationship Diagram** of tables, PK/FK columns, and foreign-key connectors.
  - Compare two database schemas (e.g., local SQLite/MySQL vs staging or two snapshots) and generate deterministic `CREATE TABLE` / `ALTER TABLE` SQL migration scripts.

### 11. Schema-Aware SQL Autocomplete & Validation Inside PHP / JS / TS Strings
- **Gap**: SQL completion exists for `.sql` files, not inside host-language query strings.
- **What It Does**: Detect SQL query strings inside `$pdo->prepare("...")`, `mysqli_query(...)`, and JS/TS raw SQL calls, providing table/column autocomplete and highlighting unknown table/column names against the active `SqlEngine` schema.

### 12. Inbound Webhook Catcher & Mock HTTP Route Server
- **Gap**: `ApiTesterView` only sends *outbound* requests, and `LiveServer` only serves static/PHP files.
- **What It Does**: Spin up a local listener inside `ApiTesterView` / `LiveServer` (`http://localhost:PORT/webhook/*`) to capture incoming webhooks (Stripe, Brevo, GitHub, OAuth callbacks), inspect headers/JSON bodies live, replay them into API Tester, and configure mock JSON routes with custom status codes (`200`, `429`, `500`) and simulated latency.

### 13. API Endpoint Benchmark / Load Runner & Response Diff Compare
- **Gap**: `ApiTesterView` runs a single request at a time and shows one response body.
- **What It Does**:
  - **Load Benchmark**: Fire `N` requests at concurrency `C` to report min / p50 / p95 / p99 latency histograms, throughput (req/s), and status breakdown.
  - **Response Diff**: Compare two captured API responses (e.g., before vs. after code change, or Staging vs. Prod JSON) using Phide's side-by-side diff view.

### 14. Outbound API Client Code Generator (`fetch`, `axios`, PHP cURL/Guzzle, Python)
- **Gap**: `ApiTesterView` can *import* cURL commands (`TryImportCurlCommand`), but cannot generate client code from a configured request.
- **What It Does**: One-click export of any API Tester tab (including headers, query params, OAuth/Bearer auth, and body) to copy-ready `fetch()`, `axios`, PHP `curl_init()` / Guzzle, Python `requests`, or cURL CLI commands.

### 15. Live Nginx + PHP + Dev-Server Request & Error Correlation Panel
- **Gap**: `StructuredViewers` tails a single log file manually, and nginx/PHP errors stay hidden until opened.
- **What It Does**: An integrated **Stack Logs** panel automatically tailing `nginx/logs/access.log`, `error.log`, and PHP/Node stderr together—highlighting 4xx/5xx responses and turning PHP/JS/Python stack frames (`file:line:col`) into Ctrl+clickable jump links.

### 16. Local TCP Port Forwarder & Wire Traffic Logger
- **Gap**: `Monitor` lists listening ports and kills processes, and `ApiTesterView` is a WebSocket/HTTP client only.
- **What It Does**: Lightweight localhost TCP proxy (`listenPort → targetHost:targetPort`) that logs live UTF-8/Hex wire payloads, byte counts, and round-trip timings for debugging local DB queries, Redis, SMTP, or webhook forwarding without external tools.

---

## Testing, Cross-File Refactoring & Comparison

### 17. Per-Test Method Isolation (`RunTest` / `DebugTest` CodeLens) & Test Watch Mode
- **Gap**: As noted in `PLAN.md` #25, `RunTest`/`DebugTest` CodeLens currently executes the whole active file instead of isolating that single test method, and tests don't auto-rerun on save.
- **What It Does**: Pass test-name filters (`pytest -k`, `jest -t`, `vitest -t`, `phpunit --filter`) when clicking an individual test's CodeLens, and add a **Watch Mode** toggle in Test Explorer that automatically re-runs affected suites on save.

### 18. Cross-File Extract Function/Component & Move Symbol + Import Rewriter
- **Gap**: `CodeActionEngine` performs local file edits and `F2` renames symbols, but cannot move a function/class/component to another file and fix imports across the workspace.
- **What It Does**:
  - **Extract with Parameter Inference**: Extract selected lines into a new function or React component, automatically computing parameters and return values.
  - **Move Symbol to File**: Relocate a symbol to a new or existing file and update every `import` / `require` / `use` statement across the workspace.

### 19. Folder-to-Folder & Branch-to-Branch Directory Diff Tree
- **Gap**: Phide supports comparing two individual files (`OpenCompareFilesDiffDocument`), but not comparing two entire directories or two Git branches in a folder tree.
- **What It Does**: Open a hierarchical tree diff comparing `Folder A vs Folder B` (or `branch A vs branch B`), filtering by `Added`, `Deleted`, `Modified`, and `Identical` files with 1-click side-by-side file diff or selective copy/sync.

### 20. Offline Developer Data Converters & JWT / Timestamp Inspector
- **Gap**: Devs routinely leave the IDE for quick deterministic format conversions and token/timestamp inspection.
- **What It Does**: Command Palette + selection context actions + inline hover:
  - **Schema Converters**: `JSON ↔ TypeScript interface ↔ PHP array ['k' => 'v'] ↔ YAML ↔ .env`.
  - **Encoders & Hashes**: Base64, URL encode/decode, HTML entities, SHA-256 / MD5 / HMAC checksums.
  - **JWT & Epoch Hover**: Hover a JWT string or 10/13-digit Unix timestamp in code to inspect decoded JWT claims/expiry or human-readable UTC/local date.
