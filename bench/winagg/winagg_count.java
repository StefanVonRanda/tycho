import java.util.*;

public class winagg_count {
    static final class List {
        long[] a; int n;
        List(int cap) { a = new long[cap]; }
        void add(long v) { a[n++] = v; }
    }
    public static void main(String[] args) {
        int nw = 40, w = 250000, g = 8000;
        long seed = 1, checksum = 0;
        for (int win = 0; win < nw; win++) {
            long seed0 = seed;
            HashMap<Long,Integer> counts = new HashMap<>(g * 2);
            for (int e = 0; e < w; e++) {
                seed = (seed * 1103515245L + 12345) % 2147483647L;
                long gid = seed % g;
                counts.merge(gid, 1, Integer::sum);
            }
            HashMap<Long,List> m = new HashMap<>(g * 2);
            for (Map.Entry<Long,Integer> e : counts.entrySet())
                m.put(e.getKey(), new List(e.getValue()));
            seed = seed0;
            for (int e = 0; e < w; e++) {
                seed = (seed * 1103515245L + 12345) % 2147483647L;
                long gid = seed % g;
                long v = (seed / g) % 100;
                m.get(gid).add(v);
            }
            for (List lst : m.values()) {
                checksum += lst.n;
                for (int i = 0; i < lst.n; i++) checksum += lst.a[i];
            }
        }
        System.out.println("windows=" + nw + " checksum=" + checksum);
    }
}
