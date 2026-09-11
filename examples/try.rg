fn boom() throws: String {
    throw "nope";
}

fn ok() throws: Int {
    return 7;
}

fn main(): Int {
    do {
        print(try boom());
    } catch e {
        print(e);
    }
    print(try ok());
    return 0;
}
