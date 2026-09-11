# RoseGoldC

A C++ interpreter for a small RoseGold subset. Built with clang-cl from VS Code / Cursor.

## Build

Needs the **C++** and **C++ Clang Compiler for Windows** workloads from Visual Studio Build Tools (or Community). You do not open the Visual Studio IDE.

In VS Code: **Ctrl+Shift+B**, or F5 (builds then debugs). From a terminal at the repo root:

```text
.\build.cmd
```

That writes `build\RoseGoldC.exe`. Run it from the repo root so `examples/` paths work.

## Editor extension

Highlighting, snippets, Run File, and Run Tests for `.rg` files:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File .\vscode\install.ps1
```

Then **Developer: Reload Window**. Details: [vscode/README.md](vscode/README.md).

## CLI

```text
.\build\RoseGoldC.exe                  # REPL
.\build\RoseGoldC.exe run <file>       # call main if present
.\build\RoseGoldC.exe test             # language suite (@test + tests/pass + tests/fail)
.\build\RoseGoldC.exe test <file>      # @test functions in one file
.\build\RoseGoldC.exe help
.\build\RoseGoldC.exe version
```

Aliases: first letter, `-letter`, `--letter`. `quit` / `exit` leave the REPL.

```text
.\build\RoseGoldC.exe run examples/hello.rg
.\build\RoseGoldC.exe run examples/const.rg
.\build\RoseGoldC.exe run examples/control.rg
.\build\RoseGoldC.exe run examples/point.rg
.\build\RoseGoldC.exe run examples/signals.rg
.\build\RoseGoldC.exe run examples/argv.rg hello
.\build\RoseGoldC.exe run examples/import.rg
.\build\RoseGoldC.exe run examples/nested.rg
.\build\RoseGoldC.exe run examples/stdlib.rg
.\build\RoseGoldC.exe run examples/class.rg
.\build\RoseGoldC.exe test
.\build\RoseGoldC.exe test examples/tests.rg
```

`run` only calls `main`. Extra args after the file are `argv` (`argv(0)` is the file path). `test <file>` only calls `@test` functions. `test` with no file runs `examples/tests.rg` plus `tests/pass` (must succeed; files without `fn main` are libraries and are skipped) and `tests/fail` (must error; first `# expect: …` comment is a substring of the message; files without `# expect:` are libraries and are skipped).

## Language (now)

`fn`, `@test`, `@deprecated`, `import` / `from` / `as`, `pub`, `mod`, `struct`, `class`, `extends`, `trait`, `impl` / `impl Trait for Type`, `super`, `signal`, `var`, `const`, `return`, `pass`, `break`, `continue`, `if` / `elif` / `else`, `while`, `print`, `assert`, `argv` / `argv_len` (`process.argv` / `process.argc`), `checks.eq` / `neq` / `eq_string` / `that`, `math`, `str`.

Ints, strings, bools, `+ - * / % == != < >`, unary `-` / `!`, user functions.

Comments: `//` and `#` to end of line, `///` for docs, `/# ... #/` for blocks.

**`const` is init-once.** The initializer is any expression: literals, operators, variables, calls. It runs once, then the name cannot be assigned.

```text
var x = 2;
const n = add(x, 40);   # n is 42
n = 1;                  # error: cannot assign to const 'n'
```

`if` / `elif` / `else` and `while` take a condition and a `{ ... }` block. No extra parens. `elif` is the keyword; `else if` is not parsed. Conditions use the same truthy rules as `assert` (non-zero, non-empty, `true`). `pass;` does nothing. `break;` / `continue;` only work inside `while`.

```text
if n > 0 {
    return 1;
} elif n < 0 {
    return -1;
} else {
    return 0;
}

var i = 0;
while true {
    if i == 2 {
        i = i + 1;
        continue;
    }
    if i > 3 {
        break;
    }
    i = i + 1;
}
```

`@deprecated` warns when that function is called, then still runs it.

**Signals** are Godot-style: declare, `connect` free functions, `emit` calls them now in connect order. A second `connect` of the same function is ignored. `disconnect` removes that function (no error if it was not connected). Arity must match on connect.

```text
signal collected(amount: Int);

fn log_coin(amount: Int) {
    print(amount);
}

fn main(): Int {
    collected.connect(log_coin);
    collected.emit(5);
    collected.disconnect(log_coin);
    collected.emit(5);   # silent
    return 0;
}
```

No deferred emit or signals on structs yet.

No arrays yet, so `run file.rg a b` is indexed: `argv(0)` / `process.argv(0)` is the file path, `argv(1)` is `a`, `argv_len()` / `process.argc()` is the count.

