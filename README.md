# RoseGoldC

A C++ interpreter for a small RoseGold subset. On Windows it builds with clang-cl from VS Code / Cursor. Linux and macOS use `./build.sh`.

## Build

Needs the **C++** and **C++ Clang Compiler for Windows** workloads from Visual Studio Build Tools (or Community). You do not open the Visual Studio IDE.

In VS Code: **Ctrl+Shift+B**, or F5 (builds then debugs). From a terminal at the repo root:

```text
.\build.cmd
```

That writes `build\RoseGoldC.exe`. Run it from the repo root so `examples/` paths work.

On Linux (X11 or Wayland) or macOS (Cocoa):

```text
chmod +x ./build.sh
./build.sh
./build/RoseGoldC test
```

Linux needs `libx11` (`libx11-dev` on Debian/Ubuntu). If `libwayland-client` is also present (`libwayland-dev`), the same binary talks X11 and Wayland and picks at runtime (`WAYLAND_DISPLAY` / `XDG_SESSION_TYPE=wayland` prefer Wayland). macOS needs the macOS SDK (Xcode or Command Line Tools). Hidden windows fall back to an offscreen host if there is no display, so `test` can still run over SSH.

## Editor extension

Highlighting, snippets, **diagnostics**, hover, go to definition, find references, signature help, completion, outline, CodeLens, highlight, rename (workspace refs), code actions, Run File, and Run Tests for `.rg` files:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File .\vscode\install.ps1
```

Then **Developer: Reload Window**. Details: [vscode/README.md](vscode/README.md).

## CLI

```text
.\build\RoseGoldC.exe                  # REPL
.\build\RoseGoldC.exe run <file>       # call main if present
.\build\RoseGoldC.exe check <file>     # parse and typecheck (no eval)
.\build\RoseGoldC.exe fmt <file>       # print formatted source (rgfmt)
.\build\RoseGoldC.exe fmt -w <file>    # format file in place
.\build\RoseGoldC.exe fmt --check <file>  # exit 1 if formatting would change
.\build\RoseGoldC.exe lsp              # language server (JSON-RPC on stdin/stdout)
.\build\RoseGoldC.exe test             # language suite (@test + tests/pass + tests/fail)
.\build\RoseGoldC.exe test <file>      # @test functions in one file
.\build\RoseGoldC.exe help
.\build\RoseGoldC.exe version
```

Aliases: first letter, `-letter`, `--letter`. `quit` / `exit` leave the REPL.

```text
.\build\RoseGoldC.exe run examples/hello.rg
.\build\RoseGoldC.exe run examples/window.rg
.\build\RoseGoldC.exe run examples/widgets.rg
.\build\RoseGoldC.exe run examples/contacts.rg
.\build\RoseGoldC.exe run examples/argv.rg hello
.\build\RoseGoldC.exe check examples/hello.rg
.\build\RoseGoldC.exe test
```

Language samples live under `examples/` (`class.rg`, `generics.rg`, …). UI demos: `window.rg` (hello), `canvas.rg`, `widgets.rg` (controls kitchen sink), `contacts.rg` (small app). Images are in `examples/assets/`.

`run` only calls `main`. Extra args after the file are `argv` (`argv(0)` is the file path). `check` lexes, parses, and typechecks without calling `main`. Parse, load, `@constexpr`, and type errors are all collected: `check` and the LSP list every one. `run` still stops at the first. `--json` prints `[{file, line, col, severity, message}, ...]`; `--stdin` reads the buffer and uses `<file>` for imports. `lsp` speaks the Language Server Protocol over stdin/stdout (JSON-RPC with `Content-Length` framing). The editor starts it once and keeps it running for diagnostics, hover, go to definition, find references, completion, outline, highlight, rename (workspace refs, not `builtin/`), CodeLens, and code actions. `test <file>` only calls `@test` functions. `test` with no file runs `examples/tests.rg` plus `tests/pass` (must succeed; files without `fn main` are libraries and are skipped) and `tests/fail` (must error; first `# expect: …` comment is a substring of the message; files without `# expect:` are libraries and are skipped).

## Language (now)

