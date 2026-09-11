abstract class Animal {
    var hp: Int = 1;
    abstract fn speak(): String;
    fn hit(): Int {
        return hp;
    }
}

class Dog extends Animal {
    fn speak(): String {
        return "woof";
    }
}

final class Cat extends Animal {
    fn speak(): String {
        return "meow";
    }
}

fn main(): Int {
    var d = Dog {};
    print(d.speak());
    print(d.hit());
    var c = Cat {};
    print(c.speak());
    return 0;
}
