# expect: requires 'try'
import std;

fn main(): Int {
    std.parse("nope");
    return 0;
}