`fn`, `@test`, `@deprecated`, `@constexpr`, `@ufcs`, `@optional`, typecheck, `import` / `from` / `as`, `pub` / `private` / `protected`, `mod`, `struct`, `data`, `class`, `extends`, `abstract`, `final`, `trait` (bounds and trait objects), `impl` / `impl Trait for Type` / `impl[T] Trait[T] for Type[T]`, `super`, `enum`, `match` / `switch`, `signal` (`connect` / `emit` / `emit_deferred` / `disconnect`, including on struct/class), closures (`fn (x: Int) { … }`, `fn [T](x: T) { … }`), generics (`fn id[T]`, `struct Box[T]`, `trait Holder[T]`, `class Child[T] extends Box[T]`, `T: Trait` bounds, `obj.wrap[U](x)`), `var`, `const`, `return`, `pass`, `break`, `continue`, `if` / `elif` / `else`, `while`, `for` / `in`, `try` / `do` / `throws` / `throw` / `catch`, `print`, `assert`, `len`, arrays (`[]`, `Array[Int]`, index, `push` / `pop`), maps (`{}`, index, `has` / `keys` / `remove`), `argv` / `argv_len` (`process.argv` / `process.argc`), `checks.eq` / `neq` / `eq_string` / `that`, `std` (`std.math`, `std.str`, `std.io`, `std.vec`, `std.time`, `std.path`, `std.json`, `std.ui`).

Ints, Floats, strings, bools, `+ - * / % == != < > <= >= && ||`, unary `-` / `!`, `+= -= *= /=`, user functions. Int/Int `+ - * / %` stay Int (`3 / 2` is `1`). If either side is Float, the result is Float (`3 / 2.0` is `1.5`). `1 == 1.0` is true. `&&` / `||` short-circuit and return Bool (`false && (1 / 0 == 1)` does not divide). Any truthy value works: `1 && "x"` is true. `n += 1` is `n = n + 1` (same for fields and indexes). String `+=` concatenates.

Comments: `//` and `#` to end of line, `///` for docs, `/# ... #/` for blocks.

**`const` is init-once.** The initializer is any expression: literals, operators, variables, calls. It runs once, then the name cannot be assigned. `const` does not require a `@constexpr` function.

```text
var x = 2;
const n = add(x, 40);   # n is 42
n = 1;                  # error: cannot assign to const 'n'
```

**`@constexpr` is a purity mark.** A function marked `@constexpr` is checked when the file loads. Its body may use literals, operators, `const`, `if` / `match` / `return` / `pass`, `len`, and calls to other `@constexpr` functions. It may not use `var`, assignment, `print` / `assert`, `while` / `for`, or `break` / `continue`. Methods may be `@constexpr` too (their bodies are checked the same way).

```text
@constexpr
fn add(a: Int, b: Int): Int {
    return a + b;
}

fn main(): Int {
    const n = add(2, 40);
    print(n);
    return 0;
}
```

**`@ufcs` is a call-syntax mark.** A function marked `@ufcs` can be called as a method: `x.fn(args)` means `fn(x, args)`. Bare `fn(x, args)` still works. Inherent methods win: `p.length()` uses `Point.length` if that method exists. Array `push` / `pop` and map / string methods are not stolen. `@ufcs` is only for free functions, not methods. After `import ui`, exported `@ufcs` helpers such as `padding` are visible as `label.padding(12)` as well as `ui.padding(label, 12)`.

```text
@ufcs
fn doubled(n: Int): Int {
    return n * 2;
}

fn main(): Int {
    print((3).doubled());
    print(doubled(3));
    return 0;
}
```

**`throws` marks a function that can `throw`.** Call it with `try`. Handle it with `do { … } catch e { … }`. `throw` outside a throwing function is a type error. A throwing call without `try` is a type error. `return try boom()` in a function that is not itself `throws` is a type error — `try` does not swallow the throw, it marks that you are propagating. Uncaught throws are a runtime error. `do` without `catch` is a block; a throw still escapes.

```text
fn boom() throws: String {
    throw "nope";
}

fn main(): Int {
    do {
        print(try boom());
    } catch e {
        print(e);
    }
    return 0;
}
```

