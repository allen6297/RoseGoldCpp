# expect: does not implement Named
trait Named {
    fn tag(self): String;
}

fn label[T: Named](x: T): String {
    return x.tag();
}

fn main(): Int {
    print(label(1));
    return 0;
}
