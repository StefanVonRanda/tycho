// Trie, Rust: HashMap<u8, Trie> per node, all nodes held live.
use std::collections::HashMap;
struct Trie { kids: HashMap<u8, Trie>, word: bool }
fn insert(t: &mut Trie, s: &[u8], i: usize, nn: &mut i64) -> i64 {
    if i >= s.len() {
        if t.word { return 0; }
        t.word = true;
        return 1;
    }
    let c = s[i];
    if !t.kids.contains_key(&c) {
        t.kids.insert(c, Trie { kids: HashMap::new(), word: false });
        *nn += 1;
    }
    insert(t.kids.get_mut(&c).unwrap(), s, i + 1, nn)
}
fn main() {
    let n: i64 = 150000;
    let mut state: u64 = 88172645463325252;
    let mut root = Trie { kids: HashMap::new(), word: false };
    let mut nn: i64 = 1;
    let mut nwords: i64 = 0;
    for _ in 0..n {
        state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        let wlen = 3 + (state & 1073741823) % 5;
        let mut s = Vec::with_capacity(wlen as usize);
        for _ in 0..wlen {
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            s.push(97 + ((state & 1073741823) % 26) as u8);
        }
        nwords += insert(&mut root, &s, 0, &mut nn);
    }
    println!("{} {}", nn, nwords);
}
