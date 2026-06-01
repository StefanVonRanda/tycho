// binary-trees, Rust — Box-owned recursive enum, RAII drop per tree.
enum Tree { Leaf, Node(Box<Tree>, Box<Tree>) }
fn make(d: i64) -> Tree {
    if d == 0 { Tree::Leaf } else { Tree::Node(Box::new(make(d - 1)), Box::new(make(d - 1))) }
}
fn check(t: &Tree) -> i64 {
    match t { Tree::Leaf => 1, Tree::Node(l, r) => 1 + check(l) + check(r) }
}
fn main() {
    let (mind, maxd) = (4i64, 18i64);
    let stretch = maxd + 1;
    println!("stretch tree of depth {} check: {}", stretch, check(&make(stretch)));
    let longlived = make(maxd);
    let mut d = mind;
    while d <= maxd {
        let mut iters = 1i64; for _ in 0..(maxd - d + mind) { iters *= 2; }
        let mut sum = 0i64;
        for _ in 0..iters { sum += check(&make(d)); }
        println!("{} trees of depth {} check: {}", iters, d, sum);
        d += 2;
    }
    println!("long-lived tree of depth {} check: {}", maxd, check(&longlived));
}
