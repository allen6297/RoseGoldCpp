# expect: var
@constexpr
fn bad(): Int {
    var x = 1;
    print(x);
    return x;
}

fn main(): Int {
    return 0;
}
