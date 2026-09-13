 /// Adds two integers.
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

/#
  Block comment.
#/

fn main(): Int {
    // line comment
    var x = 2;
    const n = add(x, 40);
    print(n);
    print(abs(-3));
    return 0;
}
