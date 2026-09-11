# expect: protected cannot apply to struct field
struct Point {
    protected x: Int;
}

fn main(): Int {
    return 0;
}
