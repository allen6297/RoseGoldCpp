# expect: throw outside throwing function
fn main(): Int {
    throw "x";
    return 0;
}
