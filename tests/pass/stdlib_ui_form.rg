import ui;

struct Hits {
    n: Int;
}

fn main(): Int {
    var w = try ui.open_hidden("rg-form", 360, 280);
    var hits = Hits { n: 0 };
    var title = Label { text: "Form" };
    var field = TextField { placeholder: "name" };
    field.changed.connect(fn () { hits.n = hits.n + 1; });
    field.submitted.connect(fn () { hits.n = hits.n + 100; });
    var tog = Toggle { label: "On" };
    tog.changed.connect(fn () { hits.n = hits.n + 10; });
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
    w.key_at(8, "");
    checks.eq_string(field.text, "H");
    checks.eq(hits.n, 3);
    w.key_at(13, "");
    checks.eq(hits.n, 103);

    w.click_at(40, y_tog);
    checks.that(!field.focused);
    checks.that(tog.on);
    checks.eq(hits.n, 113);
    w.click_at(40, y_tog);
    checks.that(!tog.on);

    checks.that(field.min_width() >= 120);
    w.close();
    return 0;
}
