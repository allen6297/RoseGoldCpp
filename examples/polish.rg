import ui;

fn main(): Int {
    var theme = Theme {
        window_bg: Color.Rgb(245, 245, 248),
        button_fill: Color.Rgb(47, 111, 196),
        button_hover: Color.Rgb(70, 140, 230),
        button_pressed: Color.Rgb(30, 80, 150)
    };
    var w = try ui.open("Polish", 480, 320);
    w.theme = theme;
    var status = theme.label("Hover the buttons");
    var quit = theme.button("Close");
    quit.clicked.connect(fn () { w.close(); });
    var ping = theme.button("Ping");
    ping.clicked.connect(fn () { status.set_text("Clicked Ping"); });
    w.add(VStack {
        spacing: 12,
        children: [
            theme.label("Theme + Spacer + hover"),
            HStack {
                spacing: 8,
                children: [
                    ping,
                    Spacer {},
                    quit
                ]
            },
            HStack {
                spacing: 8,
                align: "center",
                children: [
                    theme.label("centered"),
                    theme.button("OK")
                ]
            },
            status
        ]
    });
    w.run();
    return 0;
}
