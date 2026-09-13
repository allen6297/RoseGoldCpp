# expect: expected 1 args
fn apply(f: fn (Int): Int, x: Int): Int {
    return f();
}

fn main(): Int {
    return apply(fn (n: Int): Int { return n; }, 1);
}
