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
        var i = 0;
        while (i < len(items)) {
            if (matches(items[i])) {
                n = n + 1;
            }
            i = i + 1;
        }
        return n;
    }

    fn real_index(vis: Int): Int {
        var seen = 0;
        var i = 0;
        while (i < len(items)) {
            if (matches(items[i])) {
                if (seen == vis) {
                    return i;
                }
                seen = seen + 1;
            }
            i = i + 1;
        }
        return -1;
    }

    fn row(i: Int): Widget {
        var t = items[real_index(i)];
        var mark = "[ ] ";
        if (t.done) {
            mark = "[x] ";
        }
        return Label { text: mark + t.title };
    }
}

fn main(): Int {
    var rows = TodoRows {
        items: [
            Todo { title: "A", note: "one", done: false },
            Todo { title: "B", note: "two", done: true },
            Todo { title: "C", note: "three", done: false }
        ]
    };
    checks.eq(rows.count(), 3);
    checks.eq(rows.real_index(1), 1);

    rows.filter = 1;
    checks.eq(rows.count(), 2);
    checks.eq(rows.real_index(0), 0);
    checks.eq(rows.real_index(1), 2);

    rows.filter = 2;
    checks.eq(rows.count(), 1);
    checks.eq(rows.real_index(0), 1);

    rows.filter = 0;
    rows.query = "th";
    checks.eq(rows.count(), 1);
    checks.eq_string(rows.items[rows.real_index(0)].title, "C");

    rows.query = "";
    var theme = Theme { window_bg: Color.Rgb(246, 247, 250) };
    var w = try ui.open_hidden_theme("rg-todo", 420, 480, theme);
    var search = theme.field("Search");
    var title = theme.field("");
    var note = theme.field("");
    var done = theme.checkbox("Done");
    var status = theme.label("Select a todo");
    var filter = theme.radio_group(["All", "Active", "Done"]);
    var list = LazyColumn {
        source: rows,
        row_h: 28,
        viewport_h: 120
    };

    list.selection_changed.connect(fn () {
        var ri = rows.real_index(list.selected);
        if (ri < 0) {
            title.text = "";
            note.text = "";
            done.checked = false;
            return;
        }
        title.text = rows.items[ri].title;
        note.text = rows.items[ri].note;
        done.checked = rows.items[ri].done;
        status.set_text("Editing " + rows.items[ri].title);
    });
    search.changed.connect(fn () {
        rows.query = search.text;
        list.selected = -1;
        list.offset = 0;
        list.invalidate_cache();
    });
    filter.changed.connect(fn () {
        rows.filter = filter.selected;
        list.selected = -1;
        list.offset = 0;
        list.invalidate_cache();
    });
    done.changed.connect(fn () {
        var ri = rows.real_index(list.selected);
        if (ri >= 0) {
            rows.items[ri].done = done.checked;
            list.invalidate_cache();
            w.mark_dirty();
        }
    });

    var add = theme.button("New");
    add.clicked.connect(fn () {
        search.text = "";
        rows.query = "";
        filter.set_selected(0);
        rows.filter = 0;
        rows.items.push(Todo { title: "", note: "", done: false });
        list.invalidate_cache();
        list.select(len(rows.items) - 1);
        w.mark_dirty();
    });
    var save = theme.button("Save");
    save.clicked.connect(fn () {
        var ri = rows.real_index(list.selected);
        if (ri >= 0) {
            rows.items[ri] = Todo {
                title: title.text,
                note: note.text,
                done: done.checked
            };
            list.invalidate_cache();
            w.mark_clean();
            status.set_text("Saved");
        }
    });

    w.add(VStack {
        spacing: 8,
        children: [
            status,
            search,
            filter,
            list,
            theme.form_row("Title", title),
            theme.form_row("Note", note),
            done,
            HStack { spacing: 8, children: [add, save, Spacer {}] }
        ]
    });

    // Select first row via click geometry (padded root)
    var inner = 420 - 24;
    var y = 12 + status.height(inner) + 8 + search.height(inner) + 8
        + filter.height(inner) + 8 + 14;
    w.click_at(40, y);
    checks.eq(list.selected, 0);
    checks.eq_string(title.text, "A");
    checks.that(!done.checked);

    done.set_checked(true);
    checks.that(rows.items[0].done);
    checks.that(w.dirty);

    filter.set_selected(1);
    checks.eq(rows.filter, 1);
    checks.eq(rows.count(), 1);
    list.selected = -1;
    list.invalidate_cache();

    filter.set_selected(2);
    checks.eq(rows.count(), 2);

    add.clicked.emit();
    checks.eq(rows.filter, 0);
    checks.eq(len(rows.items), 4);
    checks.eq(list.selected, 3);
    title.set_text("D");
    note.set_text("four");
    save.clicked.emit();
    checks.eq_string(rows.items[3].title, "D");
    checks.that(!w.dirty);

    w.mark_dirty();
    w.request_close();
    checks.that(w.has_dialog());
    w.key_at(13, "");
    checks.eq(w.id, 0);
    return 0;
}
