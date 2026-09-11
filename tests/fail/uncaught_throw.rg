# expect: uncaught throw
fn boom() throws {
    throw "x";
}

fn main(): Int {
    try boom();
    return 0;
}
