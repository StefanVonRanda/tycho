// dbquery -- Rust, direct libsqlite3 FFI. Same setup/query/checksum as the
// Tycho, C and Go ports; grouping is host-side and per-round tables are dropped.
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::ptr::null_mut;

#[repr(C)] struct sqlite3 { _p: [u8; 0] }
#[repr(C)] struct sqlite3_stmt { _p: [u8; 0] }

#[link(name = "sqlite3")]
extern "C" {
    fn sqlite3_open(path: *const c_char, db: *mut *mut sqlite3) -> c_int;
    fn sqlite3_exec(db: *mut sqlite3, sql: *const c_char,
                    cb: *mut c_void, arg: *mut c_void, err: *mut *mut c_char) -> c_int;
    fn sqlite3_prepare_v2(db: *mut sqlite3, sql: *const c_char, n: c_int,
                          st: *mut *mut sqlite3_stmt, tail: *mut *const c_char) -> c_int;
    fn sqlite3_bind_int64(st: *mut sqlite3_stmt, i: c_int, v: i64) -> c_int;
    fn sqlite3_bind_text(st: *mut sqlite3_stmt, i: c_int, s: *const c_char,
                         n: c_int, dtor: *mut c_void) -> c_int;
    fn sqlite3_step(st: *mut sqlite3_stmt) -> c_int;
    fn sqlite3_reset(st: *mut sqlite3_stmt) -> c_int;
    fn sqlite3_column_int64(st: *mut sqlite3_stmt, i: c_int) -> i64;
    fn sqlite3_column_text(st: *mut sqlite3_stmt, i: c_int) -> *const u8;
    fn sqlite3_finalize(st: *mut sqlite3_stmt) -> c_int;
    fn sqlite3_close(db: *mut sqlite3) -> c_int;
}

const SQLITE_TRANSIENT: *mut c_void = -1isize as *mut c_void;

unsafe fn exec(db: *mut sqlite3, sql: &str) {
    let c = CString::new(sql).unwrap();
    sqlite3_exec(db, c.as_ptr(), null_mut(), null_mut(), null_mut());
}

unsafe fn prepare(db: *mut sqlite3, sql: &str) -> *mut sqlite3_stmt {
    let c = CString::new(sql).unwrap();
    let mut st = null_mut();
    sqlite3_prepare_v2(db, c.as_ptr(), -1, &mut st, null_mut());
    st
}

fn main() {
    unsafe {
        let mut db = null_mut();
        let path = CString::new(":memory:").unwrap();
        if sqlite3_open(path.as_ptr(), &mut db) != 0 {
            eprintln!("dbquery: open failed");
            std::process::exit(1);
        }
        exec(db, "CREATE TABLE events(id INTEGER, cat INTEGER, amt INTEGER, label TEXT)");

        let n: i64 = 60000;
        exec(db, "BEGIN");
        let ins = prepare(db, "INSERT INTO events VALUES (?,?,?,?)");
        for i in 0..n {
            sqlite3_bind_int64(ins, 1, i);
            sqlite3_bind_int64(ins, 2, i % 32);
            sqlite3_bind_int64(ins, 3, (i * 2654435761) % 1000);
            let label = CString::new(format!("u{}", i % 1000)).unwrap();
            sqlite3_bind_text(ins, 4, label.as_ptr(), -1, SQLITE_TRANSIENT);
            sqlite3_step(ins);
            sqlite3_reset(ins);
        }
        sqlite3_finalize(ins);
        exec(db, "COMMIT");

        let mut acc: i64 = 0;
        let qn: i64 = 240;
        for q in 0..qn {
            let sql = format!("SELECT cat, amt, label FROM events WHERE (id % 240) = {}", q);
            let st = prepare(db, &sql);
            let mut msum = [0i64; 32];
            let mut mcnt = [0i64; 32];
            let mut lbllen: i64 = 0;
            while sqlite3_step(st) == 100 {
                let cat = sqlite3_column_int64(st, 0) as usize;
                let amt = sqlite3_column_int64(st, 1);
                let p = sqlite3_column_text(st, 2);
                if !p.is_null() {
                    lbllen += CStr::from_ptr(p as *const c_char).to_bytes().len() as i64;
                }
                msum[cat] += amt;
                mcnt[cat] += 1;
            }
            sqlite3_finalize(st);
            let mut chk = lbllen;
            for c in 0..32 {
                if mcnt[c] != 0 {
                    chk += (c as i64 + 1) * msum[c] + mcnt[c];
                }
            }
            acc += chk;
        }
        sqlite3_close(db);
        println!("{}", acc);
    }
}
