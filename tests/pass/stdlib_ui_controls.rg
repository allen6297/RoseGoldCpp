import ui;

struct Hits {
    n: Int;
    last: Int;
}

fn main(): Int {
    var hits = Hits { n: 0, last: -1 };
    var w = try ui.open_hidden("rg-extra", 400, 360);

    var box = Checkbox { label: "Agree", checked: false };
    box.changed.connect(fn () { hits.n = hits.n + 1; });
    var radios = RadioGroup {
        items: ["Red", "Green", "Blue"],
        selected: 1
    };
    radios.changed.connect(fn () {
        hits.n = hits.n + 10;
        hits.last = radios.selected;
    });
    var bar = ProgressBar { value: 40, max_v: 100 };
    var go = Button { text: "Go" };
    go.clicked.connect(fn () { hits.n = hits.n + 100; });
    var tipped = w.tip(go, "Run action");
    var locked = Button { text: "Locked", enabled: false };
    locked.clicked.connect(fn () { hits.n = hits.n + 1000; });

    w.add(VStack {
        spacing: 8,
        children: [box, radios, bar, tipped, locked]
    });

    var inner = 400 - 24;
    var y = 12 + box.height(inner) / 2;
    w.click_at(40, y);
    checks.that(box.checked);
    checks.eq(hits.n, 1);

    var y_radio = 12 + box.height(inner) + 8 + radios.row_h() / 2;
    w.click_at(40, y_radio);
    checks.eq(radios.selected, 0);
    checks.eq(hits.last, 0);
    checks.eq(hits.n, 11);

    bar.set_value(75);
    checks.eq(bar.value, 75);
    bar.set_value(200);
    checks.eq(bar.value, 100);

    var y_go = 12 + box.height(inner) + 8 + radios.height(inner) + 8
        + bar.height(inner) + 8 + go.height(inner) / 2;
    w.hover_at(40, y_go);
    checks.that(w.tip_on);
    checks.eq_string(w.tip_text, "Run action");
    w.click_at(40, y_go);
    checks.eq(hits.n, 111);

    var y_locked = y_go + go.height(inner) + 8;
    w.click_at(40, y_locked);
    checks.eq(hits.n, 111);
    checks.that(!locked.enabled);

    go.set_enabled(false);
    w.click_at(40, y_go);
    checks.eq(hits.n, 111);

    w.close();
    return 0;
}
