func step(_ a: [Int], _ p: Int) -> [Int] {
    var b = [Int](repeating: 0, count: a.count)
    for i in 0..<a.count {
        let t = a[i] * 1103515245 + 12345
        b[i] = t - (t / p) * p
    }
    return b
}
let n = 100000
let m = 2000
let p = 2147483647
var a = Array(0..<n)
for _ in 0..<m { a = step(a, p) }
var s = 0
for x in a { s += x }
print(s - (s / 1000003) * 1000003)
