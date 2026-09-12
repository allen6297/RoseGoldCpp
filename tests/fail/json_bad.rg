# expect: invalid JSON
import json;

fn main(): Int {
    try json.parse("{");
    return 0;
}
