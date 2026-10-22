var idx = [String: [Int]]()
var seed = 1
let n = 300_000, w = 12, v = 8000
for d in 0..<n {
    for _ in 0..<w {
        seed = (seed &* 1103515245 &+ 12345) % 2147483647
        idx["t\(seed % v)", default: []].append(d)
    }
}
var sum = 0
for (k, ds) in idx {
    sum += k.utf8.count + ds.count
    for d in ds { sum += d }
}
print("vocab=\(idx.count) checksum=\(sum)")
