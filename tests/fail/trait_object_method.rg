# expect: trait Named has no method
trait Named {
    fn tag(self): String;
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

fn main(): Int {
    var r: Named = Robot { n: "r2" };
    print(r.watts());
    return 0;
}
