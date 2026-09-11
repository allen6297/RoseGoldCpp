# expect: cannot be both abstract and final
abstract final class Animal {
    var hp: Int = 1;
}

fn main(): Int {
    return 0;
}
