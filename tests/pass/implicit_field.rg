class Enemy {
    var hp: Int = 10;
    fn hurt(dmg: Int): Int {
        hp = hp - dmg;
        return hp;
    }
}

class Player {
    var hp: Int = 100;
    fn on_ready(): Int {
        take_damage(10);
        return hp;
    }
    fn take_damage(damage: Int): Int {
        hp = hp - damage;
        return hp;
    }
    fn peek(): Int {
        var hp = 0;
        return hp;
    }
}

fn main(): Int {
    var hp = 1;
    var e = Enemy {};
    print(e.hurt(3));
    print(hp);
    var p = Player {};
    print(p.on_ready());
    print(p.peek());
    print(p.hp);
    return 0;
}
