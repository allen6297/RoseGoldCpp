# expect: cannot call abstract method
abstract class Animal {
    abstract fn speak(): String;
}

class Dog extends Animal {
    fn speak(): String {
        return super.speak();
    }
}

fn main(): Int {
    var d = Dog {};
    print(d.speak());
    return 0;
}
