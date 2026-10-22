// Inverted index, map-native Rust: HashMap<String, Vec<i64>>.
use std::collections::HashMap;

fn main() {
    let n: i64 = 300_000;
    let w: i64 = 12;
    let v: i64 = 8000;
    let mut idx: HashMap<String, Vec<i64>> = HashMap::new();
    let mut seed: i64 = 1;
    for d in 0..n {
        for _ in 0..w {
            seed = (seed * 1103515245 + 12345) % 2147483647;
            let tid = seed % v;
            idx.entry(format!("t{}", tid)).or_default().push(d);
        }
    }
    let mut sum: i64 = 0;
    for (k, ds) in &idx {
        sum += k.len() as i64 + ds.len() as i64;
        for d in ds { sum += *d; }
    }
    println!("vocab={} checksum={}", idx.len(), sum);
}
