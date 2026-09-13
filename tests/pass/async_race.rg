import std.time;

async fn slow(): Int {
    await time.delay(40);
    return 1;
}

async fn fast(): Int {
    await time.delay(5);
    return 2;
}

async fn main(): Int {
    var n = await Future.race([slow(), fast()]);
    print(n);
    return 0;
}
