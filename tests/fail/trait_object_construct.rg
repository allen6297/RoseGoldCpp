# expect: cannot construct trait
trait Named {
    fn tag(self): String;
}

fn main(): Int {
    var p = Named {};
    return 0;
}
