// maptree, Rust -- expert arena control: Vec<Node> with integer links.
struct Node { leaf: bool, n: i64, l: i32, r: i32 }
fn leaf(pool: &mut Vec<Node>, n: i64) -> i32 {
    let i = pool.len() as i32;
    pool.push(Node { leaf: true, n, l: -1, r: -1 });
    i
}
fn node(pool: &mut Vec<Node>, l: i32, r: i32) -> i32 {
    let i = pool.len() as i32;
    pool.push(Node { leaf: false, n: 0, l, r });
    i
}
fn build(pool: &mut Vec<Node>, d: i64) -> i32 {
    if d == 0 { leaf(pool, 1) } else {
        let l = build(pool, d - 1);
        let r = build(pool, d - 1);
        node(pool, l, r)
    }
}
fn maptree(pool: &mut Vec<Node>, t: &[Node], i: i32) -> i32 {
    let n = &t[i as usize];
    if n.leaf { leaf(pool, n.n + 1) } else {
        let l = maptree(pool, t, n.l);
        let r = maptree(pool, t, n.r);
        node(pool, l, r)
    }
}
fn checksum(t: &[Node], i: i32) -> i64 {
    let n = &t[i as usize];
    if n.leaf { n.n } else { checksum(t, n.l) + checksum(t, n.r) }
}
fn main() {
    let mut live = Vec::new();
    let mut scratch = Vec::new();
    let t = build(&mut live, 16);
    let mut sum = 0i64;
    for _ in 0..200 {
        let m = maptree(&mut scratch, &live, t);
        sum += checksum(&scratch, m);
        scratch.clear();
    }
    println!("{}", sum);
}
