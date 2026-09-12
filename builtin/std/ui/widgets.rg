class VStack impl Widget {
    var spacing: Int = 8;
    @optional
    var children: Array[Widget];

    fn add(child: Widget): VStack {
        children.push(child);
        return self;
    }

    fn height(): Int {
        var h = 0;
        var i = 0;
        while (i < len(children)) {
            if (i > 0) {
                h = h + spacing;
            }
            h = h + children[i].height();
            i = i + 1;
        }
        return h;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        var cy = y;
        var i = 0;
        while (i < len(children)) {
            if (i > 0) {
                cy = cy + spacing;
            }
            var child = children[i];
            child.paint(win_id, x, cy, w);
            cy = cy + child.height();
            i = i + 1;
        }
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        var cy = 0;
        var i = 0;
        while (i < len(children)) {
            if (i > 0) {
                cy = cy + spacing;
            }
            var child = children[i];
            var ch = child.height();
            if (lx >= 0 && lx < w && ly >= cy && ly < cy + ch) {
                return child.handle_click(lx, ly - cy, w, ch);
            }
            cy = cy + ch;
            i = i + 1;
        }
        return false;
    }
}

class HStack impl Widget {
    var spacing: Int = 8;
    @optional
    var children: Array[Widget];

    fn add(child: Widget): HStack {
        children.push(child);
        return self;
    }

    fn height(): Int {
        var h = 0;
        var i = 0;
        while (i < len(children)) {
            var ch = children[i].height();
            if (ch > h) {
                h = ch;
            }
            i = i + 1;
        }
        return h;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        var n = len(children);
        if (n == 0) {
            return;
        }
        var gaps = spacing * (n - 1);
        var inner = w - gaps;
        if (inner < 0) {
            inner = 0;
        }
        var cx = x;
        var i = 0;
        while (i < n) {
            var cw = inner / n;
            if (i == n - 1) {
                cw = x + w - cx;
            }
            children[i].paint(win_id, cx, y, cw);
            cx = cx + cw + spacing;
            i = i + 1;
        }
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        var n = len(children);
        if (n == 0) {
            return false;
        }
        var gaps = spacing * (n - 1);
        var inner = w - gaps;
        if (inner < 0) {
            inner = 0;
        }
        var cx = 0;
        var i = 0;
        while (i < n) {
            var cw = inner / n;
            if (i == n - 1) {
                cw = w - cx;
            }
            if (lx >= cx && lx < cx + cw && ly >= 0 && ly < h) {
                return children[i].handle_click(lx - cx, ly, cw, h);
            }
            cx = cx + cw + spacing;
            i = i + 1;
        }
        return false;
    }
}

class Label impl Widget {
    var text: String = "";
    var color: Color = Color.Rgb(32, 32, 32);

    fn set_text(s: String) {
        text = s;
    }

    fn foreground(c: Color): Label {
        color = c;
        return self;
    }

    fn style(s: Style): Widget {
        if (s.ink) {
            color = s.ink;
        }
        var out: Widget = self;
        if (s.fill) {
            out = Backdrop { child: out, color: s.fill };
        }
        if (s.pad > 0) {
            out = Pad { child: out, amount: s.pad };
        }
        return out;
    }

    fn height(): Int {
        return 18;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        var ty = y + (18 - font_height()) / 2;
        __ui.text(win_id, x, ty, text, color.value());
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }
}

class Button impl Widget {
    var text: String = "OK";
    var fill: Color = Color.Rgb(47, 111, 196);
    var color: Color = Color.White;
    signal clicked();

    fn set_text(s: String) {
        text = s;
    }

    fn background(c: Color): Button {
        fill = c;
        return self;
    }

    fn foreground(c: Color): Button {
        color = c;
        return self;
    }

    fn style(s: Style): Widget {
        if (s.ink) {
            color = s.ink;
        }
        if (s.fill) {
            fill = s.fill;
        }
        var out: Widget = self;
        if (s.pad > 0) {
            out = Pad { child: out, amount: s.pad };
        }
        return out;
    }

    fn height(): Int {
        return 32;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        __ui.fill(win_id, x, y, w, 32, fill.value());
        var tw = text_width(text);
        var tx = x + 8;
        if (w > tw) {
            tx = x + (w - tw) / 2;
        }
        var ty = y + (32 - font_height()) / 2;
        __ui.text(win_id, tx, ty, text, color.value());
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        clicked.emit();
        return true;
    }
}