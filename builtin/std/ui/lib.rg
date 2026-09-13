trait Widget {
    fn min_width(): Int;
    fn height(w: Int): Int;
    fn flex(): Int;
    fn paint(win_id: Int, x: Int, y: Int, w: Int);
    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool;
    fn handle_key(code: Int, text: String): Bool;
    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool;
    fn clear_focus();
    fn append_focusables(out: Array[Widget]);
    fn focus_enter();
    fn has_focus(): Bool;
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int;
}

trait LazyRows {
    fn count(): Int;
    fn row(i: Int): Widget;
}

class Style {
    @optional
    var fill: Color;
    @optional
    var ink: Color;
    var pad: Int = 0;
}

class Pad impl Widget {
    var child: Widget;
    var amount: Int = 0;

    fn min_width(): Int {
        return child.min_width() + amount * 2;
    }

    fn height(w: Int): Int {
        var inner = w - amount * 2;
        if (inner < 0) {
            inner = 0;
        }
        return child.height(inner) + amount * 2;
    }

    fn flex(): Int {
        return child.flex();
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        var inner = w - amount * 2;
        if (inner < 0) {
            inner = 0;
        }
        child.paint(win_id, x + amount, y + amount, inner);
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        if (lx < amount || ly < amount) {
            return false;
        }
        if (lx >= w - amount || ly >= h - amount) {
            return false;
        }
        var inner_w = w - amount * 2;
        var inner_h = h - amount * 2;
        if (inner_w < 0) {
            inner_w = 0;
        }
        if (inner_h < 0) {
            inner_h = 0;
        }
        return child.handle_click(lx - amount, ly - amount, inner_w, inner_h);
    }

    fn handle_key(code: Int, text: String): Bool {
        return child.handle_key(code, text);
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        if (lx < amount || ly < amount) {
            return false;
        }
        if (lx >= w - amount || ly >= h - amount) {
            return false;
        }
        var inner_w = w - amount * 2;
        var inner_h = h - amount * 2;
        if (inner_w < 0) {
            inner_w = 0;
        }
        if (inner_h < 0) {
            inner_h = 0;
        }
        return child.handle_scroll(dx, dy, lx - amount, ly - amount, inner_w, inner_h);
    }

    fn clear_focus() {
        child.clear_focus();
    }

    fn append_focusables(out: Array[Widget]) {
        child.append_focusables(out);
    }

    fn focus_enter() {
        child.focus_enter();
    }

    fn has_focus(): Bool {
        return child.has_focus();
    }

    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        var inner_w = w - amount * 2;
        var inner_h = h - amount * 2;
        if (inner_w < 0) {
            inner_w = 0;
        }
        if (inner_h < 0) {
            inner_h = 0;
        }
        return child.hover_cursor(lx - amount, ly - amount, inner_w, inner_h);
    }
}

class Backdrop impl Widget {
    var child: Widget;
    var color: Color = Color.Black;

    fn min_width(): Int {
        return child.min_width();
    }

    fn height(w: Int): Int {
        return child.height(w);
    }

    fn flex(): Int {
        return child.flex();
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        __ui.fill(win_id, x, y, w, child.height(w), color.value());
        child.paint(win_id, x, y, w);
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return child.handle_click(lx, ly, w, h);
    }

    fn handle_key(code: Int, text: String): Bool {
        return child.handle_key(code, text);
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        return child.handle_scroll(dx, dy, lx, ly, w, h);
    }

    fn clear_focus() {
        child.clear_focus();
    }

    fn append_focusables(out: Array[Widget]) {
        child.append_focusables(out);
    }

    fn focus_enter() {
        child.focus_enter();
    }

    fn has_focus(): Bool {
        return child.has_focus();
    }

    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        return child.hover_cursor(lx, ly, w, h);
    }
}

@ufcs
fn padding(w: Widget, n: Int): Widget {
    return Pad { child: w, amount: n };
}

@ufcs
fn background(w: Widget, color: Color): Widget {
    return Backdrop { child: w, color: color };
}

@ufcs
fn style(w: Widget, s: Style): Widget {
    var out: Widget = w;
    if (s.fill) {
        out = Backdrop { child: out, color: s.fill };
    }
    if (s.pad > 0) {
        out = Pad { child: out, amount: s.pad };
    }
    return out;
}

