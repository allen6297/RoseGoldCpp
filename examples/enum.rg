enum Color {
    Red,
    Green,
    Blue,
}

enum Shape {
    Circle(Int),
    Rect(width: Int, height: Int),
}

fn main(): Int {
    var c = Color.Green;
    match c {
        Red { print("red"); }
        Green { print("green"); }
        Blue { print("blue"); }
    }

    var n = 2;
    switch n {
        1 { print("one"); }
        2 { print("two"); }
        _ { print("other"); }
    }

    var s = Shape.Rect(2, 3);
    match s {
        Circle(r) { print(r); }
        Rect(w, h) { print(w); print(h); }
    }
    return 0;
}
