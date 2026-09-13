fn now(): Int {
    return __time.now();
}

fn sleep(ms: Int) {
    __time.sleep(ms);
}

async fn delay(ms: Int) {
    await __time.delay(ms);
}
