import ui;
import str;

struct Contact {
    name: String;
    note: String;
}

class ContactRows impl LazyRows {
    var people: Array[Contact] = [];
    var query: String = "";

    fn matches(c: Contact): Bool {
        var q = str.lower(str.trim(query));
        return str.is_empty(q) || str.contains(str.lower(c.name + " " + c.note), q);
    }

    fn count(): Int {
        var n = 0;
        for c in people {
            if (matches(c)) {
                n = n + 1;
            }
        }
        return n;
    }

    fn real_index(vis: Int): Int {
        var seen = 0;
        for i in len(people) {
            if (matches(people[i])) {
                if (seen == vis) {
                    return i;
                }
                seen = seen + 1;
            }
        }
        return -1;
    }

    fn row(i: Int): Widget {
        return Label { text: people[real_index(i)].name };
    }
}

fn main(): Int {
    var rows = ContactRows {
        people: [
            Contact { name: "Ada", note: "Analytical engine" },
            Contact { name: "Grace", note: "Compilers" },
            Contact { name: "Alan", note: "Halting problem" },
            Contact { name: "Ken", note: "Unix" }
        ]
    };
    var theme = Theme { window_bg: Color.Rgb(248, 248, 252) };
    var w = try ui.open_theme("Contacts", 400, 420, theme);
    var search = theme.field("Search");
    var name = theme.field("");
    var note = theme.field("");
    var status = theme.label("Select a contact");
    var list = LazyColumn { source: rows, row_h: 28, viewport_h: 140 };
    var idx = fn (): Int { return rows.real_index(list.selected); };
    var say = fn (s: String) { status.set_text(s); };
    var dirty = fn () {
        list.invalidate_cache();
        w.mark_dirty();
    };

    list.selection_changed.connect(fn () {
        var ri = idx();
        if (ri < 0) {
            name.text = "";
            note.text = "";
            if (rows.count() == 0 && !str.is_empty(str.trim(rows.query))) {
                say("No matches");
            } elif (str.is_empty(str.trim(rows.query))) {
                say("Select a contact");
            } else {
                say("Filtered");
            }
            return;
        }
        name.text = rows.people[ri].name;
        note.text = rows.people[ri].note;
        if (str.is_empty(rows.people[ri].name)) {
            say("New contact");
        } else {
            say("Editing " + rows.people[ri].name);
        }
    });
    search.changed.connect(fn () {
        rows.query = search.text;
        list.offset = 0;
        list.invalidate_cache();
        list.selected = -1;
        list.selection_changed.emit();
    });
    name.changed.connect(fn () { w.mark_dirty(); });
    note.changed.connect(fn () { w.mark_dirty(); });

    var menu = w.menu(["Clear fields", "Delete"]);
    menu.chosen.connect(fn () {
        var ri = idx();
        if (menu.selected == 0) {
            name.text = "";
            note.text = "";
            w.mark_dirty();
            say("Cleared");
        } elif (ri >= 0) {
            var next: Array[Contact] = [];
            for i in len(rows.people) {
                if (i != ri) {
                    next.push(rows.people[i]);
                }
            }
            rows.people = next;
            list.selected = -1;
            dirty();
            list.selection_changed.emit();
            say("Deleted");
        }
    });
    list.context_requested.connect(fn () { w.show_menu_at_pointer(menu); });

    var add = theme.button("New");
    add.clicked.connect(fn () {
        search.text = "";
        rows.query = "";
        rows.people.push(Contact { name: "", note: "" });
        dirty();
        list.select(len(rows.people) - 1);
    });
    var save = theme.button("Save");
    save.clicked.connect(fn () {
        var ri = idx();
        if (ri >= 0) {
            rows.people[ri] = Contact { name: name.text, note: note.text };
            list.invalidate_cache();
            w.mark_clean();
            say("Saved " + name.text);
        } elif (!str.is_empty(str.trim(name.text))) {
            rows.people.push(Contact { name: name.text, note: note.text });
            search.text = "";
            rows.query = "";
            list.invalidate_cache();
            w.mark_clean();
            list.select(len(rows.people) - 1);
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
            list,
            theme.form_row("Name", name),
            theme.form_row("Note", note),
            HStack { spacing: 8, children: [add, save, Spacer {}] }
        ]
    });
    w.run();
    return 0;
}
