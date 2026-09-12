# expect: requires 'throws'
fn boom() throws: String {
    throw "x";
}

fn wrap(): String {
    return try boom();
}

fn main(): Int {
    return 0;
}
