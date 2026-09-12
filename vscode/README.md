# RoseGoldC for VS Code / Cursor

Highlighting, snippets, **diagnostics**, outline, CodeLens, hover, go to definition, find references, highlight, rename, code actions, **Run File**, and **Run Tests** for `.rg` files. Talks to `build\RoseGoldC.exe`.

## Install

From the repo root:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File .\vscode\install.ps1
```

Then **Developer: Reload Window**.

If both this and the Rust `allen6297.rosegold-language` extension are installed, disable one of them so they do not both claim `.rg`.

## Use

Open a `.rg` file. Title-bar play / beaker, or Command Palette:

- **RoseGoldC: Run File** → `RoseGoldC.exe run <file>`
- **RoseGoldC: Run Tests** → `RoseGoldC.exe test <file>`
- **RoseGoldC: Recheck File** → refresh parse/type squiggles
- **Go to Definition** (F12), **Find All References** (Shift+F12), **hover**, **signature help**, and **IntelliSense** go through `RoseGoldC.exe lsp` (same process as diagnostics). Completions still trigger on `.`, `@`, and `:`. After `std.`, `math.`, `str.`, `io.`, `vec.`, `time.`, `path.`, `json.`, and `ui.` you get crate members. Hover shows `throws` and `@deprecated`. Snippets: `importstd`, `fromstd`, `dataimpl`, `importui`.
- **Outline** (Ctrl+Shift+O) and breadcrumbs list `fn` / `struct` / `data` / `class` / `trait` / `enum` / `signal` / `mod` in the file. Methods nest under their type.
- **CodeLens** **Run** on `fn main` and **Run Test** on `@test` functions (same commands as the title-bar icons).
- **Highlight** occurrences of the identifier under the cursor (writes vs reads). After `io.read_text`, only that qualified name lights up.
- **Rename** (F2) updates the same set as **Find All References**, including other `.rg` files in the workspace. Stdlib under `builtin/` is left alone unless you rename from there.
- **Code actions** (lightbulb / Ctrl+.) on matching type errors: wrap a throwing call with `try`, fill missing `match` arms (cursor in the `match`), stub missing `impl Trait` methods (cursor on the type that needs them), and **Import crate std** (or `math` / `vec` / …) when a stdlib name is used without importing it.
- **Diagnostics** as you type. Parse, load, `@constexpr`, and type errors in the file are all underlined. Logs: **RoseGoldC LSP** output channel.
- **Problem matcher** on Run File / Run Tests: `file.rg:line:col: error: …` and `type error in file.rg at line:col: …` in the terminal are clickable.

Comments: `//` line, `///` docs, `/# ... #/` block (jump between delimiters with **Go to Bracket**). `#` line comments still work. `#` is also fine in older examples.

Build the interpreter first (**Ctrl+Shift+B**). Override the exe with **RoseGoldC › Cli Path** if it is not at `build/RoseGoldC.exe`. **RoseGoldC › Diagnostics Delay Ms** is how long to wait after you stop typing (default 400).
