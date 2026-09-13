import ui;

struct Hits {
    n: Int;
}

fn main(): Int {
    var lab = Label { text: "hello world from RoseGold wrap" };
    checks.that(lab.height(80) > lab.height(400));
    checks.that(lab.height(400) >= ui.line_step());

    var nl = Label { text: "one\ntwo\nthree" };
    checks.eq(nl.height(400), ui.line_step() * 3);

    var w = try ui.open_hidden("rg-slider", 360, 200);
    var hits = Hits { n: 0 };
    var title = Label { text: "Volume" };
    var s = Slider { value: 0, min_v: 0, max_v: 100 };
    s.changed.connect(fn () { hits.n = hits.n + 1; });
    w.add(VStack {
        spacing: 8,
        children: [title, s]
    });
    var sy = 12 + title.height(336) + 8 + s.height(336) / 2;
    w.click_at(40, sy);
    checks.that(s.value > 0);
    checks.that(hits.n >= 1);
    var mid = s.value;
    w.drag_to(300, sy);
    checks.that(s.value > mid);
    w.drag_end(300, sy);
    checks.that(!s.dragging);

    var px: Array[Int] = [];
    var y = 0;
    while (y < 8) {
        var x = 0;
        while (x < 8) {
            if ((x + y) % 2 == 0) {
                px.push(Color.Red.value());
            } else {
                px.push(Color.Blue.value());
            }
            x = x + 1;
        }
        y = y + 1;
    }
    var img = ui.make_image(8, 8, px);
    checks.eq(img.img_w, 8);
    checks.eq(img.img_h, 8);
    checks.eq(img.height(100), 8);
    w.add(img);
    w.hover_at(20, 80);
    w.close();

    var png = ui.load_image("examples/assets/dot.png");
    checks.eq(png.img_w, 16);
    checks.eq(png.img_h, 16);
    var wp = try ui.open_hidden("rg-png", 200, 120);
    wp.add(png);
    wp.hover_at(20, 20);
    wp.close();
    return 0;
}
