trait Named {
    fn tag(self): String;
}

struct Person impl Named {
    n: String;

    fn tag(self): String {
        return n;
    }
}

fn label[T: Named](x: T): String {
    return x.tag();
}

fn main(): Int {
    print(label(Person { n: "ada" }));
    return 0;
}
