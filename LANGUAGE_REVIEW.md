# RoseGoldC language review

Review of the language implementation (lexer → DAP), focused on bugs, type holes, resource hazards, and missing tests. Not an audit of the VS Code extension UX.

**Date:** 2026-09-13  
**Update:** High-priority items 1–6 addressed on the language-correctness branch (see Status).

## Verdict

Solid for a growing interpreter: diagnostics recover well, async has real event-loop discipline, and the pass/fail suite is large. Review high/medium correctness holes from the initial audit are largely closed; remaining work is mostly optional policy (tighter sandbox root) and tooling polish.

---

## Status

| # | Item | Status |
|---|------|--------|
| 1 | Bare `Array` / `Map` / `Future` compatibility | **Fixed** — directional `compatible`; literals may still initialize typed containers; bare *values* cannot |
| 2 | Cycle-safe `toString` / `equals` | **Fixed** — path visited-set |
| 3 | Signed integer overflow / `INT_MIN` | **Fixed** — trap with `integer overflow` |
| 4 | LSP/DAP body-size caps | **Fixed** — 16 MiB |
| 5 | Resource limits (ranges, JSON depth, `str.repeat`) | **Fixed** |
| 6 | Fmt `#` / `\x` round-trip (+ atomic `-w`) | **Fixed** |
| — | Unknown type names (`Bogus`) | **Fixed** |
| — | `str.split`: `Array[String]` | **Fixed** |
| 7 | Call-stack / sync emit depth | **Fixed** — 1024 calls / 64 sync emits |
| 9 | `time.sleep` / `delay` | **Fixed** — 60s cap |
| 12 | Lexer huge literals / junk recovery | **Fixed** |
| 14 | DAP watches + `ThrowEscape` | **Fixed** |
| 4 | `__io` / UI image path sandbox | **Fixed** — cwd jail |
| 11 | Import resolution | **Fixed** — importer dir first, entry fallback |

---

## High

### 1. Bare `Array` / `Map` / `Future` erase element types — fixed

**Was:** Bare `Array` compatible with any `Array[T]`.  
**Now:** Expecting bare `Array` still accepts `Array[T]`. Expecting `Array[T]` rejects a bare Array *value*; array/map *literals* are checked element-wise via `assignable` + `checkArrayElems`.

**Tests:** `tests/fail/array_bare_smuggle.rg`, `tests/fail/array_bare_arg.rg`

### 2. Cyclic values → native stack overflow — fixed

**Was:** `xs.push(xs)` then `print` / `==` recurse forever.  
**Now:** `toString` / `equals` use a path visited-set (`[...]` / identity on re-entry).

**Tests:** `tests/pass/cycle_print.rg`

### 3. Signed integer UB — fixed

**Was:** C++ UB on overflow and `INT_MIN / -1`.  
**Now:** Checked ops / traps with `integer overflow`.

**Tests:** `tests/fail/int_overflow_add.rg`, `tests/fail/int_overflow_div.rg`

### 4. Unsandboxed `__io` (and image loads) — fixed

**Now:** `__io` and UI image paths are resolved under the process cwd; `..` / absolute escapes raise `path outside sandbox`.

**Tests:** `tests/fail/io_sandbox.rg`, `tests/fail/io_sandbox_dotdot.rg`

### 5. LSP/DAP unbounded `Content-Length` — fixed

**Now:** Reject bodies larger than 16 MiB (DAP also rejects huge headers).

---

## Medium

### 6. Unknown type names — fixed

**Now:** `checkTypeName` errors on unknown heads (`tests/fail/unknown_type_name.rg`).

### 7. Call-stack / sync emit depth — fixed

**Now:** `runUserBody` caps call depth at 1024 (`call stack overflow`). Sync `emit` caps nesting at 64 (`signal emit nested too deeply`), matching deferred waves.

**Tests:** `tests/fail/call_stack_overflow.rg`, `tests/fail/emit_nested.rg`

### 8. Range / `for i in N` resource bombs — fixed

**Now:** Cap at 1_000_000 items; inclusive ranges stop at `LLONG_MAX`.  
**Tests:** `tests/fail/range_too_large.rg`

### 9. Allocation / hang builtins — fixed

**Now:** `str.repeat` caps output at 16 MiB; `time.sleep` / `time.delay` cap at 60_000 ms.

**Tests:** `tests/fail/str_repeat_large.rg`, `tests/fail/sleep_too_large.rg`

### 10. JSON parse nesting — fixed

**Now:** Parse depth capped at 64 (same as stringify).

### 11. Imports resolve relative to entry file — fixed

**Now:** `resolveModule` searches the importing file’s directory first, then the entry script directory (so nested packages can `import helper` locally while top-level packages still resolve from nested files).

**Tests:** `tests/pass/import_rel_mod.rg` (`relpkg` → local `helper`)

### 12. Lexer edge cases — fixed

**Now:** Huge integer/float literals record `… literal too large` (digit length capped); unexpected characters are consumed before recovery so junk streams cannot recurse forever.

**Tests:** `tests/fail/int_literal_large.rg`, `tests/fail/unexpected_char.rg`

### 13. Formatter can change meaning — fixed

**Now:** `#` line comments are emitted as comment tokens (kept by fmt); `\xHH` is decoded by the lexer; `fmt -w` writes via temp+rename.

### 14. DAP watches + `ThrowEscape` — fixed

**Now:** `debugEval` catches language `throw` / `ThrowEscape` and returns an error string instead of escaping the DAP request path.

### 15. Async `FnDecl*` across microtasks — fixed

**Now:** Async `callUser` copies the `FnDecl` into a `shared_ptr` held by the microtask so the callee cannot dangle if originating Program storage is released while the future is pending.

---

## Low

- Bidirectional `Int`↔`Float` compatibility weakens annotations (`var x: Int = 1.5` can typecheck; runtime may keep a Float).
- Bare `Fn` is a wildcard for any function type.
- Constexpr is purity checking, not compile-time eval.
- LSP re-checks whole docs on change — CPU DoS secondary to framing.
- Host UI image paths: same trust model as `__io` (size-capped ~32MB).

---

## Strengths

- Parse recovery + accumulated diagnostics for `check` / LSP.
- Async: microtasks, timers, unused-Future check, await deadlock, event-loop iteration cap, `Future.all` / `race` / `cancel`.
- Broad suite on imports, visibility, generics, traits, async, IO throws.
- Shared ownership avoids common dangling-pointer bugs (cycles remain possible but print/`==` are safe).
- Import cycles detected; AST kept via `keep()`.
- Host UI has size caps on windows/images; deferred signal emit has a nesting limit.

---

## Suggested next fix order

1. Optional tighter sandbox root (entry-file / repo-root instead of cwd) if needed  
2. DAP hit-count breakpoints / set variable  
3. Further TextField polish  
