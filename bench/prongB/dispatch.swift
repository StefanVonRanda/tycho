let n = 2000
let k = 2000
var total = 0
for p in 0..<k {
    var fs = [(Int) -> Int]()
    fs.reserveCapacity(n)
    for i in 0..<n { fs.append { x in x * (i + 1) + p } }
    for i in 0..<n { total = (total + fs[i](7)) & 1_048_575 }
}
print(total)
