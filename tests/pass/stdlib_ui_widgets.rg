import ui;

struct Hits {
    n: Int;
}

fn main(): Int {
    var w = try ui.open_hidden("rg-widgets", 320, 240);
    var hits = Hits { n: 0 };
    var hello = Label { text: "Hi" };
    var go = Button { text: "OK" };
    go.clicked.connect(fn () { hits.n = hits.n + 1; });
    w.add(hello);
    w.add(go);
    checks.eq_string(hello.text, "Hi");
    checks.eq_string(go.text, "OK");

    var inner = 320 - 24;
    var y_btn = 12 + hello.height(inner) + 8 + go.height(inner) / 2;
    w.click_at(20, 12 + hello.height(inner) / 2);
    checks.eq(hits.n, 0);

    w.click_at(20, y_btn);
    checks.eq(hits.n, 1);

    hello.set_text("Bye");
    checks.eq_string(hello.text, "Bye");
    go.set_text("Go");
    checks.eq_string(go.text, "Go");
    w.click_at(20, y_btn);
    checks.eq(hits.n, 2);

    w.set_size(480, 320);
    w.poll();
    checks.eq(w.width, 480);
    checks.eq(w.height, 320);
    y_btn = 12 + hello.height(480 - 24) + 8 + go.height(480 - 24) / 2;
    w.click_at(20, y_btn);
    checks.eq(hits.n, 3);

    w.close();

    var lab = Label { text: "X" };
    lab.foreground(Color.Red);
    checks.that(lab.color == Color.Red);
    checks.eq(Color.Black.value(), 0);
    checks.eq(Color.Red.value(), 255 * 65536);
    checks.eq(Color.White.value(), 255 * 65536 + 255 * 256 + 255);
    checks.eq(ui.rgb(47, 111, 196).value(), 47 * 65536 + 111 * 256 + 196);
    var wrapped = lab.padding(5);
    checks.eq(wrapped.height(100), lab.height(100) + 10);

    var stacked = VStack {
        spacing: 2,
        children: [
            Label { text: "A" },
            Label { text: "B" }
        ]
    };
    checks.eq(stacked.height(200), Label { text: "A" }.height(200) * 2 + 2);

    var col = VStack { spacing: 0 };
    col.add(Label { text: "A" }).add(Label { text: "B" });
    checks.eq(col.height(200), Label { text: "A" }.height(200) * 2);

    var styled = Label { text: "Z" }.style(Style { ink: Color.Blue, pad: 3 });
    checks.eq(styled.height(200), Label { text: "Z" }.height(200) + 6);

    var wpad = try ui.open_hidden("rg-pad", 320, 240);
    var pad_hits = Hits { n: 0 };
    var boxed = Button { text: "P" };
    boxed.clicked.connect(fn () { pad_hits.n = pad_hits.n + 1; });
    wpad.add(boxed.padding(10));
    wpad.click_at(20, 20);
    checks.eq(pad_hits.n, 0);
    wpad.click_at(24, 24);
    checks.eq(pad_hits.n, 1);
    wpad.close();

    var wnest = try ui.open_hidden("rg-stack", 320, 240);
    var nest_hits = Hits { n: 0 };
    var left = Button { text: "A" };
    var right = Button { text: "B" };
    left.clicked.connect(fn () { nest_hits.n = nest_hits.n + 1; });
    right.clicked.connect(fn () { nest_hits.n = nest_hits.n + 10; });
    var head = Label { text: "Hi" }.padding(6);
    var row = HStack {
        spacing: 4,
        children: [left, right]
    };
    wnest.add(VStack {
        spacing: 4,
        children: [head, row]
    });
    var nest_inner = 320 - 24;
    var y_head = 12 + head.height(nest_inner) / 2;
    var y_row = 12 + head.height(nest_inner) + 4 + row.height(nest_inner) / 2;
    wnest.click_at(22, y_head);
    checks.eq(nest_hits.n, 0);
    wnest.click_at(22, y_row);
    checks.eq(nest_hits.n, 1);
    wnest.click_at(172, y_row);
    checks.eq(nest_hits.n, 11);
    wnest.close();

    var wide = Label { text: "HelloWorld" };
    var narrow = Label { text: "Hi" };
    checks.that(wide.min_width() > narrow.min_width());
    var row = HStack {
        spacing: 4,
        children: [narrow, wide]
    };
    checks.that(row.min_width() >= narrow.min_width() + wide.min_width() + 4);

    return 0;
}
