import checks;

fn main(): Int {
    checks.that(1 <= 1);
    checks.that(1 <= 2);
    checks.that(!(2 <= 1));
    checks.that(2 >= 2);
    checks.that(2 >= 1);
    checks.that(!(1 >= 2));
    checks.that(1.5 >= 1);
    checks.that(1 <= 1.5);
    checks.that(1.0 <= 1);
    return 0;
}
