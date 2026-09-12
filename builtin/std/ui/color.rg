enum Color {
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,
    Rgb(r: Int, g: Int, b: Int),
}

fn rgb(r: Int, g: Int, b: Int): Color {
    return Color.Rgb(r, g, b);
}

@ufcs
fn value(c: Color): Int {
    match c {
        Black { return 0; }
        Red { return 255 * 65536; }
        Green { return 255 * 256; }
        Yellow { return 255 * 65536 + 255 * 256; }
        Blue { return 255; }
        Magenta { return 255 * 65536 + 255; }
        Cyan { return 255 * 256 + 255; }
        White { return 255 * 65536 + 255 * 256 + 255; }
        Rgb(r, g, b) { return r * 65536 + g * 256 + b; }
    }
    return 0;
}