# expect: cannot pass Array to
fn take(xs: Array[Int]): Int {
    return len(xs);
}

fn main(): Int {
    var mixed = [1, "a"];
    return take(mixed);
}
