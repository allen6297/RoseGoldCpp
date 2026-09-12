# expect: missing variant
enum Color {
    Red,
    Green,
    Blue,
}

fn main(): Int {
    var c = Color.Red;
    match c {
        Red { pass; }
        Green { pass; }
    }
    return 0;
}
