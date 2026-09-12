# expect: expected 0 parameter(s)
signal ping();

fn main(): Int {
    ping.connect(fn (n: Int) { pass; });
    return 0;
}
