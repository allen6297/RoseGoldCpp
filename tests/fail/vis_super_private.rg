# expect: method 'key' is private
class Animal {
    private fn key(): Int {
        return 9;
    }
}

class Dog extends Animal {
    fn speak(): Int {
        return super.key();
    }
}

fn main(): Int {
    var d = Dog {};
    return d.speak();
}
