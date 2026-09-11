# expect: unknown field
struct Point {
    x: Int;
}

fn main(): Int {
    var p = Point { x: 1, z: 2 };
    return 0;
}
