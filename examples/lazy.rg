import ui;

fn main(): Int {
    var items: Array[String] = [];
    var i = 1;
    while (i <= 200) {
        items.push("Item ");
        i = i + 1;
    }
    var w = try ui.open("Lazy", 360, 360);
    var status = Label { text: "click a row, then use arrows" };
    var list = LazyColumn {
        source: LabelRows { items: items },
        row_h: 24,
        viewport_h: 200
    };
    list.selection_changed.connect(fn () {
        status.set_text("selected");
    });
    var quit = Button { text: "Close" };
    quit.clicked.connect(fn () { w.close(); });
    w.add(VStack {
        spacing: 8,
        children: [
            Label { text: "LazyColumn — select with click / arrows" },
            list,
            status,
            quit
        ]
    });
    w.run();
    return 0;
}
