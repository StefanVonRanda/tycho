// json tree, Swift: one generic in-memory tree, held across the fold.
import Foundation

let n = 50000

final class Json {
    enum Kind { case null, bool, num, str, arr, obj }
    var kind: Kind = .null
    var num: Int64 = 0
    var str: String = ""
    var items: [Json] = []
    var keys: [String] = []
    var vals: [Json] = []
    init(_ k: Kind) { kind = k }
}

var doc = ""
doc.reserveCapacity(1 << 20)
doc.append("[")
for i in 0..<n {
    if i > 0 { doc.append(",") }
    doc += "{\"id\":\(i),\"cat\":\(i % 32),\"amt\":\((Int64(i) * 2654435761) % 1000),\"name\":\"u\(i % 1000)\"}"
}
doc.append("]")
let bytes = Array(doc.utf8)
var pos = 0

func ws() {
    while pos < bytes.count {
        let c = bytes[pos]
        if c != 32 && c != 9 && c != 10 && c != 13 { return }
        pos += 1
    }
}

func parseString() -> String {
    pos += 1
    let start = pos
    while pos < bytes.count && bytes[pos] != 34 { pos += 1 }
    let s = String(bytes: bytes[start..<pos], encoding: .utf8)!
    pos += 1
    return s
}

func parseNumber() -> Json {
    let start = pos
    if bytes[pos] == 45 { pos += 1 }
    while pos < bytes.count && bytes[pos] >= 48 && bytes[pos] <= 57 { pos += 1 }
    let s = String(bytes: bytes[start..<pos], encoding: .utf8)!
    let j = Json(.num)
    j.num = Int64(s)!
    return j
}

func parseValue() -> Json {
    ws()
    let c = bytes[pos]
    if c == 123 {
        pos += 1; ws()
        let j = Json(.obj)
        while pos < bytes.count && bytes[pos] != 125 {
            let k = parseString()
            ws()
            if bytes[pos] == 58 { pos += 1 }
            let v = parseValue()
            j.keys.append(k); j.vals.append(v)
            ws()
            if bytes[pos] == 44 { pos += 1; ws() }
        }
        if pos < bytes.count { pos += 1 }
        return j
    }
    if c == 91 {
        pos += 1; ws()
        let j = Json(.arr)
        while pos < bytes.count && bytes[pos] != 93 {
            j.items.append(parseValue())
            ws()
            if bytes[pos] == 44 { pos += 1; ws() }
        }
        if pos < bytes.count { pos += 1 }
        return j
    }
    if c == 34 { let j = Json(.str); j.str = parseString(); return j }
    if c == 116 { pos += 4; let j = Json(.bool); j.num = 1; return j }
    if c == 102 { pos += 5; return Json(.bool) }
    if c == 110 { pos += 4; return Json(.null) }
    return parseNumber()
}

func get(_ j: Json, _ key: String) -> Json {
    for i in 0..<j.keys.count where j.keys[i] == key { return j.vals[i] }
    return Json(.null)
}

let root = parseValue()
var acc: Int64 = 0
for o in root.items {
    acc += get(o, "id").num + get(o, "cat").num + get(o, "amt").num
        + Int64(get(o, "name").str.utf8.count)
}
print(acc)
