# expect: missing variant
enum Shape {
    Circle(Int),
    Rect(width: Int, height: Int),
}

fn main(): Int {
    var s = Shape.Circle(1);
    match s {
        Circle(r) { pass; }
    }
    return 0;
}
