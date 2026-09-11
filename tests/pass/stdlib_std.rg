import std;
from std import UUID;

fn main(): Int {
    checks.eq(std.math.abs(-7), 7);
    checks.eq(std.math.sqrt(9.0), 3.0);
    checks.eq_string(std.str.upper("hi"), "HI");
    checks.that(!std.io.exists("._rg_std_missing.txt"));
    var v = Vec2 { x: 3.0, y: 4.0 };
    checks.eq(v.length(), 5.0);

    var n = std.nil();
    checks.that(n.is_nil());
    checks.eq_string(n.to_string(), "00000000-0000-0000-0000-000000000000");
    checks.eq_string(n.hex(), "00000000000000000000000000000000");
    checks.eq_string(n.id().value, n.value);

    var u = try std.parse("550e8400-E29B-41D4-A716-446655440000");
    checks.eq_string(u.to_string(), "550e8400-e29b-41d4-a716-446655440000");
    var w = try std.parse("550e8400e29b41d4a716446655440000");
    checks.that(u == w);
    checks.that(std.valid("550e8400-e29b-41d4-a716-446655440000"));
    checks.that(!std.valid("abc"));

    var a = std.v4();
    var b = std.v4();
    checks.eq(len(a.to_string()), 36);
    checks.eq_string(a.to_string()[14], "4");
    checks.that(a != b);

    do {
        try std.parse("nope");
        checks.eq(1, 0);
    } catch e {
        checks.that(len(e) > 0);
    }
    return 0;
}
