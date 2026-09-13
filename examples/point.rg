struct Point {
    x: Int;
    @optional
    y: Int;

    fn mag2(): Int {
        return x * x + y * y;
    }
}

data Size {
    w: Int;
    h: Int;
}

fn main(): Int {
    var p = Point { x: 3 };
    print(p.x);
    print(p.y);
    p.y = 4;
    print(p.mag2());

    var a = Size { w: 3, h: 4 };
    var b = Size { w: 3, h: 4 };
    if a == b {
        print("eq");
    }
    return 0;
}
