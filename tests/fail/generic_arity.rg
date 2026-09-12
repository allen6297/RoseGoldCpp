# expect: expected 1 type argument(s), got 2
fn identity[T](x: T): T {
    return x;
}

fn main(): Int {
    identity[Int, String](1);
    return 0;
}
