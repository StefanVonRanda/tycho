import Foundation

final class J {
    enum Kind { case null, bool, num, str, arr, obj }
    let kind: Kind
    var num = 0
    var str = ""
    var vals = [J]()
    var keys = [String]()
    init(_ kind: Kind) { self.kind = kind }
}

var data = [UInt8]()
var pos = 0

func ws() {
    while pos < data.count {
        let c = data[pos]
        if c != 32 && c != 9 && c != 10 && c != 13 { break }
        pos += 1
    }
}
func parseString() -> String {
    pos += 1
    var out = [UInt8]()
    while pos < data.count && data[pos] != 34 {
        var c = data[pos]
        if c == 92 {
            pos += 1
            let e = data[pos]
            if e == 110 { c = 10 }
            else if e == 116 { c = 9 }
            else if e == 34 { c = 34 }
            else if e == 92 { c = 92 }
            else { c = e }
        }
        out.append(c)
        pos += 1
    }
    pos += 1
    return String(decoding: out, as: UTF8.self)
}
func parseArray() -> J {
    pos += 1
    let j = J(.arr)
    ws()
    while pos < data.count && data[pos] != 93 {
        j.vals.append(parseValue())
        ws()
        if pos < data.count && data[pos] == 44 { pos += 1; ws() }
    }
    pos += 1
    return j
}
func parseObject() -> J {
    pos += 1
    let j = J(.obj)
    ws()
    while pos < data.count && data[pos] != 125 {
        let k = parseString()
        ws()
        pos += 1
        j.keys.append(k)
        j.vals.append(parseValue())
        ws()
        if pos < data.count && data[pos] == 44 { pos += 1; ws() }
    }
    pos += 1
    return j
}
func parseValue() -> J {
    ws()
    let c = data[pos]
    if c == 123 { return parseObject() }
    if c == 91 { return parseArray() }
    if c == 34 { let j = J(.str); j.str = parseString(); return j }
    if c == 116 { pos += 4; let j = J(.bool); j.num = 1; return j }
    if c == 102 { pos += 5; return J(.bool) }
    if c == 110 { pos += 4; return J(.null) }
    var neg = false
    if data[pos] == 45 { neg = true; pos += 1 }
    var v = 0
    while pos < data.count && data[pos] >= 48 && data[pos] <= 57 {
        v = v * 10 + Int(data[pos] - 48)
        pos += 1
    }
    let j = J(.num)
    j.num = neg ? -v : v
    return j
}
func walk(_ j: J) -> Int {
    switch j.kind {
    case .num: return j.num + 1
    case .str: return j.str.utf8.count + 1
    case .bool, .null: return 1
    case .arr:
        var t = 1
        for x in j.vals { t += walk(x) }
        return t
    case .obj:
        var t = 1
        for k in j.keys { t += k.utf8.count }
        for x in j.vals { t += walk(x) }
        return t
    }
}
let input = FileHandle.standardInput.readDataToEndOfFile()
data = [UInt8](input)
var acc = 0
for _ in 0..<30 {
    pos = 0
    acc += walk(parseValue())
}
print("checksum=\(acc)")
