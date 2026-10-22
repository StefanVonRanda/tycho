var seed: Int = 1
var checksum = 0
let nw = 40
let w = 250000
let g = 8000
for _ in 0..<nw {
    var m = [Int: [Int]]()
    m.reserveCapacity(g)
    for _ in 0..<w {
        seed = (seed &* 1103515245 &+ 12345) % 2147483647
        let gid = seed % g
        let v = (seed / g) % 100
        m[gid, default: []].append(v)
    }
    for lst in m.values {
        checksum += lst.count
        for v in lst { checksum += v }
    }
}
print("windows=\(nw) checksum=\(checksum)")
