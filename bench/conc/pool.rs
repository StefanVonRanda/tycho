// Worker pool: 1 producer -> bounded sync_channel(256) -> K worker threads.
// std has no MPMC channel, so the idiomatic pool shares the single Receiver
// behind an Arc<Mutex<>> (crossbeam would avoid the lock; no external crates
// here). Each worker accumulates a local, summed at join. Same MINSTD kernel,
// cap, and K = available_parallelism as the other contenders.
use std::sync::mpsc::sync_channel;
use std::sync::{Arc, Mutex};
use std::thread;

fn work(seed: i64) -> i64 {
    let mut x = (seed + 1) % 2147483647;
    for _ in 0..50 {
        x = (x * 48271) % 2147483647;
    }
    x
}

fn main() {
    const N: i64 = 1000000;
    let (tx, rx) = sync_channel::<i64>(256);
    let rx = Arc::new(Mutex::new(rx));
    let k = thread::available_parallelism().map(|n| n.get()).unwrap_or(1);
    let mut handles = Vec::new();
    for _ in 0..k {
        let rx = Arc::clone(&rx);
        handles.push(thread::spawn(move || {
            let mut local: i64 = 0;
            loop {
                let job = rx.lock().unwrap().recv();
                match job {
                    Ok(j) => local += work(j),
                    Err(_) => break,
                }
            }
            local
        }));
    }
    for i in 0..N {
        tx.send(i).unwrap();
    }
    drop(tx);
    let sum: i64 = handles.into_iter().map(|h| h.join().unwrap()).sum();
    print!("{}", sum);
}
