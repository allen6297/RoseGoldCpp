import std.json;

fn main(): Int {
    var obj = try json.parse("{\"n\": 3, \"ok\": true, \"s\": \"hi\"}");
    checks.eq(obj["n"], 3);
    checks.that(obj["ok"]);
    checks.eq_string(obj["s"], "hi");
    checks.eq_string(json.stringify(obj), "{\"n\":3,\"ok\":true,\"s\":\"hi\"}");

    var xs = try json.parse("[1, 2, 3]");
    checks.eq(len(xs), 3);
    checks.eq(xs[1], 2);

    checks.eq(try json.parse("7"), 7);
    checks.eq(try json.parse("1.5"), 1.5);
    checks.that(try json.parse("true"));
    checks.eq_string(try json.parse("\"x\\ny\""), "x\ny");

    checks.that(json.valid("{\"a\":1}"));
    checks.that(!json.valid("{"));
    checks.that(!json.valid("null"));

    checks.eq_string(json.stringify([1, 2]), "[1,2]");
    checks.eq_string(std.json.stringify({"a": 1}), "{\"a\":1}");
    return 0;
}
