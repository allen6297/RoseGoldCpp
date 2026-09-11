# expect: method 'key' is private
class Animal {
    private fn key(): Int {
        return 9;
    }
}

fn main(): Int {
    var a = Animal {};
    return a.key();
}
