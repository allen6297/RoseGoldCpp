# expect: cannot pass Int to Array[Named]
trait Named {
    fn tag(self): String;
}

struct Person impl Named {
    n: String;

    fn tag(self): String {
        return n;
    }
}

fn main(): Int {
    var xs: Array[Named] = [Person { n: "a" }, 1];
    print(xs[0].tag());
    return 0;
}
