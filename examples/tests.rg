import calc;
from calc import double;
import checks;
import math;
import str;

struct Point {
    x: Int;
    y: Int;
}

impl Point {
    fn sum(): Int {
        return x + y;
    }

    fn bump() {
        x = x + 1;
    }
}

struct Box {
    p: Point;
}

struct Counter {
    n: Int;

    fn inc() {
        n = n + 1;
    }

    fn get(): Int {
        return n;
    }
}

@test
fn add() {
    checks.eq(2 + 2, 4);
}

@test
fn import_mod() {
    checks.eq(calc.add(2, 3), 5);
    checks.eq(calc.double(4), 8);
    checks.eq(double(4), 8);
}

@test
fn stdlib_math() {
    checks.eq(math.abs(-7), 7);
    checks.eq(math.clamp(5, 0, 3), 3);
    checks.eq(math.pow(2, 10), 1024);
}

@test
fn stdlib_str() {
    checks.eq_string(str.upper("hi"), "HI");
    checks.eq(str.length("ab"), 2);
}

@test
fn greet() {
    print("hi");
}

@test
fn const_math() {
    const n = 2 + 3 * 4;
    checks.eq(n, 14);
}

@test
fn const_from_call() {
    var x = 2;
    const n = x + 3;
    checks.eq(n, 5);
}

signal ping();
signal collected(amount: Int);
signal tap();

fn on_ping() {
    print("pong");
}

fn take(amount: Int) {
    checks.eq(amount, 5);
}

fn fail_if_called() {
    checks.eq(0, 1);
}

@test
fn signal_fire() {
    ping.connect(on_ping);
    ping.emit();
}

@test
fn signal_args() {
    collected.connect(take);
    collected.emit(5);
}

@test
fn signal_disconnect() {
    tap.connect(fail_if_called);
    tap.disconnect(fail_if_called);
    tap.emit();
}

@test
fn argv_present() {
    checks.eq(argv_len(), 1);
    checks.eq(process.argc(), 1);
    checks.that(argv(0) != "");
    checks.eq_string(process.argv(0), argv(0));
}

fn sign(n: Int): Int {
    if n > 0 {
        return 1;
    } elif n < 0 {
        return -1;
    } else {
        return 0;
    }
}

@test
fn if_then() {
    var x = 1;
    if true {
        x = 2;
    }
    checks.eq(x, 2);
}

@test
fn if_else() {
    var x = 1;
    if false {
        x = 2;
    } else {
        x = 3;
    }
    checks.eq(x, 3);
}

@test
fn if_elif() {
    checks.eq(sign(4), 1);
    checks.eq(sign(-2), -1);
    checks.eq(sign(0), 0);
}

@test
fn while_sum() {
    var i = 0;
    var s = 0;
    while i < 4 {
        s = s + i;
        i = i + 1;
    }
    checks.eq(s, 6);
}

@test
fn while_skip() {
    var n = 0;
    while false {
        n = 1;
    }
    checks.eq(n, 0);
}

@test
fn pass_ok() {
    if true {
        pass;
    } else {
        pass;
    }
    checks.eq(1, 1);
}

@test
fn while_break() {
    var i = 0;
    while true {
        if i > 2 {
            break;
        }
        i = i + 1;
    }
    checks.eq(i, 3);
}

@test
fn while_continue() {
    var i = 0;
    var s = 0;
    while i < 4 {
        i = i + 1;
        if i == 2 {
            continue;
        }
        s = s + i;
    }
    checks.eq(s, 8);
}

fn bump_x(p: Point) {
    p.x = p.x + 1;
}

@test
fn struct_new() {
    var p = Point { x: 3, y: 4 };
    checks.eq(p.x, 3);
    checks.eq(p.y, 4);
}

@test
fn struct_write() {
    var p = Point { x: 3, y: 4 };
    p.y = 9;
    checks.eq(p.y, 9);
}

@test
fn struct_share() {
    var p = Point { x: 1, y: 2 };
    var q = p;
    q.x = 8;
    checks.eq(p.x, 8);
}

@test
fn struct_eq() {
    var a = Point { x: 1, y: 2 };
    var b = Point { x: 1, y: 2 };
    var c = Point { x: 1, y: 3 };
    checks.that(a == b);
    checks.that(a != c);
}

@test
fn struct_nested() {
    var b = Box { p: Point { x: 2, y: 5 } };
    checks.eq(b.p.x, 2);
    b.p.y = 7;
    checks.eq(b.p.y, 7);
}

@test
fn struct_call() {
    var p = Point { x: 10, y: 0 };
    bump_x(p);
    checks.eq(p.x, 11);
}

@test
fn method_sum() {
    var p = Point { x: 3, y: 4 };
    checks.eq(p.sum(), 7);
}

