# expect: requires 'try'
import io;

fn main(): Int {
    io.read_text("nope.txt");
    return 0;
}
