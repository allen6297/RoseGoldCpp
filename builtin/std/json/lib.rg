fn parse(s: String) throws {
    return try __json.parse(s);
}

fn stringify(v): String {
    return __json.stringify(v);
}

fn valid(s: String): Bool {
    return __json.valid(s);
}
