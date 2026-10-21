let n = 100_000
let m = 200
let k = 1_000_003
var xs = [Int](repeating: 0, count: n)
for i in 0..<n { xs[i] = i }
var checksum = 0
for j in 0..<m {
    var ys = [Int](repeating: 0, count: n)
    for i in 0..<n {
        let t = xs[i] &* 1_103_515_245 &+ 12_345 &+ j
        ys[i] = t % k
    }
    var s = 0
    for i in 0..<n { s += ys[i] }
    checksum = (checksum + s) % k
}
print(checksum)
