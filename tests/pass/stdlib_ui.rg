import ui;

fn main(): Int {
    var b = ui.backend();
    checks.that(b == "win32" || b == "x11" || b == "wayland" || b == "cocoa" || b == "none");
    var p = ui.platform();
    checks.that(p == "windows" || p == "linux" || p == "macos");
    checks.eq_string(ui.kind(), "desktop");
    checks.that(ui.font_height() > 0);
    checks.that(ui.text_width("Hi") > ui.text_width("H"));
    checks.eq(ui.count(), 0);
    var w = try ui.open_hidden("rg-ui-test", 320, 240);
    checks.that(w.alive());
    checks.eq(ui.count(), 1);
    checks.eq_string(w.title, "rg-ui-test");
    checks.eq(w.width, 320);
    checks.eq(w.height, 240);
    w.set_title("renamed");
    checks.eq_string(w.title, "renamed");
    w.set_size(400, 300);
    checks.eq(w.width, 400);
    checks.eq(w.height, 300);
    checks.that(w.poll());
    checks.eq(w.width, 400);
    checks.eq(w.height, 300);
    w.close();
    checks.that(!w.alive());
    checks.eq(ui.count(), 0);

    var made = Window { title: "literal", width: 200, height: 100, visible: false };
    checks.that(!made.alive());
    try made.show();
    checks.that(made.alive());
    checks.eq_string(made.title, "literal");
    checks.eq(made.width, 200);
    made.close();
    checks.that(!made.alive());
    return 0;
}
