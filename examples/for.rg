fn main(): Int {
    var sum = 0;
    for x in [1, 2, 3] {
        sum = sum + x;
    }
    print(sum);

    for k in {"ada": 10, "grace": 12} {
        print(k);
    }

    for i in 3 {
        print(i);
    }

    for i in 0..3 {
        print(i);
    }

    for i in 1..=2 {
        print(i);
    }
    return 0;
}