**Typecheck runs when the file loads**, before `main`. Missing annotations stay quiet. Known types are checked: call arity, `1 + "x"`, assigning a String to an `Int`, returning the wrong type, unknown names, unknown methods/fields, and index types (`xs["a"]`, `m[0]`). `Int` and `Float` mix; `"a" + "b"` is a String. `[1, 2]` is `Array[Int]`; index, `for`, and `push` / `pop` use that element type. `Array[Int]` and `Array[String]` are valid annotations (`Array<Int>` is the same). `return try boom()` in a function that is not `throws` is a type error. Today's `const` and untyped `var` still work: a `var` with a known initializer keeps that type. `check` and the editor report every parse, load, `@constexpr`, and type error in the file; `run` still stops at the first.

`if` / `elif` / `else`, `while`, and `for x in xs` take a condition or iterable and a `{ ... }` block. No extra parens. `elif` is the keyword; `else if` is not parsed. Conditions use the same truthy rules as `assert` (non-zero, non-empty, `true`). Combine them with `&&` / `||`. `pass;` does nothing. `break;` / `continue;` only work inside `while` and `for`.

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
        i += 1;
        continue;
    }
    if i > 3 {
        break;
    }
    i += 1;
}

var sum = 0;
for x in [1, 2, 3] {
    sum += x;
}

for i in 0..5 {
    print(i);   # 0 1 2 3 4
}

for i in 1..=3 {
    print(i);   # 1 2 3
}
```

`for x in xs` walks an array, a map's keys, each character of a string, `0..n` / `1..=n`, or `0 .. n-1` when `xs` is an Int. `0..5` is exclusive (`0 1 2 3 4`); `1..=3` is inclusive (`1 2 3`). Range bounds must be Int. The loop variable does not leak; an outer `var` with the same name is restored. `impl Trait for Type` still uses `for` as that keyword.

`match` and `switch` are the same statement. Arms are a pattern plus a `{ ... }` block. No `case` or extra parens. Patterns can be `_`, an Int/string/bool literal, or an enum variant (`Red`, `Color.Red`, `Circle(r)`, `Rect(width: w, height: h)`). The first matching arm runs. Matching an enum requires `_` or every variant; `check` and the LSP report a missing one. Int/string/bool matches do not have to be exhaustive.

```text
switch n {
    1 { print("one"); }
    2 { print("two"); }
    _ { print("other"); }
}
```

**Enums** are variants, optionally with payloads. Unit variants are `Color.Red`. Payload variants are called: `Shape.Circle(5)`.

```text
enum Color {
    Red,
    Green,
    Blue,
}

enum Shape {
    Circle(Int),
    Rect(width: Int, height: Int),
}

fn main(): Int {
    var c = Color.Red;
    match c {
        Red { print("red"); }
        _ { print("other"); }
    }
    var s = Shape.Rect(2, 3);
    match s {
        Circle(r) { print(r); }
        Rect(w, h) { print(w); print(h); }
    }
    return 0;
}
```

**Arrays** are `[1, 2, 3]` (`Array[Int]`). Index with `xs[i]`, length with `len(xs)` or `xs.len()` / `xs.len`. `push` appends, `pop` removes the last element (error if empty). Index assignment is `xs[i] = v`. Copies share the same array, like structs. Strings index to a one-character string: `"hi"[0]` is `"h"`. A match arm can bind a whole payload as one array: `Rect(dims)`. Mixed literals (`[1, "a"]`) stay a plain `Array`.

```text
fn main(): Int {
    var xs = [1, 2, 3];
    print(xs[0]);
    xs.push(4);
    xs[1] = 9;
    print(len(xs));
    print(xs.pop());
    return 0;
}
```

**Maps** (dictionaries) are `{"a": 1}`. Keys are strings. Read `m[k]`, write `m[k] = v` (inserts if missing). `len(m)` / `m.len()`, `m.has(k)`, `m.keys()`, `m.remove(k)`, `m.insert(k, v)`. Copies share the same map, like arrays. A missing key is an error.

```text
fn main(): Int {
    var scores = {"ada": 10, "grace": 12};
    scores["linus"] = 9;
    print(scores["grace"]);
    print(scores.has("ada"));
    print(len(scores));
    return 0;
}
```

`@deprecated` warns when that function is called, then still runs it.

**Signals** are Godot-style. File-level `signal`s are global. `struct` / `class` (not `data`) may declare `signal`s; each instance has its own listener list (copies share the object, so they share listeners too). `connect` takes a named function or a closure. `emit` runs listeners now, in connect order. `emit_deferred` queues the same call until the current `main` / `@test` returns (idle-frame stand-in; nested deferred emits run in later waves, cap 64). A second `connect` of the same function or closure is ignored. `disconnect` removes it (no error if it was not connected). Arity must match on connect. Inside a method, a bare `collected.emit(…)` fires that instance's signal (and wins over a global of the same name). Trait signals attach to implementing instances.

```text
signal collected(amount: Int);

