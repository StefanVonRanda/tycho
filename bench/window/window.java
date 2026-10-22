public class window {
    public static void main(String[] args) {
        int n = 2_000_000, w = 50_000;
        String[] ring = new String[w];
        long acc = 0;
        for (int i = 0; i < n; i++) {
            int s = i % w;
            ring[s] = "rec" + (i % 100_000);
            acc += ring[s].length();
        }
        System.out.println(acc);
    }
}
