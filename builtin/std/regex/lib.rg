fn valid(pattern: String): Bool {
    return __regex.valid(pattern);
}

fn is_match(pattern: String, text: String): Bool {
    return __regex.is_match(pattern, text);
}

fn find(pattern: String, text: String): Int {
    return __regex.find(pattern, text);
}

fn find_match(pattern: String, text: String): String {
    return __regex.find_match(pattern, text);
}

fn captures(pattern: String, text: String): Array {
    return __regex.captures(pattern, text);
}

fn findall(pattern: String, text: String): Array {
    return __regex.findall(pattern, text);
}

fn replace(pattern: String, text: String, with: String): String {
    return __regex.replace(pattern, text, with);
}

fn split(pattern: String, text: String): Array {
    return __regex.split(pattern, text);
}
