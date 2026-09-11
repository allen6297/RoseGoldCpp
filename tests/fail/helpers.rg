mod utils {
    fn helper(n: Int): Int {
        return n + 1;
    }
    pub fn add(a: Int, b: Int): Int {
        return helper(a) + b;
    }
}
