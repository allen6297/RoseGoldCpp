import checks;

abstract class Animal {
    var hp: Int = 1;
    abstract fn speak(): String;
    fn greet(): String {
        return speak();
    }
}

abstract class Pet extends Animal {
    fn hit(): Int {
        return hp + 1;
    }
}

class Dog extends Pet {
    fn speak(): String {
        return "woof";
    }
}

fn main(): Int {
    var d = Dog {};
    checks.eq(d.hp, 1);
    checks.eq_string(d.speak(), "woof");
    checks.eq_string(d.greet(), "woof");
    checks.eq(d.hit(), 2);
    return 0;
}
