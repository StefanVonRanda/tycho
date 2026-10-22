// Inverted index, count-then-fill, Rust: exact-capacity posting vectors.
use std::collections::HashMap;
struct Posting { term: String, docs: Vec<i64>, freqs: Vec<i64> }
fn main() {
    let (n, w, v): (i64, i64, i64) = (300_000, 12, 8000);
    let mut idx: HashMap<String, usize> = HashMap::new();
    let mut terms: Vec<String> = Vec::new();
    let mut counts: Vec<usize> = Vec::new();
    let mut seed: i64 = 1;
    for _ in 0..n { for _ in 0..w {
        seed = (seed * 1103515245 + 12345) % 2147483647;
        let term = format!("t{}", seed % v);
        if let Some(&slot) = idx.get(&term) { counts[slot] += 1; }
        else { idx.insert(term.clone(), terms.len()); terms.push(term); counts.push(1); }
    }}
    let mut post: Vec<Posting> = terms.iter().enumerate().map(|(i, t)| Posting {
        term: t.clone(), docs: Vec::with_capacity(counts[i]), freqs: Vec::with_capacity(counts[i])
    }).collect();
    seed = 1;
    for d in 0..n { for _ in 0..w {
        seed = (seed * 1103515245 + 12345) % 2147483647;
        let pi = idx[&format!("t{}", seed % v)];
        let p = &mut post[pi];
        if p.docs.last() == Some(&d) { let k = p.freqs.len(); p.freqs[k - 1] += 1; }
        else { p.docs.push(d); p.freqs.push(1); }
    }}
    let mut sum: i64 = 0;
    for p in &post { sum += p.term.len() as i64 + p.docs.len() as i64; for f in &p.freqs { sum += *f; } }
    let mut hits: i64 = 0;
    if let (Some(&p0), Some(&p1)) = (idx.get("t0"), idx.get("t1")) {
        for d in &post[p0].docs { if post[p1].docs.contains(d) { hits += 1; } }
    }
    println!("vocab={} checksum={} and01={}", post.len(), sum, hits);
}
