fn step(a: &[i64], p: i64) -> Vec<i64> {
    let mut b = Vec::with_capacity(a.len());
    for &x in a { let t = x * 1103515245 + 12345; b.push(t - (t / p) * p); }
    b
}
fn main() {
    let n: i64 = 100000; let m = 2000; let p: i64 = 2147483647;
    let mut a: Vec<i64> = (0..n).collect();
    for _ in 0..m { a = step(&a, p); }
    let mut s: i64 = 0; for &x in &a { s += x; }
    println!("{}", s - (s / 1000003) * 1000003);
}
