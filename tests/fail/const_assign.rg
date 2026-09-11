# expect: cannot assign to const
fn main(): Int {
    const n = 1;
    n = 2;
    return 0;
}
