/* SQLite shim for the dbquery head-to-head benchmark. Same return-the-handle
 * facade as examples/sqlite, plus bind/reset so the row insert can use a single
 * prepared statement (fast, and identical work across the C / Go / tycho ports). */
#include <sqlite3.h>
#include <stddef.h>

void *sx_open(void) {
    sqlite3 *db = NULL;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) { sqlite3_close(db); return NULL; }
    return db;
}
long  sx_exec(void *db, const char *sql) { return sqlite3_exec((sqlite3 *)db, sql, NULL, NULL, NULL); }
void *sx_prepare(void *db, const char *sql) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2((sqlite3 *)db, sql, -1, &st, NULL) != SQLITE_OK) return NULL;
    return st;
}
void  sx_bind_int(void *st, long i, long v)        { sqlite3_bind_int64((sqlite3_stmt *)st, (int)i, v); }
void  sx_bind_text(void *st, long i, const char *s){ sqlite3_bind_text((sqlite3_stmt *)st, (int)i, s, -1, SQLITE_TRANSIENT); }
long  sx_step(void *st) {
    int rc = sqlite3_step((sqlite3_stmt *)st);
    if (rc == SQLITE_ROW)  return 1;
    if (rc == SQLITE_DONE) return 0;
    return -1;
}
void  sx_reset(void *st)                  { sqlite3_reset((sqlite3_stmt *)st); }
long  sx_col_int(void *st, long i)        { return sqlite3_column_int64((sqlite3_stmt *)st, (int)i); }
const char *sx_col_text(void *st, long i) {
    const unsigned char *t = sqlite3_column_text((sqlite3_stmt *)st, (int)i);
    return t ? (const char *)t : "";
}
void  sx_finalize(void *st) { sqlite3_finalize((sqlite3_stmt *)st); }
void  sx_close(void *db)    { sqlite3_close((sqlite3 *)db); }
