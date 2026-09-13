import ui;

struct Hits {
    n: Int;
}

fn main(): Int {
    var hits = Hits { n: 0 };
    var theme = Theme {
        window_bg: Color.Rgb(240, 240, 244),
        accent: Color.Rgb(20, 120, 80)
    };
    var w = try ui.open_hidden_theme("rg-ergo", 360, 420, theme);
    checks.that(w.theme.accent.value() == theme.accent.value());

    var labels = FilteredLabels {
        items: ["Ada", "Alan", "Grace", "Ken"]
    };
    checks.eq(labels.count(), 4);
    checks.eq(labels.real_index(2), 2);
    labels.query = "an";
    checks.eq(labels.count(), 1);
    checks.eq(labels.real_index(0), 1);
    checks.eq_string(labels.items[labels.real_index(0)], "Alan");
    labels.query = "a";
    checks.eq(labels.count(), 3);
    labels.query = "  ";
    checks.eq(labels.count(), 4);

    var box = theme.checkbox("Agree");
    var radios = theme.radio_group(["A", "B"]);
    var bar = theme.progress();
    bar.set_value(30);
    var slide = theme.slider();
    slide.value = 10;
    var drop = theme.dropdown("Pick", ["One", "Two"]);
    var tog = theme.toggle("On");
    var field = theme.field("Name");
    var row = theme.form_row("Name", field);
    checks.that(row.height(200) > field.height(200));

    var menu = w.menu(["Ping"]);
    menu.chosen.connect(fn () { hits.n = hits.n + 1; });
    checks.eq_string(menu.items[0], "Ping");

    w.add(VStack {
        spacing: 6,
        children: [row, box, radios, bar, slide, drop, tog]
    });

    var d = w.confirm("Close", "Leave?", "Close");
    checks.that(w.has_dialog());
    checks.eq(len(d.buttons), 2);
    w.key_at(27, "");
    checks.that(!w.has_dialog());

    var a = w.alert("Saved", "Contact stored.");
    checks.that(w.has_dialog());
    checks.eq(len(a.buttons), 1);
    w.key_at(13, "");
    checks.that(!w.has_dialog());

    w.mark_dirty();
    checks.that(w.dirty);
    w.request_close();
    checks.that(w.has_dialog());
    w.key_at(13, "");
    checks.that(!w.dirty);
    checks.eq(w.id, 0);

    w = try ui.open_hidden_theme("rg-ergo2", 200, 160, theme);
    w.request_close();
    checks.eq(w.id, 0);

    w = try ui.open_hidden("rg-ergo3", 200, 160);
    var d2 = w.confirm("X", "Y", "OK");
    d2.chosen.connect(fn () { hits.n = hits.n + 10; });
    w.key_at(13, "");
    checks.eq(hits.n, 10);

    w.close();
    return 0;
}
