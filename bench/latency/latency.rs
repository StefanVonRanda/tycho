// Latency workload, Rust: exact-size Vec per round, dropped each round.
fn main() {
    let iters: i64 = 2000;
    let k: usize = 100_000;
    let mut acc: i64 = 0;
    for it in 0..iters {
        let mut xs = vec![0i64; k];
        for j in 0..k { xs[j] = (it + j as i64) % 997; }
        let mut s: i64 = 0;
        for m in 0..k { s += xs[m]; }
        acc = (acc + s) % 1_000_000_007;
    }
    println!("{}", acc);
}
