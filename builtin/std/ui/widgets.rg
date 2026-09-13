class VStack impl Widget {
    var spacing: Int = 8;
    @optional
    var children: Array[Widget];

    fn add(child: Widget): VStack {
        children.push(child);
        return self;
    }

    fn min_width(): Int {
        var mw = 0;
        var i = 0;
        while (i < len(children)) {
            var cw = children[i].min_width();
            if (cw > mw) {
                mw = cw;
            }
            i = i + 1;
        }
        return mw;
    }

    fn height(w: Int): Int {
        var h = 0;
        var i = 0;
        while (i < len(children)) {
            if (i > 0) {
                h = h + spacing;
            }
            h = h + children[i].height(w);
            i = i + 1;
        }
        return h;
    }

    fn flex(): Int {
        return 0;
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
            cy = cy + child.height(w);
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
            var ch = child.height(w);
            if (lx >= 0 && lx < w && ly >= cy && ly < cy + ch) {
                return child.handle_click(lx, ly - cy, w, ch);
            }
            cy = cy + ch;
            i = i + 1;
        }
        return false;
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        var cy = 0;
        var i = 0;
        while (i < len(children)) {
            if (i > 0) {
                cy = cy + spacing;
            }
            var child = children[i];
            var ch = child.height(w);
            if (lx >= 0 && lx < w && ly >= cy && ly < cy + ch) {
                return child.handle_right_click(lx, ly - cy, w, ch);
            }
            cy = cy + ch;
            i = i + 1;
        }
        return false;
    }

    fn handle_key(code: Int, text: String): Bool {
        var i = 0;
        while (i < len(children)) {
            if (children[i].handle_key(code, text)) {
                return true;
            }
            i = i + 1;
        }
        return false;
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        var cy = 0;
        var i = 0;
        while (i < len(children)) {
            if (i > 0) {
                cy = cy + spacing;
            }
            var child = children[i];
            var ch = child.height(w);
            if (lx >= 0 && lx < w && ly >= cy && ly < cy + ch) {
                return child.handle_scroll(dx, dy, lx, ly - cy, w, ch);
            }
            cy = cy + ch;
            i = i + 1;
        }
        return false;
    }

    fn clear_focus() {
        var i = 0;
        while (i < len(children)) {
            children[i].clear_focus();
            i = i + 1;
        }
    }

    fn append_focusables(out: Array[Widget]) {
        var i = 0;
        while (i < len(children)) {
            children[i].append_focusables(out);
            i = i + 1;
        }
    }

    fn focus_enter() {
    }

    fn has_focus(): Bool {
        var i = 0;
        while (i < len(children)) {
            if (children[i].has_focus()) {
                return true;
            }
            i = i + 1;
        }
        return false;
    }
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        var cy = 0;
        var i = 0;
        while (i < len(children)) {
            if (i > 0) {
                cy = cy + spacing;
            }
            var child = children[i];
            var ch = child.height(w);
            if (hit_test(lx, ly - cy, w, ch)) {
                var c = child.hover_cursor(lx, ly - cy, w, ch);
                if (c != 0) {
                    return c;
                }
            }
            cy = cy + ch;
            i = i + 1;
        }
        return 0;
    }
}

class HStack impl Widget {
    var spacing: Int = 8;
    var align: String = "fill";
    @optional
    var children: Array[Widget];

    fn add(child: Widget): HStack {
        children.push(child);
        return self;
    }

    fn min_width(): Int {
        var n = len(children);
        if (n == 0) {
            return 0;
        }
        var total = spacing * (n - 1);
        var i = 0;
        while (i < n) {
            total = total + children[i].min_width();
            i = i + 1;
        }
        return total;
    }

    fn height(w: Int): Int {
        var widths = child_widths(w);
        var h = 0;
        var i = 0;
        while (i < len(children)) {
            var ch = children[i].height(widths[i]);
            if (ch > h) {
                h = ch;
            }
            i = i + 1;
        }
        return h;
    }

    fn flex(): Int {
        return 0;
    }

    fn child_widths(w: Int): Array[Int] {
        var n = len(children);
        var widths: Array[Int] = [];
        if (n == 0) {
            return widths;
        }
        var gaps = spacing * (n - 1);
        var inner = w - gaps;
        if (inner < 0) {
            inner = 0;
        }
        var mins = 0;
        var flex_total = 0;
        var i = 0;
        while (i < n) {
            mins = mins + children[i].min_width();
            flex_total = flex_total + children[i].flex();
            i = i + 1;
        }
        var leftover = inner - mins;
        if (leftover < 0) {
            leftover = 0;
        }
        if (flex_total > 0) {
            var given = 0;
            i = 0;
            while (i < n) {
                var cw = children[i].min_width();
                var f = children[i].flex();
                if (f > 0) {
                    cw = cw + leftover * f / flex_total;
                }
                widths.push(cw);
                if (i < n - 1) {
                    given = given + cw + spacing;
                }
                i = i + 1;
            }
            if (n > 0) {
                var last = w - given;
                if (last < children[n - 1].min_width()) {
                    last = children[n - 1].min_width();
                }
                widths[n - 1] = last;
            }
            return widths;
        }
        if (align != "fill") {
            i = 0;
            while (i < n) {
                widths.push(children[i].min_width());
                i = i + 1;
            }
            return widths;
        }
        var base = leftover / n;
        var rem = leftover - base * n;
        i = 0;
        while (i < n) {
            var cw = children[i].min_width() + base;
            if (i < rem) {
                cw = cw + 1;
            }
            widths.push(cw);
            i = i + 1;
        }
        var used = 0;
        i = 0;
        while (i < n - 1) {
            used = used + widths[i] + spacing;
            i = i + 1;
        }
        if (n > 0) {
            var last = w - used;
            if (last < children[n - 1].min_width()) {
                last = children[n - 1].min_width();
            }
            widths[n - 1] = last;
        }
        return widths;
    }

