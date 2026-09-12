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

    w.click_at(20, 20);
    checks.eq(hits.n, 0);

    w.click_at(20, 50);
    checks.eq(hits.n, 1);

    hello.set_text("Bye");
    checks.eq_string(hello.text, "Bye");
    go.set_text("Go");
    checks.eq_string(go.text, "Go");
    w.click_at(20, 50);
    checks.eq(hits.n, 2);

    w.set_size(480, 320);
    w.poll();
    checks.eq(w.width, 480);
    checks.eq(w.height, 320);
    w.click_at(20, 50);
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
    checks.eq(wrapped.height(), 28);

    var stacked = VStack {
        spacing: 2,
        children: [
            Label { text: "A" },
            Label { text: "B" }
        ]
    };
    checks.eq(stacked.height(), 38);

    var col = VStack { spacing: 0 };
    col.add(Label { text: "A" }).add(Label { text: "B" });
    checks.eq(col.height(), 36);

    var styled = Label { text: "Z" }.style(Style { ink: Color.Blue, pad: 3 });
    checks.eq(styled.height(), 24);

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
    wnest.add(VStack {
        spacing: 4,
        children: [
            Label { text: "Hi" }.padding(6),
            HStack {
                spacing: 4,
                children: [left, right]
            }
        ]
    });
    wnest.click_at(22, 20);
    checks.eq(nest_hits.n, 0);
    wnest.click_at(22, 56);
    checks.eq(nest_hits.n, 1);
    wnest.click_at(172, 56);
    checks.eq(nest_hits.n, 11);
    wnest.close();

    return 0;
}
