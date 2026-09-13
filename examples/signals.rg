signal collected(amount: Int);

struct Coin {
    signal grabbed(amount: Int);

    fn grab(self, n: Int) {
        grabbed.emit(n);
    }
}

fn log_coin(amount: Int) {
    print(amount);
}

fn main(): Int {
    var add = fn (x: Int): Int { return x + 1; };
    print(add(2));
    print((fn (n: Int): Int { return n * 2; })(3));

    collected.connect(log_coin);
    collected.emit(5);
    collected.emit_deferred(9);

    var c = Coin {};
    c.grabbed.connect(fn (amount: Int) { print(amount); });
    c.grab(3);
    return 0;
}
