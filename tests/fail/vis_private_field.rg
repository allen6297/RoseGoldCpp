# expect: field 'secret' is private
class Animal {
    private var secret: Int = 9;
}

fn main(): Int {
    var a = Animal {};
    return a.secret;
}
