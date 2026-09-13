import ui;

struct Hits {
    n: Int;
    last: Int;
    cancel: Int;
}

fn main(): Int {
    var hits = Hits { n: 0, last: -1, cancel: 0 };
    var w = try ui.open_hidden("rg-dialog", 400, 300);
    var dlg = Dialog {
        title: "Confirm",
        message: "Delete this item?",
        buttons: ["Cancel", "Delete"]
    };
    dlg.chosen.connect(fn () {
        hits.n = hits.n + 1;
        hits.last = dlg.selected;
    });
    dlg.cancelled.connect(fn () {
        hits.cancel = hits.cancel + 1;
    });
    checks.that(!w.has_dialog());
    checks.that(!dlg.open);

    w.show_dialog(dlg);
    checks.that(w.has_dialog());
    checks.that(dlg.open);
    checks.eq(dlg.btn_focus, 1);

    // Click outside panel — stays open
    w.click_at(5, 5);
    checks.that(w.has_dialog());
    checks.that(dlg.open);

    // Esc cancels
    w.key_at(27, "");
    checks.that(!w.has_dialog());
    checks.that(!dlg.open);
    checks.eq(hits.cancel, 1);
    checks.eq(hits.n, 0);

    w.show_dialog(dlg);
    // Activate primary with Enter
    w.key_at(13, "");
    checks.eq(hits.n, 1);
    checks.eq(hits.last, 1);
    checks.that(!w.has_dialog());

    // Arrow then choose Cancel
    w.show_dialog(dlg);
    w.key_at(37, "");
    checks.eq(dlg.btn_focus, 0);
    w.key_at(13, "");
    checks.eq(hits.n, 2);
    checks.eq(hits.last, 0);

    var prompt = Dialog {
        title: "Rename",
        message: "New name:",
        field_placeholder: "Name",
        buttons: ["Cancel", "OK"]
    };
    prompt.chosen.connect(fn () {
        hits.n = hits.n + 1;
        hits.last = prompt.selected;
    });
    w.show_dialog(prompt);
    checks.that(prompt.field_focused);
    w.key_at(0, "H");
    w.key_at(0, "i");
    checks.eq_string(prompt.field_text, "Hi");
    w.key_at(13, "");
    checks.eq(hits.n, 3);
    checks.eq(hits.last, 1);
    checks.eq_string(prompt.field_text, "Hi");
    checks.that(!w.has_dialog());

    // Click primary button by geometry
    w.show_dialog(dlg);
    dlg.layout(w.width, w.height);
    var bx = dlg.button_x(1) + dlg.btn_w(1) / 2;
    var by = dlg.buttons_y() + dlg.btn_h() / 2;
    w.hover_at(bx, by);
    checks.eq(dlg.hover_btn, 1);
    var cx = dlg.button_x(0) + dlg.btn_w(0) / 2;
    w.hover_at(cx, by);
    checks.eq(dlg.hover_btn, 0);
    w.click_at(bx, by);
    checks.eq(hits.n, 4);
    checks.eq(hits.last, 1);
    checks.that(!w.has_dialog());

    w.close();
    return 0;
}
