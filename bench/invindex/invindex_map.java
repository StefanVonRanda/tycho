import java.util.*;

public class invindex_map {
    public static void main(String[] args) {
        int n = 300_000, w = 12, v = 8000;
        HashMap<String, ArrayList<Long>> idx = new HashMap<>();
        long seed = 1;
        for (int d = 0; d < n; d++) for (int j = 0; j < w; j++) {
            seed = (seed * 1103515245L + 12345) % 2147483647L;
            long tid = seed % v;
            idx.computeIfAbsent("t" + tid, k -> new ArrayList<>()).add((long)d);
        }
        long sum = 0;
        for (Map.Entry<String, ArrayList<Long>> e : idx.entrySet()) {
            sum += e.getKey().length() + e.getValue().size();
            for (long d : e.getValue()) sum += d;
        }
        System.out.println("vocab=" + idx.size() + " checksum=" + sum);
    }
}
