fn sign(n: Int): Int {
    if n > 0 {
        return 1;
    } elif n < 0 {
        return -1;
    } else {
        return 0;
    }
}

fn main(): Int {
    print(sign(4));
    print(sign(-2));
    print(sign(0));

    if false && (1 / 0 == 1) {
        print("and-bad");
    }
    if true || (1 / 0 == 1) {
        print("or-ok");
    }
    print(true && false);

    var i = 0;
    var s = 0;
    while i < 4 {
        s = s + i;
        i = i + 1;
    }
    print(s);

    var sum = 0;
    for x in [1, 2, 3] {
        sum = sum + x;
    }
    print(sum);
    for i in 0..3 {
        print(i);
    }
    for i in 1..=2 {
        print(i);
    }
    return 0;
}
