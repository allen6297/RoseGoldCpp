fn main(): Int {
    print(argv_len());
    var i = 0;
    while i < argv_len() {
        print(argv(i));
        i = i + 1;
    }
    return 0;
}
