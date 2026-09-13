import ui;

struct Contact {
    name: String;
    note: String;
}

class ContactRows impl LazyRows {
    var people: Array[Contact] = [];

    fn count(): Int {
        return len(people);
    }

    fn row(i: Int): Widget {
        return Label { text: people[i].name };
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
    var w = try ui.open("Contacts", 420, 420);
    var theme = Theme { window_bg: Color.Rgb(248, 248, 252) };
    w.theme = theme;
    var name = theme.field("Name");
    var note = theme.field("Note");
    var status = theme.label("Select a contact");
    var list = LazyColumn {
        source: rows,
        row_h: 28,
        viewport_h: 200
    };
    list.selection_changed.connect(fn () {
        if (list.selected >= 0 && list.selected < len(people)) {
            name.set_text(people[list.selected].name);
            note.set_text(people[list.selected].note);
            status.set_text("Editing");
        }
    });
    var save = theme.button("Save");
    save.clicked.connect(fn () {
        if (list.selected >= 0 && list.selected < len(people)) {
            people[list.selected] = Contact { name: name.text, note: note.text };
            list.invalidate_cache();
            status.set_text("Saved");
        }
    });
    var avatar = ui.load_image("examples/assets/dot.png");
    var quit = theme.button("Close");
    quit.clicked.connect(fn () { w.close(); });
    w.add(VStack {
        spacing: 10,
        children: [
            HStack {
                spacing: 12,
                children: [avatar, status, Spacer {}, quit]
            },
            list,
            name,
            note,
            save
        ]
    });
    w.run();
    return 0;
}
