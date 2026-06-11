// Channel pipeline -- Rust, std::sync::mpsc::sync_channel(256), 1 producer ->
// 4 consumers sharing the Receiver behind a Mutex (std has no MPMC receiver).
// Strings MOVE through the channel -- ownership transfer, no copy, no GC.
use std::sync::mpsc::sync_channel;
use std::sync::{Arc, Mutex};

const N: usize = 1_000_000;
const W: usize = 4;

fn main() {
    let (tx, rx) = sync_channel::<String>(256);
    let rx = Arc::new(Mutex::new(rx));
    let mut total: usize = 0;
    std::thread::scope(|s| {
        let mut handles = Vec::new();
        for _ in 0..W {
            let rx = Arc::clone(&rx);
            handles.push(s.spawn(move || {
                let mut c = 0usize;
                loop {
                    let msg = rx.lock().unwrap().recv();
                    match msg {
                        Ok(v) => c += v.len(),
                        Err(_) => return c,
                    }
                }
            }));
        }
        s.spawn(move || {
            for i in 0..N {
                tx.send(format!("item-{}", i)).unwrap();
            }
        });
        for h in handles {
            total += h.join().unwrap();
        }
    });
    print!("{}", total);
}
