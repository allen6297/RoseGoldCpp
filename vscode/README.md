# RoseGoldC for VS Code / Cursor

Highlighting, snippets, **diagnostics**, **Run File**, and **Run Tests** for `.rg` files. Talks to `build\RoseGoldC.exe`.

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
- **Go to Definition** (F12), **hover**, and **IntelliSense** go through `RoseGoldC.exe lsp` (same process as diagnostics). Completions still trigger on `.`, `@`, and `:`. After `std.`, `math.`, `str.`, `io.`, and `vec.` you get crate members. Snippets: `importstd`, `fromstd`, `dataimpl`.
- **Diagnostics** as you type. Parse, load, `@constexpr`, and type errors in the file are all underlined. Logs: **RoseGoldC LSP** output channel.

Comments: `//` line, `///` docs, `/# ... #/` block (jump between delimiters with **Go to Bracket**). `#` line comments still work. `#` is also fine in older examples.

Build the interpreter first (**Ctrl+Shift+B**). Override the exe with **RoseGoldC › Cli Path** if it is not at `build/RoseGoldC.exe`. **RoseGoldC › Diagnostics Delay Ms** is how long to wait after you stop typing (default 400).
