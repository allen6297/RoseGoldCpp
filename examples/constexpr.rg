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

fn main(): Int {
    const n = add(2, 40);
    print(n);
    print(abs(-3));
    return 0;
}
