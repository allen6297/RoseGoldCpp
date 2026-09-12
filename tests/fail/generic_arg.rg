# expect: cannot pass String to 'identity', expected Int
fn identity[T](x: T): T {
    return x;
}

fn main(): Int {
    identity[Int]("no");
    return 0;
}
