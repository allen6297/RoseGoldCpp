import std;
from vec import Vec2;

fn main(): Int {
    print(std.math.abs(-7));
    print(std.math.sqrt(9.0));
    print(std.str.upper("hi"));
    print(std.str.split("a,b", ",")[0]);
    print(std.io.exists("README.md"));
    print(std.nil().to_string());
    var v = Vec2 { x: 3.0, y: 4.0 };
    print(v.length());
    return 0;
}
