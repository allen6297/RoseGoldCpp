# expect: requires 'try'
fn boom() throws {
    throw "x";
}

fn main(): Int {
    boom();
    return 0;
}
