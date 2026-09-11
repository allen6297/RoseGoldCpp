# expect: @optional cannot apply to function
@optional
fn bad(): Int {
    return 0;
}

fn main(): Int {
    return 0;
}
