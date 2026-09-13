import ui;

fn main(): Int {
    var w = try ui.open("Form", 420, 320);
    var name = TextField { placeholder: "Name" };
    var remember = Toggle { label: "Remember me" };
    var status = Label { text: "Ready" };
    var go = Button { text: "Submit" };
    var quit = Button { text: "Close" };
    go.clicked.connect(fn () {
        if (remember.on) {
            status.set_text(name.text + " (saved)");
        } else {
            status.set_text(name.text);
        }
    });
    quit.clicked.connect(fn () { w.close(); });
    name.submitted.connect(fn () { go.clicked.emit(); });
    w.add(VStack {
        spacing: 12,
        children: [
            Label { text: "Sign in" }.foreground(Color.Black),
            name,
            remember,
            HStack {
                spacing: 8,
                children: [go, quit]
            },
            status
        ]
    });
    w.run();
    return 0;
}
