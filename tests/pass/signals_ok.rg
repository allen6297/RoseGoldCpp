signal ping();

struct Coin {
    signal grabbed(amount: Int);

    fn grab(self, n: Int) {
        grabbed.emit(n);
    }
}

class Base {
    signal hit();
    var n: Int = 0;
}

class Child extends Base {}

fn mark(amount: Int) {
    print(amount);
}

fn pinged() {
    print("pong");
}

fn main(): Int {
    ping.connect(pinged);
    ping.emit();
    ping.emit_deferred();

    var c = Coin {};
    c.grabbed.connect(mark);
    c.grab(5);

    var kid = Child {};
    kid.hit.connect(fn () { print("hit"); });
    kid.hit.emit();
    return 0;
}
