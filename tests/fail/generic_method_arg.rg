# expect: cannot pass Int to 'wrap', expected String
struct Box {
    n: Int;

    fn wrap[U](self, x: U): U {
        return x;
    }
}

fn main(): Int {
    Box { n: 1 }.wrap[String](1);
    return 0;
}
