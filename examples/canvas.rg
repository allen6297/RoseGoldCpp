import ui;

fn main(): Int {
    var w = try ui.open("Canvas", 400, 280);
    var chart = Canvas { w_hint: 300, h_px: 140 };
    w.add(VStack {
        spacing: 10,
        children: [
            Label { text: "Canvas stroke demo" },
            chart.padding(4).background(Color.Rgb(230, 230, 230))
        ]
    });
    w.run();
    return 0;
}
