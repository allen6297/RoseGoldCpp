import checks;

struct Point {
    x: Int;
    @optional
    y: Int;
}

class Enemy {
    var hp: Int = 10;
    @optional
    var name: String;
}

class Slime extends Enemy {
    @optional
    var goo: Int;
}

fn main(): Int {
    var p = Point { x: 3 };
    checks.eq(p.x, 3);
    checks.eq(p.y, 0);
    var e = Enemy {};
    checks.eq(e.hp, 10);
    checks.eq_string(e.name, "");
    var s = Slime { hp: 4 };
    checks.eq(s.hp, 4);
    checks.eq(s.goo, 0);
    checks.eq_string(s.name, "");
    return 0;
}
