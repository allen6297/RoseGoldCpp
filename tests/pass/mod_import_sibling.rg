mod left {
    import right;
    pub fn go(): Int {
        return right.x();
    }
}

mod right {
    pub fn x(): Int {
        return 7;
    }
}

fn main(): Int {
    checks.eq(left.go(), 7);
    return 0;
}
