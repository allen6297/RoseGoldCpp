import checks;

async fn boom() throws: Int {
    throw "nope";
}

async fn ok(): Int {
    return 7;
}

async fn main(): Int {
    do {
        checks.eq(await ok(), 7);
        await try boom();
        checks.eq(1, 0);
    } catch e {
        checks.eq_string(e, "nope");
    }
    do {
        await Future.all([try boom(), ok()]);
        checks.eq(1, 0);
    } catch e {
        checks.eq_string(e, "nope");
    }
    return 0;
}
