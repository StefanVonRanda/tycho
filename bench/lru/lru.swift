// lru, Swift: fixed-capacity LRU over the shared LCG operation stream.
import Foundation

let CAP = 200000, KEYSPACE = 600000
let nops = 5000000
final class Node {
    var key: Int, val: Int
    var prev: Node?, next: Node?
    init(_ k: Int, _ v: Int) { key = k; val = v }
}
var head: Node?, tail: Node?
var size = 0
var idx = [Int: Node]()

func unlink(_ n: Node) {
    if let p = n.prev { p.next = n.next } else { head = n.next }
    if let x = n.next { x.prev = n.prev } else { tail = n.prev }
}
func front(_ n: Node) {
    n.prev = nil; n.next = head
    if let h = head { h.prev = n } else { tail = n }
    head = n
}
func touch(_ n: Node) { unlink(n); front(n) }
func get(_ k: Int) -> (Int, Int) {
    guard let n = idx[k] else { return (0, 0) }
    touch(n); return (1, n.val)
}
func put(_ k: Int, _ v: Int) {
    if let n = idx[k] { n.val = v; touch(n); return }
    if size >= CAP, let t = tail {
        unlink(t); idx.removeValue(forKey: t.key)
        t.key = k; t.val = v; idx[k] = t; front(t); return
    }
    let n = Node(k, v); idx[k] = n; size += 1; front(n)
}
var state: UInt64 = 88172645463325252
var hits = 0, sum = 0
for _ in 0..<nops {
    state = state &* 6364136223846793005 &+ 1442695040888963407
    let k = Int((state & 1073741823) % UInt64(KEYSPACE))
    state = state &* 6364136223846793005 &+ 1442695040888963407
    let r = Int(state & 1073741823)
    if r % 100 < 70 {
        let (h, v) = get(k); if h != 0 { hits += h; sum += v }
    } else {
        state = state &* 6364136223846793005 &+ 1442695040888963407
        put(k, Int((state & 1073741823) % 1000000))
    }
}
print("\(hits) \(sum)")
