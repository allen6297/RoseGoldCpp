# expect: @optional cannot apply to method
struct Point {
    x: Int;
    @optional
    fn mag(): Int {
        return x;
    }
}

fn main(): Int {
    return 0;
}
