import ui;

fn main(): Int {
    var theme = Theme {
        window_bg: Color.Rgb(230, 240, 255),
        button_fill: Color.Rgb(20, 120, 80),
        button_hover: Color.Rgb(40, 150, 100),
        button_pressed: Color.Rgb(10, 90, 60)
    };
    checks.eq(theme.window_bg.value(), 230 * 65536 + 240 * 256 + 255);

    var w = try ui.open_hidden("rg-polish", 400, 240);
    w.theme = theme;
    var go = theme.button("Go");
    checks.eq(go.fill.value(), theme.button_fill.value());
    checks.that(!go.hot);

    var left = theme.label("L");
    var right = theme.label("R");
    var row = HStack {
        spacing: 4,
        children: [left, Spacer {}, right]
    };
    checks.eq(Spacer {}.flex(), 1);
    checks.eq(left.flex(), 0);
    checks.that(row.min_width() >= left.min_width() + right.min_width() + 4);

    var centered = HStack {
        spacing: 4,
        align: "center",
        children: [Label { text: "A" }, Label { text: "B" }]
    };
    checks.eq_string(centered.align, "center");

    w.add(VStack {
        spacing: 8,
        children: [go, row, centered]
    });

    w.hover_at(20, 20);
    checks.that(go.hot);
    w.hover_at(20, 200);
    checks.that(!go.hot);

    w.close();
    return 0;
}