@test
fn method_mut() {
    var p = Point { x: 1, y: 0 };
    p.bump();
    checks.eq(p.x, 2);
}

@test
fn method_omit_self() {
    var c = Counter { n: 0 };
    c.inc();
    c.inc();
    checks.eq(c.get(), 2);
}

@test
fn method_nested() {
    var b = Box { p: Point { x: 2, y: 5 } };
    checks.eq(b.p.sum(), 7);
    b.p.bump();
    checks.eq(b.p.x, 3);
}

class Enemy {
    var hp: Int = 10;
    fn hurt(dmg: Int): Int {
        hp = hp - dmg;
        return hp;
    }
}

class Slime extends Enemy {
    var goo: Int = 1;
    fn hurt(self, dmg: Int): Int {
        return super.hurt(dmg / 2);
    }
}

trait Named {
    fn label(self): String;
}

class Token impl Named {
    var n: Int = 1;
    fn label(self): String {
        return "tok";
    }
}

@test
fn class_defaults() {
    var e = Enemy {};
    checks.eq(e.hp, 10);
    checks.eq(e.hurt(3), 7);
}

@test
fn class_extends_super() {
    var s = Slime {};
    checks.eq(s.hp, 10);
    checks.eq(s.goo, 1);
    checks.eq(s.hurt(4), 8);
}

@test
fn class_trait() {
    var t = Token {};
    checks.eq_string(t.label(), "tok");
}

@test
fn implicit_field() {
    var hp = 1;
    var e = Enemy {};
    checks.eq(e.hurt(3), 7);
    checks.eq(hp, 1);
}

enum Color {
    Red,
    Green,
}

enum Shape {
    Circle(Int),
    Rect(Int, Int),
}

@test
fn enum_unit() {
    var c = Color.Red;
    checks.that(c == Color.Red);
    checks.that(c != Color.Green);
}

@test
fn switch_int() {
    var n = 2;
    var got = 0;
    switch n {
        1 { got = 1; }
        2 { got = 2; }
        _ { got = 9; }
    }
    checks.eq(got, 2);
}

@test
fn match_payload() {
    var s = Shape.Rect(2, 3);
    var w = 0;
    var h = 0;
    match s {
        Circle(r) { w = r; }
        Rect(a, b) { w = a; h = b; }
    }
    checks.eq(w, 2);
    checks.eq(h, 3);
}

@test
fn array_index() {
    var xs = [10, 20, 30];
    checks.eq(xs[1], 20);
    xs[1] = 21;
    checks.eq(xs[1], 21);
    checks.eq(len(xs), 3);
    checks.eq(xs.len(), 3);
}

@test
fn array_push_pop() {
    var xs = [1];
    xs.push(2);
    xs.push(3);
    checks.eq(xs.pop(), 3);
    checks.eq(len(xs), 2);
}

@test
fn array_match_payload() {
    var s = Shape.Rect(2, 3);
    var n = 0;
    match s {
        Circle(r) { n = r; }
        Rect(dims) { n = dims[0] + dims[1]; }
    }
    checks.eq(n, 5);
}

@test
fn map_index() {
    var scores = {"ada": 10, "grace": 12};
    checks.eq(scores["ada"], 10);
    scores["linus"] = 9;
    checks.eq(len(scores), 3);
    checks.that(scores.has("linus"));
    checks.eq(scores.remove("ada"), 10);
    checks.that(!scores.has("ada"));
}

@test
fn for_array() {
    var sum = 0;
    for x in [1, 2, 3] {
        sum = sum + x;
    }
    checks.eq(sum, 6);
}

@test
fn for_map_and_int() {
    var n = 0;
    for k in {"a": 1} {
        n = n + 1;
        checks.eq_string(k, "a");
    }
    checks.eq(n, 1);
    var s = 0;
    for i in 4 {
        s = s + i;
    }
    checks.eq(s, 6);
}

@test
fn float_arith() {
    checks.eq(1.5 + 1.5, 3);
    checks.eq(3.0 / 2.0, 1.5);
    checks.that(1 == 1.0);
    checks.eq(-0.5 * 2.0, -1);
}

@constexpr
fn cx_add(a: Int, b: Int): Int {
    return a + b;
}

@test
fn constexpr_call() {
    const n = cx_add(2, 40);
    checks.eq(n, 42);
    checks.eq(cx_add(1, 2), 3);
}

fn typed_add(a: Int, b: Int): Int {
    return a + b;
}

@test
fn typecheck_ok() {
    checks.eq(typed_add(2, 3), 5);
    checks.eq_string("a" + "b", "ab");
    checks.eq(1 + 0.5, 1.5);
}
