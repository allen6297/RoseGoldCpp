import calc;
from calc import double as twice;

mod outer {
    pub mod inner {
        pub fn add(a: Int, b: Int): Int {
            return a + b;
        }
    }
}

fn main(): Int {
    print(calc.add(2, 3));
    print(twice(4));
    print(outer.inner.add(2, 3));
    return 0;
}
