import ui;

struct Hits {
    n: Int;
    last: Int;
}

class ButtonRows impl LazyRows {
    var hits: Hits;
    var n: Int = 0;

    fn count(): Int {
        return n;
    }

    fn row(i: Int): Widget {
        var b = Button { text: "row" };
        b.clicked.connect(fn () { hits.n = hits.n + 1; });
        return b;
    }
}

class FieldRows impl LazyRows {
    var hits: Hits;
    var n: Int = 0;

    fn count(): Int {
        return n;
    }

    fn row(i: Int): Widget {
        var f = TextField { placeholder: "edit" };
        f.changed.connect(fn () { hits.n = hits.n + 1; });
        return f;
    }
}

fn main(): Int {
    var items: Array[String] = [];
    var i = 0;
    while (i < 100) {
        items.push("item");
        i = i + 1;
    }
    var labels = LabelRows { items: items };
    var lazy = LazyColumn {
        source: labels,
        row_h: 24,
        viewport_h: 96
    };
    checks.eq(lazy.height(320), 96);
    checks.eq(lazy.content_h(), 2400);
    checks.eq(lazy.offset, 0);
    checks.eq(lazy.selected, -1);

    var w = try ui.open_hidden("rg-lazy", 320, 240);
    w.add(lazy);
    w.scroll_at(0, -48, 40, 40);
    checks.that(lazy.offset > 0);
    var before = lazy.offset;
    w.scroll_at(0, 2000, 40, 40);
    checks.that(lazy.offset < before);
    checks.eq(lazy.offset, 0);

    var sel_hits = Hits { n: 0, last: -1 };
    lazy.selection_changed.connect(fn () {
        sel_hits.n = sel_hits.n + 1;
        sel_hits.last = lazy.selected;
    });
    w.click_at(40, 12 + 12);
    checks.that(lazy.focused);
    checks.eq(lazy.selected, 0);
    checks.eq(sel_hits.n, 1);
    w.key_at(40, "");
    checks.eq(lazy.selected, 1);
    checks.eq(sel_hits.n, 2);
    w.key_at(40, "");
    checks.eq(lazy.selected, 2);
    w.key_at(38, "");
    checks.eq(lazy.selected, 1);
    w.key_at(35, "");
    checks.eq(lazy.selected, 99);
    checks.that(lazy.offset > 0);
    w.key_at(36, "");
    checks.eq(lazy.selected, 0);
    checks.eq(lazy.offset, 0);
    var cw = 320 - 24;
    w.click_at(12 + cw - 4, 12 + 80);
    checks.that(lazy.offset > 0);
    w.close();

    var hits = Hits { n: 0, last: -1 };
    var buttons = ButtonRows { hits: hits, n: 20 };
    var col = LazyColumn {
        source: buttons,
        row_h: 32,
        viewport_h: 128
    };
    var w2 = try ui.open_hidden("rg-lazy-btn", 320, 240);
    w2.add(col);
    w2.click_at(40, 28);
    checks.eq(hits.n, 1);
    checks.eq(col.selected, 0);
    w2.scroll_at(0, -64, 40, 40);
    w2.click_at(40, 28);
    checks.eq(hits.n, 2);
    checks.that(col.selected > 0);
    w2.close();

    var edits = Hits { n: 0, last: -1 };
    var fields = FieldRows { hits: edits, n: 8 };
    var fcol = LazyColumn {
        source: fields,
        row_h: 32,
        viewport_h: 128
    };
    var w3 = try ui.open_hidden("rg-lazy-field", 320, 240);
    w3.add(fcol);
    w3.click_at(40, 28);
    w3.key_at(0, "A");
    checks.eq(edits.n, 1);
    w3.hover_at(40, 28);
    w3.key_at(0, "B");
    checks.eq(edits.n, 2);
    checks.eq(len(fcol.cache), fcol.last_index() - fcol.first_index() + 1);
    w3.close();
    return 0;
}
