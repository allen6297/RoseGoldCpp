import std.time;

async fn work(): Int {
    await time.delay(10);
    return 42;
}

async fn boom() throws: Int {
    throw "nope";
}

async fn main(): Int {
    print(await work());

    var xs = await Future.all([work(), work()]);
    print(xs[0]);
    print(xs[1]);

    var n = await Future.race([work(), work()]);
    print(n);

    var f = time.delay(1000);
    f.cancel();
    do {
        await f;
    } catch e {
        print(e);
    }

    do {
        await try boom();
    } catch e {
        print(e);
    }

    return 0;
}
