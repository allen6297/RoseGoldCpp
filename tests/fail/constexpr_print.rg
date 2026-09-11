# expect: print
@constexpr
fn bad() {
    print(1);
}

fn main(): Int {
    return 0;
}
