# expect: requires 'try'
class Enemy {
    var hp: Int = 10;
    fn boom() throws {
        throw "hit";
    }
}

class Slime extends Enemy {
    fn go() {
        super.boom();
    }
}

fn main(): Int {
    var s = Slime { hp: 10 };
    s.go();
    return 0;
}
