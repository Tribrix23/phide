# Phide — 20 Practical Developer Features Roadmap

A curated list of 20 deterministic, zero-bloat, non-LLM features designed specifically for **phide**'s native C++20 Direct2D/DirectWrite architecture and full-stack Web / PHP / JS / TS / Python / SQL workflows.

---

## Core Editing & Refactoring

### 1. Multi-File Search & Replace with Live Diff Preview
- **Problem**: Global find-and-replace without seeing context risks breaking unrelated files.
- **How It Works**: Expand Code Search with a live side-by-side/inline replacement diff preview, per-file and per-match checkboxes, regex capture groups (`$1`, `$2`), and **Preserve Case** matching (`user` → `item`, `User` → `Item`, `USER` → `ITEM`).
- **Safety**: Wrap the entire multi-file replace in a single workspace undo transaction + automatic `LocalTimeline` snapshot.

### 2. AST/Bracket-Aware Expand & Shrink Selection
- **Problem**: Manually selecting a string, function arguments, JSX tag, or whole block slows down editing.
- **How It Works**: `Shift+Alt+Right` / `Shift+Alt+Left` incrementally expands or shrinks selection across logical boundaries: `identifier` → `string contents` → `string + quotes` → `argument` → `argument list` → `statement` → `block { ... }` → `function / tag`.

### 3. Multi-Cursor Alignment, Case Transforms & Sequence Generator
- **Problem**: Cleaning up config maps, arrays, or enums requires tedious manual spacing and typing.
- **How It Works**: Built-in commands for active multi-cursors:
  - **Align by Delimiter** (`=`, `:`, `=>`, `|`) across selected lines or cursors.
  - **Generate Sequence** (insert `1, 2, 3...`, `a, b, c...`, or UUIDs across cursors).
  - **Instant Case Cycle** (`camelCase`, `PascalCase`, `snake_case`, `kebab-case`, `CONSTANT_CASE`).

### 4. Sticky Scroll (Pinned Scope Headers)
- **Problem**: In large PHP classes, JS/TS modules, or deeply nested HTML templates, it's easy to lose track of which function/tag/class you're inside.
- **How It Works**: Pin the enclosing `namespace` / `class` / `function` / `<section>` headers (up to 4 lines) cleanly at the top of `EditorCanvas` while scrolling; clicking any pinned line jumps to its opening line.

### 5. Deterministic Refactorings (Extract Function / Move Symbol / Safe Delete)
- **Problem**: Moving a function/component to another file or extracting helper logic requires manually fixing parameters and imports everywhere.
- **How It Works**: Using `WorkspaceNavigation` + `LspClient`:
  - **Extract to Function / Component**: Automatically detects referenced outer variables as parameters and return values.
  - **Move Symbol to File**: Creates or appends to target file and rewrites all workspace imports/uses.
  - **Safe Delete**: Checks all callers/references before deleting a file or symbol and warns if usages remain.

---

## Full-Stack Web, PHP & Database Tooling

### 6. Live Database Schema Explorer & Table Data Grid
- **Problem**: Switching to a browser/phpMyAdmin just to check a column name or inspect 20 rows breaks flow.
- **How It Works**: Add a **Database Schema** tree in the sidebar backed by `SqlEngine`: browse databases, tables, columns, types, indexes, and foreign keys; double-click a table to open a fast Direct2D virtualized data grid with sorting, filtering, inline editing, and CSV/JSON export.

### 7. Schema-Aware SQL Autocomplete Inside PHP & JS/TS Strings
- **Problem**: Typos in SQL table/column names inside PDO, MySQLi, or Prisma/raw queries only fail at runtime.
- **How It Works**: Cache connected database table/column metadata and offer autocomplete + validation inside `SELECT ... FROM`, `WHERE`, `JOIN`, and `INSERT` string literals in `.sql`, `.php`, `.js`, and `.ts` files.

