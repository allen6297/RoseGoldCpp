# expect: cannot assign to data field
data Point {
    x: Int;
    y: Int;
}

fn main(): Int {
    var p = Point { x: 3, y: 4 };
    p.x = 1;
    return 0;
}
