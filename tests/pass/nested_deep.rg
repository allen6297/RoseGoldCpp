mod a {
    pub mod b {
        pub mod c {
            pub fn id(n: Int): Int {
                return n;
            }
        }
    }
}

fn main(): Int {
    checks.eq(a.b.c.id(7), 7);
    return 0;
}