    fn origin_x(w: Int, widths: Array[Int]): Int {
        if (align == "fill" || align == "leading") {
            return 0;
        }
        var n = len(widths);
        if (n == 0) {
            return 0;
        }
        var total = spacing * (n - 1);
        var i = 0;
        while (i < n) {
            total = total + widths[i];
            i = i + 1;
        }
        var extra = w - total;
        if (extra < 0) {
            extra = 0;
        }
        if (align == "center") {
            return extra / 2;
        }
        if (align == "trailing") {
            return extra;
        }
        return 0;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        var n = len(children);
        if (n == 0) {
            return;
        }
        var widths = child_widths(w);
        var stack_h = height(w);
        var cx = x + origin_x(w, widths);
        var i = 0;
        while (i < n) {
            var cw = widths[i];
            var ch = children[i].height(cw);
            var oy = (stack_h - ch) / 2;
            if (oy < 0) {
                oy = 0;
            }
            children[i].paint(win_id, cx, y + oy, cw);
            cx = cx + cw + spacing;
            i = i + 1;
        }
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        var n = len(children);
        if (n == 0) {
            return false;
        }
        var widths = child_widths(w);
        var cx = origin_x(w, widths);
        var i = 0;
        while (i < n) {
            var cw = widths[i];
            var ch = children[i].height(cw);
            var oy = (h - ch) / 2;
            if (oy < 0) {
                oy = 0;
            }
            if (hit_test(lx - cx, ly - oy, cw, ch)) {
                return children[i].handle_click(lx - cx, ly - oy, cw, ch);
            }
            cx = cx + cw + spacing;
            i = i + 1;
        }
        return false;
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        var n = len(children);
        if (n == 0) {
            return false;
        }
        var widths = child_widths(w);
        var cx = origin_x(w, widths);
        var i = 0;
        while (i < n) {
            var cw = widths[i];
            var ch = children[i].height(cw);
            var oy = (h - ch) / 2;
            if (oy < 0) {
                oy = 0;
            }
            if (hit_test(lx - cx, ly - oy, cw, ch)) {
                return children[i].handle_right_click(lx - cx, ly - oy, cw, ch);
            }
            cx = cx + cw + spacing;
            i = i + 1;
        }
        return false;
    }

    fn handle_key(code: Int, text: String): Bool {
        var i = 0;
        while (i < len(children)) {
            if (children[i].handle_key(code, text)) {
                return true;
            }
            i = i + 1;
        }
        return false;
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        var n = len(children);
        if (n == 0) {
            return false;
        }
        var widths = child_widths(w);
        var cx = origin_x(w, widths);
        var i = 0;
        while (i < n) {
            var cw = widths[i];
            var ch = children[i].height(cw);
            var oy = (h - ch) / 2;
            if (oy < 0) {
                oy = 0;
            }
            if (hit_test(lx - cx, ly - oy, cw, ch)) {
                return children[i].handle_scroll(dx, dy, lx - cx, ly - oy, cw, ch);
            }
            cx = cx + cw + spacing;
            i = i + 1;
        }
        return false;
    }

    fn clear_focus() {
        var i = 0;
        while (i < len(children)) {
            children[i].clear_focus();
            i = i + 1;
        }
    }

    fn append_focusables(out: Array[Widget]) {
        var i = 0;
        while (i < len(children)) {
            children[i].append_focusables(out);
            i = i + 1;
        }
    }

    fn focus_enter() {
    }

    fn has_focus(): Bool {
        var i = 0;
        while (i < len(children)) {
            if (children[i].has_focus()) {
                return true;
            }
            i = i + 1;
        }
        return false;
    }
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        var n = len(children);
        if (n == 0) {
            return 0;
        }
        var widths = child_widths(w);
        var cx = origin_x(w, widths);
        var i = 0;
        while (i < n) {
            var cw = widths[i];
            var ch = children[i].height(cw);
            var oy = (h - ch) / 2;
            if (oy < 0) {
                oy = 0;
            }
            if (hit_test(lx - cx, ly - oy, cw, ch)) {
                var c = children[i].hover_cursor(lx - cx, ly - oy, cw, ch);
                if (c != 0) {
                    return c;
                }
            }
            cx = cx + cw + spacing;
            i = i + 1;
        }
        return 0;
    }
}

class Spacer impl Widget {
    var min_w: Int = 0;
    var min_h: Int = 0;

    fn min_width(): Int {
        return min_w;
    }

