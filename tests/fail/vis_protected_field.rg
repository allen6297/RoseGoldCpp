# expect: field 'armor' is protected
class Animal {
    protected var armor: Int = 0;
}

fn main(): Int {
    var a = Animal {};
    return a.armor;
}
