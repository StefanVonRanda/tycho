// Compute-bound parallel reduction over FLOAT work -- Rust, scoped threads, one
// row-chunk per core, i64 partials joined at the end. Mirrors mandelbrot.ty
// op-for-op. Rust does not form FMA without an explicit call, and the multiply is
// materialized into `m` anyway, so the chaotic escape counts agree bit-for-bit.
use std::thread;

const W: i32 = 1200;
const H: i32 = 1200;
const MAXIT: i32 = 2000;

const XMIN: f64 = 0.0 - 2.5;
const XMAX: f64 = 1.0;
const YMIN: f64 = 0.0 - 1.25;
const YMAX: f64 = 1.25;

fn escape(cx: f64, cy: f64) -> i32 {
    let mut zx = 0.0f64;
    let mut zy = 0.0f64;
    let mut i = 0;
    while i < MAXIT {
        let x2 = zx * zx;
        let y2 = zy * zy;
        if x2 + y2 > 4.0 {
            return i;
        }
        let m = 2.0 * zx * zy;
        zy = m + cy;
        zx = x2 - y2 + cx;
        i += 1;
    }
    MAXIT
}

fn main() {
    let k = thread::available_parallelism().map(|n| n.get()).unwrap_or(1) as i32;
    let mut partials: Vec<(i64, i64)> = Vec::new();
    thread::scope(|s| {
        let mut handles = Vec::new();
        for c in 0..k {
            let lo = H * c / k;
            let hi = H * (c + 1) / k;
            handles.push(s.spawn(move || {
                let mut total: i64 = 0;
                let mut inset: i64 = 0;
                for py in lo..hi {
                    let cy = YMIN + (YMAX - YMIN) * (py as f64) / (H as f64);
                    for px in 0..W {
                        let cx = XMIN + (XMAX - XMIN) * (px as f64) / (W as f64);
                        let e = escape(cx, cy);
                        total += e as i64;
                        if e >= MAXIT {
                            inset += 1;
                        }
                    }
                }
                (total, inset)
            }));
        }
        for h in handles {
            partials.push(h.join().unwrap());
        }
    });
    let mut total: i64 = 0;
    let mut inset: i64 = 0;
    for (t, ins) in partials {
        total += t;
        inset += ins;
    }
    print!("{} {}", total, inset);
}
