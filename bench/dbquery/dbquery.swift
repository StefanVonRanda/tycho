import Foundation

@_silgen_name("sqlite3_open")
func sqlite3_open(_ path: UnsafePointer<CChar>?, _ db: UnsafeMutablePointer<OpaquePointer?>?) -> Int32
@_silgen_name("sqlite3_exec")
func sqlite3_exec(_ db: OpaquePointer?, _ sql: UnsafePointer<CChar>?, _ cb: UnsafeMutableRawPointer?, _ arg: UnsafeMutableRawPointer?, _ err: UnsafeMutableRawPointer?) -> Int32
@_silgen_name("sqlite3_prepare_v2")
func sqlite3_prepare_v2(_ db: OpaquePointer?, _ sql: UnsafePointer<CChar>?, _ n: Int32, _ st: UnsafeMutablePointer<OpaquePointer?>?, _ tail: UnsafeMutablePointer<UnsafePointer<CChar>?>?) -> Int32
@_silgen_name("sqlite3_bind_int64")
func sqlite3_bind_int64(_ st: OpaquePointer?, _ i: Int32, _ v: Int64) -> Int32
@_silgen_name("sqlite3_bind_text")
func sqlite3_bind_text(_ st: OpaquePointer?, _ i: Int32, _ s: UnsafePointer<CChar>?, _ n: Int32, _ dtor: UnsafeMutableRawPointer?) -> Int32
@_silgen_name("sqlite3_step")
func sqlite3_step(_ st: OpaquePointer?) -> Int32
@_silgen_name("sqlite3_reset")
func sqlite3_reset(_ st: OpaquePointer?) -> Int32
@_silgen_name("sqlite3_column_int64")
func sqlite3_column_int64(_ st: OpaquePointer?, _ i: Int32) -> Int64
@_silgen_name("sqlite3_column_text")
func sqlite3_column_text(_ st: OpaquePointer?, _ i: Int32) -> UnsafePointer<UInt8>?
@_silgen_name("sqlite3_finalize")
func sqlite3_finalize(_ st: OpaquePointer?) -> Int32
@_silgen_name("sqlite3_close")
func sqlite3_close(_ db: OpaquePointer?) -> Int32

let transient = unsafeBitCast(Int(-1), to: UnsafeMutableRawPointer.self)

func exec(_ db: OpaquePointer?, _ sql: String) {
    sql.withCString { sqlite3_exec(db, $0, nil, nil, nil) }
}
func prepare(_ db: OpaquePointer?, _ sql: String) -> OpaquePointer? {
    var st: OpaquePointer?
    sql.withCString { sqlite3_prepare_v2(db, $0, -1, &st, nil) }
    return st
}

var db: OpaquePointer?
if sqlite3_open(":memory:", &db) != 0 { fatalError("dbquery: open failed") }
exec(db, "CREATE TABLE events(id INTEGER, cat INTEGER, amt INTEGER, label TEXT)")
let n: Int64 = 60000
exec(db, "BEGIN")
let ins = prepare(db, "INSERT INTO events VALUES (?,?,?,?)")
for i in 0..<n {
    sqlite3_bind_int64(ins, 1, i)
    sqlite3_bind_int64(ins, 2, i % 32)
    sqlite3_bind_int64(ins, 3, (i * 2654435761) % 1000)
    let label = "u\(i % 1000)"
    label.withCString { sqlite3_bind_text(ins, 4, $0, -1, transient) }
    sqlite3_step(ins)
    sqlite3_reset(ins)
}
sqlite3_finalize(ins)
exec(db, "COMMIT")

var acc: Int64 = 0
let qn: Int64 = 240
for q in 0..<qn {
    let st = prepare(db, "SELECT cat, amt, label FROM events WHERE (id % 240) = \(q)")
    var msum = [Int64](repeating: 0, count: 32)
    var mcnt = [Int64](repeating: 0, count: 32)
    var lbllen: Int64 = 0
    while sqlite3_step(st) == 100 {
        let c = Int(sqlite3_column_int64(st, 0))
        let amt = sqlite3_column_int64(st, 1)
        if let p = sqlite3_column_text(st, 2) { lbllen += Int64(strlen(unsafeBitCast(p, to: UnsafePointer<CChar>.self))) }
        msum[c] += amt
        mcnt[c] += 1
    }
    sqlite3_finalize(st)
    var chk = lbllen
    for c in 0..<32 where mcnt[c] != 0 { chk += Int64(c + 1) * msum[c] + mcnt[c] }
    acc += chk
}
sqlite3_close(db)
print(acc)
