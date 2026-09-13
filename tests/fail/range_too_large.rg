# expect: range too large
fn main(): Int {
    for i in 1000001 {
        print(i);
    }
    return 0;
}
