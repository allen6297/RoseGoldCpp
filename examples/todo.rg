import ui;
import str;

struct Todo {
    title: String;
    note: String;
    done: Bool;
}

class TodoRows impl LazyRows {
    var items: Array[Todo] = [];
    var query: String = "";
    // 0 = all, 1 = active, 2 = done
    var filter: Int = 0;

    fn matches(t: Todo): Bool {
        if (filter == 1 && t.done) {
            return false;
        }
        if (filter == 2 && !t.done) {
            return false;
        }
        var q = str.lower(str.trim(query));
        return str.is_empty(q) || str.contains(str.lower(t.title + " " + t.note), q);
    }

    fn count(): Int {
        var n = 0;
        for t in items {
            if (matches(t)) {
                n = n + 1;
            }
        }
        return n;
    }

    fn real_index(vis: Int): Int {
        var seen = 0;
        for i in len(items) {
            if (matches(items[i])) {
                if (seen == vis) {
                    return i;
                }
                seen = seen + 1;
            }
        }
        return -1;
    }

    fn row(i: Int): Widget {
        var t = items[real_index(i)];
        var mark = "[ ] ";
        if (t.done) {
            mark = "[x] ";
        }
        if (str.is_empty(str.trim(t.title))) {
            return Label { text: mark + "(untitled)" };
        }
        return Label { text: mark + t.title };
    }
}

fn main(): Int {
    var rows = TodoRows {
        items: [
            Todo { title: "Ship color chips", note: "LSP + editor", done: true },
            Todo { title: "Dogfood todo app", note: "Find UI gaps", done: false },
            Todo { title: "Grow fmt", note: "Keep comments", done: false },
            Todo { title: "Caret blink", note: "TextField polish", done: false }
        ]
    };
    var theme = Theme { window_bg: Color.Rgb(246, 247, 250) };
    var w = try ui.open_theme("Todos", 420, 520, theme);
    var search = theme.field("Search");
    var title = theme.field("");
    var note = theme.field("");
    var done = theme.checkbox("Done");
    var status = theme.label("Select a todo");
    var filter = theme.radio_group(["All", "Active", "Done"]);
    var list = LazyColumn { source: rows, row_h: 28, viewport_h: 160 };
    var idx = fn (): Int { return rows.real_index(list.selected); };
    var say = fn (s: String) { status.set_text(s); };
    var dirty = fn () {
        list.invalidate_cache();
        w.mark_dirty();
    };
    var refresh = fn () {
        list.invalidate_cache();
        list.selection_changed.emit();
    };

    list.selection_changed.connect(fn () {
        var ri = idx();
        if (ri < 0) {
            title.text = "";
            note.text = "";
            done.checked = false;
            if (rows.count() == 0 && (!str.is_empty(str.trim(rows.query)) || rows.filter != 0)) {
                say("No matches");
            } elif (str.is_empty(str.trim(rows.query)) && rows.filter == 0) {
                say("Select a todo");
            } else {
                say("Filtered");
            }
            return;
        }
        title.text = rows.items[ri].title;
        note.text = rows.items[ri].note;
        done.checked = rows.items[ri].done;
        if (str.is_empty(rows.items[ri].title)) {
            say("New todo");
        } elif (rows.items[ri].done) {
            say("Done · " + rows.items[ri].title);
        } else {
            say("Editing " + rows.items[ri].title);
        }
    });

    search.changed.connect(fn () {
        rows.query = search.text;
        list.offset = 0;
        list.selected = -1;
        refresh();
    });
    filter.changed.connect(fn () {
        rows.filter = filter.selected;
        list.offset = 0;
        list.selected = -1;
        refresh();
    });
    title.changed.connect(fn () { w.mark_dirty(); });
    note.changed.connect(fn () { w.mark_dirty(); });
    done.changed.connect(fn () {
        var ri = idx();
        if (ri < 0) {
            return;
        }
        rows.items[ri].done = done.checked;
        dirty();
        if (done.checked) {
            say("Done · " + rows.items[ri].title);
        } else {
            say("Editing " + rows.items[ri].title);
        }
    });

    var menu = w.menu(["Toggle done", "Delete", "Clear completed"]);
    menu.chosen.connect(fn () {
        if (menu.selected == 2) {
            var next: Array[Todo] = [];
            for t in rows.items {
                if (!t.done) {
                    next.push(t);
                }
            }
            rows.items = next;
            list.selected = -1;
            dirty();
            refresh();
            say("Cleared completed");
            return;
        }
        var ri = idx();
        if (ri < 0) {
            return;
        }
        if (menu.selected == 0) {
            rows.items[ri].done = !rows.items[ri].done;
            done.checked = rows.items[ri].done;
            dirty();
            refresh();
            say("Toggled");
        } elif (menu.selected == 1) {
            var next: Array[Todo] = [];
            for i in len(rows.items) {
                if (i != ri) {
                    next.push(rows.items[i]);
                }
            }
            rows.items = next;
            list.selected = -1;
            dirty();
            refresh();
            say("Deleted");
        }
    });
    list.context_requested.connect(fn () { w.show_menu_at_pointer(menu); });

    var add = theme.button("New");
    add.clicked.connect(fn () {
        search.text = "";
        rows.query = "";
        filter.selected = 0;
        rows.filter = 0;
        rows.items.push(Todo { title: "", note: "", done: false });
        dirty();
        list.select(len(rows.items) - 1);
    });
    var save = theme.button("Save");
    save.clicked.connect(fn () {
        var ri = idx();
        if (ri >= 0) {
            rows.items[ri] = Todo {
                title: title.text,
                note: note.text,
                done: done.checked
            };
            list.invalidate_cache();
            w.mark_clean();
            say("Saved " + title.text);
        } elif (!str.is_empty(str.trim(title.text))) {
            rows.items.push(Todo {
                title: title.text,
                note: note.text,
                done: done.checked
            });
            search.text = "";
            rows.query = "";
            list.invalidate_cache();
            w.mark_clean();
            list.select(len(rows.items) - 1);
        }
    });
    var more = theme.button("Menu");
    more.clicked.connect(fn () { w.show_menu_at_pointer(menu); });
    var quit = theme.button("Close");
    quit.clicked.connect(fn () { w.request_close(); });

    w.add(VStack {
        spacing: 8,
        children: [
            HStack { spacing: 8, children: [status, Spacer {}, more, quit] },
            search,
            filter,
            list,
            theme.form_row("Title", title),
            theme.form_row("Note", note),
            done,
            HStack { spacing: 8, children: [add, save, Spacer {}] }
        ]
    });
    w.run();
    return 0;
}
