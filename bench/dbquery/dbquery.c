/* dbquery — C port. Same SQLite work + same checksum as dbquery.ty, written the
 * way a C programmer would: the column_text pointer is used in place (no copy,
 * since it's only read within the row), and the per-round cat grouping is a
 * small fixed array (cat is 0..31). This is the manual-memory baseline; the
 * checksum is order-independent so array-vs-map doesn't change the result.
 *
 * Build: cc -O2 dbquery.c -o dbq_c $(pkg-config --libs sqlite3) */
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    sqlite3 *db = NULL;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) { fprintf(stderr, "open\n"); return 1; }
    sqlite3_exec(db, "CREATE TABLE events(id INTEGER, cat INTEGER, amt INTEGER, label TEXT)", 0, 0, 0);

    long n = 60000;
    sqlite3_exec(db, "BEGIN", 0, 0, 0);
    sqlite3_stmt *ins = NULL;
    sqlite3_prepare_v2(db, "INSERT INTO events VALUES (?,?,?,?)", -1, &ins, NULL);
    for (long i = 0; i < n; i++) {
        char label[16];
        snprintf(label, sizeof label, "u%ld", i % 1000);
        sqlite3_bind_int64(ins, 1, i);
        sqlite3_bind_int64(ins, 2, i % 32);
        sqlite3_bind_int64(ins, 3, (i * 2654435761L) % 1000);
        sqlite3_bind_text(ins, 4, label, -1, SQLITE_TRANSIENT);
        sqlite3_step(ins);
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
    sqlite3_exec(db, "COMMIT", 0, 0, 0);

    long acc = 0, qn = 240;
    for (long q = 0; q < qn; q++) {
        char sql[96];
        snprintf(sql, sizeof sql, "SELECT cat, amt, label FROM events WHERE (id %% 240) = %ld", q);
        sqlite3_stmt *st = NULL;
        sqlite3_prepare_v2(db, sql, -1, &st, NULL);
        long m_sum[32] = {0}, m_cnt[32] = {0}, lbllen = 0;
        while (sqlite3_step(st) == SQLITE_ROW) {
            long cat = sqlite3_column_int64(st, 0);
            long amt = sqlite3_column_int64(st, 1);
            const unsigned char *label = sqlite3_column_text(st, 2);
            lbllen += (long)strlen((const char *)label);
            m_sum[cat] += amt;
            m_cnt[cat] += 1;
        }
        sqlite3_finalize(st);
        long chk = lbllen;
        for (long c = 0; c < 32; c++)
            if (m_cnt[c]) chk += (c + 1) * m_sum[c] + m_cnt[c];
        acc += chk;
    }
    sqlite3_close(db);
    printf("%ld\n", acc);
    return 0;
}
