// trie, Swift: deterministic prefix tree held across the whole run.
import Foundation

let n = 150000
final class Node {
    var kids: [UInt8: Node] = [:]
    var word = false
}
var nodes: Int64 = 1

func insert(_ root: Node, _ s: [UInt8]) -> Int {
    var t = root
    for c in s {
        if let nx = t.kids[c] {
            t = nx
        } else {
            let nx = Node()
            t.kids[c] = nx
            nodes += 1
            t = nx
        }
    }
    if t.word { return 0 }
    t.word = true
    return 1
}

var state: UInt64 = 88172645463325252
let root = Node()
var nwords: Int64 = 0
for _ in 0..<n {
    state = state &* 6364136223846793005 &+ 1442695040888963407
    let wlen = 3 + Int((state & 1073741823) % 5)
    var s = [UInt8]()
    s.reserveCapacity(wlen)
    for _ in 0..<wlen {
        state = state &* 6364136223846793005 &+ 1442695040888963407
        s.append(UInt8(97 + (state & 1073741823) % 26))
    }
    nwords += Int64(insert(root, s))
}
print("\(nodes) \(nwords)")
