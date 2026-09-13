# RoseGoldC for VS Code / Cursor

Highlighting, snippets, **diagnostics**, **Format Document** (LSP + `fmt` fallback), **inlay hints**, **folding**, **semantic tokens**, **DAP debug**, outline, CodeLens, **color chips**, hover, go to definition, find references, highlight, rename, code actions, **Run File**, **Run Tests**, and **Test Suite** for `.rg` files. Talks to `build\RoseGoldC.exe`.

**DAP debugger** (F5): breakpoints (including conditions), watches / evaluate, continue / step over / step in / step out, call stack, and locals across imported files. Adapter: `RoseGoldC.exe dap`.
## Install

From the repo root:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File .\vscode\install.ps1
```

Then **Developer: Reload Window**. After upgrading, run **RoseGoldC: Restart Language Server** so the editor picks up the rebuilt `RoseGoldC.exe` (the LSP runs from a temp copy and can keep an older binary until restart).

If both this and the Rust `allen6297.rosegold-language` extension are installed, disable one of them so they do not both claim `.rg`.

## Use

Open a `.rg` file. Title-bar play / beaker, or Command Palette:

- **RoseGoldC: Run File** → `RoseGoldC.exe run <file>`
- **RoseGoldC: Run Tests** (Ctrl+Shift+T) → `RoseGoldC.exe test <file>`
- **RoseGoldC: Test Suite** (Ctrl+Alt+T) → `RoseGoldC.exe test` (whole language suite, workspace cwd)
- **RoseGoldC: Recheck File** → refresh parse/type squiggles
- **RoseGoldC: Restart Language Server** → pick up a rebuilt `RoseGoldC.exe`
- **Format Document** (Shift+Alt+F) → prefers LSP `textDocument/formatting`; falls back to spawning `RoseGoldC.exe fmt` on a temp file. Format On Save is on by default for `[rosegold]`. Settings: **RoseGoldC › Format: Compact** / **Strip Comments** apply to the spawn fallback only (LSP uses defaults). **RoseGoldC › Format: Check On Save** (default on) runs `fmt --check` after save/format and shows `$(warning) RoseGoldC fmt` in the status bar when the file still needs formatting.
- **Inlay hints** (enabled by default for `[rosegold]`) show inferred types on `var`/`const` and parameter names at call sites via `textDocument/inlayHint`.
- **Folding** ranges come from the LSP (`textDocument/foldingRange`) for top-level declarations.
- **Semantic tokens** (`textDocument/semanticTokens/full`) color keywords, comments, strings, numbers, functions, types, variables, operators, namespaces, methods, and properties (modifiers: declaration, deprecated).
- **DAP debug** — click the gutter for breakpoints on `.rg`, then **RoseGoldC: Debug File** (title bar / palette) or F5 → **RoseGoldC: Debug .rg file**. Stack, locals, step over/in/out via `RoseGoldC.exe dap`.
- **Go to Definition** (F12), **Find All References** (Shift+F12), **hover**, **signature help**, and **IntelliSense** go through `RoseGoldC.exe lsp` (same process as diagnostics). Completions still trigger on `.`, `@`, and `:`. After `std.`, `math.`, `str.`, `io.`, `vec.`, `time.`, `path.`, `json.`, and `ui.` you get crate members. Hover shows `throws` and `@deprecated`. Snippets include `async` / `await` / `futureall` / `futurerace` / `uiframe` / `uibutton`, plus `importstd`, `fromstd`, `dataimpl`, `importui`.
- **Outline** (Ctrl+Shift+O) and breadcrumbs list `fn` / `struct` / `data` / `class` / `trait` / `enum` / `signal` / `mod` in the file. Methods nest under their type.
- **CodeLens** **Run** on `fn main` and **Run Test** on `@test` functions (same commands as the title-bar icons).
- **Highlight** occurrences of the identifier under the cursor (writes vs reads). After `io.read_text`, only that qualified name lights up.
- **Rename** (F2) updates the same set as **Find All References**, including other `.rg` files in the workspace. Stdlib under `builtin/` is left alone unless you rename from there.
- **Code actions** (lightbulb / Ctrl+.) on matching type errors: wrap a throwing call with `try`, **Await Future** on unused Futures, fill missing `match` arms (cursor in the `match`), stub missing `impl Trait` methods (cursor on the type that needs them), and **Import crate std** (or `math` / `vec` / …) when a stdlib name is used without importing it.
- **Color chips** on `Color.Rgb(...)`, `Color.Argb(...)`, named `Color.Red` / `White` / …, and `rgb` / `argb` / `ui.rgb` — click the chip to pick a color.
- **Diagnostics** as you type. Parse, load, `@constexpr`, and type errors in the file are all underlined. Logs: **RoseGoldC LSP** output channel.
- **Tasks** (Terminal → Run Task): **build** (`build.ps1`), **RoseGoldC: Test Suite**, **RoseGoldC: Run Current File** — problem matchers `$rosegoldc` / `$rosegoldc-located`.
- **Problem matcher** on Run File / Run Tests: `file.rg:line:col: error: …` and `type error in file.rg at line:col: …` in the terminal are clickable.

Comments: `//` line, `///` docs, `/# ... #/` block (jump between delimiters with **Go to Bracket**). `#` line comments still work.

Build the interpreter first (**Ctrl+Shift+B**). Override the exe with **RoseGoldC › Cli Path** if it is not at `build/RoseGoldC.exe`. **RoseGoldC › Diagnostics Delay Ms** is how long to wait after you stop typing (default 400).
