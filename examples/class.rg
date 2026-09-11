class Enemy {
    var hp: Int = 10;

    fn hurt(dmg: Int): Int {
        hp = hp - dmg;
        return hp;
    }
}

class Slime extends Enemy {
    var goo: Int = 1;

    fn hurt(dmg: Int): Int {
        return super.hurt(dmg / 2);
    }
}

trait Named {
    fn label(): String;
}

class Point impl Named {
    var x: Int = 3;
    var y: Int = 4;

    fn mag2(): Int {
        return x * x + y * y;
    }

    fn label(): String {
        return "point";
    }
}

fn main(): Int {
    var s = Slime {};
    print(s.hp);
    print(s.goo);
    print(s.hurt(4));
    var p = Point {};
    print(p.mag2());
    print(p.label());
    return 0;
}
