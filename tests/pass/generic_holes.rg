trait Holder[T] {
    fn get(self): T;
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

impl[T] Holder[T] for Box[T] {
}

fn take[C: Holder[Int]](x: C): Int {
    return x.get();
}

class Slot[T] {
    var value: T;

    fn get(self): T {
        return value;
    }
}

class Packed[T] extends Slot[T] {
}

fn main(): Int {
    print(take(Box { value: 9 }));
    print(Box { value: 1 }.wrap[String]("ok"));
    print(Packed[Int] { value: 4 }.value);
    print(Packed { value: 5 }.get());
    print((fn [T](x: T): T { return x; })(3));
    return 0;
}