    fn height(w: Int): Int {
        return min_h;
    }

    fn flex(): Int {
        return 1;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }


    fn handle_key(code: Int, text: String): Bool {
        return false;
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }

    fn clear_focus() {
    }

    fn append_focusables(out: Array[Widget]) {
    }

    fn focus_enter() {
    }

    fn has_focus(): Bool {
        return false;
    }
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        return 0;
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

    fn min_width(): Int {
        var lines = wrap_lines(text, 100000);
        var mw = 0;
        var i = 0;
        while (i < len(lines)) {
            var tw = text_width(lines[i]);
            if (tw > mw) {
                mw = tw;
            }
            i = i + 1;
        }
        return mw;
    }

    fn height(w: Int): Int {
        return wrapped_height(text, w);
    }

    fn flex(): Int {
        return 0;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        var lines = wrap_lines(text, w);
        var step = line_step();
        var i = 0;
        while (i < len(lines)) {
            __ui.text(win_id, x, y + i * step, lines[i], color.value());
            i = i + 1;
        }
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }


    fn handle_key(code: Int, text: String): Bool {
        return false;
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }

    fn clear_focus() {
    }

    fn append_focusables(out: Array[Widget]) {
    }

    fn focus_enter() {
    }

    fn has_focus(): Bool {
        return false;
    }
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        return 0;
    }

}

class Button impl Widget {
    var text: String = "OK";
    var fill: Color = Color.Rgb(47, 111, 196);
    var hover: Color = Color.Rgb(66, 133, 220);
    var pressed: Color = Color.Rgb(30, 80, 150);
    var color: Color = Color.White;
    var hot: Bool = false;
    var down: Bool = false;
    var focused: Bool = false;
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

    fn min_width(): Int {
        return text_width(text) + 16;
    }

    fn height(w: Int): Int {
        return control_height(32, 12);
    }

    fn flex(): Int {
        return 0;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        var h = height(w);
        hot = pointer_over(win_id, x, y, w, h);
        if (hot) {
            cursor_hand(win_id);
        }
        down = hot && __ui.mouse_down(win_id);
        var bg = fill;
        if (down) {
            bg = pressed;
        } elif (hot) {
            bg = hover;
        }
        fill_round(win_id, x, y, w, h, corner_r(), bg.value());
        if (focused) {
            stroke_round(win_id, x, y, w, h, corner_r(), Color.Rgb(47, 111, 196).value());
        }
        var tw = text_width(text);
        var tx = x + 8;
        if (w > tw) {
            tx = x + (w - tw) / 2;
        }
        var ty = y + (h - font_height()) / 2;
        __ui.text(win_id, tx, ty, text, color.value());
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        focused = true;
        clicked.emit();
        return true;
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }


    fn handle_key(code: Int, text: String): Bool {
        if (focused && code == 13) {
            clicked.emit();
            return true;
        }
        return false;
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }

    fn clear_focus() {
        focused = false;
    }

    fn append_focusables(out: Array[Widget]) {
        out.push(self);
    }

    fn focus_enter() {
        focused = true;
    }

    fn has_focus(): Bool {
        return focused;
    }
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        if (hit_test(lx, ly, w, h)) {
            return 1;
        }
        return 0;
    }

}

class TextField impl Widget {
    var text: String = "";
    var placeholder: String = "";
    var focused: Bool = false;
    var fill: Color = Color.White;
    var ink: Color = Color.Rgb(32, 32, 32);
    var border: Color = Color.Rgb(160, 160, 160);
    signal changed();
    signal submitted();

    fn set_text(s: String) {
        text = s;
        changed.emit();
    }

    fn min_width(): Int {
        return 120;
    }

    fn height(w: Int): Int {
        return control_height(28, 10);
    }

    fn flex(): Int {
        return 0;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        var h = height(w);
        if (pointer_over(win_id, x, y, w, h)) {
            cursor_ibeam(win_id);
        }
        fill_round(win_id, x, y, w, h, corner_r(), fill.value());
        var bcol = border;
        if (focused) {
            bcol = Color.Rgb(47, 111, 196);
        }
        stroke_round(win_id, x, y, w, h, corner_r(), bcol.value());
        var shown = text;
        var col = ink;
        if (len(shown) == 0 && !focused) {
            shown = placeholder;
            col = Color.Rgb(140, 140, 140);
        }
        var ty = y + (h - font_height()) / 2;
        __ui.text(win_id, x + 6, ty, shown, col.value());
        if (focused) {
            var cx = x + 6 + text_width(text);
            var ch = font_height();
            if (ch < 12) {
                ch = 12;
            }
            if (ch > h - 8) {
                ch = h - 8;
            }
            __ui.fill(win_id, cx, y + (h - ch) / 2, 1, ch, ink.value());
        }
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        focused = true;
        return true;
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }


    fn handle_key(code: Int, text_in: String): Bool {
        if (!focused) {
            return false;
        }
        if (code == 8) {
            text = drop_last(text);
            changed.emit();
            return true;
        }
        if (code == 13) {
            submitted.emit();
            return true;
        }
        if (len(text_in) > 0) {
            text = text + text_in;
            changed.emit();
            return true;
        }
        return true;
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }

