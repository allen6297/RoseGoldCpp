class Enemy {
    var hp: Int = 10;
    fn hurt(self, dmg: Int): Int {
        self.hp = self.hp - dmg;
        return self.hp;
    }
}

class Slime extends Enemy {
    var goo: Int = 1;
}

fn main(): Int {
    var s = Slime {};
    print(s.hp);
    print(s.goo);
    print(s.hurt(3));
    return 0;
}
