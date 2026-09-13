import ui;

async fn main(): Int {
    var w = try ui.open_hidden("rg-async-frame", 320, 240);
    checks.that(w.alive());
    checks.that(await w.next_frame());
    checks.that(w.alive());
    w.close();
    checks.that(!(await w.next_frame()));
    return 0;
}
