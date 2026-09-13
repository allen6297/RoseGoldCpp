import ui;

struct Hits {
    n: Int;
    last: Int;
}

fn main(): Int {
    var hits = Hits { n: 0, last: -1 };
    var w = try ui.open_hidden("rg-menu", 360, 280);
    var menu = PopupMenu {
        items: ["Cut", "Copy", "Paste"]
    };
    menu.chosen.connect(fn () {
        hits.n = hits.n + 1;
        hits.last = menu.selected;
    });
    checks.that(!w.has_menu());
    checks.that(!menu.open);

    w.show_menu(menu, 40, 40);
    checks.that(w.has_menu());
    checks.that(menu.open);
    checks.eq(menu.selected, 0);
    checks.eq(menu.at_x, 40);
    checks.eq(menu.at_y, 40);

    // Second item ("Copy")
    w.click_at(50, 40 + menu.item_h() + menu.item_h() / 2);
    checks.eq(hits.n, 1);
    checks.eq(hits.last, 1);
    checks.that(!w.has_menu());
    checks.that(!menu.open);

    w.last_click_x = 80;
    w.last_click_y = 100;
    w.show_menu_at_pointer(menu);
    checks.eq(menu.at_x, 80);
    checks.eq(menu.at_y, 100);
    w.dismiss_menus();

    w.show_menu(menu, 20, 20);
    w.key_at(40, "");
    checks.eq(menu.selected, 1);
    w.key_at(40, "");
    checks.eq(menu.selected, 2);
    w.key_at(13, "");
    checks.eq(hits.n, 2);
    checks.eq(hits.last, 2);
    checks.that(!w.has_menu());

    w.show_menu(menu, 30, 30);
    w.click_at(200, 200);
    checks.that(!w.has_menu());

    w.show_menu(menu, 30, 30);
    w.key_at(27, "");
    checks.that(!w.has_menu());
    w.close();
    return 0;
}
