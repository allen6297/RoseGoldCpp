# expect: Future has no method
import std.time;

async fn main(): Int {
    var f = time.delay(1);
    f.nope();
    return 0;
}
