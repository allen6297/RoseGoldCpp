mod outer {
    pub mod inner {
        pub fn add(a: Int, b: Int): Int {
            return a + b;
        }
    }
}

fn main(): Int {
    print(outer.inner.add(2, 3));
    return 0;
}
