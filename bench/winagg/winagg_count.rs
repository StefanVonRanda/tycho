// Windowed group-by, expert Rust: count first, reserve exact Vecs, replay.
use std::collections::HashMap;
fn main() {
    let (nw, w, g) = (40i64, 250000i64, 8000i64);
    let mut seed = 1i64;
    let mut checksum = 0i64;
    for _ in 0..nw {
        let seed0 = seed;
        let mut counts: HashMap<i64, usize> = HashMap::with_capacity(g as usize);
        for _ in 0..w {
            seed = (seed * 1103515245 + 12345) % 2147483647;
            *counts.entry(seed % g).or_insert(0) += 1;
        }
        let mut m: HashMap<i64, Vec<i64>> = HashMap::with_capacity(g as usize);
        for (k, c) in &counts {
            let mut v = Vec::with_capacity(*c);
            v.reserve(*c);
            m.insert(*k, v);
        }
        seed = seed0;
        for _ in 0..w {
            seed = (seed * 1103515245 + 12345) % 2147483647;
            let gid = seed % g;
            let v = (seed / g) % 100;
            m.get_mut(&gid).unwrap().push(v);
        }
        for lst in m.values() {
            checksum += lst.len() as i64;
            for v in lst { checksum += *v; }
        }
    }
    println!("windows={} checksum={}", nw, checksum);
}
