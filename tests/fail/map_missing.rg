# expect: key 'missing' not found
fn main(): Int {
    var m = {"ada": 1};
    print(m["missing"]);
    return 0;
}
