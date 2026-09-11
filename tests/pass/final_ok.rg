import checks;

class Animal {
    var hp: Int = 1;
    final fn hit(): Int {
        return hp;
    }
}

class Dog extends Animal {
    fn speak(): String {
        return "woof";
    }
}

final class Cat {
    var lives: Int = 9;
}

fn main(): Int {
    var d = Dog {};
    checks.eq(d.hit(), 1);
    checks.eq_string(d.speak(), "woof");
    var c = Cat {};
    checks.eq(c.lives, 9);
    return 0;
}
