let m = 2_000_000
var live = [String](repeating: "", count: m)
for i in 0..<m { live[i] = "x\(i % 1000)" }
var acc = 0
for r in 0..<300 {
    var xs = [Int](repeating: 0, count: 50_000)
    for j in 0..<xs.count { xs[j] = (r + j) % 997 }
    var s = 0
    for v in xs { s += v }
    acc = (acc + s) % 1_000_000_007
}
var lacc = 0
for s in live { lacc += s.utf8.count }
print("\(acc) \(lacc)")
