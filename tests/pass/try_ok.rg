import checks;

fn boom() throws: String {
    throw "nope";
}

fn ok() throws: Int {
    return 7;
}

fn main(): Int {
    do {
        checks.eq(try ok(), 7);
        try boom();
        checks.eq(1, 0);
    } catch e {
        checks.eq_string(e, "nope");
    }
    return 0;
}
