import geo;

fn main(): Int {
    var p = Point { x: 3, y: 4 };
    checks.eq(p.mag2(), 25);
    return 0;
}
