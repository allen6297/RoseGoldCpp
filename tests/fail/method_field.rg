# expect: conflicts with field
struct Point {
    x: Int;

    fn x(self) {
        pass;
    }
}

fn main(): Int {
    return 0;
}
