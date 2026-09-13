import ui;

struct Hits {
    n: Int;
}

fn main(): Int {
    var hits = Hits { n: 0 };
    var w = try ui.open_hidden("rg-dd", 360, 320);
    var dd = Dropdown {
        placeholder: "Pick",
        items: ["Alpha", "Beta", "Gamma"]
    };
    dd.changed.connect(fn () { hits.n = hits.n + 1; });
    w.add(dd);
    checks.eq(dd.height(336), dd.closed_h());
    checks.eq(dd.selected, -1);
    checks.that(!dd.open);

    // pad 12 → control at (12,12)
    w.click_at(40, 12 + dd.closed_h() / 2);
    checks.that(dd.open);
    checks.that(dd.focused);
    checks.that(dd.height(336) > dd.closed_h());

    var row = 12 + dd.closed_h() + dd.item_h() + dd.item_h() / 2;
    w.click_at(40, row);
    checks.eq(dd.selected, 1);
    checks.eq_string(dd.current_label(), "Beta");
    checks.that(!dd.open);
    checks.eq(hits.n, 1);

    w.key_at(13, "");
    checks.that(dd.open);
    w.key_at(40, "");
    checks.eq(dd.selected, 2);
    checks.eq(hits.n, 2);
    w.key_at(27, "");
    checks.that(!dd.open);
    w.close();
    return 0;
}
