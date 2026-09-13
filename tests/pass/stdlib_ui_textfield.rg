import ui;

struct Hits {
    n: Int;
}

fn main(): Int {
    var w = try ui.open_hidden("rg-text-rich", 400, 360);
    var hits = Hits { n: 0 };
    var single = TextField { placeholder: "one" };
    var multi = TextField { placeholder: "many", multiline: true, rows: 3 };
    multi.changed.connect(fn () { hits.n = hits.n + 1; });
    w.add(VStack {
        spacing: 8,
        children: [single, multi]
    });

    var inner = 400 - 24;
    var y_single = 12 + single.height(inner) / 2;
    var y_multi = 12 + single.height(inner) + 8 + 12;

    w.click_at(40, y_single);
    w.key_at(0, "A");
    w.key_at(0, "B");
    checks.eq_string(single.text, "AB");
    w.key_at(65, "ctrl");
    w.key_at(67, "ctrl");
    checks.eq_string(ui.clipboard_get(), "AB");
    w.key_at(8, "");
    checks.eq_string(single.text, "");
    ui.clipboard_set("XY");
    w.key_at(86, "ctrl");
    checks.eq_string(single.text, "XY");

    w.click_at(40, y_multi);
    checks.that(multi.focused);
    w.key_at(0, "H");
    w.key_at(0, "i");
    w.key_at(13, "");
    w.key_at(0, "B");
    checks.eq_string(multi.text, "Hi\nB");
    checks.eq(multi.line_count(), 2);
    checks.eq(hits.n, 4);
    w.key_at(38, "");
    checks.eq(multi.line_of(multi.caret), 0);
    w.key_at(40, "");
    checks.eq(multi.line_of(multi.caret), 1);

    var taller = multi.height(inner);
    checks.that(taller > single.height(inner));

    w.close();
    return 0;
}