### 8. Per-Project Isolated Runtime & Database Profiles
- **Problem**: Global MySQL/XAMPP data directories and global PHP/Node versions bleed databases and configs across unrelated projects.
- **How It Works**: Store `.phide/workspace-profile.json` per workspace to pin:
  - Isolated MySQL/MariaDB `--datadir` (so Project A's tables don't pollute Project B's phpMyAdmin)
  - Project-specific PHP binary, Node version, Python `.venv`, and nginx document root/port.

### 9. Live Server & PHP/Nginx Log Tail with Clickable Stack Traces
- **Problem**: Hunting through raw `error.log` files to find which line threw a PHP Fatal Error or Node exception wastes time.
- **How It Works**: A structured **Server Logs** panel tailing `nginx/logs/error.log`, PHP error logs, and dev server stderr in real time—parsing `file:line(:col)` frames into clickable links that open the exact file and highlight the offending line.

### 10. Local Webhook Catcher & Request Replay (Inside API Tester)
- **Problem**: Testing webhooks (Stripe, Brevo, GitHub, OAuth callbacks) during local dev usually requires third-party web tools.
- **How It Works**: Add a local HTTP listener tab inside `ApiTesterView` (`Listen on :port`) that captures incoming requests (method, headers, query params, raw/JSON body) and lets you inspect or replay them with one click.

### 11. `.env` & `.env.example` Sync + Undefined Env Key Diagnostics
- **Problem**: Missing environment variables cause silent crashes after deployment or when teammates clone a repo.
- **How It Works**: Cross-check `.env` against `.env.example` and scan `$_ENV[...]`, `getenv(...)`, `process.env.*`, and `os.getenv(...)` calls to flag missing keys as editor warnings, with a one-click **"Sync keys to .env.example"** action (values stripped).

### 12. CSS / Tailwind Specificity & Unused Selector Inspector
- **Problem**: Overridden CSS rules and dead stylesheets bloat web projects.
- **How It Works**: On hover over any CSS selector or Tailwind class, display its computed specificity `(a, b, c)` and resolved CSS properties; add a workspace command to list CSS classes defined in stylesheets that never appear in HTML/PHP/JSX templates.

---

## Git, History & Code Review

### 13. Hunk & Line-Level Git Staging Inside the Diff Editor
- **Problem**: Staging an entire file when only 3 lines belong to the current commit mixes unrelated changes.
- **How It Works**: Inside Phide's side-by-side diff view, add gutter controls on each diff hunk (and selected lines) for **Stage Hunk**, **Unstage Hunk**, and **Revert Hunk** via `git apply --cached`.

### 14. Visual 3-Way Merge Conflict Editor
- **Problem**: Resolving complex merge conflicts inline with `<<<<<<< / ======= / >>>>>>>` markers is error-prone.
- **How It Works**: A dedicated 3-pane layout (`Incoming` | `Merged Result` | `Current`) with one-click **Accept Current**, **Accept Incoming**, **Accept Both**, conflict-jump buttons (`Prev / Next Conflict`), and auto-stage when 0 conflicts remain.

### 15. Git Worktree Manager & Visual Interactive Rebase
- **Problem**: Switching branches mid-work to review a PR forces stashing or rebuilding dependencies; CLI `git rebase -i` is awkward.
- **How It Works**:
  - **Worktrees**: Create, open in a new Phide window, or prune Git worktrees right from Source Control.
  - **Interactive Rebase UI**: Drag-reorder commits, toggle `pick / squash / fixup / reword / drop`, and execute `git rebase` cleanly.

### 16. Timeline Diff & Pre-Refactor Restore Points
- **Problem**: Undo history is lost when files are closed or when multi-file refactors go wrong.
- **How It Works**: Enhance `LocalTimeline` with a clickable per-file snapshot list (auto-captured on save, format, and multi-file refactor), instant diff vs. current buffer, and manual **"Create Checkpoint"** labels.

---

## Testing, Diagnostics & Asset Inspection

### 17. Test Coverage Gutter & Auto-Watch Mode
- **Problem**: Running tests manually and opening HTML coverage reports breaks focus.
- **How It Works**: Parse local `lcov.info`, PHPUnit Clover XML, and `coverage.xml` files to render crisp green (covered) / red (uncovered) gutter bars directly in `EditorCanvas`, paired with a **Watch** toggle in `Test Explorer` that re-runs affected tests on save.

### 18. Interactive Regex & Cron Expression Tester
- **Problem**: Developers constantly leave their IDE to test a regex or decode a cron schedule on external websites.
- **How It Works**: A built-in offline **Regex & Cron Lab** tool (plus inline hover testing on regex literals) showing live match highlights, numbered/named capture groups, replacement output, ReDoS/backtracking warnings, and next 5 scheduled Cron run timestamps.

### 19. Fast Virtualized Data Grid for CSV / TSV / JSON Arrays
- **Problem**: Large `.csv` exports or JSON API payloads are unreadable as raw text in an editor.
- **How It Works**: Offer a **"View as Table"** tab toggle for `.csv`, `.tsv`, and `.json` files using a Direct2D virtualized grid supporting column sorting, instant text/numeric filtering, column stats (min/max/sum/count), and copy-as-SQL/Markdown.

### 20. Offline Security & Bug-Pattern Static Checks + Asset Inspector
- **Problem**: Common security mistakes and unoptimized assets slip into commits unnoticed.
- **How It Works**:
  - **Offline Static Rules** (in `SyntaxValidator`): Flag raw SQL string concatenation (`mysqli_query` / PDO interpolation), unescaped XSS sinks (`innerHTML`, `echo $_GET[...]`), hardcoded private keys/tokens before `git commit`, and empty `catch` blocks.
  - **Asset & Font Inspector**: Clicking `.png`/`.jpg`/`.webp`/`.svg`/`.ttf`/`.woff2` files (or hovering asset paths in code) displays dimensions, file size, live SVG split source/render preview, and one-click Base64 / `<img>` / `@font-face` snippet copy.
