// dijkstra, Swift: deterministic graph + binary min-heap with lazy deletion.
import Foundation

let n = 300000, deg = 4
let INF: Int64 = 4000000000000000000
struct Edge { let to: Int; let w: Int64 }
struct H { let d: Int64; let node: Int }

var state: UInt64 = 88172645463325252
var adj = [[Edge]](repeating: [], count: n)
for u in 0..<n {
    var row = [Edge]()
    row.reserveCapacity(deg)
    for _ in 0..<deg {
        state = state &* 6364136223846793005 &+ 1442695040888963407
        let to = Int((state & 1073741823) % UInt64(n))
        state = state &* 6364136223846793005 &+ 1442695040888963407
        let w = Int64((state & 1073741823) % 100) + 1
        row.append(Edge(to: to, w: w))
    }
    adj[u] = row
}
var dist = [Int64](repeating: INF, count: n)
dist[0] = 0
var heap = [H]()
heap.reserveCapacity(1024)
heap.append(H(d: 0, node: 0))
while !heap.isEmpty {
    let c = heap[0]
    heap[0] = heap[heap.count - 1]
    heap.removeLast()
    var i = 0
    while true {
        let l = 2*i+1, r = 2*i+2
        var s = i
        if l < heap.count && heap[l].d < heap[s].d { s = l }
        if r < heap.count && heap[r].d < heap[s].d { s = r }
        if s == i { break }
        heap.swapAt(i, s); i = s
    }
    if c.d > dist[c.node] { continue }
    for e in adj[c.node] {
        let nd = c.d + e.w
        if nd < dist[e.to] {
            dist[e.to] = nd
            heap.append(H(d: nd, node: e.to))
            var j = heap.count - 1
            while j > 0 {
                let p = (j-1)/2
                if heap[p].d <= heap[j].d { break }
                heap.swapAt(p, j); j = p
            }
        }
    }
}
var sum: Int64 = 0
var reach = 0
for i in 0..<n where dist[i] < INF { sum += dist[i]; reach += 1 }
print("\(reach) \(sum)")
