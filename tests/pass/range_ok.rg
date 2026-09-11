import checks;

fn main(): Int {
    var n = 0;
    for i in 0..5 {
        n = n + i;
    }
    checks.eq(n, 10);

    var m = 0;
    for i in 1..=3 {
        m = m + i;
    }
    checks.eq(m, 6);

    var empty = 0;
    for i in 5..2 {
        empty = empty + 1;
    }
    checks.eq(empty, 0);

    var one = 0;
    for i in 5..=5 {
        one = one + i;
    }
    checks.eq(one, 5);

    var none = 0;
    for i in 5..5 {
        none = none + 1;
    }
    checks.eq(none, 0);

    var r = 0..3;
    var s = 0;
    for i in r {
        s = s + i;
    }
    checks.eq(s, 3);
    checks.that(r == (0..3));

    var neg = 0;
    for i in -1..2 {
        neg = neg + i;
    }
    checks.eq(neg, 0);
    return 0;
}
