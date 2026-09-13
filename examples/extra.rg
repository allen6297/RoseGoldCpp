import ui;

fn main(): Int {
    var w = try ui.open("Extra", 420, 360);
    var status = Label { text: "drag the slider" };
    var s = Slider { value: 25, min_v: 0, max_v: 100 };
    s.changed.connect(fn () {
        status.set_text("value");
    });
    var px: Array[Int] = [];
    var row = 0;
    while (row < 24) {
        var col = 0;
        while (col < 24) {
            var c = Color.Rgb(40 + row * 8, 40 + col * 8, 180);
            px.push(c.value());
            col = col + 1;
        }
        row = row + 1;
    }
    var rgb = ui.make_image(24, 24, px);
    var file = ui.load_image("examples/dot.ppm");
    var quit = Button { text: "Close" };
    quit.clicked.connect(fn () { w.close(); });
    w.add(VStack {
        spacing: 10,
        children: [
            Label { text: "This is a long label that should wrap across several lines when the window is not very wide." },
            s,
            status,
            HStack {
                spacing: 12,
                children: [rgb, file, Spacer {}, quit]
            }
        ]
    });
    w.run();
    return 0;
}
