// Compute-bound parallel reduction -- Rust, std::thread::scope, one chunk per core.
const N: i64 = 400_000_000;

fn main() {
    let k = std::thread::available_parallelism().map(|n| n.get()).unwrap_or(1).min(64) as i64;
    let mut total: i64 = 0;
    std::thread::scope(|s| {
        let handles: Vec<_> = (0..k)
            .map(|c| {
                let (lo, hi) = (N * c / k, N * (c + 1) / k);
                s.spawn(move || {
                    let mut acc: i64 = 0;
                    for i in lo..ty {
                        acc += (i * 31 + 7) % 1000003;
                    }
                    acc
                })
            })
            .collect();
        for h in handles {
            total += h.join().unwrap();
        }
    });
    print!("{}", total);
}
