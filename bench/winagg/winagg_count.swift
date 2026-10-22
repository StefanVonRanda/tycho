let nw = 40
let w = 250000
let g = 8000
var seed = 1
var checksum = 0
for _ in 0..<nw {
    let seed0 = seed
    var counts = [Int: Int]()
    counts.reserveCapacity(g)
    for _ in 0..<w {
        seed = (seed &* 1103515245 &+ 12345) % 2147483647
        counts[seed % g, default: 0] += 1
    }
    var m = [Int: [Int]]()
    m.reserveCapacity(g)
    for (k, c) in counts {
        var v = [Int](); v.reserveCapacity(c); m[k] = v
    }
    seed = seed0
    for _ in 0..<w {
        seed = (seed &* 1103515245 &+ 12345) % 2147483647
        let gid = seed % g
        m[gid, default: []].append((seed / g) % 100)
    }
    for lst in m.values {
        checksum += lst.count
        for v in lst { checksum += v }
    }
}
print("windows=\(nw) checksum=\(checksum)")
