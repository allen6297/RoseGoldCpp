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

fn total_coin(amount: Int) {
    print(amount);
}

fn main(): Int {
    collected.connect(log_coin);
    collected.connect(total_coin);
    collected.emit(5);
    collected.emit_deferred(9);

    var c = Coin {};
    c.grabbed.connect(fn (amount: Int) { print(amount); });
    c.grab(3);
    return 0;
}
