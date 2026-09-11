class Vec2 {
    var x: Float = 0.0;
    var y: Float = 0.0;

    fn length(): Float {
        return __math.sqrt(x * x + y * y);
    }

    fn add(other: Vec2): Vec2 {
        return Vec2 { x: x + other.x, y: y + other.y };
    }
}

class Vec3 extends Vec2 {
    var z: Float = 0.0;

    fn length(): Float {
        return __math.sqrt(x * x + y * y + z * z);
    }

    fn add(other: Vec3): Vec3 {
        return Vec3 { x: x + other.x, y: y + other.y, z: z + other.z };
    }
}
