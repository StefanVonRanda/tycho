// binary-trees, Rust -- expert arena control: Vec<Node> with integer children.
struct Node { l: i32, r: i32 }
fn make(pool: &mut Vec<Node>, d: i64) -> i32 {
    let idx = pool.len() as i32;
    pool.push(Node { l: -1, r: -1 });
    if d > 0 {
        let l = make(pool, d - 1);
        let r = make(pool, d - 1);
        pool[idx as usize].l = l;
        pool[idx as usize].r = r;
    }
    idx
}
fn check(pool: &[Node], i: i32) -> i64 {
    let n = &pool[i as usize];
    if n.l < 0 { 1 } else { 1 + check(pool, n.l) + check(pool, n.r) }
}
fn main() {
    let (mind, maxd) = (4i64, 18i64);
    let stretch = maxd + 1;
    let mut scratch = Vec::new();
    let mut live = Vec::new();
    make(&mut scratch, stretch);
    println!("stretch tree of depth {} check: {}", stretch, check(&scratch, 0));
    scratch.clear();
    make(&mut live, maxd);
    let mut d = mind;
    while d <= maxd {
        let mut iters = 1i64;
        for _ in 0..(maxd - d + mind) { iters *= 2; }
        let mut sum = 0i64;
        for _ in 0..iters {
            make(&mut scratch, d);
            sum += check(&scratch, 0);
            scratch.clear();
        }
        println!("{} trees of depth {} check: {}", iters, d, sum);
        d += 2;
    }
    println!("long-lived tree of depth {} check: {}", maxd, check(&live, 0));
}
