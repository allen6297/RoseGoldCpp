# expect: cannot assign to data field
data Point {
    x: Int;

    fn bump() {
        x = x + 1;
    }
}

fn main(): Int {
    var p = Point { x: 1 };
    p.bump();
    return 0;
}
