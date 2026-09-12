# expect: data cannot declare signals
data Point {
    signal hit();
    x: Int;
}

fn main(): Int {
    return 0;
}
