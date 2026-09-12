# expect: does not implement Holder
trait Holder[T] {
    fn get(self): T;
}

fn take[C: Holder[Int]](x: C): Int {
    return x.get();
}

fn main(): Int {
    print(take(1));
    return 0;
}
