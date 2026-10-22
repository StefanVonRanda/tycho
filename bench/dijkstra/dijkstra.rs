// Dijkstra, Rust: adjacency Vec<Vec<Edge>> and a binary min-heap.
use std::cmp::Reverse;
use std::collections::BinaryHeap;
struct Edge { to: usize, w: i64 }
fn main() {
    let n: usize = 300000;
    let deg: usize = 4;
    let mut state: u64 = 88172645463325252;
    let mut adj: Vec<Vec<Edge>> = (0..n).map(|_| Vec::with_capacity(deg)).collect();
    for u in 0..n {
        for _ in 0..deg {
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            let to = ((state & 1073741823) as usize) % n;
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            let w = ((state & 1073741823) % 100) as i64 + 1;
            adj[u].push(Edge { to, w });
        }
    }
    let inf: i64 = 4000000000000000000;
    let mut dist = vec![inf; n];
    dist[0] = 0;
    let mut heap: BinaryHeap<Reverse<(i64, usize)>> = BinaryHeap::new();
    heap.push(Reverse((0, 0)));
    while let Some(Reverse((d, u))) = heap.pop() {
        if d > dist[u] { continue; }
        for e in &adj[u] {
            let nd = d + e.w;
            if nd < dist[e.to] {
                dist[e.to] = nd;
                heap.push(Reverse((nd, e.to)));
            }
        }
    }
    let mut sum: i64 = 0;
    let mut reach: i64 = 0;
    for &d in &dist {
        if d < inf { sum += d; reach += 1; }
    }
    println!("{} {}", reach, sum);
}
