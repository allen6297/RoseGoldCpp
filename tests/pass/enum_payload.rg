enum Shape {
    Circle(Int),
    Rect(width: Int, height: Int),
}

fn main(): Int {
    var s = Shape.Circle(5);
    match s {
        Circle(r) { print(r); }
        Rect(w, h) { print(w); }
    }
    var t = Shape.Rect(2, 3);
    match t {
        Circle(r) { print(r); }
        Rect(width: w, height: h) { print(w); print(h); }
    }
    return 0;
}
