import checks;

enum Color {
    Red,
    Green,
    Blue,
}

fn main(): Int {
    var c = Color.Green;
    var got = "";
    match c {
        Color.Red { got = "r"; }
        _ { got = "other"; }
    }
    checks.eq_string(got, "other");
    switch c {
        Red { got = "red"; }
        Green { got = "green"; }
        Blue { got = "blue"; }
    }
    checks.eq_string(got, "green");
    return 0;
}
