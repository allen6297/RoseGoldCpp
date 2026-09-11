# expect: cannot override final method
class Animal {
    final fn hit(): Int {
        return 1;
    }
}

class Dog extends Animal {
    fn hit(): Int {
        return 2;
    }
}

fn main(): Int {
    return 0;
}
