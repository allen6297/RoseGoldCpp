# expect: integer overflow
fn main(): Int {
    var n = -9223372036854775807;
    n = n - 1;
    print(n / -1);
    return 0;
}
