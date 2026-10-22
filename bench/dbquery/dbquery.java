import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;
import java.util.*;

public class dbquery {
    static final Linker LINKER = Linker.nativeLinker();
    static final Arena ARENA = Arena.ofConfined();
    static final SymbolLookup SQLITE;

    static {
        System.loadLibrary("sqlite3");
        SQLITE = SymbolLookup.loaderLookup();
    }

    static MethodHandle h(String name, FunctionDescriptor d) {
        return LINKER.downcallHandle(SQLITE.find(name).orElseThrow(), d);
    }

    static final MethodHandle OPEN = h("sqlite3_open",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
    static final MethodHandle EXEC = h("sqlite3_exec",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle PREPARE = h("sqlite3_prepare_v2",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.ADDRESS));
    static final MethodHandle BIND_INT = h("sqlite3_bind_int64",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG));
    static final MethodHandle BIND_TEXT = h("sqlite3_bind_text",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                    ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS));
    static final MethodHandle STEP = h("sqlite3_step",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    static final MethodHandle RESET = h("sqlite3_reset",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    static final MethodHandle COL_INT = h("sqlite3_column_int64",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    static final MethodHandle COL_TEXT = h("sqlite3_column_text",
            FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    static final MethodHandle FINALIZE = h("sqlite3_finalize",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
    static final MethodHandle CLOSE = h("sqlite3_close",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));

    static MemorySegment cstr(String s) { return ARENA.allocateFrom(s); }

    static void exec(MemorySegment db, String sql) {
        try { EXEC.invoke(db, cstr(sql), MemorySegment.NULL, MemorySegment.NULL, MemorySegment.NULL); }
        catch (Throwable t) { throw new RuntimeException(t); }
    }

    static MemorySegment prepare(MemorySegment db, String sql) {
        try {
            MemorySegment st = ARENA.allocate(ValueLayout.ADDRESS);
            PREPARE.invoke(db, cstr(sql), -1, st, MemorySegment.NULL);
            return st.get(ValueLayout.ADDRESS, 0);
        } catch (Throwable t) { throw new RuntimeException(t); }
    }

    public static void main(String[] args) {
        try {
            MemorySegment dbOut = ARENA.allocate(ValueLayout.ADDRESS);
            OPEN.invoke(cstr(":memory:"), dbOut);
            MemorySegment db = dbOut.get(ValueLayout.ADDRESS, 0);
            exec(db, "CREATE TABLE events(id INTEGER, cat INTEGER, amt INTEGER, label TEXT)");

            long n = 60000;
            exec(db, "BEGIN");
            MemorySegment ins = prepare(db, "INSERT INTO events VALUES (?,?,?,?)");
            for (long i = 0; i < n; i++) {
                BIND_INT.invoke(ins, 1, i);
                BIND_INT.invoke(ins, 2, i % 32);
                BIND_INT.invoke(ins, 3, (i * 2654435761L) % 1000);
                BIND_TEXT.invoke(ins, 4, cstr("u" + (i % 1000)), -1,
                        MemorySegment.ofAddress(-1));
                STEP.invoke(ins);
                RESET.invoke(ins);
            }
            FINALIZE.invoke(ins);
            exec(db, "COMMIT");

            long acc = 0, qn = 240;
            for (long q = 0; q < qn; q++) {
                MemorySegment st = prepare(db, "SELECT cat, amt, label FROM events WHERE (id % 240) = " + q);
                long[] msum = new long[32], mcnt = new long[32];
                long lbllen = 0;
                while ((int) STEP.invoke(st) == 100) {
                    int cat = (int) (long) COL_INT.invoke(st, 0);
                    long amt = (long) COL_INT.invoke(st, 1);
                    MemorySegment p = (MemorySegment) COL_TEXT.invoke(st, 2);
                    if (p.address() != 0)
                        lbllen += MemorySegment.ofAddress(p.address())
                                .reinterpret(Long.MAX_VALUE).getString(0).length();
                    msum[cat] += amt;
                    mcnt[cat]++;
                }
                FINALIZE.invoke(st);
                long chk = lbllen;
                for (int c = 0; c < 32; c++) if (mcnt[c] != 0) chk += (c + 1) * msum[c] + mcnt[c];
                acc += chk;
            }
            CLOSE.invoke(db);
            System.out.println(acc);
        } catch (Throwable t) {
            t.printStackTrace();
            System.exit(1);
        }
    }
}
