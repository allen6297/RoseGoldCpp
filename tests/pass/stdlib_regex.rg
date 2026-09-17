import regex;

fn main(): Int {
    checks.that(regex.valid("\\d+"));
    checks.that(!regex.valid("("));

    checks.that(regex.is_match("\\d+", "a42b"));
    checks.that(!regex.is_match("^\\d+$", "a42b"));

    checks.eq(regex.find("\\d+", "ab12cd"), 2);
    checks.eq(regex.find("z+", "ab12cd"), -1);
    checks.eq_string(regex.find_match("\\d+", "ab12cd"), "12");
    checks.eq_string(regex.find_match("z+", "ab12cd"), "");

    var caps = regex.captures("(\\w+)@(\\w+)", "hi user@host ok");
    checks.eq(len(caps), 3);
    checks.eq_string(caps[0], "user@host");
    checks.eq_string(caps[1], "user");
    checks.eq_string(caps[2], "host");
    checks.eq(len(regex.captures("z+", "none")), 0);

    var all = regex.findall("\\d+", "a1b22c333");
    checks.eq(len(all), 3);
    checks.eq_string(all[0], "1");
    checks.eq_string(all[1], "22");
    checks.eq_string(all[2], "333");

    checks.eq_string(regex.replace("\\d+", "x1y22", "#"), "x#y#");
    checks.eq_string(regex.replace("(\\w+)=(\\w+)", "a=1 b=2", "$2:$1"),
                     "1:a 2:b");

    var parts = regex.split("[,;]", "a,b;c");
    checks.eq(len(parts), 3);
    checks.eq_string(parts[0], "a");
    checks.eq_string(parts[1], "b");
    checks.eq_string(parts[2], "c");

    checks.that(std.regex.is_match("hi", "say hi"));
    return 0;
}