fn drop_last(s: String): String {
    var n = len(s);
    if (n == 0) {
        return "";
    }
    var out = "";
    var i = 0;
    while (i < n - 1) {
        out = out + s[i];
        i = i + 1;
    }
    return out;
}

fn line_step(): Int {
    var h = font_height() + 2;
    if (h < 18) {
        return 18;
    }
    return h;
}

fn control_height(min_h: Int, pad: Int): Int {
    var h = font_height() + pad;
    if (h < min_h) {
        return min_h;
    }
    return h;
}

fn sb_width(): Int {
    return 14;
}

fn corner_r(): Int {
    return 8;
}

fn fill_round(win_id: Int, x: Int, y: Int, w: Int, h: Int, r: Int, color: Int) {
    __ui.fill_round(win_id, x, y, w, h, r, color);
}

fn stroke_round(win_id: Int, x: Int, y: Int, w: Int, h: Int, r: Int, color: Int) {
    __ui.stroke_round(win_id, x, y, w, h, r, color);
}

fn paint_chevron(win_id: Int, cx: Int, cy: Int, size: Int, up: Bool, color: Int) {
    var s = size;
    if (s < 4) {
        s = 4;
    }
    if (up) {
        __ui.line(win_id, cx - s, cy + s / 2, cx, cy - s / 2, color);
        __ui.line(win_id, cx + s, cy + s / 2, cx, cy - s / 2, color);
        __ui.line(win_id, cx - s + 1, cy + s / 2, cx, cy - s / 2 + 1, color);
        __ui.line(win_id, cx + s - 1, cy + s / 2, cx, cy - s / 2 + 1, color);
    } else {
        __ui.line(win_id, cx - s, cy - s / 2, cx, cy + s / 2, color);
        __ui.line(win_id, cx + s, cy - s / 2, cx, cy + s / 2, color);
        __ui.line(win_id, cx - s + 1, cy - s / 2, cx, cy + s / 2 - 1, color);
        __ui.line(win_id, cx + s - 1, cy - s / 2, cx, cy + s / 2 - 1, color);
    }
}

fn paint_vscroll(win_id: Int, x: Int, y: Int, w: Int, vh: Int, offset: Int, content_h: Int) {
    var max = content_h - vh;
    if (max <= 0 || vh < 1) {
        return;
    }
    var bw = sb_width();
    var tx = x + w - bw;
    fill_round(win_id, tx + 1, y + 1, bw - 2, vh - 2, 5, Color.Rgb(236, 236, 240).value());
    var thumb = (vh * vh) / content_h;
    if (thumb < 24) {
        thumb = 24;
    }
    if (thumb > vh - 4) {
        thumb = vh - 4;
    }
    var track = vh - 4 - thumb;
    var ty = y + 2;
    if (track > 0 && max > 0) {
        ty = y + 2 + (offset * track) / max;
    }
    fill_round(win_id, tx + 3, ty, bw - 6, thumb, 4, Color.Rgb(160, 160, 168).value());
}

fn vscroll_hit(lx: Int, ly: Int, w: Int, vh: Int): Bool {
    return lx >= w - sb_width() && lx < w && ly >= 0 && ly < vh;
}

fn vscroll_offset_at(ly: Int, vh: Int, content_h: Int): Int {
    var max = content_h - vh;
    if (max <= 0 || vh < 1) {
        return 0;
    }
    var thumb = (vh * vh) / content_h;
    if (thumb < 24) {
        thumb = 24;
    }
    if (thumb > vh - 4) {
        thumb = vh - 4;
    }
    var track = vh - 4 - thumb;
    if (track < 1) {
        return 0;
    }
    var y = ly - 2 - thumb / 2;
    if (y < 0) {
        y = 0;
    }
    if (y > track) {
        y = track;
    }
    return (y * max) / track;
}