struct Coin {
    signal grabbed(amount: Int);

    fn grab(self, n: Int) {
        grabbed.emit(n);
    }
}

fn log_coin(amount: Int) {
    print(amount);
}

fn main(): Int {
    collected.connect(log_coin);
    collected.emit(5);
    collected.emit_deferred(7);
    collected.disconnect(log_coin);

    var c = Coin {};
    c.grabbed.connect(fn (amount: Int) { print(amount); });
    c.grab(3);
    return 0;
}
```

**Closures** are `fn (params) [: Type] { … }` in expression position, and may take type parameters: `fn [T](x: T): T { return x; }`. They capture enclosing locals: structs, arrays, and maps are shared objects; ints, floats, bools, and strings are snapshotted. Closures are `Fn` values (`connect` accepts them). They are not `@constexpr`.

```text
fn main(): Int {
    var add = fn (x: Int): Int { return x + 1; };
    print(add(2));
    print((fn (n: Int): Int { return n * 2; })(3));
    return 0;
}
```

**Generics** are typecheck-only (the interpreter is still dynamic). `fn`, `struct`, `data`, `class`, and `trait` take `[T]` or `[T: Trait]` (also `<>`). Bounds are a compile-time checklist: `T` must `impl` that trait. The trait name is also a type (a trait object): `fn label(x: Named)` and `Array[Named]` accept any implementor. You only get the trait's methods, not the concrete fields. There is no downcast. Calls infer type arguments from values; you can write them: `id[Int](3)`, `Box[Int] { value: 1 }`, `obj.wrap[String]("hi")`. `impl[T] Holder[T] for Box[T]` and `class Child[T] extends Slot[T]` substitute along the type. `Array[T]` unifies the same way. `Map[String, Int]` now keeps both arguments.

```text
fn identity[T](x: T): T {
    return x;
}

struct Box[T] {
    value: T;
    fn get(self): T {
        return value;
    }
}

fn first[T](xs: Array[T]): T {
    return xs[0];
}

trait Holder[T] {
    fn get(self): T;
}

impl[T] Holder[T] for Box[T] {
}

class Slot[T] {
    var value: T;
}

class Packed[T] extends Slot[T] {
}

fn main(): Int {
    print(identity(3));
    print(identity[String]("hi"));
    var b = Box { value: 9 };
    print(b.get());
    print(first([1, 2]));
    print(Packed { value: 4 }.value);
    print((fn [T](x: T): T { return x; })(3));
    return 0;
}
```

A **trait object** is the trait used as a type. `Person` and `Robot` can both sit in a `Named` variable or `Array[Named]`; you can only call `Named` methods.

```text
trait Named {
    fn tag(self): String;
}

struct Person impl Named {
    n: String;
    fn tag(self): String { return n; }
}

fn label(x: Named): String {
    return x.tag();
}

