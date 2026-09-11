import checks;

fn add(a: Int, b: Int): Int {
    return a + b;
}

fn main(): Int {
    checks.eq(add(2, 40), 42);
    checks.eq_string("a" + "b", "ab");
    checks.eq(1 + 2.5, 3.5);
    var n: Int = 1;
    n = 2;
    const m: Int = add(1, 1);
    checks.eq(m, 2);
    checks.eq(n, 2);
    return 0;
}
