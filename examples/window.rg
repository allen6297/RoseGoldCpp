import ui;

fn main(): Int {
    var w = try ui.open("RoseGold", 400, 240);
    var go = Button { text: "Close" }
        .background(Color.Blue)
        .foreground(Color.White);
    go.clicked.connect(fn () { w.close(); });
    w.add(VStack {
        spacing: 12,
        children: [
            Label { text: "Hello" }
                .foreground(Color.Black)
                .padding(8),
            go
        ]
    }.padding(12));
    w.run();
    return 0;
}
