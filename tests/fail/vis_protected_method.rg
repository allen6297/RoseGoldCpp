# expect: method 'soak' is protected
class Animal {
    protected fn soak(): Int {
        return 0;
    }
}

fn main(): Int {
    var a = Animal {};
    return a.soak();
}
