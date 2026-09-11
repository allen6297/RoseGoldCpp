import str;

fn main(): Int {
    checks.that(str.contains("hello", "ell"));
    checks.that(str.starts_with("hello", "he"));
    checks.that(str.ends_with("hello", "lo"));
    checks.eq(str.length("hi"), 2);
    checks.that(str.is_empty(""));
    checks.eq_string(str.repeat("ab", 3), "ababab");
    checks.eq_string(str.upper("Hi"), "HI");
    checks.eq_string(str.lower("Hi"), "hi");
    checks.eq_string(str.trim("  x  "), "x");
    checks.eq_string(str.slice("hello", 1, 4), "ell");
    return 0;
}
