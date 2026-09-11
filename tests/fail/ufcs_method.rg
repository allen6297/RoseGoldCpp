# expect: @ufcs cannot apply to method
struct Point {
    x: Int;
}

impl Point {
    @ufcs
    fn go() {
        pass;
    }
}

fn main(): Int {
    return 0;
}
