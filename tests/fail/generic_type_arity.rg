# expect: expected 1 type argument(s), got 2
struct Box[T] {
    value: T;
}

fn main(): Int {
    var b: Box[Int, String] = Box { value: 1 };
    print(b.value);
    return 0;
}
