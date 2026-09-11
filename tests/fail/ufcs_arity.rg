# expect: expected 3 args
@ufcs
fn clamp(n: Int, lo: Int, hi: Int): Int {
    return n;
}

fn main(): Int {
    return 1.clamp(0);
}
