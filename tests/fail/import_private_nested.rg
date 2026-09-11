# expect: no export
import pack;
fn main(): Int {
    return pack.secret.id(1);
}
