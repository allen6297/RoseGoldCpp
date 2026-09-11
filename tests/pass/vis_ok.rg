import checks;

class Animal {
    pub var hp: Int = 1;
    protected var armor: Int = 0;
    private var secret: Int = 9;

    pub fn hit(): Int {
        return hp + key();
    }

    protected fn soak(): Int {
        return armor;
    }

    private fn key(): Int {
        return secret;
    }
}

class Dog extends Animal {
    fn speak(): String {
        soak();
        return "woof";
    }
}

struct Point {
    pub x: Int;
    private y: Int;

    fn sum(): Int {
        return x + y;
    }
}

fn main(): Int {
    var d = Dog {};
    checks.eq(d.hp, 1);
    checks.eq(d.hit(), 10);
    checks.eq_string(d.speak(), "woof");
    var p = Point { x: 3, y: 4 };
    checks.eq(p.x, 3);
    checks.eq(p.sum(), 7);
    return 0;
}
