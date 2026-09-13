fn main(): Int {
    print(argv_len());
    /#
    HELLO
    #/
    var i = 0;
    while (i < argv_len()) {
        print(argv(i));
        i = i + 1;
    }
    return 0;
}
