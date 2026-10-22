let iters = 2000
let k = 100000
var acc = 0
for it in 0..<iters {
    var xs = [Int](repeating: 0, count: k)
    for j in 0..<k { xs[j] = (it + j) % 997 }
    var s = 0
    for m in 0..<k { s += xs[m] }
    acc = (acc + s) % 1000000007
}
print(acc)
