// Map-native inverted index, count-then-fill, Rust.
use std::collections::HashMap;
fn main() {
    let (n, w, v): (i64, i64, i64) = (300_000, 12, 8000);
    let mut cnt: HashMap<String, i64> = HashMap::new();
    let mut seed: i64 = 1;
    for _ in 0..n { for _ in 0..w {
        seed = (seed * 1103515245 + 12345) % 2147483647;
        *cnt.entry(format!("t{}", seed % v)).or_insert(0) += 1;
    }}
    let mut idx: HashMap<String, Vec<i64>> = HashMap::new();
    for (k, c) in &cnt { idx.insert(k.clone(), Vec::with_capacity(*c as usize)); }
    seed = 1;
    for d in 0..n { for _ in 0..w {
        seed = (seed * 1103515245 + 12345) % 2147483647;
        idx.get_mut(&format!("t{}", seed % v)).unwrap().push(d);
    }}
    let mut sum: i64 = 0;
    for (k, ds) in &idx { sum += k.len() as i64 + ds.len() as i64; for d in ds { sum += *d; } }
    println!("vocab={} checksum={}", idx.len(), sum);
}
