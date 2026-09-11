import checks;

fn main(): Int {
    var sum = 0;
    for x in [1, 2, 3] {
        sum = sum + x;
    }
    checks.eq(sum, 6);

    var keys = "";
    for k in {"a": 1, "b": 2} {
        keys = keys + k;
    }
    checks.eq_string(keys, "ab");

    var chars = "";
    for c in "hi" {
        chars = chars + c;
    }
    checks.eq_string(chars, "hi");

    var n = 0;
    for i in 4 {
        n = n + i;
    }
    checks.eq(n, 6);

    var saw = 0;
    for x in [1, 2, 3, 4] {
        if x == 2 {
            continue;
        }
        if x == 4 {
            break;
        }
        saw = saw + x;
    }
    checks.eq(saw, 4);

    var x = 9;
    for x in [1] {
        checks.eq(x, 1);
    }
    checks.eq(x, 9);
    return 0;
}
