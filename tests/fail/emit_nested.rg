# expect: signal emit nested too deeply
signal ping();

fn on_ping() {
    ping.emit();
}

fn main(): Int {
    ping.connect(on_ping);
    ping.emit();
    return 0;
}
