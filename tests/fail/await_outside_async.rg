# expect: 'await' is only allowed in async functions
import std.time;

fn main(): Int {
    await time.delay(1);
    return 0;
}