```text
fn main(): Int {
    print(argv(0));
    if argv_len() > 1 {
        print(argv(1));
    }
    return 0;
}
```

**Structs** are live: declare fields, construct with `Name { field: value, ... }`, read `p.x`, write `p.x = 3`. All declared fields must be set. Copies share the same object (mutating `q.x` after `var q = p` also changes `p.x`).

**Methods** take `self` (or omit it — it is still bound). Call with `p.mag2()`. Put them in the struct body or in `impl Point { ... }`. Method names cannot overlap fields.

Inside a method, a bare name is a local or parameter first, then a field on this instance, then a global. You do not have to write `self.x`; `self.x` still works. A `var` with the same name shadows the field. `take_damage(10)` inside a method calls that method on this instance if no free function exists.

```text
struct Point {
    x: Int;
    y: Int;

    fn mag2(): Int {
        return x * x + y * y;
    }
}

impl Point {
    fn bump() {
        x = x + 1;
    }
}

fn main(): Int {
    var p = Point { x: 3, y: 4 };
    print(p.mag2());
    p.bump();
    return 0;
}
```

**Classes** use `var` fields (optional `= expr` defaults). Omitted fields at construction are filled from those defaults (parent first, then child). Structs still require every field.

`class Slime extends Enemy` copies parent fields and methods. A child method can override a parent method and call it with `super.method()`. `super` is only valid in a method of a class that extends another.

**Traits** declare method signatures (`fn name(...): Type;`) and optional `signal`s. Implement with `class Point impl Named`, a nested `impl Named { ... }` in the class body, or `impl Named for Point { ... }`. Existing `impl Point { ... }` is still an inherent impl (no `for`). A type that claims a trait must provide each method.

```text
class Enemy {
    var hp: Int = 10;
    fn hurt(dmg: Int): Int {
        hp = hp - dmg;
        return hp;
    }
}

class Slime extends Enemy {
    var goo: Int = 1;
    fn hurt(dmg: Int): Int {
        return super.hurt(dmg / 2);
    }
}

trait Named {
    fn label(): String;
}

class Point impl Named {
    var x: Int = 0;
    fn label(): String {
        return "point";
    }
}

impl Named for Enemy {
    fn label(): String {
        return "enemy";
    }
}
```

**Modules** are directories or `mod` blocks, not `name.rg` files. `import calc;` loads every `.rg` file in a sibling `calc/` folder (file-level items are all exported). It also loads any sibling file that declares `mod calc { ... }`. Several files can contribute to the same name and get merged. Call exports as `calc.add()`. `import calc as c;` binds `c`. `from calc import add;` and `from calc import add as sum;` bind a name in the importer.

Inside a `mod Name { }`, only `pub` items are exported; private functions can still call each other. Nested `mod` works: `outer.inner.add()`, and parent code can call `inner.add()`. Nested mods need `pub` to be visible outside the parent. `import bag.math;` loads `bag/math/` or a nested `pub mod math`. `from bag import math;` binds that nested module. `import` / `from` inside a `mod` bind names in that module only. `import checks;` / `import process;` are no-ops (`checks.eq` already works without import).

```text
# calc/lib.rg
fn add(a: Int, b: Int): Int {
    return a + b;
}

fn double(n: Int): Int {
    return add(n, n);
}

# main.rg
import calc;
from calc import double as twice;

fn main(): Int {
    print(calc.add(2, 3));
    print(twice(4));
    return 0;
}
```

**Stdlib** lives in `stdlib/` and is resolved from the repo (walk up from the script). `import math;` and `import str;` are required. `math` is Int-only (`abs`, `sign`, `min`, `max`, `clamp`, `gcd`, `pow`, `rand_int`). `str` wraps host primitives (`contains`, `starts_with`, `ends_with`, `length`, `is_empty`, `repeat`, `upper`, `lower`, `trim`, `slice`). `checks` stays a host builtin and does not need import.

A **framework** here is an optional stack behind `import`, not new syntax. `ui` / engine hosts are not built yet.

```text
import math;
import str;

fn main(): Int {
    print(math.abs(-7));
    print(str.upper("hi"));
    return 0;
}
```

## Next steps

Grow the language before more editor polish.

**Not yet:** constexpr, arrays, Float, `ui` framework, typecheck, LSP diagnostics. Those live in the Rust tree.

## Later: constexpr (not built)

A stricter kind of const, closer to C++ `constexpr`. A function would be marked pure / `@constexpr`. Its body could only use literals, operators, `const`, and calls to other constexpr functions — no `var`, no `print`, no other side effects.

Then `const n = add(2, 40);` would fold at check time only if `add` is constexpr. That is separate from today's `const`, which only freezes the binding.
