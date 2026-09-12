fn exists(path: String): Bool {
    return __io.exists(path);
}

fn remove(path: String): Bool {
    return __io.remove(path);
}

fn read_text(path: String) throws: String {
    return try __io.read_text(path);
}

fn read_lines(path: String) throws: Array[String] {
    return try __io.read_lines(path);
}

fn write_text(path: String, content: String) throws {
    try __io.write_text(path, content);
}
