import checks;
import std.path;

fn main(): Int {
    checks.eq_string(path.join("a", "b"), "a/b");
    checks.eq_string(path.parent("a/b/c.txt"), "a/b");
    checks.eq_string(path.stem("a/b/c.txt"), "c");
    checks.eq_string(std.path.stem("n.rg"), "n");
    return 0;
}
