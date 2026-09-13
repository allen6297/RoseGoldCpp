import std.time;

async fn inner(): Int {
    await time.delay(5);
    return 7;
}

async fn main(): Int {
    var n = await inner();
    print(n);
    return 0;
}
