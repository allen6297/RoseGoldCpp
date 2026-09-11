# expect: missing abstract method 'speak'
abstract class Animal {
    abstract fn speak(): String;
}

class Dog extends Animal {
    var hp: Int = 1;
}

fn main(): Int {
    return 0;
}
