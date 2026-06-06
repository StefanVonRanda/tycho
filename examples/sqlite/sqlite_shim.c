/* A thin C shim adapting SQLite's out-param / callback API into hier-friendly
 * signatures (handles as ptr, results by return value). This is the `*_shim.c`
 * companion-file pattern (FFI Stage 3, `--shim`): hier's FFI can't express
 * `sqlite3**` out-params, so the shim returns the handle instead.
 *
 * Build:  hierc demo.hi -o demo --shim sqlite_shim.c --link sqlite3
 * (or --pkg sqlite3 instead of --link). */
#include <sqlite3.h>
#include <stddef.h>

void *sx_open(void) {                              /* open an in-memory DB; NULL on failure */
    sqlite3 *db = NULL;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) { sqlite3_close(db); return NULL; }
    return db;
}

long sx_exec(void *db, const char *sql) {          /* run a result-less statement; 0 = OK */
    return sqlite3_exec((sqlite3 *)db, sql, NULL, NULL, NULL);
}

void *sx_prepare(void *db, const char *sql) {      /* compile a query -> stmt handle, or NULL */
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2((sqlite3 *)db, sql, -1, &st, NULL) != SQLITE_OK) return NULL;
    return st;
}

long sx_step(void *stmt) {                          /* 1 = a row is ready, 0 = done, -1 = error */
    int rc = sqlite3_step((sqlite3_stmt *)stmt);
    if (rc == SQLITE_ROW)  return 1;
    if (rc == SQLITE_DONE) return 0;
    return -1;
}

long sx_col_int(void *stmt, long i)  { return sqlite3_column_int((sqlite3_stmt *)stmt, (int)i); }

const char *sx_col_text(void *stmt, long i) {       /* the pointer is valid only until the next
                                                     * step/finalize — hier arena-copies it on
                                                     * return, so the copy outlives the cursor. */
    const unsigned char *t = sqlite3_column_text((sqlite3_stmt *)stmt, (int)i);
    return t ? (const char *)t : "";
}

void sx_finalize(void *stmt) { sqlite3_finalize((sqlite3_stmt *)stmt); }
void sx_close(void *db)      { sqlite3_close((sqlite3 *)db); }
