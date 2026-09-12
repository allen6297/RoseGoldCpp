# expect: constexpr function cannot use closures
@constexpr
fn bad(): Int {
    const f = fn (): Int { return 1; };
    return 1;
}

fn main(): Int {
    return bad();
}
