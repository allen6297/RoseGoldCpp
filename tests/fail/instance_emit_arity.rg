# expect: expected 1 argument(s), got 0
struct Coin {
    signal grabbed(amount: Int);
}

fn take(amount: Int) {
    pass;
}

fn main(): Int {
    var c = Coin {};
    c.grabbed.connect(take);
    c.grabbed.emit();
    return 0;
}
