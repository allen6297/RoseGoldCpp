fn main(): Int {
    if false && (1 / 0 == 1) {
        print("and-bad");
    }
    if true || (1 / 0 == 1) {
        print("or-ok");
    }
    print(true && false);
    print(true || false);
    return 0;
}
