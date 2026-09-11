# expect: cannot extend data type
data Point {
    x: Int;
}

class Dot extends Point {
    var y: Int = 0;
}

fn main(): Int {
    return 0;
}
