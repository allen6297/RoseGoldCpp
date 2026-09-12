trait Named {
    fn tag(self): String;
}

struct Person impl Named {
    n: String;

    fn tag(self): String {
        return n;
    }
}

struct Robot impl Named {
    n: String;

    fn tag(self): String {
        return n;
    }

    fn watts(self): Int {
        return 40;
    }
}

trait Holder[T] {
    fn get(self): T;
}

struct Box[T] impl Holder[T] {
    value: T;

    fn get(self): T {
        return value;
    }
}

fn label(x: Named): String {
    return x.tag();
}

fn first_named(xs: Array[Named]): String {
    return xs[0].tag();
}

fn unbox(x: Holder[Int]): Int {
    return x.get();
}

fn main(): Int {
    var p: Named = Person { n: "ada" };
    print(p.tag());
    print(label(Robot { n: "r2" }));
    var xs: Array[Named] = [Person { n: "a" }, Robot { n: "b" }];
    print(first_named(xs));
    print(unbox(Box { value: 9 }));
    return 0;
}
