# expect: trait Named has no field
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
    var p: Named = Person { n: "ada" };
    print(p.n);
    return 0;
}
