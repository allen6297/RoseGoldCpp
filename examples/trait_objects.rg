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
}

fn label(x: Named): String {
    return x.tag();
}

fn main(): Int {
    var p: Named = Person { n: "ada" };
    print(p.tag());
    print(label(Robot { n: "r2" }));
    var xs: Array[Named] = [Person { n: "a" }, Robot { n: "b" }];
    print(xs[0].tag());
    print(xs[1].tag());
    return 0;
}
