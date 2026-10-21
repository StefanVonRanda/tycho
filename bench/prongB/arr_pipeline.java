public class arr_pipeline {
    public static void main(String[] args) {
        int n = 100000, m = 200;
        long k = 1000003;
        long[] xs = new long[n];
        for (int i = 0; i < n; i++) xs[i] = i;
        long checksum = 0;
        for (int j = 0; j < m; j++) {
            long[] ys = new long[n];
            for (int i = 0; i < n; i++) {
                long t = xs[i] * 1103515245L + 12345 + j;
                ys[i] = t % k;
            }
            long s = 0;
            for (int i = 0; i < n; i++) s += ys[i];
            checksum = (checksum + s) % k;
        }
        System.out.println(checksum);
    }
}
