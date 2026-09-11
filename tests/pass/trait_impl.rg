trait Named {
    fn label(self): String;
}

trait Drawable {
    fn draw(self): String;
}

class Point impl Named, Drawable {
    var x: Int = 3;
    fn label(self): String {
        return "point";
    }
    fn draw(self): String {
        return "dot";
    }
}

class Vec2 {
    var x: Int = 3;
    var y: Int = 4;
    impl Named {
        fn label(self): String {
            return "vec2";
        }
    }
}

class Origin {
    var n: Int = 0;
}

impl Named for Origin {
    fn label(self): String {
        return "origin";
    }
}

fn main(): Int {
    var p = Point {};
    print(p.label());
    print(p.draw());
    var v = Vec2 {};
    print(v.label());
    var o = Origin {};
    print(o.label());
    return 0;
}
