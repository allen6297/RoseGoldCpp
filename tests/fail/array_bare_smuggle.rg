# expect: but initializer looks like Array
fn main(): Int {
    var bare = [];
    bare.push("a");
    var xs: Array[Int] = bare;
    return 0;
}
