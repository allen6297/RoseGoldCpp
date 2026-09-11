enum Color {
    Red,
    Green,
    Blue,
}

fn main(): Int {
    var c = Color.Red;
    match c {
        Red { print("red"); }
        Green { print("green"); }
        Blue { print("blue"); }
    }
    print(c == Color.Red);
    return 0;
}
