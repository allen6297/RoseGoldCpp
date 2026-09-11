data Point {
    x: Int;
    y: Int;

    fn mag2(): Int {
        return x * x + y * y;
    }
}

fn main(): Int {
    var p = Point { x: 3, y: 4 };
    print(p.x);
    print(p.mag2());
    var q = Point { x: 3, y: 4 };
    if p == q {
        print("eq");
    }
    return 0;
}
