import checks;

@constexpr
fn add(a: Int, b: Int): Int {
    return a + b;
}

@constexpr
fn abs(n: Int): Int {
    if n < 0 {
        return 0 - n;
    }
    return n;
}

@constexpr
fn pick(n: Int): Int {
    const t = n + 1;
    match t {
        2 { return 20; }
        _ { pass; }
    }
    return t;
}

@constexpr
fn double(n: Int): Int {
    return add(n, n);
}

@constexpr
fn count(): Int {
    return len([1, 2, 3]);
}

fn main(): Int {
    const n = add(2, 40);
    checks.eq(n, 42);
    checks.eq(double(21), 42);
    checks.eq(abs(-3), 3);
    checks.eq(pick(1), 20);
    checks.eq(pick(3), 4);
    checks.eq(count(), 3);
    return 0;
}
