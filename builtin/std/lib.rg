trait Identifiable {
    fn id(self): UUID;
}

data UUID impl Identifiable {
    value: String;

    fn id(): UUID {
        return UUID { value: value };
    }

    fn to_string(): String {
        return value;
    }

    fn hex(): String {
        return __str.replace(value, "-", "");
    }

    fn is_nil(): Bool {
        return value == "00000000-0000-0000-0000-000000000000";
    }
}

fn nil(): UUID {
    return UUID { value: "00000000-0000-0000-0000-000000000000" };
}

fn v4(): UUID {
    return UUID { value: __uuid.v4() };
}

fn valid(s: String): Bool {
    return __uuid.valid(s);
}

fn parse(s: String) throws: UUID {
    return UUID { value: try __uuid.parse(s) };
}
