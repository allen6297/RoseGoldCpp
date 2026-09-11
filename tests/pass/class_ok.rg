class Enemy {
    var hp: Int = 10;

    fn hurt(self, dmg: Int): Int {
        self.hp = self.hp - dmg;
        return self.hp;
    }

    fn tag(): String {
        return "enemy";
    }
}

fn main(): Int {
    var e = Enemy {};
    print(e.hp);
    print(e.hurt(3));
    print(e.tag());
    var f = Enemy { hp: 4 };
    print(f.hp);
    return 0;
}
