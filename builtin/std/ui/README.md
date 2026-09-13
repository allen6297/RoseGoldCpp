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
.\build\RoseGoldC.exe run examples/widgets.rg
.\build\RoseGoldC.exe run examples/contacts.rg
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
| `Labeled` | title above a child; `theme.form_row("Name", field)` |
| `Button` | `clicked`; hover / pressed; `enabled` / `set_enabled` |
| `TextField` | focus + keys; `changed` / `submitted`; `enabled` |
| `Toggle` | `on` bool; `changed`; `enabled`; `theme.toggle` |
| `Checkbox` | `checked` + label; `changed`; `enabled`; `theme.checkbox` |
| `RadioGroup` | `items` / `selected`; click or arrows; `changed`; `enabled`; `theme.radio_group` |
| `ProgressBar` | `value` / `max_v`; `set_value`; `theme.progress` |
| `Slider` | `value` / `min_v` / `max_v`; drag or click; `changed`; `theme.slider` |
| `Dropdown` | `items`, `selected`, expands when `open`; click / Enter / arrows / Esc; `changed`; `theme.dropdown` |
| `TipWrap` | `w.tip(child, "text")` — hover bubble via Window overlay |
| `PopupMenu` | Overlay menu via `w.show_menu` / `w.menu([...])` + `show_menu_at_pointer`; `chosen` |
| `Dialog` | Modal overlay; also `w.confirm` / `w.alert` / `w.request_close` (dirty) |
| `ScrollView` | `child`, `viewport_h`, wheel / `scroll_at`; vertical scrollbar chrome |
| `LazyColumn` | virtualized list: `LazyRows`, fixed `row_h`, scroll/clip, selection, scrollbar, **visible-row cache**; right-click → `context_requested` |
| `LabelRows` | `LazyRows` helper over `Array[String]` → `Label` rows |
| `FilteredLabels` | `LabelRows` + query filter + `real_index` |
| `Canvas` | stroked frame + diagonal; `redraw` signal for custom host draws |
| `ImageView` | `ui.make_image(w, h, pixels)` or `ui.load_image("x.ppm"\|"x.png")` |
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
var w = try ui.open_theme("Hi", 400, 240, theme);
var go = theme.button("Go");
var name = theme.form_row("Name", theme.field("Name"));
var ok = theme.checkbox("Agree");
```

Theme factories: `button`, `label`, `field`, `form_row`, `toggle`, `checkbox`, `radio_group`, `progress`, `slider`, `dropdown`.

## Window API

| Call | Purpose |
|---|---|
| `ui.open` / `ui.open_hidden` | Create (`throws`) |
| `ui.open_theme` / `ui.open_hidden_theme` | Create with `theme` applied |
| `w.add(widget)` | Attach root child |
| `w.run()` / `w.poll()` | Event loop / one pump |
| `w.confirm` / `w.alert` | One-shot dialogs |
| `w.menu([...])` | Build a `PopupMenu` |
| `w.mark_dirty` / `w.mark_clean` / `w.request_close` | Dirty flag + confirm-on-close |
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
    fn handle_right_click(lx: Int, ly: Int, w: Int, h: Int): Bool;
    fn handle_key(code: Int, text: String): Bool;
    fn handle_scroll(dx: Int, dy: Int, lx: Int, ly: Int, w: Int, h: Int): Bool;
    fn clear_focus();
    fn append_focusables(out: Array[Widget]);
    fn focus_enter();
    fn has_focus(): Bool;
    fn hover_cursor(lx: Int, ly: Int, w: Int, h: Int): Int;
}
```

For `LazyColumn`, implement `LazyRows` (`count` / `row(i)`) or use `LabelRows`.

Draw with `__ui.fill` / `__ui.fill_round` / `__ui.text` / `__ui.line` / `__ui.stroke_rect` / `__ui.stroke_round` / `__ui.image` / `__ui.image_rgb` / `__ui.clip_push` / `__ui.clip_pop`. Helpers: `fill_round` / `stroke_round` / `paint_chevron` / `corner_r()`.

Hover cursors: `Window.tick` sets the cursor from `root.hover_cursor(...)` after paint (hand/I-beam). Widgets may still call `cursor_hand` / `cursor_ibeam` while painting for immediate feedback. Right-click is routed through `handle_right_click`; `LazyColumn` emits `context_requested` (pair with `w.show_menu_at_pointer`).

## Tests

```text
.\build\RoseGoldC.exe run tests/pass/stdlib_ui.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_widgets.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_form.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_scroll.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_polish.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_extra.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_lazy.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_tab.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_dropdown.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_menu.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_context.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_contacts.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_dialog.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_controls.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_ergonomics.rg
.\build\RoseGoldC.exe run tests/pass/stdlib_ui_todo.rg
```

## Not yet

- `.on_click` / `.on_change` UFCS aliases (language has no fn-as-parameter types yet)
- Caret blink / text selection; Wayland input parity
- Mobile/web backends (names reserved on the same `__ui` surface)

Tab / Shift+Tab moves focus across Button, TextField, Toggle, Slider, and LazyColumn. Enter activates the focused button; Space/Enter toggles; arrow keys nudge a focused slider.

`LazyColumn` keeps a cache of visible row widgets so TextField/Button state survives paints; rows that scroll off are dropped. Hover uses `cursor_hand` / `cursor_ibeam` via `__ui.cursor`.
