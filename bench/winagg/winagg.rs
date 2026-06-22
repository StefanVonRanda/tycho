// Windowed group-by aggregation — Rust, HashMap<i64, Vec<i64>> per window, dropped each
// cycle (RAII frees every Vec + the map at scope end — the manual-but-automatic churn
// teardown). Same LCG and checksum as winagg.ty (oracle: windows=40 checksum=505098931).
use std::collections::HashMap;

fn main() {
    const NW: i64 = 40;
    const W: i64 = 250000;
    const G: i64 = 8000;
    let mut seed: i64 = 1;
    let mut checksum: i64 = 0;
    for _win in 0..NW {
        let mut m: HashMap<i64, Vec<i64>> = HashMap::with_capacity(G as usize); // fresh per window
        for _e in 0..W {
            seed = (seed * 1103515245 + 12345) % 2147483647;
            let gid = seed % G;
            let v = (seed / G) % 100;
            m.entry(gid).or_default().push(v);
        }
        for (_k, lst) in &m {
            checksum += lst.len() as i64;
            for &v in lst {
                checksum += v;
            }
        }
        // m is dropped here: RAII frees every Vec and the table before the next window
    }
    println!("windows={} checksum={}", NW, checksum);
}
