# expect: uncaught throw
async fn boom() throws: Int {
    throw "nope";
}

async fn main(): Int {
    await try boom();
    return 0;
}
