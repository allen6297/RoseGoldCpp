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
    var city = Dropdown {
        placeholder: "City",
        items: ["Austin", "Boston", "Chicago", "Denver"]
    };
    var status = theme.label("Theme, form, scroll, slider, lazy");
    volume.changed.connect(fn () { status.set_text("volume"); });
    city.changed.connect(fn () { status.set_text(city.current_label()); });

    var menu = PopupMenu {
        items: ["New", "Open", "Save", "Quit"]
    };
    var more = theme.button("Menu");
    more.clicked.connect(fn () {
        w.show_menu_at_pointer(menu);
    });

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
    list.context_requested.connect(fn () {
        w.show_menu_at_pointer(menu);
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

    var close_dlg = Dialog {
        title: "Close",
        message: "Leave the widgets demo?",
        buttons: ["Cancel", "Close"]
    };
    close_dlg.chosen.connect(fn () {
        if (close_dlg.selected == 1) {
            w.close();
        }
    });
    var quit = theme.button("Close");
    quit.clicked.connect(fn () {
        w.show_dialog(close_dlg);
    });

    var prompt = Dialog {
        title: "Submit",
        message: "Confirm the name to save:",
        field_placeholder: "Name",
        buttons: ["Cancel", "OK"]
    };
    prompt.chosen.connect(fn () {
        if (prompt.selected == 1) {
            name.set_text(prompt.field_text);
            if (remember.on) {
                status.set_text(prompt.field_text + " (saved)");
            } else {
                status.set_text(prompt.field_text);
            }
        }
    });
    var submit = theme.button("Submit");
    submit.clicked.connect(fn () {
        prompt.field_text = name.text;
        w.show_dialog(prompt);
    });
    name.submitted.connect(fn () { submit.clicked.emit(); });
    menu.chosen.connect(fn () {
        if (menu.selected == 3) {
            w.show_dialog(close_dlg);
        } else {
            status.set_text(menu.items[menu.selected]);
        }
    });

    w.add(VStack {
        spacing: 10,
        children: [
            theme.label("Long labels wrap when the row is narrow enough to need it."),
            name,
            remember,
            volume,
            city,
            ScrollView { child: scroll_col, viewport_h: 72 },
            list,
            HStack {
                spacing: 8,
                align: "center",
                children: [swatch, file, Spacer {}, more, submit, quit]
            },
            status
        ]
    });
    w.run();
    return 0;
}
