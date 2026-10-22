import java.util.*;
public class invindex_exact {
    static final class Posting { String term; ArrayList<Long> docs, freqs; Posting(String t, int cap) { term=t; docs=new ArrayList<>(cap); freqs=new ArrayList<>(cap); } }
    public static void main(String[] args) {
        int n=300_000, w=12, v=8000;
        HashMap<String,Integer> idx=new HashMap<>(); ArrayList<String> terms=new ArrayList<>(); ArrayList<Integer> counts=new ArrayList<>();
        long seed=1;
        for (int d=0; d<n; d++) for (int j=0; j<w; j++) { seed=(seed*1103515245L+12345)%2147483647L; String term="t"+(seed%v); Integer slot=idx.get(term); if (slot==null) { idx.put(term,terms.size()); terms.add(term); counts.add(1); } else counts.set(slot,counts.get(slot)+1); }
        ArrayList<Posting> post=new ArrayList<>();
        for (int i=0;i<terms.size();i++) post.add(new Posting(terms.get(i),counts.get(i)));
        seed=1;
        for (int d=0; d<n; d++) for (int j=0; j<w; j++) { seed=(seed*1103515245L+12345)%2147483647L; Posting p=post.get(idx.get("t"+(seed%v))); int k=p.docs.size(); if (k>0 && p.docs.get(k-1)==d) p.freqs.set(k-1,p.freqs.get(k-1)+1); else { p.docs.add((long)d); p.freqs.add(1L); } }
        long sum=0; for (Posting p:post) { sum+=p.term.length()+p.docs.size(); for (long f:p.freqs) sum+=f; }
        long hits=0; Integer p0=idx.get("t0"), p1=idx.get("t1");
        if (p0!=null && p1!=null) for (long d:post.get(p0).docs) if (post.get(p1).docs.contains(d)) hits++;
        System.out.println("vocab="+post.size()+" checksum="+sum+" and01="+hits);
    }
}
