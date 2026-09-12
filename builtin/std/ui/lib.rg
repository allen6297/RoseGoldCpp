trait Widget {
    fn height(): Int;
    fn paint(win_id: Int, x: Int, y: Int, w: Int);
    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool;
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

    fn height(): Int {
        return child.height() + amount * 2;
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
}

class Backdrop impl Widget {
    var child: Widget;
    var color: Color = Color.Black;

    fn height(): Int {
        return child.height();
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        __ui.fill(win_id, x, y, w, child.height(), color.value());
        child.paint(win_id, x, y, w);
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return child.handle_click(lx, ly, w, h);
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

class Window {
    var id: Int = 0;
    var title: String = "RoseGold";
    var width: Int = 800;
    var height: Int = 600;
    var visible: Bool = true;
    @optional
    var children: Array[Widget];

    fn bind_frame() {
        __ui.set_frame(id, fn () { self.tick(); });
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

    fn tick() {
        if (id == 0) {
            return;
        }
        width = __ui.width(id);
        height = __ui.height(id);
        if (len(children) == 0) {
            return;
        }
        __ui.clear(id, 242 * 65536 + 242 * 256 + 242);
        var pad = 12;
        var inner = width - pad * 2;
        if (inner < 1) {
            inner = 1;
        }
        var y = pad;
        var i = 0;
        while (i < len(children)) {
            var child = children[i];
            var h = child.height();
            child.paint(id, pad, y, inner);
            y = y + h + 8;
            i = i + 1;
        }
        __ui.present(id);
        if (__ui.take_click(id)) {
            var mx = __ui.mouse_x(id);
            var my = __ui.mouse_y(id);
            y = pad;
            i = 0;
            while (i < len(children)) {
                var child = children[i];
                var h = child.height();
                if (mx >= pad && mx < pad + inner && my >= y && my < y + h) {
                    child.handle_click(mx - pad, my - y, inner, h);
                }
                y = y + h + 8;
                i = i + 1;
            }
        }
    }

    fn click_at(x: Int, y: Int) {
        if (id == 0) {
            return;
        }
        __ui.feed_click(id, x, y);
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
