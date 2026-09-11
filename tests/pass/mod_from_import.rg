mod incfrom {
    from util import inc;
    pub fn bump(n: Int): Int {
        return inc(n);
    }
}

fn main(): Int {
    checks.eq(incfrom.bump(4), 5);
    return 0;
}
