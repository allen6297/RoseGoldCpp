# expect: invalid regex
import regex;

fn main(): Int {
    regex.is_match("(", "x");
    return 0;
}
