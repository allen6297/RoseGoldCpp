# expect: cannot pass Int to 'label', expected Named
trait Named {
    fn tag(self): String;
}

fn label(x: Named): String {
    return x.tag();
}

fn main(): Int {
    print(label(1));
    return 0;
}
