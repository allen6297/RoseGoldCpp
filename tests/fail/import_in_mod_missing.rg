# expect: not found
mod m {
    import nope_inside_mod;
    pub fn x(): Int {
        return 1;
    }
}

fn main(): Int {
    return 0;
}
