# expect: takes 1 argument
enum Shape {
    Circle(Int),
}

fn main(): Int {
    var s = Shape.Circle();
    return 0;
}
