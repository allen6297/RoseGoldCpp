import ui;

struct Hits {
    n: Int;
}

fn main(): Int {
    var w = try ui.open_hidden("rg-form", 360, 280);
    var hits = Hits { n: 0 };
    var title = Label { text: "Form" };
    var field = TextField { placeholder: "name" };
    field.on_change(fn () { hits.n = hits.n + 1; });
    field.submitted.connect(fn () { hits.n = hits.n + 100; });
    var tog = Toggle { label: "On" };
    tog.on_change(fn () { hits.n = hits.n + 10; });
    w.add(VStack {
        spacing: 8,
        children: [
            title,
            field,
            tog
        ]
    });

    var inner = 360 - 24;
    var y_field = 12 + title.height(inner) + 8 + field.height(inner) / 2;
    var y_tog = 12 + title.height(inner) + 8 + field.height(inner) + 8 + tog.height(inner) / 2;

    checks.eq_string(field.text, "");
    checks.that(!field.focused);
    w.click_at(40, y_field);
    checks.that(field.focused);
    w.key_at(0, "H");
    w.key_at(0, "i");
    checks.eq_string(field.text, "Hi");
    checks.eq(hits.n, 2);
    checks.eq(field.caret, 2);

    w.key_at(37, "");
    checks.eq(field.caret, 1);
    w.key_at(37, "shift");
    checks.that(field.has_sel());
    checks.eq(field.sel_lo(), 0);
    checks.eq(field.sel_hi(), 1);
    w.key_at(0, "X");
    checks.eq_string(field.text, "Xi");
    checks.eq(field.caret, 1);
    checks.eq(hits.n, 3);

    w.key_at(65, "ctrl");
    checks.eq(field.sel_lo(), 0);
    checks.eq(field.sel_hi(), 2);
    w.key_at(8, "");
    checks.eq_string(field.text, "");
    checks.eq(hits.n, 4);

    w.key_at(0, "A");
    w.key_at(0, "B");
    w.key_at(0, "C");
    checks.eq_string(field.text, "ABC");
    checks.eq(hits.n, 7);
    w.key_at(36, "");
    checks.eq(field.caret, 0);
    w.key_at(46, "");
    checks.eq_string(field.text, "BC");
    checks.eq(hits.n, 8);
    w.key_at(35, "");
    checks.eq(field.caret, 2);

    w.key_at(13, "");
    checks.eq(hits.n, 108);
    w.click_at(40, y_tog);
    checks.that(tog.on);
    checks.eq(hits.n, 118);
    w.close();
    return 0;
}
