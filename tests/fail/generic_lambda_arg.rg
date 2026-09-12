# expect: cannot pass String to '<fn>', expected Int
fn main(): Int {
    print((fn [T](x: T): T { return x; })[Int]("no"));
    return 0;
}
