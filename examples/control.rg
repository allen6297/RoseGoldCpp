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

    var i = 0;
    var s = 0;
    while i < 4 {
        s = s + i;
        i = i + 1;
    }
    print(s);
    return 0;
}
