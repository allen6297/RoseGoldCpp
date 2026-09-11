fn abs(n: Int): Int {
    if n < 0 {
        return -n;
    }
    return n;
}

fn sign(n: Int): Int {
    if n > 0 {
        return 1;
    }
    if n < 0 {
        return -1;
    }
    return 0;
}

fn min(a: Int, b: Int): Int {
    if a < b {
        return a;
    }
    return b;
}

fn max(a: Int, b: Int): Int {
    if a > b {
        return a;
    }
    return b;
}

fn clamp(v: Int, lo: Int, hi: Int): Int {
    if v < lo {
        return lo;
    }
    if v > hi {
        return hi;
    }
    return v;
}

fn gcd(a: Int, b: Int): Int {
    var x = a;
    var y = b;
    if x < 0 {
        x = -x;
    }
    if y < 0 {
        y = -y;
    }
    while y != 0 {
        var t = y;
        y = x % y;
        x = t;
    }
    return x;
}

fn pow(a: Int, b: Int): Int {
    return __math.pow(a, b);
}

fn rand_int(n: Int): Int {
    return __math.rand_int(n);
}
