public class window_int {
    public static void main(String[] args) {
        int n = 2_000_000, w = 50_000;
        int[] ring = new int[w];
        long acc = 0;
        for (int i = 0; i < n; i++) {
            int s = i % w;
            ring[s] = i % 100_000;
            acc += ring[s];
        }
        System.out.println(acc);
    }
}
