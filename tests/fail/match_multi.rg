# expect: missing variant
enum Color {
    Red,
    Green,
}

enum Shape {
    Circle(Int),
    Rect(width: Int, height: Int),
}

fn main(): Int {
    var c = Color.Red;
    match c {
        Red { pass; }
    }
    var s = Shape.Circle(1);
    switch s {
        Circle(r) { pass; }
    }
    return 0;
}
