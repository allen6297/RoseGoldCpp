mod colors {
    pub enum Color {
        Red,
        Blue,
    }
}

from colors import Color;

fn main(): Int {
    var c = Color.Blue;
    match c {
        Red { print("red"); }
        Blue { print("blue"); }
    }
    return 0;
}
