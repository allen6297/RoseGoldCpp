class Animal {
    pub var hp: Int = 1;
    protected var armor: Int = 0;
    private var secret: Int = 9;

    pub fn hit(): Int {
        return hp;
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

fn main(): Int {
    var d = Dog {};
    print(d.hp);
    print(d.hit());
    print(d.speak());
    return 0;
}
