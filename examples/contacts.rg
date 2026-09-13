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
        var q = str.trim(query);
        if (str.is_empty(q)) {
            return true;
        }
        q = str.lower(q);
        return str.contains(str.lower(c.name), q) || str.contains(str.lower(c.note), q);
    }

    fn count(): Int {
        var n = 0;
        var i = 0;
        while (i < len(people)) {
            if (matches(people[i])) {
                n = n + 1;
            }
            i = i + 1;
        }
        return n;
    }

    fn real_index(vis: Int): Int {
        var seen = 0;
        var i = 0;
        while (i < len(people)) {
            if (matches(people[i])) {
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
        var ri = real_index(i);
        return Label { text: people[ri].name };
    }
}

fn main(): Int {
    var people: Array[Contact] = [
        Contact { name: "Ada", note: "Analytical engine" },
        Contact { name: "Grace", note: "Compilers" },
        Contact { name: "Alan", note: "Halting problem" },
        Contact { name: "Katherine", note: "Orbital mechanics" },
        Contact { name: "Margaret", note: "Apollo software" },
        Contact { name: "Barbara", note: "Software engineering" },
        Contact { name: "Dennis", note: "C and Unix" },
        Contact { name: "Ken", note: "Unix" },
        Contact { name: "Donald", note: "Art of Computer Programming" },
        Contact { name: "John", note: "Lisp" },
        Contact { name: "Guido", note: "Python" },
        Contact { name: "Bjarne", note: "C++" }
    ];
    var rows = ContactRows { people: people };
    var w = try ui.open("Contacts", 420, 460);
    var theme = Theme { window_bg: Color.Rgb(248, 248, 252) };
    w.theme = theme;
    var search = theme.field("Search");
    var name = theme.field("Name");
    var note = theme.field("Note");
    var status = theme.label("Select a contact");
    var list = LazyColumn {
        source: rows,
        row_h: 28,
        viewport_h: 180
    };

    list.selection_changed.connect(fn () {
        var ri = rows.real_index(list.selected);
        if (ri < 0) {
            name.text = "";
            note.text = "";
            if (!str.is_empty(str.trim(rows.query))) {
                if (rows.count() == 0) {
                    status.set_text("No matches");
                } else {
                    status.set_text("Filtered");
                }
            } else {
                status.set_text("Select a contact");
            }
            return;
        }
        name.text = people[ri].name;
        note.text = people[ri].note;
        if (str.is_empty(people[ri].name)) {
            status.set_text("New contact");
        } else {
            status.set_text("Editing " + people[ri].name);
        }
    });

    search.changed.connect(fn () {
        rows.query = search.text;
        list.selected = -1;
        list.offset = 0;
        list.invalidate_cache();
        name.text = "";
        note.text = "";
        if (str.is_empty(str.trim(rows.query))) {
            status.set_text("Select a contact");
        } elif (rows.count() == 0) {
            status.set_text("No matches");
        } else {
            status.set_text("Filtered");
        }
    });

    var menu = PopupMenu {
        items: ["Clear fields", "Duplicate", "Delete"]
    };
    menu.chosen.connect(fn () {
        if (menu.selected == 0) {
            name.text = "";
            note.text = "";
            status.set_text("Cleared");
        } elif (menu.selected == 1) {
            var ri = rows.real_index(list.selected);
            if (ri >= 0) {
                people.push(people[ri]);
                rows.people = people;
                list.invalidate_cache();
                list.select(rows.count() - 1);
                status.set_text("Duplicated");
            }
        } elif (menu.selected == 2) {
            var ri = rows.real_index(list.selected);
            if (ri >= 0) {
                var next: Array[Contact] = [];
                var i = 0;
                while (i < len(people)) {
                    if (i != ri) {
                        next.push(people[i]);
                    }
                    i = i + 1;
                }
                people = next;
                rows.people = people;
                list.selected = -1;
                list.invalidate_cache();
                name.text = "";
                note.text = "";
                status.set_text("Deleted");
            }
        }
    });
    list.context_requested.connect(fn () {
        w.show_menu_at_pointer(menu);
    });

    var actions = theme.button("Actions");
    actions.clicked.connect(fn () {
        w.show_menu_at_pointer(menu);
    });

    var add = theme.button("New");
    add.clicked.connect(fn () {
        search.text = "";
        rows.query = "";
        people.push(Contact { name: "", note: "" });
        rows.people = people;
        list.invalidate_cache();
        list.select(len(people) - 1);
        name.text = "";
        note.text = "";
        status.set_text("New contact");
    });

    var save = theme.button("Save");
    save.clicked.connect(fn () {
        var ri = rows.real_index(list.selected);
        if (ri >= 0) {
            people[ri] = Contact { name: name.text, note: note.text };
            rows.people = people;
            list.invalidate_cache();
            if (str.is_empty(str.trim(name.text))) {
                status.set_text("Saved");
            } else {
                status.set_text("Saved " + name.text);
            }
        } elif (!str.is_empty(str.trim(name.text))) {
            people.push(Contact { name: name.text, note: note.text });
            rows.people = people;
            search.text = "";
            rows.query = "";
            list.invalidate_cache();
            list.select(len(people) - 1);
            status.set_text("Added " + name.text);
        }
    });

    var avatar = ui.load_image("examples/assets/dot.png");
    var close_dlg = Dialog {
        title: "Close",
        message: "Close Contacts?",
        buttons: ["Cancel", "Close"]
    };
    close_dlg.chosen.connect(fn () {
        if (close_dlg.selected == 1) {
            w.close();
        }
    });
    var quit = theme.button("Close");
    quit.clicked.connect(fn () {
        w.show_dialog(close_dlg);
    });
    w.add(VStack {
        spacing: 10,
        children: [
            HStack {
                spacing: 12,
                children: [avatar, status, Spacer {}, actions, quit]
            },
            search,
            list,
            name,
            note,
            HStack {
                spacing: 8,
                children: [add, save, Spacer {}]
            }
        ]
    });
    w.run();
    return 0;
}
