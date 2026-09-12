import checks;
import std.time;

fn main(): Int {
    checks.that(time.now() > 0);
    time.sleep(0);
    checks.that(std.time.now() > 0);
    return 0;
}
