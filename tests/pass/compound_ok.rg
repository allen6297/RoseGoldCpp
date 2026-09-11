import checks;

struct Point {
    x: Int;
    y: Int;
}

fn main(): Int {
    var n = 1;
    n += 2;
    checks.eq(n, 3);
    n -= 1;
    checks.eq(n, 2);
    n *= 4;
    checks.eq(n, 8);
    n /= 2;
    checks.eq(n, 4);

    var f = 1.5;
    f += 0.5;
    checks.eq(f, 2.0);

    var s = "a";
    s += "b";
    checks.eq_string(s, "ab");

    var p = Point { x: 1, y: 2 };
    p.x += 3;
    checks.eq(p.x, 4);

    var xs = [1, 2];
    xs[0] += 3;
    checks.eq(xs[0], 4);

    var m = {"a": 1};
    m["a"] += 2;
    checks.eq(m["a"], 3);
    return 0;
}
