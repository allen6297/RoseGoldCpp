import checks;

enum Shape {
    Circle(Int),
    Rect(width: Int, height: Int),
}

fn main(): Int {
    var s = Shape.Rect(2, 3);
    var w = 0;
    var h = 0;
    match s {
        Circle(r) { w = r; }
        Rect(dims) {
            checks.eq(len(dims), 2);
            w = dims[0];
            h = dims[1];
        }
    }
    checks.eq(w, 2);
    checks.eq(h, 3);
    return 0;
}
