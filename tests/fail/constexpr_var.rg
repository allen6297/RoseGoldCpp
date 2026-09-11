# expect: var
@constexpr
fn bad(): Int {
    var x = 1;
    return x;
}

fn main(): Int {
    return 0;
}
