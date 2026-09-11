import checks;

class Enemy {
    var hp: Int = 10;
    fn boom() throws {
        throw "hit";
    }
    fn ok(): Int {
        return self.hp;
    }
}

class Slime extends Enemy {
    fn run(): Int {
        do {
            try super.boom();
        } catch e {
            checks.eq_string(e, "hit");
        }
        return super.ok();
    }
}

fn main(): Int {
    var s = Slime { hp: 10 };
    checks.eq(s.run(), 10);
    return 0;
}
