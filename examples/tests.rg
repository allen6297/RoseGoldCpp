import calc;
from calc import double;
import checks;
import std;
import math;
import str;
import io;
import time;
import path;
import json;
import regex;
from vec import Vec2;
from std import UUID;

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
    checks.eq(math.sqrt(9.0), 3.0);
    checks.eq(math.lerp(0.0, 10.0, 0.5), 5.0);
}

@test
fn stdlib_str() {
    checks.eq_string(str.upper("hi"), "HI");
    checks.eq(str.length("ab"), 2);
    checks.eq(len(str.split("a,b", ",")), 2);
    checks.eq_string(str.replace("aa", "a", "b"), "bb");
}

@test
fn stdlib_io() {
    const path = "._rg_io_unit.txt";
    io.remove(path);
    try io.write_text(path, "ok");
    checks.eq_string(try io.read_text(path), "ok");
    io.remove(path);
}

@test
fn stdlib_time() {
    checks.that(time.now() > 0);
    time.sleep(0);
}

@test
fn stdlib_path() {
    checks.eq_string(path.join("a", "b"), "a/b");
    checks.eq_string(path.stem("c.txt"), "c");
}

@test
fn stdlib_json() {
    var obj = try json.parse("{\"n\": 3}");
    checks.eq(obj["n"], 3);
    checks.eq_string(json.stringify([1, 2]), "[1,2]");
    checks.that(json.valid("true"));
    checks.that(!json.valid("null"));
}

@test
fn stdlib_regex() {
    checks.that(regex.is_match("\\d+", "a42"));
    checks.eq(regex.find("\\d+", "ab12"), 2);
    checks.eq_string(regex.find_match("\\d+", "ab12"), "12");
    checks.eq(len(regex.findall("[a-z]", "a1b")), 2);
    checks.eq_string(regex.replace("a+", "xaaay", "b"), "xby");
    checks.eq(len(regex.split("-", "a-b-c")), 3);
    checks.that(regex.valid("\\w+"));
    checks.that(!regex.valid("("));
}

@test
fn stdlib_vec() {
    var v = Vec2 { x: 3.0, y: 4.0 };
    checks.eq(v.length(), 5.0);
}

