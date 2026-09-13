import ui;

struct Hits {
    n: Int;
}

fn main(): Int {
    var hits = Hits { n: 0 };
    var w = try ui.open_hidden("rg-rclick", 320, 240);
    var items: Array[String] = ["Ada", "Grace", "Alan"];
    var list = LazyColumn {
        source: LabelRows { items: items },
        row_h: 28,
        viewport_h: 120
    };
    var menu = PopupMenu { items: ["Ping", "Pong"] };
    menu.chosen.connect(fn () { hits.n = hits.n + 1; });
    list.context_requested.connect(fn () {
        w.show_menu_at_pointer(menu);
    });
    w.add(list);
    checks.eq(list.selected, -1);
    checks.that(!w.has_menu());

    // pad 12 → list at (12,12); second row mid
    w.right_click_at(40, 12 + 28 + 14);
    checks.eq(list.selected, 1);
    checks.that(w.has_menu());
    checks.that(menu.open);

    w.click_at(menu.at_x + 8, menu.at_y + menu.item_h() / 2);
    checks.eq(hits.n, 1);
    checks.that(!w.has_menu());
    w.close();
    return 0;
}