fn main(): Int {
    var p: Named = Person { n: "ada" };
    print(label(p));
    return 0;
}
```

`run file.rg a b` is indexed: `argv(0)` / `process.argv(0)` is the file path, `argv(1)` is `a`, `argv_len()` / `process.argc()` is the count.

```text
fn main(): Int {
    print(argv(0));
    if argv_len() > 1 {
        print(argv(1));
    }
    return 0;
}
```

**Structs** are live: declare fields, construct with `Name { field: value, ... }`, read `p.x`, write `p.x = 3`. Required fields must be set. `@optional` fields may be omitted and fill with a zero for that type (`0`, `0.0`, `""`, `false`, `[]`, `{}`). Copies share the same object (mutating `q.x` after `var q = p` also changes `p.x`).

**`data` is frozen.** Construction is the same (`Point { x: 3, y: 4 }`). Fields cannot be assigned (`p.x = 1` is an error, including inside methods). Methods may read fields. A `data` type cannot be extended (`class Dot extends Point` is an error). `abstract` cannot apply. `@optional` and `pub` / `private` work like struct. `impl Trait for Point` is allowed. Equality is by fields: two separately constructed `Point { x: 1, y: 2 }` values compare equal. Use `data` for catalog rows and values; keep `struct` / `class` when something mutates or inherits.

```text
data Point {
    x: Int;
    y: Int;

    fn mag2(): Int {
        return x * x + y * y;
    }
}

fn main(): Int {
    var p = Point { x: 3, y: 4 };
    print(p.mag2());
    return 0;
}
```

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

**Classes** use `var` fields (optional `= expr` defaults, or `@optional`). Omitted fields at construction are filled from defaults (parent first, then child), then `@optional` zeros. Structs still require every field that is not `@optional`.

`class Slime extends Enemy` copies parent fields and methods. A child method can override a parent method and call it with `super.method()`. `super` is only valid in a method of a class that extends another. `super` cannot call an `abstract` parent method.

`abstract class` cannot be constructed. `abstract fn` is a signature (`fn speak(): String;`); a class with one must itself be `abstract`; a concrete child must implement every inherited abstract method. `final class` cannot be extended. `final fn` cannot be overridden. `abstract` + `final` together is an error (class or method). `trait` stays the interface. Skip abstract traits, final fields, and anonymous classes. `@optional` fields work on abstract classes like any other class.

**Visibility** on fields and methods is load/typecheck. Unmarked members stay usable like `pub`. `main` may not use `protected` or `private` members on a value.

| Modifier | Who can use it |
|---|---|
| `pub` | anyone who can see the type |
| `protected` | this class and subclasses |
| `private` | only this type |
| *(unmarked member)* | same as `pub` |

`protected` is for class members (`extends`). Structs get `pub` / `private` only. On module items, `pub` still exports; unmarked stays in the module; `private` is the explicit form of unmarked; `protected` is not for free functions, types, or trait-impl methods. A subclass may call `protected` parent methods (`super` or a bare call inside a child method) and read/write `protected` parent fields there. Construction may set a type’s **own** fields at any visibility; inherited `private` / `protected` fields in a literal still need access. `pub` on a `class` / `fn` in a `mod` is still module export.

```text
abstract class Animal {
    var hp: Int = 1;
    abstract fn speak(): String;
    fn hit(): Int {
        return self.hp;
    }
}

class Dog extends Animal {
    fn speak(): String {
        return "woof";
    }
}

final class Cat extends Animal {
    fn speak(): String {
        return "meow";
    }
}
```

A concrete method on an abstract class may call an abstract method; dispatch uses the instance type (`Dog {}.greet()` runs `Dog.speak`).

**Traits** declare method signatures (`fn name(...): Type;`) and optional `signal`s. Implement with `class Point impl Named`, `data Token impl Named`, a nested `impl Named { ... }` in the class body, or `impl Named for Point { ... }`. Existing `impl Point { ... }` is still an inherent impl (no `for`). A type that claims a trait must provide each method.

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

class Guard {
    pub var hp: Int = 1;
    protected var armor: Int = 0;
    private var secret: Int = 9;

    pub fn hit(): Int {
        return hp;
    }

    protected fn soak(): Int {
        return armor;
    }

    private fn key(): Int {
        return secret;
    }
}

class Pup extends Guard {
    fn speak(): String {
        soak();
        return "woof";
    }
}
```

`Pup.speak` may call `soak()` (`protected`). It may not call `key()` (`private`). `main` may use `d.hp` / `d.hit()`, not `d.armor` / `d.secret`.

