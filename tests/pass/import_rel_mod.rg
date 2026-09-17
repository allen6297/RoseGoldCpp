import relpkg;
import checks;

fn main(): Int {
    checks.eq(relpkg.use_helper(), 7);
    return 0;
}