    fn clear_focus() {
        focused = false;
    }

    fn append_focusables(out: Array[Widget]) {
        out.push(self);
    }

    fn focus_enter() {
        focused = true;
    }

    fn has_focus(): Bool {
        return focused;
    }
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        if (hit_test(lx, ly, w, h)) {
            return 2;
        }
        return 0;
    }

}

class Toggle impl Widget {
    var on: Bool = false;
    var label: String = "";
    var focused: Bool = false;
    signal changed();

    fn set_on(v: Bool) {
        on = v;
        changed.emit();
    }

    fn min_width(): Int {
        return 50 + text_width(label);
    }

    fn height(w: Int): Int {
        return control_height(24, 8);
    }

    fn flex(): Int {
        return 0;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        var h = height(w);
        if (pointer_over(win_id, x, y, w, h)) {
            cursor_hand(win_id);
        }
        var track = Color.Rgb(180, 180, 180);
        if (on) {
            track = Color.Rgb(47, 111, 196);
        }
        var mid = y + (h - 22) / 2;
        fill_round(win_id, x, mid, 42, 22, 11, track.value());
        var knob_x = x + 3;
        if (on) {
            knob_x = x + 21;
        }
        fill_round(win_id, knob_x, mid + 2, 18, 18, 9, Color.White.value());
        stroke_round(win_id, knob_x, mid + 2, 18, 18, 9, Color.Rgb(140, 140, 148).value());
        if (len(label) > 0) {
            var ty = y + (h - font_height()) / 2;
            __ui.text(win_id, x + 50, ty, label, Color.Rgb(32, 32, 32).value());
        }
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        focused = true;
        on = !on;
        changed.emit();
        return true;
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }


    fn handle_key(code: Int, text: String): Bool {
        if (focused && (code == 13 || code == 32)) {
            on = !on;
            changed.emit();
            return true;
        }
        return false;
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }

    fn clear_focus() {
        focused = false;
    }

    fn append_focusables(out: Array[Widget]) {
        out.push(self);
    }

    fn focus_enter() {
        focused = true;
    }

    fn has_focus(): Bool {
        return focused;
    }
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        if (hit_test(lx, ly, w, h)) {
            return 1;
        }
        return 0;
    }

}

class ScrollView impl Widget {
    var child: Widget;
    var viewport_h: Int = 120;
    var offset: Int = 0;
    var layout_w: Int = 0;
    var bar_drag: Bool = false;

    fn min_width(): Int {
        return child.min_width();
    }

    fn height(w: Int): Int {
        return viewport_h;
    }

    fn flex(): Int {
        return 0;
    }

    fn content_width(w: Int): Int {
        if (w < 1) {
            return 1;
        }
        if (child.height(w) > viewport_h) {
            var cw = w - sb_width();
            if (cw < 1) {
                return 1;
            }
            return cw;
        }
        if (w > sb_width()) {
            var cw = w - sb_width();
            if (child.height(cw) > viewport_h) {
                return cw;
            }
        }
        return w;
    }

    fn content_h(): Int {
        var w = layout_w;
        if (w < 1) {
            w = 1;
        }
        return child.height(content_width(w));
    }

    fn max_offset(): Int {
        var max = content_h() - viewport_h;
        if (max < 0) {
            max = 0;
        }
        return max;
    }

    fn clamp_offset() {
        if (offset < 0) {
            offset = 0;
        }
        var max = max_offset();
        if (offset > max) {
            offset = max;
        }
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        layout_w = w;
        clamp_offset();
        if (bar_drag && __ui.mouse_down(win_id)) {
            var my = __ui.mouse_y(win_id) - y;
            offset = vscroll_offset_at(my, viewport_h, content_h());
            clamp_offset();
        }
        if (!__ui.mouse_down(win_id)) {
            bar_drag = false;
        }
        var cw = content_width(w);
        __ui.clip_push(win_id, x, y, cw, viewport_h);
        child.paint(win_id, x, y - offset, cw);
        __ui.clip_pop(win_id);
        paint_vscroll(win_id, x, y, w, viewport_h, offset, content_h());
        stroke_round(win_id, x, y, w, viewport_h, corner_r(), Color.Rgb(200, 200, 200).value());
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        layout_w = w;
        if (ly < 0 || ly >= viewport_h || lx < 0 || lx >= w) {
            return false;
        }
        if (vscroll_hit(lx, ly, w, viewport_h) && max_offset() > 0) {
            bar_drag = true;
            offset = vscroll_offset_at(ly, viewport_h, content_h());
            clamp_offset();
            return true;
        }
        var cw = content_width(w);
        if (lx >= cw) {
            return false;
        }
        return child.handle_click(lx, ly + offset, cw, child.height(cw));
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        layout_w = w;
        if (ly < 0 || ly >= viewport_h || lx < 0 || lx >= w) {
            return false;
        }
        if (vscroll_hit(lx, ly, w, viewport_h) && max_offset() > 0) {
            return true;
        }
        var cw = content_width(w);
        if (lx >= cw) {
            return false;
        }
        return child.handle_right_click(lx, ly + offset, cw, child.height(cw));
    }

