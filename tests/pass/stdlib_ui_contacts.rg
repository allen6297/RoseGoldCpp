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
        Contact { name: "Ken", note: "Unix" }
    ];
    var rows = ContactRows { people: people };
    checks.eq(rows.count(), 4);
    checks.eq(rows.real_index(0), 0);
    checks.eq(rows.real_index(3), 3);

    rows.query = "al";
    checks.eq(rows.count(), 2);
    checks.eq(rows.real_index(0), 0);
    checks.eq(rows.real_index(1), 2);
    checks.eq_string(people[rows.real_index(1)].name, "Alan");

    rows.query = "unix";
    checks.eq(rows.count(), 1);
    checks.eq(rows.real_index(0), 3);

    rows.query = "zzz";
    checks.eq(rows.count(), 0);
    checks.eq(rows.real_index(0), -1);

    var w = try ui.open_hidden("rg-contacts", 420, 460);
    var theme = Theme { window_bg: Color.Rgb(248, 248, 252) };
    w.theme = theme;
    rows.query = "";
    var search = theme.field("Search");
    var name = theme.field("Name");
    var note = theme.field("Note");
    var status = theme.label("Select a contact");
    var list = LazyColumn {
        source: rows,
        row_h: 28,
        viewport_h: 120
    };
    list.selection_changed.connect(fn () {
        var ri = rows.real_index(list.selected);
        if (ri < 0) {
            name.text = "";
            note.text = "";
            return;
        }
        name.text = people[ri].name;
        note.text = people[ri].note;
        status.set_text("Editing " + people[ri].name);
    });
    search.changed.connect(fn () {
        rows.query = search.text;
        list.selected = -1;
        list.offset = 0;
        list.invalidate_cache();
        name.text = "";
        note.text = "";
        if (rows.count() == 0) {
            status.set_text("No matches");
        } else {
            status.set_text("Filtered");
        }
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
            status.set_text("Saved");
        }
    });
    var actions = HStack {
        spacing: 8,
        children: [add, save, Spacer {}]
    };
    w.add(VStack {
        spacing: 8,
        children: [status, search, list, name, note, actions]
    });

    var inner = 420 - 24;
    var y = 12;
    y = y + status.height(inner) + 8;
    var y_search = y + search.height(inner) / 2;
    y = y + search.height(inner) + 8;
    var y_list = y + 14;
    y = y + list.height(inner) + 8;
    y = y + name.height(inner) + 8;
    y = y + note.height(inner) + 8;
    var y_btns = y + actions.height(inner) / 2;
    var x_new = 12 + add.min_width() / 2;
    var x_save = 12 + add.min_width() + 8 + save.min_width() / 2;

    w.click_at(40, y_list);
    checks.eq(list.selected, 0);
    checks.eq_string(name.text, "Ada");

    w.click_at(40, y_search);
    checks.that(search.focused);
    w.key_at(0, "a");
    w.key_at(0, "l");
    checks.eq_string(search.text, "al");
    checks.eq(rows.count(), 2);
    checks.eq(list.selected, -1);
    checks.eq_string(status.text, "Filtered");

    w.click_at(40, y_list);
    checks.eq(list.selected, 0);
    checks.eq_string(name.text, "Ada");
    w.click_at(40, y_list + 28);
    checks.eq(list.selected, 1);
    checks.eq_string(name.text, "Alan");
    checks.eq(rows.real_index(1), 2);

    name.text = "Alan Turing";
    note.text = "Enigma";
    w.click_at(x_save, y_btns);
    checks.eq_string(people[2].name, "Alan Turing");
    checks.eq_string(people[2].note, "Enigma");
    checks.eq_string(status.text, "Saved");

    search.text = "";
    rows.query = "";
    list.invalidate_cache();
    w.click_at(x_new, y_btns);
    checks.eq(len(people), 5);
    checks.eq(list.selected, 4);
    checks.eq_string(status.text, "New contact");
    name.text = "Lin";
    note.text = "Git";
    w.click_at(x_save, y_btns);
    checks.eq_string(people[4].name, "Lin");
    checks.eq_string(status.text, "Saved");

    w.close();
    return 0;
}
