mod incwrap {
    import util;
    pub fn bump(n: Int): Int {
        return util.inc(n);
    }
}

fn main(): Int {
    checks.eq(incwrap.bump(1), 2);
    return 0;
}
