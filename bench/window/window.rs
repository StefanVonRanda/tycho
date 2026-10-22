// Sliding window of heap strings, Rust: the evicted String is dropped per slot.
fn main() {
    let n: usize = 2_000_000;
    let w: usize = 50_000;
    let mut ring: Vec<String> = vec![String::new(); w];
    let mut acc: i64 = 0;
    for i in 0..n {
        let s = i % w;
        ring[s] = format!("rec{}", i % 100_000);
        acc += ring[s].len() as i64;
    }
    println!("{}", acc);
}