    fn handle_key(code: Int, text: String): Bool {
        return child.handle_key(code, text);
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        layout_w = w;
        if (ly < 0 || ly >= viewport_h || lx < 0 || lx >= w) {
            return false;
        }
        offset = offset - dy;
        clamp_offset();
        return true;
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
        layout_w = w;
        if (!hit_test(lx, ly, w, viewport_h)) {
            return 0;
        }
        if (vscroll_hit(lx, ly, w, viewport_h) && max_offset() > 0) {
            return 1;
        }
        var cw = content_width(w);
        if (lx >= cw) {
            return 0;
        }
        return child.hover_cursor(lx, ly + offset, cw, child.height(cw));
    }
}

class Canvas impl Widget {
    var w_hint: Int = 160;
    var h_px: Int = 100;
    var fill: Color = Color.Rgb(250, 250, 250);
    var stroke: Color = Color.Rgb(47, 111, 196);
    signal redraw(win_id: Int, x: Int, y: Int, w: Int, h: Int);

    fn min_width(): Int {
        return w_hint;
    }

    fn height(w: Int): Int {
        return h_px;
    }

    fn flex(): Int {
        return 0;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        __ui.fill(win_id, x, y, w, h_px, fill.value());
        __ui.stroke_rect(win_id, x, y, w, h_px, stroke.value());
        __ui.line(win_id, x + 8, y + h_px - 8, x + w - 8, y + 8, stroke.value());
        redraw.emit(win_id, x, y, w, h_px);
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }


    fn handle_key(code: Int, text: String): Bool {
        return false;
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }

    fn clear_focus() {
    }

    fn append_focusables(out: Array[Widget]) {
    }

    fn focus_enter() {
    }

    fn has_focus(): Bool {
        return false;
    }
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        return 0;
    }

}

class Slider impl Widget {
    var value: Int = 0;
    var min_v: Int = 0;
    var max_v: Int = 100;
    var track: Color = Color.Rgb(180, 180, 180);
    var fill: Color = Color.Rgb(47, 111, 196);
    var dragging: Bool = false;
    var focused: Bool = false;
    signal changed();

    fn set_value(v: Int) {
        var next = v;
        if (next < min_v) {
            next = min_v;
        }
        if (next > max_v) {
            next = max_v;
        }
        if (next != value) {
            value = next;
            changed.emit();
        }
    }

    fn min_width(): Int {
        return 120;
    }

    fn height(w: Int): Int {
        return control_height(24, 8);
    }

    fn flex(): Int {
        return 0;
    }

    fn value_from_x(lx: Int, w: Int): Int {
        var span = max_v - min_v;
        if (span < 0) {
            span = 0;
        }
        var inner = w - 12;
        if (inner < 1) {
            inner = 1;
        }
        var x = lx - 6;
        if (x < 0) {
            x = 0;
        }
        if (x > inner) {
            x = inner;
        }
        return min_v + (span * x) / inner;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        var h = height(w);
        var over = pointer_over(win_id, x, y, w, h);
        if (over) {
            cursor_hand(win_id);
        }
        var mx = __ui.mouse_x(win_id);
        var my = __ui.mouse_y(win_id);
        if (__ui.mouse_down(win_id) && (dragging || over)) {
            dragging = true;
            set_value(value_from_x(mx - x, w));
        }
        if (!__ui.mouse_down(win_id)) {
            dragging = false;
        }
        var mid = y + h / 2;
        __ui.fill(win_id, x + 6, mid - 2, w - 12, 4, track.value());
        var span = max_v - min_v;
        if (span < 1) {
            span = 1;
        }
        var inner = w - 12;
        if (inner < 1) {
            inner = 1;
        }
        var thumb = ((value - min_v) * inner) / span;
        __ui.fill(win_id, x + 6, mid - 2, thumb, 4, fill.value());
        fill_round(win_id, x + 6 + thumb - 7, mid - 7, 14, 14, 7, fill.value());
        stroke_round(win_id, x + 6 + thumb - 7, mid - 7, 14, 14, 7,
                     Color.Rgb(30, 80, 150).value());
        if (focused) {
            stroke_round(win_id, x, y, w, h, corner_r(), Color.Rgb(47, 111, 196).value());
        }
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        focused = true;
        dragging = true;
        set_value(value_from_x(lx, w));
        return true;
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }


    fn handle_key(code: Int, text: String): Bool {
        if (!focused) {
            return false;
        }
        if (code == 37) {
            set_value(value - 1);
            return true;
        }
        if (code == 39) {
            set_value(value + 1);
            return true;
        }
        return false;
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }

    fn clear_focus() {
        focused = false;
    }

    fn append_focusables(out: Array[Widget]) {
        out.push(self);
    }

    fn focus_enter() {
        focused = true;
    }

    fn has_focus(): Bool {
        return focused;
    }
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        if (hit_test(lx, ly, w, h)) {
            return 1;
        }
        return 0;
    }

}

class LabelRows impl LazyRows {
    var items: Array[String] = [];

    fn count(): Int {
        return len(items);
    }

    fn row(i: Int): Widget {
        return Label { text: items[i] };
    }
}

