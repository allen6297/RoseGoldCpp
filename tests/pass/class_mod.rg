mod pets {
    class Animal {
        var hp: Int = 10;
        fn hurt(self, dmg: Int): Int {
            self.hp = self.hp - dmg;
            return self.hp;
        }
    }
    pub class Dog extends Animal {
        var name: String = "Rex";
        fn hurt(self, dmg: Int): Int {
            return super.hurt(dmg);
        }
    }
}

from pets import Dog;

fn main(): Int {
    var d = Dog {};
    print(d.name);
    print(d.hurt(3));
    return 0;
}
