fn main(): Int {
    var scores = {"ada": 10, "grace": 12};
    scores["linus"] = 9;
    print(scores);
    print(scores["grace"]);
    print(len(scores));
    print(scores.has("ada"));
    print(scores.remove("ada"));
    print(scores.has("ada"));
    return 0;
}
