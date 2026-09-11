class Enemy {
    var hp: Int = 10;
    fn hurt(self, dmg: Int): Int {
        self.hp = self.hp - dmg;
        return self.hp;
    }
}

class Slime extends Enemy {
    fn hurt(self, dmg: Int): Int {
        return super.hurt(dmg / 2);
    }
}

fn main(): Int {
    var s = Slime { hp: 10 };
    print(s.hurt(4));
    return 0;
}
