import ui;

fn main(): Int {
    var w = try ui.open("Scroll", 360, 280);
    var col = VStack { spacing: 4 };
    var i = 1;
    while (i <= 40) {
        col.add(Label { text: "Item " });
        i = i + 1;
    }
    w.add(VStack {
        spacing: 8,
        children: [
            Label { text: "Scroll the list" },
            ScrollView { child: col, viewport_h: 180 }
        ]
    });
    w.run();
    return 0;
}
