# RoseGoldC language review

Review of the language implementation (lexer → DAP), focused on bugs, type holes, resource hazards, and missing tests. Not an audit of the VS Code extension UX.

**Date:** 2026-09-13

## Verdict

Solid for a growing interpreter: diagnostics recover well, async has real event-loop discipline, and the pass/fail suite is large. Clearest correctness holes: **type-erasing container compatibility**, **cyclic values crashing `toString`/`==`**, and **signed integer UB**. Security matters mainly if you run untrusted `.rg` or expose LSP/DAP beyond a local editor.

---

## High

### 1. Bare `Array` / `Map` / `Future` erase element types

**Where:** `compatible()` in `RoseGoldC/typecheck.cpp`

**Why:** Bare `Array` is compatible with any `Array[T]` (same for `Map`, `Future`). Empty or mixed literals often infer as bare `Array`, so values can flow into `Array[Int]` without a static error; `push` element checks are skipped when the elem type is unknown.

**Tests:** No fail coverage for bare-container smuggling. `tests/fail/array_elem.rg` only covers typed `push` mismatch.

### 2. Cyclic values → native stack overflow

**Where:** `Value::toString` / `Value::equals` in `RoseGoldC/value.cpp`; `Array.push` in `eval.cpp`

**Why:** Arrays/maps/structs are `shared_ptr`-backed. `xs.push(xs)` (easy on bare `Array`) creates a cycle; `print`, `==`, and DAP evaluate recurse with no visited-set. JSON stringify has a depth cap for trees, not cycle detection.

**Tests:** None (only import/class cycles).

### 3. Signed integer UB

**Where:** `applyBinop` / unary `-` in `RoseGoldC/eval.cpp`

**Why:** `+ - * / %` on `long long` and `-INT_MIN` / `INT_MIN / -1` / `INT_MIN % -1` are C++ UB. Div/mod by zero are checked; overflow and min/−1 are not.

**Tests:** `div_zero.rg`, `mod_zero.rg`, `float_div_zero.rg` only.

### 4. Unsandboxed `__io` (and image loads)

**Where:** `__io` in `eval.cpp`; UI image load in `host_ui.cpp`

**Why:** `read_text` / `write_text` / `remove` take arbitrary paths. Fine for a trusted scripting host; full FS access if untrusted code runs.

**Note:** Import paths are identifiers only, so classic `import ../..` does not parse. The real escape is `__io` / UI file reads, not module tokens.

**Tests:** `stdlib_io.rg`, `io_try.rg`, `io_uncaught.rg` — no hostile-path cases.

### 5. LSP/DAP unbounded `Content-Length`

**Where:** `readMessage` in `lsp.cpp`; `dap.cpp`

**Why:** Hostile framing can allocate a huge body and OOM the process. LSP caps header size; body length is not meaningfully bounded.

**Tests:** None for hostile framing.

---

## Medium

### 6. Unknown type names can slip through

**Where:** `checkTypeName` in `typecheck.cpp`

**Why:** Free-floating annotations like `Bogus` / `fn f(x: NotAType)` may typecheck while later checks no-op.

**Tests:** Failures cover undefined structs on construction, not unknown type *names*.

### 7. No call-stack / recursion limit

**Where:** `callUser` / `runUserBody` in `eval.cpp`

**Why:** Deep recursion or sync `signal.emit` re-entrancy can blow the native stack. Deferred emit has a wave cap; sync emit does not.

### 8. Range / `for i in N` resource bombs

**Where:** `iterItems` in `eval.cpp`

**Why:** `for i in hugeInt` materializes the whole range. Inclusive ranges can spin if `n++` wraps. Easy DoS from typechecked code.

**Tests:** `range_ok.rg` uses tiny ranges only.

### 9. Allocation / hang builtins

**Where:** `__str.repeat`, `__time.sleep` in `eval.cpp`

**Why:** Huge `n` / ms → memory exhaustion or hang with no caps.

### 10. JSON parse: no nesting limit

**Where:** `JsonParser::parseValue` in `eval.cpp`

**Why:** Stringify caps depth; parse is recursive with no guard → stack overflow on deep JSON.

### 11. Imports resolve relative to entry file

**Where:** `Interpreter::resolveModule` in `modules.cpp` (`(void)fromFile`; uses entry `Interpreter::file`)

**Why:** Nested packages that expect “import relative to this module” will surprise you.

### 12. Lexer edge cases

**Where:** `lexer.cpp` (`stoll` / `stod`; unexpected-char → recursive `next()`)

**Why:** Huge integer literals can throw; long junk streams can stack-overflow recovery.

### 13. Formatter can change meaning

**Where:** `format.cpp` `escapeString`; lexer `#` skip; `fmt -w` in `RoseGoldC.cpp`

**Why:**

- `#` line comments are skipped trivia → **dropped** by `fmt` (only `//` and `/#…#/` are kept).
- Control chars emit as `\xHH`, which the lexer does not decode → round-trip corruption.
- `fmt -w` truncates in place (no temp+rename).

### 14. DAP watches + `ThrowEscape`

**Where:** `debugEval` in `eval.cpp`; `ThrowEscape` in `interp.h`

**Why:** Catches `std::exception` only; language `throw` / `__io` failures can escape the DAP request path.

### 15. Async `FnDecl*` across microtasks

**Where:** `callUser` async path in `eval.cpp`

**Why:** Safe while the `Interpreter`/kept AST lives; brittle if futures outlive program storage. Event-loop iteration limit and await-deadlock checks mitigate hangs.

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
- Broad suite (~100 pass / ~130+ fail) on imports, visibility, generics, traits, async, IO throws.
- Shared ownership avoids common dangling-pointer bugs (cycles remain the sharp edge).
- Import cycles detected; AST kept via `keep()`.
- Host UI has size caps on windows/images; deferred signal emit has a nesting limit.

---

## Highest-value missing tests

| Gap | Suggested coverage |
|-----|-------------------|
| Bare `Array` → `Array[Int]` | fail: smuggle via `[]` / mixed literal |
| `xs.push(xs)` then `print`/`==` | fail or crash-guard |
| `INT_MIN / -1`, overflow | fail or define wrap/trap policy |
| Hostile `__io` paths | policy tests if sandboxing is a goal |
| Huge literal / junk bytes | lexer must not throw/recurse-crash |
| `fmt` + `#` / `\x` strings | round-trip identity |
| Deep JSON / huge `for i in N` | resource limits |
| Sync signal re-entrancy | stack or dynamic error |
| LSP/DAP max body length | framing DoS |

---

## Suggested fix order

1. Bare `Array` / `Map` / `Future` compatibility + push elem checks  
2. Cycle-safe `toString` / `equals` (visited set)  
3. Defined integer overflow / `INT_MIN` policy  
4. LSP/DAP body-size caps  
5. Resource limits (ranges, JSON depth, `str.repeat`)  
6. Fmt `#` / `\x` round-trip  
