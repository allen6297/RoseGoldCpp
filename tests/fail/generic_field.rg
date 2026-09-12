# expect: cannot assign String to field 'value'
struct Box[T] {
    value: T;
}

fn main(): Int {
    var b = Box[Int] { value: "x" };
    return 0;
}
