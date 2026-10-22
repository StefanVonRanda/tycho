public class gcscan {
    public static void main(String[] args) {
        int m = 2_000_000;
        String[] live = new String[m];
        for (int i = 0; i < m; i++) live[i] = "x" + (i % 1000);
        long acc = 0;
        for (int r = 0; r < 300; r++) {
            int[] xs = new int[50_000];
            for (int j = 0; j < xs.length; j++) xs[j] = (r + j) % 997;
            long s = 0;
            for (int v : xs) s += v;
            acc = (acc + s) % 1_000_000_007L;
        }
        long lacc = 0;
        for (String s : live) lacc += s.length();
        System.out.println(acc + " " + lacc);
    }
}
