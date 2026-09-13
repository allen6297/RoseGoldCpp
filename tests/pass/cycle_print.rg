import checks;

fn main(): Int {
    var xs = [];
    xs.push(xs);
    print(xs);
    checks.that(xs == xs);

    var a = [];
    var b = [];
    a.push(b);
    b.push(a);
    print(a);
    checks.that(!(a == [1]));
    checks.that(a == a);
    return 0;
}
