import checks;

fn main(): Int {
    var scores: Map<String, Int> = {"ada": 10, "grace": 12};
    checks.eq(scores["ada"], 10);
    checks.eq(len(scores), 2);
    checks.eq(scores.len(), 2);
    checks.eq(scores.len, 2);
    checks.that(scores.has("ada"));
    checks.that(!scores.has("linus"));

    scores["linus"] = 9;
    checks.eq(scores["linus"], 9);
    checks.eq(len(scores), 3);

    scores.insert("ada", 11);
    checks.eq(scores["ada"], 11);

    var keys = scores.keys();
    checks.eq(len(keys), 3);
    checks.eq_string(keys[0], "ada");
    checks.eq_string(keys[1], "grace");
    checks.eq_string(keys[2], "linus");

    checks.eq(scores.remove("grace"), 12);
    checks.that(!scores.has("grace"));
    checks.eq(len(scores), 2);

    var empty = {};
    checks.eq(len(empty), 0);

    var t = scores;
    t["copy"] = 1;
    checks.eq(scores["copy"], 1);
    checks.that({"a": 1} == {"a": 1});
    checks.that({"a": 1} != {"a": 2});
    return 0;
}
