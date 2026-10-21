public class dispatch {
    interface F { long call(long x); }
    public static void main(String[] args) {
        int n = 2000, k = 2000;
        long total = 0;
        for (int p = 0; p < k; p++) {
            F[] fs = new F[n];
            for (int i = 0; i < n; i++) {
                final long ii = i;
                final long pp = p;
                fs[i] = x -> x * (ii + 1) + pp;
            }
            for (int i = 0; i < n; i++) total = (total + fs[i].call(7)) & 1048575;
        }
        System.out.println(total);
    }
}
