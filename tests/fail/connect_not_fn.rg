# expect: connect expects a function
signal ping();

fn main(): Int {
    ping.connect(1);
    return 0;
}
