import ui;

struct Hits {
    n: Int;
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

    var w = try ui.open_hidden("rg-lazy", 320, 240);
    w.add(lazy);
    w.scroll_at(0, -48, 40, 40);
    checks.that(lazy.offset > 0);
    var before = lazy.offset;
    w.scroll_at(0, 2000, 40, 40);
    checks.that(lazy.offset < before);
    checks.eq(lazy.offset, 0);
    w.close();

    var hits = Hits { n: 0 };
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
    w2.scroll_at(0, -64, 40, 40);
    w2.click_at(40, 28);
    checks.eq(hits.n, 2);
    w2.close();
    return 0;
}
