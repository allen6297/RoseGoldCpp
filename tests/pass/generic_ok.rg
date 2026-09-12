fn identity[T](x: T): T {
    return x;
}

struct Box[T] {
    value: T;

    fn get(self): T {
        return value;
    }
}

fn first[T](xs: Array[T]): T {
    return xs[0];
}

@ufcs
fn head[T](xs: Array[T]): T {
    return xs[0];
}

fn main(): Int {
    print(identity(3));
    print(identity("hi"));
    var b = Box { value: 9 };
    print(b.get());
    print(Box[Int] { value: 4 }.value);
    print(first([10, 20]));
    print([7, 8].head());
    return 0;
}
