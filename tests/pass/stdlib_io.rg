import io;

fn main(): Int {
    const path = "._rg_io_scratch.txt";
    io.remove(path);
    checks.that(!io.exists(path));
    try io.write_text(path, "hi\nthere");
    checks.that(io.exists(path));
    checks.eq_string(try io.read_text(path), "hi\nthere");
    var lines = try io.read_lines(path);
    checks.eq(len(lines), 2);
    checks.eq_string(lines[0], "hi");
    checks.eq_string(lines[1], "there");
    checks.that(io.remove(path));
    checks.that(!io.exists(path));

    do {
        try io.read_text("._rg_io_missing_xyz.txt");
        checks.eq(1, 0);
    } catch e {
        checks.that(len(e) > 0);
    }
    return 0;
}
