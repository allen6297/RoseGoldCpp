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
    print(p.x);
    print(p.y);
    var e = Enemy {};
    print(e.hp);
    print(e.name);
    var s = Slime { hp: 4 };
    print(s.goo);
    return 0;
}
