import checks;

fn sum(xs: Array[Int]): Int {
    var n = 0;
    for x in xs {
        n += x;
    }
    return n;
}

fn main(): Int {
    var xs = [1, 2, 3];
    var n: Int = xs[0];
    checks.eq(n, 1);
    xs.push(4);
    checks.eq(xs.pop(), 4);
    checks.eq(sum(xs), 6);
    var nest = [[1, 2], [3, 4]];
    checks.eq(nest[1][0], 3);
    var words: Array[String] = ["hi", "there"];
    checks.eq_string(words[1], "there");
    return 0;
}
