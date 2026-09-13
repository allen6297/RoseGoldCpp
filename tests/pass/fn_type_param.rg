import checks;

fn apply(f: fn (Int): Int, x: Int): Int {
    return f(x);
}

fn main(): Int {
    var r = apply(fn (n: Int): Int { return n * 2; }, 21);
    checks.eq(r, 42);
    var id: fn (Int): Int = fn (n: Int): Int { return n; };
    checks.eq(id(7), 7);
    return 0;
}
