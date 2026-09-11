# expect: no export
mod outer {
    mod inner {
        pub fn add(a: Int, b: Int): Int {
            return a + b;
        }
    }
    pub fn wrap(): Int {
        return inner.add(1, 2);
    }
}

fn main(): Int {
    return outer.inner.add(1, 2);
}
