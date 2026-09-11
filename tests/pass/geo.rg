mod geo {
    struct Hidden {
        n: Int;
    }
    pub struct Point {
        x: Int;
        y: Int;
    }
    impl Point {
        fn mag2(self): Int {
            return self.x * self.x + self.y * self.y;
        }
    }
}