class LazyColumn impl Widget {
    var source: LazyRows;
    var row_h: Int = 28;
    var viewport_h: Int = 120;
    var offset: Int = 0;
    var layout_w: Int = 0;
    var selected: Int = -1;
    var focused: Bool = false;
    var bar_drag: Bool = false;
    var select_fill: Color = Color.Rgb(200, 220, 255);
    var cache: Array[Widget] = [];
    var cache_first: Int = 0;
    var cache_source_n: Int = -1;
    signal selection_changed();
    signal context_requested();

    fn min_width(): Int {
        return 80;
    }

    fn height(w: Int): Int {
        return viewport_h;
    }

    fn flex(): Int {
        return 0;
    }

    fn step(): Int {
        if (row_h < 1) {
            return 1;
        }
        return row_h;
    }

    fn content_h(): Int {
        return source.count() * step();
    }

    fn content_width(w: Int): Int {
        var cw = w;
        if (content_h() > viewport_h) {
            cw = w - sb_width();
        }
        if (cw < 1) {
            return 1;
        }
        return cw;
    }

    fn max_offset(): Int {
        var max = content_h() - viewport_h;
        if (max < 0) {
            max = 0;
        }
        return max;
    }

    fn clamp_offset() {
        if (offset < 0) {
            offset = 0;
        }
        var max = max_offset();
        if (offset > max) {
            offset = max;
        }
    }

    fn first_index(): Int {
        return offset / step();
    }

    fn last_index(): Int {
        var n = source.count();
        if (n < 1) {
            return -1;
        }
        var bottom = offset + viewport_h - 1;
        if (bottom < offset) {
            bottom = offset;
        }
        var last = bottom / step();
        if (last >= n) {
            last = n - 1;
        }
        return last;
    }

    fn invalidate_cache() {
        cache = [];
        cache_first = 0;
        cache_source_n = -1;
    }

    fn sync_cache() {
        clamp_offset();
        var n = source.count();
        var first = first_index();
        var last = last_index();
        if (last < first || n < 1) {
            invalidate_cache();
            return;
        }
        if (cache_source_n == n && cache_first == first && len(cache) == last - first + 1) {
            return;
        }
        var next: Array[Widget] = [];
        var i = first;
        while (i <= last) {
            if (cache_source_n == n && i >= cache_first && i < cache_first + len(cache)) {
                next.push(cache[i - cache_first]);
            } else {
                next.push(source.row(i));
            }
            i = i + 1;
        }
        cache = next;
        cache_first = first;
        cache_source_n = n;
    }

    fn cached_row(i: Int): Widget {
        sync_cache();
        return cache[i - cache_first];
    }

    fn ensure_visible(i: Int) {
        var n = source.count();
        if (i < 0 || i >= n) {
            return;
        }
        var top = i * step();
        var bot = top + step();
        if (top < offset) {
            offset = top;
        }
        if (bot > offset + viewport_h) {
            offset = bot - viewport_h;
        }
        clamp_offset();
    }

    fn select(i: Int) {
        var n = source.count();
        var next = i;
        if (n < 1) {
            next = -1;
        } elif (next < 0) {
            next = -1;
        } elif (next >= n) {
            next = n - 1;
        }
        if (next != selected) {
            selected = next;
            if (selected >= 0) {
                ensure_visible(selected);
            }
            selection_changed.emit();
        } elif (selected >= 0) {
            ensure_visible(selected);
        }
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        layout_w = w;
        sync_cache();
        if (bar_drag && __ui.mouse_down(win_id)) {
            var my = __ui.mouse_y(win_id) - y;
            offset = vscroll_offset_at(my, viewport_h, content_h());
            clamp_offset();
            sync_cache();
        }
        if (!__ui.mouse_down(win_id)) {
            bar_drag = false;
        }
        var cw = content_width(w);
        if (pointer_over(win_id, x, y, w, viewport_h)) {
            cursor_hand(win_id);
        }
        __ui.clip_push(win_id, x, y, cw, viewport_h);
        var first = first_index();
        var last = last_index();
        var i = first;
        while (i <= last) {
            var ry = y + i * step() - offset;
            if (i == selected) {
                fill_round(win_id, x, ry, cw, step(), 4, select_fill.value());
            }
            cached_row(i).paint(win_id, x, ry, cw);
            i = i + 1;
        }
        __ui.clip_pop(win_id);
        paint_vscroll(win_id, x, y, w, viewport_h, offset, content_h());
        var border = Color.Rgb(200, 200, 200);
        if (focused) {
            border = Color.Rgb(47, 111, 196);
        }
        stroke_round(win_id, x, y, w, viewport_h, corner_r(), border.value());
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        layout_w = w;
        sync_cache();
        if (ly < 0 || ly >= viewport_h || lx < 0 || lx >= w) {
            return false;
        }
        focused = true;
        if (vscroll_hit(lx, ly, w, viewport_h) && max_offset() > 0) {
            bar_drag = true;
            offset = vscroll_offset_at(ly, viewport_h, content_h());
            clamp_offset();
            sync_cache();
            return true;
        }
        var cw = content_width(w);
        if (lx >= cw) {
            return true;
        }
        var n = source.count();
        if (n < 1) {
            return true;
        }
        var y = ly + offset;
        var i = y / step();
        if (i < 0 || i >= n) {
            return true;
        }
        select(i);
        sync_cache();
        cached_row(i).handle_click(lx, y - i * step(), cw, step());
        return true;
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        layout_w = w;
        sync_cache();
        if (ly < 0 || ly >= viewport_h || lx < 0 || lx >= w) {
            return false;
        }
        focused = true;
        if (vscroll_hit(lx, ly, w, viewport_h) && max_offset() > 0) {
            return true;
        }
        var cw = content_width(w);
        if (lx >= cw) {
            return true;
        }
        var n = source.count();
        if (n < 1) {
            return true;
        }
        var y = ly + offset;
        var i = y / step();
        if (i < 0 || i >= n) {
            return true;
        }
        select(i);
        context_requested.emit();
        return true;
    }

