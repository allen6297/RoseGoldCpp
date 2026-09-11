# RoseGoldC for VS Code / Cursor

Highlighting, snippets, **Run File**, and **Run Tests** for `.rg` files. Talks to `build\RoseGoldC.exe`.

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
- **Go to Definition** (F12) on a `fn` / `signal` / `struct` name
- Hover shows the declaration and any `///` / `//` / `#` comments above it
- **IntelliSense**: keywords, types, `print` / `checks.*`, file symbols; `connect`/`emit` after a signal name + `.`; `@test` / `@deprecated` after `@`

Comments: `//` line, `///` docs, `/# ... #/` block (jump between delimiters with **Go to Bracket**). `#` line comments still work. `#` is also fine in older examples.

Build the interpreter first (**Ctrl+Shift+B**). Override the exe with **RoseGoldC › Cli Path** if it is not at `build/RoseGoldC.exe`.
