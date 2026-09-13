import ui;

fn main(): Int {
    var theme = Theme {
        window_bg: Color.Rgb(245, 245, 248),
        button_fill: Color.Rgb(47, 111, 196),
        button_hover: Color.Rgb(70, 140, 230),
        button_pressed: Color.Rgb(30, 80, 150)
    };
    var w = try ui.open("Widgets", 440, 520);
    w.theme = theme;

    var name = theme.field("Name");
    var remember = Toggle { label: "Remember me" };
    var volume = Slider { value: 40, min_v: 0, max_v: 100 };
    var status = theme.label("Theme, form, scroll, slider, lazy");
    volume.changed.connect(fn () { status.set_text("volume"); });

    var scroll_col = VStack { spacing: 4 };
    var i = 1;
    while (i <= 20) {
        scroll_col.add(Label { text: "Scroll row" });
        i = i + 1;
    }

    var items: Array[String] = [];
    i = 1;
    while (i <= 80) {
        items.push("Lazy row");
        i = i + 1;
    }
    var list = LazyColumn {
        source: LabelRows { items: items },
        row_h: 24,
        viewport_h: 100
    };
    list.selection_changed.connect(fn () {
        status.set_text("lazy selected");
    });

    var rgb_px: Array[Int] = [];
    var row = 0;
    while (row < 16) {
        var col = 0;
        while (col < 16) {
            rgb_px.push(Color.Rgb(40 + row * 10, 40 + col * 10, 180).value());
            col = col + 1;
        }
        row = row + 1;
    }
    var swatch = ui.make_image(16, 16, rgb_px);
    var file = ui.load_image("examples/assets/dot.png");

    var quit = theme.button("Close");
    quit.clicked.connect(fn () { w.close(); });
    var submit = theme.button("Submit");
    submit.clicked.connect(fn () {
        if (remember.on) {
            status.set_text(name.text + " (saved)");
        } else {
            status.set_text(name.text);
        }
    });
    name.submitted.connect(fn () { submit.clicked.emit(); });

    w.add(VStack {
        spacing: 10,
        children: [
            theme.label("Long labels wrap when the row is narrow enough to need it."),
            name,
            remember,
            volume,
            ScrollView { child: scroll_col, viewport_h: 72 },
            list,
            HStack {
                spacing: 8,
                align: "center",
                children: [swatch, file, Spacer {}, submit, quit]
            },
            status
        ]
    });
    w.run();
    return 0;
}
