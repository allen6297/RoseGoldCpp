# expect: cannot pass String
@ufcs
fn doubled(n: Int): Int {
    return n * 2;
}

fn main(): Int {
    return "x".doubled();
}
