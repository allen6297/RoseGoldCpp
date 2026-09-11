from vec import Vec2;
from vec import Vec3;

fn main(): Int {
    var a = Vec2 { x: 3.0, y: 4.0 };
    checks.eq(a.length(), 5.0);
    var b = a.add(Vec2 { x: 1.0, y: 2.0 });
    checks.eq(b.x, 4.0);
    checks.eq(b.y, 6.0);

    var c = Vec3 { x: 1.0, y: 2.0, z: 2.0 };
    checks.eq(c.length(), 3.0);
    var d = c.add(Vec3 { x: 0.0, y: 0.0, z: 1.0 });
    checks.eq(d.z, 3.0);
    return 0;
}