fn wrap_lines(text: String, max_w: Int): Array[String] {
    var lines: Array[String] = [];
    var width = max_w;
    if (width < 1) {
        width = 1;
    }
    var para = "";
    var i = 0;
    var n = len(text);
    while (i <= n) {
        var at_end = i == n;
        var ch = "";
        if (!at_end) {
            ch = text[i];
        }
        if (at_end || ch == "\n") {
            if (len(para) == 0) {
                lines.push("");
            } else {
                var word = "";
                var line = "";
                var j = 0;
                while (j <= len(para)) {
                    var done = j == len(para);
                    var c = "";
                    if (!done) {
                        c = para[j];
                    }
                    if (done || c == " ") {
                        var candidate = word;
                        if (len(line) > 0) {
                            candidate = line + " " + word;
                        }
                        if (len(word) > 0 && text_width(candidate) <= width) {
                            line = candidate;
                        } else {
                            if (len(line) > 0) {
                                lines.push(line);
                            }
                            if (text_width(word) <= width) {
                                line = word;
                            } else {
                                var piece = "";
                                var k = 0;
                                while (k < len(word)) {
                                    var next = piece + word[k];
                                    if (len(piece) > 0 && text_width(next) > width) {
                                        lines.push(piece);
                                        piece = word[k];
                                    } else {
                                        piece = next;
                                    }
                                    k = k + 1;
                                }
                                line = piece;
                            }
                        }
                        word = "";
                    } else {
                        word = word + c;
                    }
                    j = j + 1;
                }
                if (len(line) > 0) {
                    lines.push(line);
                }
            }
            para = "";
            if (at_end) {
                break;
            }
        } else {
            para = para + ch;
        }
        i = i + 1;
    }
    if (len(lines) == 0) {
        lines.push("");
    }
    return lines;
}

fn wrapped_height(text: String, max_w: Int): Int {
    var lines = wrap_lines(text, max_w);
    var step = line_step();
    var h = len(lines) * step;
    if (h < step) {
        h = step;
    }
    return h;
}

class Window {
    var id: Int = 0;
    var title: String = "RoseGold";
    var width: Int = 800;
    var height: Int = 600;
    var visible: Bool = true;
    var theme: Theme = Theme {};
    @optional
    var children: Array[Widget];

    fn bind_frame() {
        __ui.set_frame(id, fn () { self.tick(); });
    }

    fn root_widget(): Widget {
        return VStack { spacing: 8, children: children }.padding(12);
    }

    fn show() throws {
        if (id == 0) {
            id = try __ui.open(title, width, height, visible);
            title = __ui.title(id);
            width = __ui.width(id);
            height = __ui.height(id);
            bind_frame();
            tick();
            return;
        }
        visible = true;
        __ui.show(id);
        tick();
    }

    fn hide() {
        visible = false;
        if (id == 0) {
            return;
        }
        __ui.hide(id);
    }

    fn close() {
        if (id == 0) {
            return;
        }
        __ui.close(id);
        id = 0;
    }

    fn poll(): Bool {
        if (id == 0) {
            return false;
        }
        var ok = __ui.poll(id);
        width = __ui.width(id);
        height = __ui.height(id);
        tick();
        return ok;
    }

    fn alive(): Bool {
        if (id == 0) {
            return false;
        }
        return __ui.alive(id);
    }

    fn set_title(s: String) {
        title = s;
        if (id != 0) {
            __ui.set_title(id, s);
        }
    }

    fn set_size(w: Int, h: Int) {
        width = w;
        height = h;
        if (id != 0) {
            __ui.set_size(id, w, h);
            width = __ui.width(id);
            height = __ui.height(id);
            tick();
        }
    }

    fn add(w: Widget): Window {
        children.push(w);
        tick();
        return self;
    }

    fn focus_step(back: Bool) {
        if (len(children) == 0) {
            return;
        }
        var root = root_widget();
        var xs: Array[Widget] = [];
        root.append_focusables(xs);
        var n = len(xs);
        if (n < 1) {
            return;
        }
        var cur = -1;
        var i = 0;
        while (i < n) {
            if (xs[i].has_focus()) {
                cur = i;
            }
            i = i + 1;
        }
        var next = 0;
        if (back) {
            if (cur <= 0) {
                next = n - 1;
            } else {
                next = cur - 1;
            }
        } else {
            if (cur < 0 || cur >= n - 1) {
                next = 0;
            } else {
                next = cur + 1;
            }
        }
        root.clear_focus();
        xs[next].focus_enter();
    }

