mod pack {
    mod secret {
        pub fn id(n: Int): Int {
            return n;
        }
    }
    pub fn wrap(): Int {
        return secret.id(1);
    }
}
