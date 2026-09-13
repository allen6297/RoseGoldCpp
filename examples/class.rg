abstract class Animal {
    pub var hp: Int = 1;
    protected var armor: Int = 0;
    abstract fn speak(): String;
    pub fn hit(): Int {
        return hp;
    }
    protected fn soak(): Int {
        return armor;
    }
}

class Dog extends Animal {
    fn speak(): String {
        soak();
        return "woof";
    }
}

final class Cat extends Animal {
    fn speak(): String {
        return "meow";
    }
}

class Enemy {
    var hp: Int = 10;
    @optional
    var name: String;
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
    print(s.hurt(4));
    print(s.name);
    var d = Dog {};
    print(d.speak());
    print(d.hit());
    print(d.hp);
    print(Cat {}.speak());
    var p = Point {};
    print(p.mag2());
    print(p.label());
    return 0;
}
