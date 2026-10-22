public class maptree {
    static final class Tree {
        boolean leaf;
        int n;
        Tree l, r;
        Tree(int n) { leaf = true; this.n = n; }
        Tree(Tree l, Tree r) { this.l = l; this.r = r; }
    }
    static Tree build(int d) {
        return d == 0 ? new Tree(1) : new Tree(build(d - 1), build(d - 1));
    }
    static Tree map(Tree t) {
        return t.leaf ? new Tree(t.n + 1) : new Tree(map(t.l), map(t.r));
    }
    static long checksum(Tree t) {
        return t.leaf ? t.n : checksum(t.l) + checksum(t.r);
    }
    public static void main(String[] args) {
        int depth = 16;
        Tree t = build(depth);
        long sum = 0;
        for (int i = 0; i < 200; i++) sum += checksum(map(t));
        System.out.println(sum);
    }
}
