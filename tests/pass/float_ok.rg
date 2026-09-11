import checks;
import math;

fn main(): Int {
    checks.eq(1.5 + 1.5, 3);
    checks.eq(3.0 - 1.5, 1.5);
    checks.eq(1.5 * 2, 3);
    checks.eq(3.0 / 2.0, 1.5);
    checks.eq(1 + 0.5, 1.5);
    checks.eq(-1.5, 0.0 - 1.5);
    checks.that(1.5 > 1);
    checks.that(1 < 1.5);
    checks.that(1 == 1.0);
    checks.that(1.0 != 2);

    var x: Float = 0.5;
    x = x + 0.5;
    checks.eq(x, 1);

    match 1.5 {
        1.5 { checks.eq(1, 1); }
        _ { checks.eq(1, 0); }
    }

    checks.eq(math.abs(-3.0), 3);
    if 0.0 {
        checks.eq(1, 0);
    }
    checks.that(1.5);
    return 0;
}
