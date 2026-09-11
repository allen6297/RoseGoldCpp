# expect: no export
import pack.secret;
fn main(): Int {
    return secret.id(1);
}
