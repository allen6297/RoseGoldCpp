# expect: cannot construct abstract class
abstract class Animal {
    var hp: Int = 1;
}

fn main(): Int {
    var a = Animal {};
    return 0;
}
