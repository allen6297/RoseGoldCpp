struct Box {
    n: Int;
}

fn main(): Int {
    var add = fn (x: Int): Int { return x + 1; };
    print(add(2));
    print((fn (n: Int): Int { return n * 2; })(3));
    var box = Box { n: 1 };
    var read = fn (): Int { return box.n; };
    box.n = 9;
    print(read());
    return 0;
}
