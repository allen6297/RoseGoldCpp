# expect: cycle in class inheritance
class A extends B {
    var n: Int = 1;
}
class B extends A {
    var m: Int = 2;
}

fn main(): Int {
    return 0;
}
