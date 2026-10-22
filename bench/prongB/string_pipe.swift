let m = 4000
let kk = 256
let p = 1000003
var checksum = 0
for j in 0..<m {
    var s = [UInt8]()
    s.reserveCapacity(kk)
    for i in 0..<kk { s.append(UInt8(ascii: "0") + UInt8((i + j) % 10)) }
    var h = 0
    for b in s { h += Int(b) }
    checksum = (checksum + h) % p
}
print(checksum)