@test
fn stdlib_std() {
    checks.eq(std.math.abs(-7), 7);
    checks.eq_string(std.str.upper("hi"), "HI");
    var n = std.nil();
    checks.that(n.is_nil());
    var u = try std.parse("550e8400-e29b-41d4-a716-446655440000");
    checks.eq_string(u.to_string(), "550e8400-e29b-41d4-a716-446655440000");
    checks.eq(len(std.v4().to_string()), 36);
    checks.that(std.time.now() > 0);
    checks.eq_string(std.path.stem("README.md"), "README");
    checks.that(std.regex.is_match("README", "README.md"));
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
signal later();

struct Coin {
    signal grabbed(amount: Int);

    fn grab(self, n: Int) {
        grabbed.emit(n);
    }
}

struct Flag {
    n: Int;
}

fn identity[T](x: T): T {
    return x;
}

struct Cell[T] {
    value: T;

    fn get(self): T {
        return value;
    }

    fn wrap[U](self, x: U): U {
        return x;
    }
}

fn first[T](xs: Array[T]): T {
    return xs[0];
}

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
fn signal_instance() {
    var c = Coin {};
    c.grabbed.connect(take);
    c.grabbed.emit(5);
}

@test
fn signal_method_emit() {
    var c = Coin {};
    c.grabbed.connect(take);
    c.grab(5);
}

@test
fn signal_deferred() {
    var box = Flag { n: 0 };
    later.connect(fn () { box.n = 1; });
    later.emit_deferred();
    checks.eq(box.n, 0);
}

@test
fn closure_add() {
    var f = fn (x: Int): Int { return x + 1; };
    checks.eq(f(1), 2);
    checks.eq((fn (n: Int): Int { return n * 2; })(3), 6);
}

@test
fn closure_capture() {
    var box = Flag { n: 3 };
    var f = fn (): Int { return box.n; };
    box.n = 4;
    checks.eq(f(), 4);
}

@test
fn closure_call_local() {
    var box = Flag { n: 0 };
    var bump = fn () { box.n = box.n + 1; };
    var run = fn () { bump(); };
    run();
    checks.eq(box.n, 1);
}

@test
fn generic_id() {
    checks.eq(identity(3), 3);
    checks.eq_string(identity("a"), "a");
}

@test
fn generic_box() {
    var b = Cell { value: 8 };
    checks.eq(b.get(), 8);
    checks.eq(Cell[Int] { value: 2 }.value, 2);
}

@test
fn generic_array() {
    checks.eq(first([4, 5]), 4);
}

@test
fn generic_method_targ() {
    checks.eq_string(Cell { value: 1 }.wrap[String]("z"), "z");
}

@test
fn generic_lambda() {
    checks.eq((fn [T](x: T): T { return x; })(8), 8);
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

fn tagged[T: Named](x: T): String {
    return x.label();
}

@test
fn generic_bound() {
    var t = Token {};
    checks.eq_string(tagged(t), "tok");
}

fn shout(x: Named): String {
    return x.label();
}

@test
fn trait_object() {
    var t: Named = Token {};
    checks.eq_string(t.label(), "tok");
    checks.eq_string(shout(Token {}), "tok");
}

@test
fn implicit_field() {
    var hp = 1;
    var e = Enemy {};
    checks.eq(e.hurt(3), 7);
    checks.eq(hp, 1);
}

enum Hue {
    Red,
    Green,
}

enum Shape {
    Circle(Int),
    Rect(Int, Int),
}

@test
fn enum_unit() {
    var c = Hue.Red;
    checks.that(c == Hue.Red);
    checks.that(c != Hue.Green);
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
fn compare_le_ge() {
    checks.that(1 <= 1);
    checks.that(!(2 <= 1));
    checks.that(2 >= 1);
    checks.that(1.5 >= 1);
    checks.that(1 <= 1.5);
}

@test
fn logic_and_or() {
    checks.that(true && true);
    checks.that(!(false && true));
    checks.that(false || true);
    checks.that(1 < 2 && 3 > 2);
    checks.that(1 && "x");
    checks.that(!(false && (1 / 0 == 1)));
    checks.that(true || (1 / 0 == 1));
}

@test
fn compound_assign() {
    var n = 1;
    n += 2;
    checks.eq(n, 3);
    n *= 2;
    checks.eq(n, 6);
    n -= 1;
    checks.eq(n, 5);
    n /= 5;
    checks.eq(n, 1);
    var s = "a";
    s += "b";
    checks.eq_string(s, "ab");
    var xs = [1];
    xs[0] += 4;
    checks.eq(xs[0], 5);
}

@test
fn for_range() {
    var n = 0;
    for i in 0..5 {
        n = n + i;
    }
    checks.eq(n, 10);
    var m = 0;
    for i in 1..=3 {
        m = m + i;
    }
    checks.eq(m, 6);
    var r = 0..3;
    var s = 0;
    for i in r {
        s = s + i;
    }
    checks.eq(s, 3);
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

@ufcs
fn doubled(n: Int): Int {
    return n * 2;
}

@test
fn ufcs_call() {
    checks.eq((3).doubled(), 6);
    checks.eq(doubled(4), 8);
}

fn boom() throws: String {
    throw "nope";
}

fn ok_throw() throws: Int {
    return 7;
}

@test
fn try_do_catch() {
    do {
        checks.eq(try ok_throw(), 7);
        try boom();
        checks.eq(1, 0);
    } catch e {
        checks.eq_string(e, "nope");
    }
}

struct OptPoint {
    x: Int;
    @optional
    y: Int;
}

@test
fn optional_field() {
    var p = OptPoint { x: 3 };
    checks.eq(p.x, 3);
    checks.eq(p.y, 0);
}

abstract class AbsPet {
    var hp: Int = 1;
    abstract fn speak(): String;
    fn greet(): String {
        return speak();
    }
}

class AbsDog extends AbsPet {
    fn speak(): String {
        return "woof";
    }
}

final class AbsCat extends AbsPet {
    fn speak(): String {
        return "meow";
    }
}

@test
fn abstract_final() {
    var d = AbsDog {};
    checks.eq_string(d.speak(), "woof");
    checks.eq_string(d.greet(), "woof");
    checks.eq(d.hp, 1);
    var c = AbsCat {};
    checks.eq_string(c.speak(), "meow");
}

class VisAnimal {
    pub var hp: Int = 1;
    protected var armor: Int = 0;
    private var secret: Int = 9;

    pub fn hit(): Int {
        return hp + key();
    }

    protected fn soak(): Int {
        return armor;
    }

    private fn key(): Int {
        return secret;
    }
}

class VisDog extends VisAnimal {
    fn speak(): String {
        soak();
        return "woof";
    }
}

@test
fn vis_members() {
    var d = VisDog {};
    checks.eq(d.hp, 1);
    checks.eq(d.hit(), 10);
    checks.eq_string(d.speak(), "woof");
}

data DPoint {
    x: Int;
    y: Int;

    fn mag2(): Int {
        return x * x + y * y;
    }
}

@test
fn data_ok() {
    var p = DPoint { x: 3, y: 4 };
    checks.eq(p.x, 3);
    checks.eq(p.mag2(), 25);
    var q = DPoint { x: 3, y: 4 };
    checks.that(p == q);
}


