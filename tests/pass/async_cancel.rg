import std.time;

async fn main(): Int {
    var f = time.delay(1000);
    f.cancel();
    do {
        await f;
        return 1;
    } catch e {
        print(e);
    }
    var g = time.delay(5);
    await g;
    g.cancel();
    return 0;
}
