import checks;

@ufcs
fn doubled(n: Int): Int {
    return n * 2;
}

@ufcs
fn clamp(n: Int, lo: Int, hi: Int): Int {
    if n < lo {
        return lo;
    }
    if n > hi {
        return hi;
    }
    return n;
}

struct Point {
    x: Int;
    y: Int;
}

impl Point {
    fn length(): Int {
        return 9;
    }
}

@ufcs
fn length(n: Int): Int {
    return n;
}

@ufcs
fn push(n: Int, x: Int): Int {
    return n + x;
}

@constexpr
@ufcs
fn trip(n: Int): Int {
    return n * 3;
}

@constexpr
fn six(): Int {
    return (2).trip();
}

fn main(): Int {
    checks.eq((3).doubled(), 6);
    checks.eq(doubled(3), 6);
    checks.eq(3.doubled(), 6);
    checks.eq(400.clamp(0, 200), 200);
    checks.eq(clamp(50, 0, 200), 50);

    var p = Point { x: 3, y: 4 };
    checks.eq(p.length(), 9);
    checks.eq(3.length(), 3);

    var a = [1];
    a.push(2);
    checks.eq(len(a), 2);
    checks.eq(3.push(4), 7);

    const n = six();
    checks.eq(n, 6);
    return 0;
}
