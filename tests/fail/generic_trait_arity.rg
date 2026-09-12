# expect: expected 1 type argument(s), got 2
trait Holder[T] {
    fn get(self): T;
}

struct Box impl Holder[Int, String] {
    n: Int;

    fn get(self): Int {
        return n;
    }
}

fn main(): Int {
    return 0;
}
