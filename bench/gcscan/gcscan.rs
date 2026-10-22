// GC-scan cost, Rust: 2M retained Strings plus transient Vec churn.
fn main() {
    let m: usize = 2_000_000;
    let mut live: Vec<String> = Vec::with_capacity(m);
    for i in 0..m { live.push(format!("x{}", i % 1000)); }
    let mut acc: i64 = 0;
    for r in 0..300 {
        let mut xs: Vec<i64> = Vec::with_capacity(50_000);
        for j in 0..50_000 { xs.push(((r + j) % 997) as i64); }
        let mut s: i64 = 0;
        for v in &xs { s += *v; }
        acc = (acc + s) % 1_000_000_007;
    }
    let mut lacc: i64 = 0;
    for s in &live { lacc += s.len() as i64; }
    println!("{} {}", acc, lacc);
}
