# expect: str.repeat result too large
import str;

fn main(): Int {
    print(str.repeat("abcdefghij", 2000000));
    return 0;
}
