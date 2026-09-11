# expect: has no method
struct Point {
    x: Int;
}

fn main(): Int {
    var p = Point { x: 1 };
    p.go();
    return 0;
}
