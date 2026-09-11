mod outer {
    pub mod inner {
        import util;
        pub fn bump(n: Int): Int {
            return util.inc(n);
        }
    }
}

fn main(): Int {
    checks.eq(outer.inner.bump(1), 2);
    return 0;
}
