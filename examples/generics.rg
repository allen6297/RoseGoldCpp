fn identity[T](x: T): T {
    return x;
}

struct Box[T] {
    value: T;

    fn get(self): T {
        return value;
    }

    fn wrap[U](self, x: U): U {
        return x;
    }
}

trait Holder[T] {
    fn get(self): T;
}

impl[T] Holder[T] for Box[T] {
}

fn first[T](xs: Array[T]): T {
    return xs[0];
}

class Slot[T] {
    var value: T;
}

class Packed[T] extends Slot[T] {
}

fn main(): Int {
    print(identity(3));
    print(identity[String]("hi"));
    var b = Box { value: 9 };
    print(b.get());
    var c = Box[String] { value: "ok" };
    print(c.value);
    print(first([1, 2]));
    print(b.wrap[String]("ok"));
    print(Packed { value: 4 }.value);
    print((fn [T](x: T): T { return x; })(3));
    return 0;
}
