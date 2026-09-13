import ui;

fn main(): Int {
    var theme = Theme {
        window_bg: Color.Rgb(245, 245, 248),
        button_fill: Color.Rgb(47, 111, 196),
        button_hover: Color.Rgb(70, 140, 230),
        button_pressed: Color.Rgb(30, 80, 150)
    };
    var w = try ui.open_theme("Widgets", 420, 520, theme);
    var status = theme.label("Ready");
    var say = fn (s: String) { status.set_text(s); };

    var name = theme.field("Name");
    var remember = theme.toggle("Remember me");
    var agree = theme.checkbox("Email updates");
    var theme_pick = theme.radio_group(["Light", "Dark", "System"]);
    var volume = theme.slider();
    volume.value = 40;
    var load = theme.progress();
    load.set_value(40);
    var city = theme.dropdown("City", ["Austin", "Boston", "Chicago"]);
    volume.changed.connect(fn () {
        load.set_value(volume.value);
        say("volume");
    });
    city.changed.connect(fn () { say(city.current_label()); });
    agree.changed.connect(fn () { say("checkbox"); });
    theme_pick.changed.connect(fn () { say(theme_pick.items[theme_pick.selected]); });

    var menu = w.menu(["New", "Open", "Quit"]);
    menu.chosen.connect(fn () {
        if (menu.selected == 2) {
            w.request_close();
        } else {
            say(menu.items[menu.selected]);
        }
    });
    var more = theme.button("Menu");
    more.clicked.connect(fn () { w.show_menu_at_pointer(menu); });

    var items: Array[String] = [];
    for i in 12 {
        items.push("Row");
    }
    var list = LazyColumn {
        source: LabelRows { items: items },
        row_h: 24,
        viewport_h: 96
    };
    list.selection_changed.connect(fn () { say("lazy selected"); });
    list.context_requested.connect(fn () { w.show_menu_at_pointer(menu); });

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
                say(prompt.field_text + " (saved)");
            } else {
                say(prompt.field_text);
            }
        }
    });
    var submit = theme.button("Submit");
    submit.clicked.connect(fn () {
        prompt.field_text = name.text;
        w.show_dialog(prompt);
    });
    name.submitted.connect(fn () { submit.clicked.emit(); });
    name.changed.connect(fn () { w.mark_dirty(); });

    var quit = theme.button("Close");
    quit.clicked.connect(fn () { w.request_close(); });
    w.add(VStack {
        spacing: 8,
        children: [
            theme.form_row("Name", name),
            remember,
            agree,
            theme_pick,
            volume,
            load,
            city,
            list,
            Canvas { w_hint: 200, h_px: 72 }.padding(4).background(Color.Rgb(230, 230, 230)),
            HStack {
                spacing: 8,
                align: "center",
                children: [
                    ui.load_image("examples/assets/dot.png"),
                    Spacer {},
                    w.tip(more, "Open menu"),
                    submit,
                    quit
                ]
            },
            status
        ]
    });
    w.run();
    return 0;
}