**Modules** are directories or `mod` blocks, not `name.rg` files. `import calc;` loads every `.rg` file in a sibling `calc/` folder (file-level items are all exported). It also loads any sibling file that declares `mod calc { ... }`. Several files can contribute to the same name and get merged. Call exports as `calc.add()`. `import calc as c;` binds `c`. `from calc import add;` and `from calc import add as sum;` bind a name in the importer.

Inside a `mod Name { }`, only `pub` items are exported; private functions can still call each other. Nested `mod` works: `outer.inner.add()`, and parent code can call `inner.add()`. Nested mods need `pub` to be visible outside the parent. `import` / `from` inside a `mod` bind names in that module only.

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

**Imports** pick a module, then bind a name. A dotted path loads the parent first. `import bag.math;` looks for `bag/math/` or a nested `pub mod math`, then binds `math` (or `as` alias). If the path starts with `std`, `std` is bound too, so `import std.math;` gives both `math.abs` and `std.math.abs`.

Short stdlib crate names rewrite to `std.*`: `import math;` is `import std.math;` (same for `str`, `io`, `vec`, `time`, `path`, `json`, `ui`). `from vec import Vec2;` and `from std import UUID;` still pull one export. `import checks;` / `import process;` are no-ops (`checks.eq` already works).

`import std;` loads `builtin/std/lib.rg` and attaches every child crate folder, so `std.math` works without a second import. `import math;` also loads `std` first (parent of `std.math`), so the other crates come along while the stdlib is small. Extra `.rg` files in a crate folder are the same crate: `import ui` loads `ui/lib.rg`, `ui/color.rg`, and `ui/widgets.rg`, so `Color`, `Label`, and `VStack` need no extra import. Nested folders under a crate attach the same way `std` attaches `math`.

Using a stdlib name without importing it names the crate in the error: `undefined trait 'Identifiable' (in crate std; try 'import std')`. The editor lightbulb can insert that import.

```text
import math;
import std.str;
from vec import Vec2;

fn main(): Int {
    print(math.abs(-7));
    print(std.str.upper("hi"));
    print(Vec2 { x: 3.0, y: 4.0 }.length());
    return 0;
}
```

**Stdlib** lives in `builtin/std/` and is resolved from the repo (walk up from the script). `math` keeps Int helpers (`abs`, `sign`, `min`, `max`, `clamp`, `gcd`, `pow`, `rand_int`) and adds Float host ops (`sin`, `cos`, `atan2`, `sqrt`, `powf`, `to_int`, `to_float`, `floor`, `ceil`, `random`) plus `lerp` / `move_toward` in RoseGold. Comparisons and unary minus also work on Float, so `math.abs(-3.0)` is `3.0`. `str` wraps host primitives (`contains`, `starts_with`, `ends_with`, `length`, `is_empty`, `repeat`, `upper`, `lower`, `trim`, `slice`, `split`, `replace`, `find`). `io.exists` / `io.remove` return Bool; `io.read_text` / `io.read_lines` / `io.write_text` are `throws` (catch with `do` / `catch`; this subset has no Result). `io.read_lines` is `Array[String]`. `time.now()` is unix epoch milliseconds; `time.sleep(ms)` waits. `path.join` / `path.parent` / `path.stem` use `/` in the returned string. `json.parse(s)` (`throws`) turns a JSON object into a Map, an array into an Array, and numbers/strings/bools into those values; `json.stringify` goes the other way (structs become objects; enums become the variant name). JSON `null` is not in this subset — parse throws. `json.valid(s)` is true when parse would succeed. `vec` is `Vec2` / `Vec3` classes (`Vec2 { x: 3.0, y: 4.0 }` — type names are not `std.vec.Vec2 { }`). `UUID` is frozen `data` (`value: String`). After `import std;`, `std.v4()`, `std.nil()`, `std.parse(s)` (`throws`), `std.valid(s)`, plus `u.to_string()` / `u.hex()` / `u.is_nil()`. `std.parse` accepts dashed or 32-hex, upper or lower, and stores canonical lowercase dashed form. `ui` opens a native window on the current OS (`ui.backend()` is the blit host: `win32`, `x11`, `wayland`, `cocoa`, or `none`; later `android`, `ios`, `web`). `ui.platform()` is the OS family (`windows`, `linux`, `macos`; later `android`, `ios`, `web`). `ui.kind()` is `desktop`, `mobile`, or `web` so layout can branch without listing OS names. Android, iOS, and web hosts are not implemented yet — the same `.rg` widget tree paints pixels; each OS only blits. `try ui.open("RoseGold", 800, 600)` or `Window { title: "Hi", width: 640, height: 480 }` then `try w.show()`. Layout is Swift-ish: nest `VStack` / `HStack` (`children:` or `.add()`), then chain `.padding(n)`, `.background(Color.Blue)`, `.foreground(Color.Red)`, or `.style(Style { fill, ink, pad })`. Named colors are `Color.Black` / `Red` / `Green` / `Yellow` / `Blue` / `Magenta` / `Cyan` / `White`; custom is `ui.rgb(r, g, b)` or `Color.Rgb(r, g, b)` (`.value()` is the packed Int the host draws). `w.add(Label { text: "Hi" })` and `Button { text: "OK" }` (`clicked` signal) still stack vertically if you skip the stacks. `TextField` / `Toggle` take keyboard and click focus; `ScrollView` clips a tall child; `Canvas` strokes a frame and diagonal. Connect signals on the `Button` before wrapping it. `w.run()` pumps until that window closes. `ui.run()` still pumps every window that has no widget tree. `open` / `show` are `throws`. `ui.open_hidden` is the same window with no flash (tests). Visible `open` still needs a display; hidden windows can run offscreen if the display is missing. Drawing is fill plus the OS UI font on Windows (`Segoe UI` / the message font) and an 8×8 bitmap fallback elsewhere. `ui.font_height()` / `ui.text_width(s)` measure that font. `checks` stays a host builtin and does not need import.