    fn handle_key(code: Int, text: String): Bool {
        if (!focused) {
            return false;
        }
        var n = source.count();
        if (n < 1) {
            return false;
        }
        sync_cache();
        var first = first_index();
        var last = last_index();
        var i = first;
        while (i <= last) {
            if (cached_row(i).handle_key(code, text)) {
                return true;
            }
            i = i + 1;
        }
        // VK_UP=38, VK_DOWN=40, VK_PRIOR=33, VK_NEXT=34, VK_HOME=36, VK_END=35
        if (code == 38) {
            if (selected <= 0) {
                select(0);
            } else {
                select(selected - 1);
            }
            return true;
        }
        if (code == 40) {
            if (selected < 0) {
                select(0);
            } elif (selected >= n - 1) {
                select(n - 1);
            } else {
                select(selected + 1);
            }
            return true;
        }
        if (code == 36) {
            select(0);
            return true;
        }
        if (code == 35) {
            select(n - 1);
            return true;
        }
        if (code == 33) {
            var page = viewport_h / step();
            if (page < 1) {
                page = 1;
            }
            if (selected < 0) {
                select(0);
            } elif (selected < page) {
                select(0);
            } else {
                select(selected - page);
            }
            return true;
        }
        if (code == 34) {
            var page = viewport_h / step();
            if (page < 1) {
                page = 1;
            }
            if (selected < 0) {
                select(0);
            } elif (selected + page >= n) {
                select(n - 1);
            } else {
                select(selected + page);
            }
            return true;
        }
        return false;
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        layout_w = w;
        if (ly < 0 || ly >= viewport_h || lx < 0 || lx >= w) {
            return false;
        }
        offset = offset - dy;
        clamp_offset();
        return true;
    }

    fn clear_focus() {
        focused = false;
        sync_cache();
        var first = first_index();
        var last = last_index();
        var i = first;
        while (i <= last) {
            cached_row(i).clear_focus();
            i = i + 1;
        }
    }

    fn append_focusables(out: Array[Widget]) {
        out.push(self);
    }

    fn focus_enter() {
        focused = true;
    }

    fn has_focus(): Bool {
        return focused;
    }
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        layout_w = w;
        if (!hit_test(lx, ly, w, viewport_h)) {
            return 0;
        }
        if (vscroll_hit(lx, ly, w, viewport_h) && max_offset() > 0) {
            return 1;
        }
        var cw = content_width(w);
        sync_cache();
        var n = source.count();
        if (n > 0 && lx < cw) {
            var y = ly + offset;
            var i = y / step();
            if (i >= 0 && i < n) {
                var c = cached_row(i).hover_cursor(lx, y - i * step(), cw, step());
                if (c != 0) {
                    return c;
                }
            }
        }
        return 1;
    }
}

class Dropdown impl Widget {
    var items: Array[String] = [];
    var selected: Int = -1;
    var open: Bool = false;
    var focused: Bool = false;
    var placeholder: String = "Select";
    var fill: Color = Color.White;
    var ink: Color = Color.Rgb(32, 32, 32);
    var border: Color = Color.Rgb(160, 160, 160);
    var select_fill: Color = Color.Rgb(200, 220, 255);
    signal changed();

    fn closed_h(): Int {
        return control_height(32, 12);
    }

    fn item_h(): Int {
        return control_height(28, 8);
    }

    fn current_label(): String {
        if (selected >= 0 && selected < len(items)) {
            return items[selected];
        }
        return placeholder;
    }

    fn set_selected(i: Int) {
        var next = i;
        var n = len(items);
        if (n < 1) {
            next = -1;
        } elif (next < 0) {
            next = -1;
        } elif (next >= n) {
            next = n - 1;
        }
        if (next != selected) {
            selected = next;
            changed.emit();
        }
    }

    fn min_width(): Int {
        var mw = text_width(placeholder) + 28;
        var i = 0;
        while (i < len(items)) {
            var tw = text_width(items[i]) + 28;
            if (tw > mw) {
                mw = tw;
            }
            i = i + 1;
        }
        return mw;
    }

    fn height(w: Int): Int {
        var h = closed_h();
        if (open) {
            h = h + len(items) * item_h();
        }
        return h;
    }

