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

    checks.eq(math.sin(0.0), 0.0);
    checks.eq(math.cos(0.0), 1.0);
    checks.eq(math.atan2(0.0, 1.0), 0.0);
    checks.eq(math.sqrt(9.0), 3.0);
    checks.eq(math.powf(2.0, 3.0), 8.0);
    checks.eq(math.to_int(3.9), 3);
    checks.eq(math.to_float(3), 3.0);
    checks.eq(math.floor(3.9), 3.0);
    checks.eq(math.ceil(3.1), 4.0);
    checks.eq(math.lerp(0.0, 10.0, 0.5), 5.0);
    checks.eq(math.move_toward(0.0, 10.0, 3.0), 3.0);
    checks.eq(math.move_toward(8.0, 10.0, 3.0), 10.0);
    var n = math.random();
    checks.that(n >= 0.0);
    checks.that(n <= 1.0);
    return 0;
}
