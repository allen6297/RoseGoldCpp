# expect: missing 'label'
trait Named {
    fn label(self): String;
}

class Point impl Named {
    var x: Int = 0;
}

fn main(): Int {
    return 0;
}
