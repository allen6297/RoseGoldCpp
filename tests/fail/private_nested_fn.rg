# expect: no export
mod outer {
    pub mod inner {
        fn helper(n: Int): Int {
            return n;
        }
        pub fn add(a: Int, b: Int): Int {
            return a + b;
        }
    }
}

fn main(): Int {
    return outer.inner.helper(1);
}
