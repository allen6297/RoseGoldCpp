# expect: not constexpr
fn add(a: Int, b: Int): Int {
    return a + b;
}

@constexpr
fn twice(n: Int): Int {
    return add(n, n);
}

fn main(): Int {
    return 0;
}
