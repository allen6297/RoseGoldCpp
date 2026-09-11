import checks;

fn main(): Int {
    var xs = [1, 2, 3];
    checks.eq(xs[0], 1);
    checks.eq(len(xs), 3);
    checks.eq(xs.len(), 3);
    checks.eq(xs.len, 3);

    xs[1] = 9;
    checks.eq(xs[1], 9);

    xs.push(4);
    checks.eq(len(xs), 4);
    checks.eq(xs.pop(), 4);
    checks.eq(len(xs), 3);

    var empty = [];
    checks.eq(len(empty), 0);

    var nest = [[1, 2], [3, 4]];
    checks.eq(nest[1][0], 3);
    nest[1][0] = 8;
    checks.eq(nest[1][0], 8);

    var t = xs;
    t.push(7);
    checks.eq(len(xs), 4);

    checks.eq_string("hi"[0], "h");
    checks.eq(len("ab"), 2);
    checks.that([1, 2] == [1, 2]);
    checks.that([1, 2] != [1, 3]);
    return 0;
}
