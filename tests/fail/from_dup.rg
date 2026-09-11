# expect: duplicate import
from utils import add;
from utils import add;

fn main(): Int {
    return add(1, 2);
}
