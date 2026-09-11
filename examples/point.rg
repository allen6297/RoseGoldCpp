struct Point {
    x: Int;
    y: Int;

    fn mag2(): Int {
        return x * x + y * y;
    }
}

fn main(): Int {
    var p = Point { x: 3, y: 4 };
    print(p);
    print(p.x);
    p.y = 5;
    print(p.y);
    print(p.mag2());
    return 0;
}
