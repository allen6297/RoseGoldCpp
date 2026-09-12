# expect: can only call a function
fn main(): Int {
    var n = 1;
    n();
    return 0;
}
