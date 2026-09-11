mod outer {
    pub mod inner {
        fn helper(n: Int): Int {
            return n + 1;
        }
        pub fn add(a: Int, b: Int): Int {
            return helper(a) + b;
        }
    }
    pub fn wrap(): Int {
        return inner.add(2, 3);
    }
}

fn main(): Int {
    checks.eq(outer.wrap(), 6);
    checks.eq(outer.inner.add(2, 3), 6);
    return 0;
}
