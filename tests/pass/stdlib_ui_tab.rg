import ui;

struct Hits {
    n: Int;
}

fn main(): Int {
    var hits = Hits { n: 0 };
    var w = try ui.open_hidden("rg-tab", 360, 280);
    var a = TextField { placeholder: "A" };
    var b = TextField { placeholder: "B" };
    var go = Button { text: "Go" };
    go.clicked.connect(fn () { hits.n = hits.n + 1; });
    var tog = Toggle { label: "On" };
    w.add(VStack {
        spacing: 8,
        children: [a, b, go, tog]
    });

    checks.that(!a.focused);
    checks.that(!b.focused);
    w.key_at(9, "");
    checks.that(a.focused);
    checks.that(!b.focused);
    w.key_at(9, "");
    checks.that(!a.focused);
    checks.that(b.focused);
    w.key_at(9, "");
    checks.that(go.focused);
    w.key_at(13, "");
    checks.eq(hits.n, 1);
    w.key_at(9, "");
    checks.that(tog.focused);
    w.key_at(9, "");
    checks.that(a.focused);
    w.key_at(9, "shift");
    checks.that(tog.focused);
    w.key_at(32, "");
    checks.that(tog.on);
    w.close();
    return 0;
}
