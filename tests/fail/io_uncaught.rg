# expect: uncaught throw
import io;

fn main(): Int {
    try io.read_text("._rg_io_missing_xyz.txt");
    return 0;
}
