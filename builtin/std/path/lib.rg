fn join(a: String, b: String): String {
    return __path.join(a, b);
}

fn parent(p: String): String {
    return __path.parent(p);
}

fn stem(p: String): String {
    return __path.stem(p);
}
