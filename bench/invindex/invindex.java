import java.util.*;

public class invindex {
    static final class Posting {
        String term;
        ArrayList<Long> docs = new ArrayList<>();
        ArrayList<Long> freqs = new ArrayList<>();
        Posting(String t) { term = t; }
    }
    static void add(ArrayList<Posting> post, HashMap<String,Integer> idx, String term, long doc) {
        Integer pi = idx.get(term);
        if (pi == null) {
            Posting p = new Posting(term);
            p.docs.add(doc); p.freqs.add(1L);
            idx.put(term, post.size()); post.add(p);
        } else {
            Posting p = post.get(pi);
            int n = p.docs.size();
            if (n > 0 && p.docs.get(n - 1) == doc) p.freqs.set(n - 1, p.freqs.get(n - 1) + 1);
            else { p.docs.add(doc); p.freqs.add(1L); }
        }
    }
    public static void main(String[] args) {
        int n = 300_000, w = 12, v = 8000;
        ArrayList<Posting> post = new ArrayList<>();
        HashMap<String,Integer> idx = new HashMap<>();
        long seed = 1;
        for (int d = 0; d < n; d++) for (int j = 0; j < w; j++) {
            seed = (seed * 1103515245L + 12345) % 2147483647L;
            long tid = seed % v;
            add(post, idx, "t" + tid, d);
        }
        long sum = 0;
        for (Posting p : post) {
            sum += p.term.length() + p.docs.size();
            for (long f : p.freqs) sum += f;
        }
        long hits = 0;
        Integer p0 = idx.get("t0"), p1 = idx.get("t1");
        if (p0 != null && p1 != null) {
            for (long doc : post.get(p0).docs) if (post.get(p1).docs.contains(doc)) hits++;
        }
        System.out.println("vocab=" + post.size() + " checksum=" + sum + " and01=" + hits);
    }
}
