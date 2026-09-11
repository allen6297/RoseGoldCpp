signal collected(amount: Int);

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
    return 0;
}
