let n = 2_000_000
let w = 50_000
var ring = [Int](repeating: 0, count: w)
var acc = 0
for i in 0..<n {
    let s = i % w
    ring[s] = i % 100_000
    acc += ring[s]
}
print(acc)