    fn tick() {
        if (id == 0) {
            return;
        }
        width = __ui.width(id);
        height = __ui.height(id);
        if (len(children) == 0) {
            return;
        }
        var root = root_widget();
        var rh = root.height(width);
        if (__ui.take_click(id)) {
            var mx = __ui.mouse_x(id);
            var my = __ui.mouse_y(id);
            root.clear_focus();
            root.handle_click(mx, my, width, rh);
        }
        while (__ui.take_key(id)) {
            var code = __ui.key_code(id);
            var text = __ui.key_text(id);
            if (code == 9) {
                focus_step(text == "shift");
            } else {
                root.handle_key(code, text);
            }
        }
        if (__ui.take_scroll(id)) {
            root.handle_scroll(__ui.scroll_dx(id), __ui.scroll_dy(id),
                               __ui.mouse_x(id), __ui.mouse_y(id), width, rh);
        }
        __ui.clear(id, theme.window_bg.value());
        root.paint(id, 0, 0, width);
        __ui.cursor(id, root.hover_cursor(__ui.mouse_x(id), __ui.mouse_y(id), width, rh));
        __ui.present(id);
    }

    fn hover_at(x: Int, y: Int) {
        if (id == 0) {
            return;
        }
        __ui.feed_mouse(id, x, y);
        tick();
    }

    fn drag_to(x: Int, y: Int) {
        if (id == 0) {
            return;
        }
        __ui.feed_down(id, true);
        __ui.feed_mouse(id, x, y);
        tick();
    }

    fn drag_end(x: Int, y: Int) {
        if (id == 0) {
            return;
        }
        __ui.feed_mouse(id, x, y);
        __ui.feed_down(id, false);
        tick();
    }

    fn click_at(x: Int, y: Int) {
        if (id == 0) {
            return;
        }
        __ui.feed_click(id, x, y);
        tick();
    }

    fn key_at(code: Int, text: String) {
        if (id == 0) {
            return;
        }
        __ui.feed_key(id, code, text);
        tick();
    }

    fn scroll_at(dx: Int, dy: Int, x: Int, y: Int) {
        if (id == 0) {
            return;
        }
        __ui.feed_click(id, x, y);
        __ui.take_click(id);
        __ui.feed_scroll(id, dx, dy);
        tick();
    }

    fn run() {
        while (alive()) {
            __ui.wait();
            poll();
        }
    }
}

fn open(title: String, width: Int, height: Int) throws: Window {
    var id = try __ui.open(title, width, height, true);
    var w = Window {
        id: id,
        title: __ui.title(id),
        width: __ui.width(id),
        height: __ui.height(id)
    };
    w.bind_frame();
    return w;
}

fn open_hidden(title: String, width: Int, height: Int) throws: Window {
    var id = try __ui.open(title, width, height, false);
    var w = Window {
        id: id,
        title: __ui.title(id),
        width: __ui.width(id),
        height: __ui.height(id)
    };
    w.bind_frame();
    return w;
}

fn cursor_arrow(win_id: Int) {
    __ui.cursor(win_id, 0);
}

fn cursor_hand(win_id: Int) {
    __ui.cursor(win_id, 1);
}

fn cursor_ibeam(win_id: Int) {
    __ui.cursor(win_id, 2);
}

fn pointer_over(win_id: Int, x: Int, y: Int, w: Int, h: Int): Bool {
    var mx = __ui.mouse_x(win_id);
    var my = __ui.mouse_y(win_id);
    return hit_test(mx - x, my - y, w, h);
}

fn hit_test(lx: Int, ly: Int, w: Int, h: Int): Bool {
    // 2px slop so borders / AA edges keep hover.
    return lx >= -2 && lx < w + 2 && ly >= -2 && ly < h + 2;
}

fn font_height(): Int {
    return __ui.font_height();
}

fn text_width(s: String): Int {
    return __ui.text_width(s);
}

fn run() {
    __ui.run();
}

fn count(): Int {
    return __ui.count();
}

fn backend(): String {
    return __ui.backend();
}

fn platform(): String {
    return __ui.platform();
}

fn kind(): String {
    var p = platform();
    if (p == "android" || p == "ios") {
        return "mobile";
    }
    if (p == "web") {
        return "web";
    }
    return "desktop";
}
