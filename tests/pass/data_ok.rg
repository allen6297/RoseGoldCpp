import checks;

data Point {
    x: Int;
    y: Int;
    @optional
    z: Int;

    fn mag2(): Int {
        return x * x + y * y;
    }
}

trait Named {
    fn label(): String;
}

data Token impl Named {
    n: Int;

    fn label(): String {
        return "tok";
    }
}

fn main(): Int {
    var p = Point { x: 3, y: 4 };
    checks.eq(p.x, 3);
    checks.eq(p.y, 4);
    checks.eq(p.z, 0);
    checks.eq(p.mag2(), 25);
    var q = Point { x: 3, y: 4 };
    checks.that(p == q);
    var t = Token { n: 1 };
    checks.eq_string(t.label(), "tok");
    return 0;
}
