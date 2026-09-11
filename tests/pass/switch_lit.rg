fn main(): Int {
    var n = 2;
    switch n {
        1 { print("one"); }
        2 { print("two"); }
        _ { print("other"); }
    }
    match "hi" {
        "no" { print("no"); }
        "hi" { print("yes"); }
        _ { print("miss"); }
    }
    return 0;
}
