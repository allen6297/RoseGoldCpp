import math;

fn main(): Int {
    checks.eq(math.abs(-7), 7);
    checks.eq(math.sign(-2), -1);
    checks.eq(math.min(3, 8), 3);
    checks.eq(math.max(3, 8), 8);
    checks.eq(math.clamp(5, 0, 3), 3);
    checks.eq(math.pow(2, 10), 1024);
    checks.eq(math.gcd(48, 18), 6);
    var r = math.rand_int(10);
    checks.that(r < 10);
    checks.that(r > -1);
    return 0;
}
