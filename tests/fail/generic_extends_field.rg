# expect: cannot assign String to field 'value'
class Slot[T] {
    var value: T;
}

class Packed[T] extends Slot[T] {
}

fn main(): Int {
    Packed[Int] { value: "x" };
    return 0;
}
