import std.time;

async fn left(): Int {
    await time.delay(5);
    return 1;
}

async fn right(): Int {
    await time.delay(5);
    return 2;
}

async fn main(): Int {
    var xs = await Future.all([left(), right()]);
    print(xs[0]);
    print(xs[1]);
    var empty = await Future.all([]);
    print(len(empty));
    return 0;
}
