# expect: duplicate field
struct Point {
    x: Int;
    x: Int;
}

fn main(): Int {
    return 0;
}
