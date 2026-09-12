# expect: json null
import json;

fn main(): Int {
    try json.parse("null");
    return 0;
}
