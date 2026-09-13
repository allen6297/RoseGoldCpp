# `std.ui`

Swift-ish widgets on a software framebuffer. `import ui` loads this crate (`color.rg`, `lib.rg`, `widgets.rg`). Each OS only opens a window and blits pixels (`win32` / `x11` / `wayland` / `cocoa`).

## Quick start

```text
import ui;

fn main(): Int {
    var w = try ui.open("Hi", 400, 240);
    var quit = Button { text: "Close" };
    quit.clicked.connect(fn () { w.close(); });
    w.add(VStack {
        spacing: 12,
        children: [
            Label { text: "Hello" }.padding(8),
            quit.background(Color.Blue)
        ]
    });
    w.run();
    return 0;
}
```

```text
.\build\RoseGoldC.exe run examples/window.rg
.\build\RoseGoldC.exe run examples/form.rg
.\build\RoseGoldC.exe run examples/scroll.rg
.\build\RoseGoldC.exe run examples/canvas.rg
.\build\RoseGoldC.exe run examples/polish.rg
.\build\RoseGoldC.exe run examples/extra.rg
.\build\RoseGoldC.exe run examples/lazy.rg
```

Hidden windows work without a display (tests): `ui.open_hidden(...)`.

On Windows the host is per-monitor DPI aware: ClearType fonts scale with window DPI, and framebuffer presents use filtered blits when stretched.

## Layout model

Width is assigned by the parent; height is `height(w)` (so labels can wrap to that width).

| Concept | Role |
|---|---|
| `VStack` / `HStack` | Nest children; `spacing`, optional `children:` or `.add()` |
| `Spacer` | `flex() == 1` — takes leftover width in an `HStack` |
| `HStack.align` | `"fill"` (default), `"leading"`, `"center"`, `"trailing"` |
| `.padding(n)` / `.background(c)` / `.style(...)` | UFCS wrappers (`Pad`, `Backdrop`) |
| `Window.add` | Appends roots; painted as one padded `VStack` |

`min_width()` is a floor. In `HStack`, leftover space goes to `flex() > 0` children first; otherwise `"fill"` shares it across all children.

## Widgets

| Widget | Notes |
|---|---|
| `Label` | `text`, soft-wrap + `\n`; `.foreground(Color)`, `.set_text` |
| `Button` | `clicked` signal; hover / pressed colors from mouse |
| `TextField` | click to focus; keys insert / backspace (`8`) / submit (`13`); `changed` / `submitted` |
| `Toggle` | `on` bool; `changed` |
| `Slider` | `value` / `min_v` / `max_v`; drag or click; `changed` |
| `ScrollView` | `child`, `viewport_h`, wheel / `scroll_at`; clips via host |
| `LazyColumn` | virtualized list: `LazyRows` source, fixed `row_h`, built-in scroll/clip |
| `LabelRows` | `LazyRows` helper over `Array[String]` → `Label` rows |
| `Canvas` | stroked frame + diagonal; `redraw` signal for custom host draws |
| `ImageView` | `ui.make_image(w, h, pixels)` or `ui.load_image("x.ppm")` (PPM P3/P6) |
| `Spacer` | expanding gap in rows |

## Color and theme

```text
Color.Red
Color.Rgb(47, 111, 196)
ui.rgb(47, 111, 196)
```

`Theme` holds window and control defaults. Set `w.theme` so `tick` clears with `window_bg`. Factories are UFCS on `Theme`:

```text
var theme = Theme { window_bg: Color.Rgb(245, 245, 248) };
w.theme = theme;
var go = theme.button("Go");
var name = theme.field("Name");
var title = theme.label("Hi");
```

## Window API

| Call | Purpose |
|---|---|
| `ui.open` / `ui.open_hidden` | Create (`throws`) |
| `w.add(widget)` | Attach root child |
| `w.run()` / `w.poll()` | Event loop / one pump |
| `w.click_at` / `w.key_at` / `w.scroll_at` / `w.hover_at` | Tests / synthetic input |
| `ui.backend()` / `ui.platform()` / `ui.kind()` | Host / OS / `desktop` |

## Custom widgets

Implement `Widget`:

```text
trait Widget {
    fn min_width(): Int;
    fn height(w: Int): Int;
    fn flex(): Int;
    fn paint(win_id: Int, x: Int, y: Int, w: Int);
    fn handle_click(lx: Int, ly: Int, w: Int, h: Int): Bool;
    fn handle_key(code: Int, text: String): Bool;
    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool;
    fn clear_focus();
}
```

For `LazyColumn`, implement `LazyRows` (`count` / `row(i)`) or use `LabelRows`.

Draw with `__ui.fill` / `__ui.text` / `__ui.line` / `__ui.stroke_rect` / `__ui.image` / `__ui.image_rgb` / `__ui.clip_push` / `__ui.clip_pop`.

## Tests

```text
.\build\RoseGoldC.exe run tests/pass/stdlib_ui.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_widgets.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_form.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_scroll.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_polish.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_extra.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_lazy.rg
```

## Not yet

List selection, more image formats, mobile/web backends (names reserved on the same `__ui` surface).

Rows in `LazyColumn` are rebuilt per paint/hit-test (no widget cache); sticky focus/hover inside rows is out of scope for v1.