A **framework** here is an optional stack behind `import`, not new syntax. Windowing is `import ui`. Widgets are `Label` (wrap), `Button`, `TextField`, `Toggle`, `Slider`, `VStack`, `HStack`, `Spacer`, `ScrollView`, `LazyColumn` (`LazyRows` / `LabelRows`), `Canvas`, and `ImageView`, plus modifiers (`.padding`, `.background`, `.style`), `Theme`, and HStack `align` (`fill` / `leading` / `center` / `trailing`). Write once: that widget tree paints a software framebuffer; win32/X11/Wayland/Cocoa blit it today, and Android/iOS/web are reserved names for the same `__ui` surface later. Details: [builtin/std/ui/README.md](builtin/std/ui/README.md).

```text
import ui;

fn main(): Int {
    var w = try ui.open("RoseGold", 400, 240);
    var go = Button { text: "Close" };
    go.clicked.connect(fn () { w.close(); });
    w.add(VStack {
        spacing: 12,
        children: [
            Label { text: "Hello" }.padding(8),
            go.background(Color.Blue)
        ]
    }.padding(12));
    w.run();
    return 0;
}
```

```text
import std;
from vec import Vec2;

fn main(): Int {
    print(std.math.abs(-7));
    print(std.str.upper("hi"));
    print(std.io.exists("README.md"));
    print(std.time.now() > 0);
    print(std.path.stem("README.md"));
    print(std.json.stringify({"ok": true}));
    var v = Vec2 { x: 3.0, y: 4.0 };
    print(v.length());
    return 0;
}
```

## Next steps

**Desktop UI**

1. Dogfood a second small app (todo list, settings panel, or tiny file browser) — surfaces more gaps.
2. Optional polish: richer text (caret blink, selection), Wayland input parity, more image formats beyond PPM/PNG.
3. When the language grows fn-as-parameter types: `.on_click` / `.on_change` UFCS aliases for signal connect.

**Tooling / language**

- Optional: `import math` not loading the rest of `std` (already correct; stdlib is tiny).
- Grow `fmt` / rgfmt (comments, exact float literals, style knobs).

**Later / when you have a concrete target**

- Mobile or web `__ui` host (names already reserved on the same surface as win32/X11/Wayland/Cocoa).
