// Sliding window of fixed-size ints, Rust.
fn main() {
    let n: i64 = 2_000_000;
    let w: i64 = 50_000;
    let mut ring = vec![0i64; w as usize];
    let mut acc: i64 = 0;
    for i in 0..n {
        let s = (i % w) as usize;
        ring[s] = i % 100_000;
        acc += ring[s];
    }
    println!("{}", acc);
}
