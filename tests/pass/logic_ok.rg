import checks;

fn boom(): Bool {
    return 1 / 0 == 1;
}

fn main(): Int {
    checks.that(true && true);
    checks.that(!(true && false));
    checks.that(!(false && true));
    checks.that(true || false);
    checks.that(false || true);
    checks.that(!(false || false));
    checks.that(1 < 2 && 3 > 2);
    checks.that(!(1 > 2 || 3 < 2));
    checks.that(1 && "x");
    checks.that(!("" && true));
    checks.that(0 || 1);
    checks.that(!(false && boom()));
    checks.that(true || boom());
    checks.that(true || false && false);
    checks.that(!(false || true && false));
    return 0;
}
