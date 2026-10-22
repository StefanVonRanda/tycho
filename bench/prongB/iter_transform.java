public class iter_transform {
    static long[] step(long[] a, long p) {
        long[] b = new long[a.length];
        for (int i = 0; i < a.length; i++) {
            long t = a[i] * 1103515245L + 12345;
            b[i] = t - (t / p) * p;
        }
        return b;
    }
    public static void main(String[] args) {
        int n = 100000, m = 2000;
        long p = 2147483647L;
        long[] a = new long[n];
        for (int i = 0; i < n; i++) a[i] = i;
        for (int j = 0; j < m; j++) a = step(a, p);
        long s = 0;
        for (long x : a) s += x;
        System.out.println(s - (s / 1000003L) * 1000003L);
    }
}
