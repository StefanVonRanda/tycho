import java.util.*;

public class winagg {
    static final class List {
        long[] a = new long[4];
        int n = 0;
        void add(long v) {
            if (n == a.length) a = Arrays.copyOf(a, n * 2);
            a[n++] = v;
        }
    }
    public static void main(String[] args) {
        int nw = 40, w = 250000, g = 8000;
        long seed = 1, checksum = 0;
        for (int win = 0; win < nw; win++) {
            HashMap<Long, List> m = new HashMap<>(g * 2);
            for (int e = 0; e < w; e++) {
                seed = (seed * 1103515245L + 12345) % 2147483647L;
                long gid = seed % g;
                long v = (seed / g) % 100;
                m.computeIfAbsent(gid, k -> new List()).add(v);
            }
            for (List lst : m.values()) {
                checksum += lst.n;
                for (int i = 0; i < lst.n; i++) checksum += lst.a[i];
            }
        }
        System.out.println("windows=" + nw + " checksum=" + checksum);
    }
}
