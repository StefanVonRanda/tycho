// Inverted index, Rust: HashMap<term, slot> + Posting with growing Vecs.
use std::collections::HashMap;

struct Posting {
    term: String,
    docs: Vec<i64>,
    freqs: Vec<i64>,
}

fn add(post: &mut Vec<Posting>, idx: &mut HashMap<String, usize>, term: String, doc: i64) {
    if let Some(&pi) = idx.get(&term) {
        let p = &mut post[pi];
        if let Some(&last) = p.docs.last() {
            if last == doc {
                let n = p.freqs.len();
                p.freqs[n - 1] += 1;
                return;
            }
        }
        p.docs.push(doc);
        p.freqs.push(1);
    } else {
        let pi = post.len();
        post.push(Posting { term: term.clone(), docs: vec![doc], freqs: vec![1] });
        idx.insert(term, pi);
    }
}

fn main() {
    let n: i64 = 300_000;
    let w: i64 = 12;
    let v: i64 = 8000;
    let mut post: Vec<Posting> = Vec::new();
    let mut idx: HashMap<String, usize> = HashMap::new();
    let mut seed: i64 = 1;
    for d in 0..n {
        for _ in 0..w {
            seed = (seed * 1103515245 + 12345) % 2147483647;
            let tid = seed % v;
            add(&mut post, &mut idx, format!("t{}", tid), d);
        }
    }
    let mut sum: i64 = 0;
    for p in &post {
        sum += p.term.len() as i64 + p.docs.len() as i64;
        for f in &p.freqs { sum += *f; }
    }
    let mut hits: i64 = 0;
    if let Some(&p0) = idx.get("t0") {
        if let Some(&p1) = idx.get("t1") {
            for doc in &post[p0].docs {
                if post[p1].docs.contains(doc) { hits += 1; }
            }
        }
    }
    println!("vocab={} checksum={} and01={}", post.len(), sum, hits);
}
