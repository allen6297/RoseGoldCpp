fn main(): Int {
    print(1.5 + 2.5);
    print(3 / 2.0);

    var n = 1;
    n += 2;
    print(n);

    var xs = [1, 2, 3];
    print(xs[0]);
    xs.push(4);
    xs[1] = 9;
    xs[0] += 3;
    print(len(xs));
    print(xs.pop());
    print("hi"[0]);

    var scores = {"ada": 10, "grace": 12};
    scores["linus"] = 9;
    print(scores["grace"]);
    print(scores.has("ada"));
    print(len(scores));
    return 0;
}
