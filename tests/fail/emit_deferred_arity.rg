# expect: expected 1 argument(s), got 0
signal ping(n: Int);

fn take(n: Int) {
    pass;
}

fn main(): Int {
    ping.connect(take);
    ping.emit_deferred();
    return 0;
}
