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

    var parts = str.split("a,b,c", ",");
    checks.eq(len(parts), 3);
    checks.eq_string(parts[0], "a");
    checks.eq_string(parts[2], "c");
    checks.eq_string(str.replace("aa-bb-aa", "aa", "x"), "x-bb-x");
    checks.eq(str.find("hello", "ll"), 2);
    checks.eq(str.find("hello", "z"), -1);
    return 0;
}
