public class latency {
    public static void main(String[] args) {
        int iters = 2000, k = 100000;
        long acc = 0;
        for (int it = 0; it < iters; it++) {
            long[] xs = new long[k];
            for (int j = 0; j < k; j++) xs[j] = (it + j) % 997;
            long s = 0;
            for (int m = 0; m < k; m++) s += xs[m];
            acc = (acc + s) % 1000000007L;
        }
        System.out.println(acc);
    }
}