    fn flex(): Int {
        return 0;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        if (!focused) {
            open = false;
        }
        var ch = closed_h();
        var ih = item_h();
        var r = corner_r();
        var over = pointer_over(win_id, x, y, w, height(w));
        if (over) {
            cursor_hand(win_id);
        }
        var edge = border;
        if (focused || open) {
            edge = Color.Rgb(47, 111, 196);
        }
        if (open && len(items) > 0) {
            var full_h = ch + len(items) * ih;
            fill_round(win_id, x, y, w, full_h, r, fill.value());
            stroke_round(win_id, x, y, w, full_h, r, edge.value());
            __ui.fill(win_id, x + 1, y + ch, w - 2, 1, Color.Rgb(220, 220, 226).value());
            var i = 0;
            while (i < len(items)) {
                var ry = y + ch + i * ih;
                if (i == selected) {
                    fill_round(win_id, x + 4, ry + 2, w - 8, ih - 4, 4, select_fill.value());
                }
                __ui.text(win_id, x + 8, ry + (ih - font_height()) / 2, items[i], ink.value());
                i = i + 1;
            }
        } else {
            fill_round(win_id, x, y, w, ch, r, fill.value());
            stroke_round(win_id, x, y, w, ch, r, edge.value());
        }
        var ty = y + (ch - font_height()) / 2;
        __ui.text(win_id, x + 8, ty, current_label(), ink.value());
        paint_chevron(win_id, x + w - 16, y + ch / 2, 5, open, Color.Rgb(90, 90, 98).value());
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        if (ly < 0 || ly >= h || lx < 0 || lx >= w) {
            return false;
        }
        focused = true;
        var ch = closed_h();
        if (ly < ch) {
            open = !open;
            return true;
        }
        if (!open) {
            return true;
        }
        var ih = item_h();
        var i = (ly - ch) / ih;
        if (i >= 0 && i < len(items)) {
            set_selected(i);
            open = false;
        }
        return true;
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }


    fn handle_key(code: Int, text: String): Bool {
        if (!focused) {
            return false;
        }
        // Esc
        if (code == 27) {
            if (open) {
                open = false;
                return true;
            }
            return false;
        }
        // Enter / Space
        if (code == 13 || code == 32) {
            if (open) {
                open = false;
            } else {
                open = true;
                if (selected < 0 && len(items) > 0) {
                    set_selected(0);
                }
            }
            return true;
        }
        // Up / Down
        if (code == 38 || code == 40) {
            if (!open) {
                open = true;
            }
            var n = len(items);
            if (n < 1) {
                return true;
            }
            if (code == 38) {
                if (selected <= 0) {
                    set_selected(0);
                } else {
                    set_selected(selected - 1);
                }
            } else {
                if (selected < 0) {
                    set_selected(0);
                } elif (selected >= n - 1) {
                    set_selected(n - 1);
                } else {
                    set_selected(selected + 1);
                }
            }
            return true;
        }
        return false;
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }

    fn clear_focus() {
        focused = false;
    }

    fn append_focusables(out: Array[Widget]) {
        out.push(self);
    }

    fn focus_enter() {
        focused = true;
    }

    fn has_focus(): Bool {
        return focused;
    }
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        if (hit_test(lx, ly, w, h)) {
            return 1;
        }
        return 0;
    }

}

class ImageView impl Widget {
    var path: String = "";
    var pixels: Array[Int] = [];
    var img_w: Int = 0;
    var img_h: Int = 0;

    fn min_width(): Int {
        if (img_w > 0) {
            return img_w;
        }
        return 16;
    }

    fn height(w: Int): Int {
        if (img_h > 0) {
            return img_h;
        }
        return 16;
    }

    fn flex(): Int {
        return 0;
    }

    fn paint(win_id: Int, x: Int, y: Int, w: Int) {
        if (len(path) > 0) {
            __ui.image(win_id, x, y, path);
            return;
        }
        if (img_w > 0 && img_h > 0 && len(pixels) >= img_w * img_h) {
            __ui.image_rgb(win_id, x, y, img_w, img_h, pixels);
        }
    }

    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }


    fn handle_key(code: Int, text: String): Bool {
        return false;
    }

    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool {
        return false;
    }

    fn clear_focus() {
    }

    fn append_focusables(out: Array[Widget]) {
    }

    fn focus_enter() {
    }

    fn has_focus(): Bool {
        return false;
    }
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int {
        return 0;
    }

}

fn make_image(iw: Int, ih: Int, pixels: Array[Int]) {
    return ImageView { path: "", pixels: pixels, img_w: iw, img_h: ih };
}

fn load_image(p: String) {
    return ImageView {
        path: p,
        pixels: [],
        img_w: __ui.image_width(p),
        img_h: __ui.image_height(p)
    };
}

@ufcs
fn button(t: Theme, text: String): Button {
    return Button {
        text: text,
        fill: t.button_fill,
        hover: t.button_hover,
        pressed: t.button_pressed,
        color: t.button_ink
    };
}

@ufcs
fn label(t: Theme, text: String): Label {
    return Label { text: text, color: t.label_ink };
}

@ufcs
fn field(t: Theme, placeholder: String): TextField {
    return TextField {
        placeholder: placeholder,
        fill: t.field_fill,
        border: t.field_border
    };
}
