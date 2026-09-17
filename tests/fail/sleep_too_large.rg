# expect: time.sleep duration too large
import time;

fn main(): Int {
    time.sleep(60001);
    return 0;
}
