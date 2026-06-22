// dbquery — Go port, via cgo against the system libsqlite3 (no external driver
// module needed). Same SQLite work + same checksum as the tycho/C ports; host
// grouping in Go (GC-managed). Build: go build -o dbq_go dbquery.go
package main

/*
#cgo pkg-config: sqlite3
#include <sqlite3.h>
#include <stdlib.h>
*/
import "C"
import (
	"fmt"
	"unsafe"
)

func cstr(s string) *C.char { return C.CString(s) } // caller frees

func exec(db *C.sqlite3, sql string) {
	cs := cstr(sql)
	C.sqlite3_exec(db, cs, nil, nil, nil)
	C.free(unsafe.Pointer(cs))
}

func prepare(db *C.sqlite3, sql string) *C.sqlite3_stmt {
	cs := cstr(sql)
	var st *C.sqlite3_stmt
	C.sqlite3_prepare_v2(db, cs, -1, &st, nil)
	C.free(unsafe.Pointer(cs))
	return st
}

func main() {
	var db *C.sqlite3
	mem := cstr(":memory:")
	C.sqlite3_open(mem, &db)
	C.free(unsafe.Pointer(mem))
	exec(db, "CREATE TABLE events(id INTEGER, cat INTEGER, amt INTEGER, label TEXT)")

	const n = 60000
	exec(db, "BEGIN")
	ins := prepare(db, "INSERT INTO events VALUES (?,?,?,?)")
	for i := int64(0); i < n; i++ {
		C.sqlite3_bind_int64(ins, 1, C.sqlite3_int64(i))
		C.sqlite3_bind_int64(ins, 2, C.sqlite3_int64(i%32))
		C.sqlite3_bind_int64(ins, 3, C.sqlite3_int64((i*2654435761)%1000))
		label := cstr(fmt.Sprintf("u%d", i%1000))
		C.sqlite3_bind_text(ins, 4, label, -1, C.SQLITE_TRANSIENT)
		C.sqlite3_step(ins)
		C.sqlite3_reset(ins)
		C.free(unsafe.Pointer(label))
	}
	C.sqlite3_finalize(ins)
	exec(db, "COMMIT")

	var acc int64
	const qn = 240
	for q := int64(0); q < qn; q++ {
		st := prepare(db, fmt.Sprintf("SELECT cat, amt, label FROM events WHERE (id %% 240) = %d", q))
		var mSum, mCnt [32]int64
		var lbllen int64
		for C.sqlite3_step(st) == C.SQLITE_ROW {
			cat := int64(C.sqlite3_column_int64(st, 0))
			amt := int64(C.sqlite3_column_int64(st, 1))
			label := C.GoString((*C.char)(unsafe.Pointer(C.sqlite3_column_text(st, 2)))) // copy into a Go (GC) string
			lbllen += int64(len(label))
			mSum[cat] += amt
			mCnt[cat]++
		}
		C.sqlite3_finalize(st)
		chk := lbllen
		for c := int64(0); c < 32; c++ {
			if mCnt[c] != 0 {
				chk += (c+1)*mSum[c] + mCnt[c]
			}
		}
		acc += chk
	}
	C.sqlite3_close(db)
	fmt.Println(acc)
}
