# expect: has no variant
enum Color {
    Red,
}

fn main(): Int {
    var c = Color.Red;
    match c {
        Yellow { pass; }
        Red { pass; }
    }
    return 0;
}
