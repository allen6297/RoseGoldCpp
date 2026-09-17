# expect: path outside sandbox
import io;

fn main(): Int {
    print(io.exists("../../../../../../../../etc/passwd"));
    return 0;
}
