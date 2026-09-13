# expect: can only await Future
async fn main(): Int {
    await 1;
    return 0;
}
