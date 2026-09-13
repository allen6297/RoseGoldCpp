# expect: expects Array of Future
async fn main(): Int {
    await Future.all([1, 2]);
    return 0;
}
