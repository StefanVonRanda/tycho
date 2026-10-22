// LRU cache, Rust: arena Vec<Node> + HashMap<key, index>.
use std::collections::HashMap;
struct Node { key: i64, val: i64, prev: i64, next: i64 }
struct Lru { pool: Vec<Node>, idx: HashMap<i64, usize>, head: i64, tail: i64, size: usize }
impl Lru {
    fn new(cap: usize) -> Lru {
        Lru { pool: Vec::with_capacity(cap), idx: HashMap::with_capacity(cap), head: -1, tail: -1, size: 0 }
    }
    fn unlink(&mut self, i: usize) {
        let p = self.pool[i].prev;
        let nx = self.pool[i].next;
        if p == -1 { self.head = nx; } else { self.pool[p as usize].next = nx; }
        if nx == -1 { self.tail = p; } else { self.pool[nx as usize].prev = p; }
    }
    fn push_front(&mut self, i: usize) {
        self.pool[i].prev = -1;
        self.pool[i].next = self.head;
        if self.head == -1 { self.tail = i as i64; } else { self.pool[self.head as usize].prev = i as i64; }
        self.head = i as i64;
    }
    fn touch(&mut self, i: usize) { self.unlink(i); self.push_front(i); }
    fn get(&mut self, key: i64) -> (i64, i64) {
        if let Some(&i) = self.idx.get(&key) {
            self.touch(i);
            (1, self.pool[i].val)
        } else { (0, 0) }
    }
    fn put(&mut self, cap: usize, key: i64, val: i64) {
        if let Some(&i) = self.idx.get(&key) {
            self.pool[i].val = val;
            self.touch(i);
            return;
        }
        if self.size >= cap {
            let t = self.tail;
            self.unlink(t as usize);
            let old = self.pool[t as usize].key;
            self.idx.remove(&old);
            self.pool[t as usize].key = key;
            self.pool[t as usize].val = val;
            self.idx.insert(key, t as usize);
            self.push_front(t as usize);
            return;
        }
        let i = self.pool.len();
        self.pool.push(Node { key, val, prev: -1, next: -1 });
        self.size += 1;
        self.idx.insert(key, i);
        self.push_front(i);
    }
}
fn main() {
    let cap: usize = 200000;
    let keyspace: i64 = 600000;
    let nops: i64 = 5000000;
    let mut c = Lru::new(cap);
    let mut state: u64 = 88172645463325252;
    let mut hits: i64 = 0;
    let mut sum: i64 = 0;
    for _ in 0..nops {
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        let key = ((state & 1073741823) as i64) % keyspace;
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        let r = state & 1073741823;
        if r % 100 < 70 {
            let (h, v) = c.get(key);
            hits += h;
            sum += v;
        } else {
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            let val = ((state & 1073741823) as i64) % 1000000;
            c.put(cap, key, val);
        }
    }
    println!("{} {}", hits, sum);
}
