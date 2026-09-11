# expect: missing field
struct Point {
    x: Int;
    y: Int;
}

fn main(): Int {
    var p = Point { x: 1 };
    return 0;
}
