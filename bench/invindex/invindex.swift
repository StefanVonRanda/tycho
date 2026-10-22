final class Posting {
    let term: String
    var docs = [Int]()
    var freqs = [Int]()
    init(_ t: String) { term = t }
}
var post = [Posting]()
var idx = [String: Int]()
var seed = 1
let n = 300_000, w = 12, v = 8000
func add(_ term: String, _ doc: Int) {
    if let pi = idx[term] {
        let p = post[pi]
        if let last = p.docs.last, last == doc {
            p.freqs[p.freqs.count - 1] += 1
        } else {
            p.docs.append(doc); p.freqs.append(1)
        }
    } else {
        let p = Posting(term)
        p.docs.append(doc); p.freqs.append(1)
        idx[term] = post.count
        post.append(p)
    }
}
for d in 0..<n {
    for _ in 0..<w {
        seed = (seed &* 1103515245 &+ 12345) % 2147483647
        add("t\(seed % v)", d)
    }
}
var sum = 0
for p in post {
    sum += p.term.utf8.count + p.docs.count
    for f in p.freqs { sum += f }
}
var hits = 0
if let p0 = idx["t0"], let p1 = idx["t1"] {
    for doc in post[p0].docs where post[p1].docs.contains(doc) { hits += 1 }
}
print("vocab=\(post.count) checksum=\(sum) and01=\(hits)")
