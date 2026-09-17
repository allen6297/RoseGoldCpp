# expect: call stack overflow
fn boom(n: Int): Int {
    return boom(n + 1);
}

fn main(): Int {
    return boom(0);
}
