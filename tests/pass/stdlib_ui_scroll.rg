import ui;

fn main(): Int {
    var w = try ui.open_hidden("rg-scroll", 320, 240);
    var col = VStack { spacing: 2 };
    var i = 0;
    while (i < 30) {
        col.add(Label { text: "row" });
        i = i + 1;
    }
    var scroll = ScrollView { child: col, viewport_h: 80 };
    checks.eq(scroll.height(320), 80);
    checks.eq(scroll.offset, 0);
    w.add(scroll);
    w.scroll_at(0, -40, 40, 40);
    checks.that(scroll.offset > 0);
    var before = scroll.offset;
    w.scroll_at(0, 1000, 40, 40);
    checks.that(scroll.offset < before);
    checks.eq(scroll.offset, 0);
    w.close();
    return 0;
}
