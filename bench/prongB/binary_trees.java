public class binary_trees {
    static final class Tree {
        Tree l, r;
        Tree(Tree l, Tree r) { this.l = l; this.r = r; }
    }
    static Tree make(int d) {
        if (d == 0) return new Tree(null, null);
        return new Tree(make(d - 1), make(d - 1));
    }
    static long check(Tree t) {
        if (t.l == null) return 1;
        return 1 + check(t.l) + check(t.r);
    }
    public static void main(String[] args) {
        int mind = 4, maxd = 18, stretch = maxd + 1;
        System.out.println("stretch tree of depth " + stretch + " check: " + check(make(stretch)));
        Tree longlived = make(maxd);
        for (int d = mind; d <= maxd; d += 2) {
            long iters = 1L << (maxd - d + mind);
            long sum = 0;
            for (long i = 0; i < iters; i++) sum += check(make(d));
            System.out.println(iters + " trees of depth " + d + " check: " + sum);
        }
        System.out.println("long-lived tree of depth " + maxd + " check: " + check(longlived));
    }
}
