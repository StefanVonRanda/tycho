public class string_pipe {
    public static void main(String[] args) {
        int m = 4000, kk = 256;
        long p = 1000003;
        long checksum = 0;
        for (int j = 0; j < m; j++) {
            StringBuilder s = new StringBuilder(kk);
            for (int i = 0; i < kk; i++)
                s.append((char) ('0' + ((i + j) % 10)));
            long h = 0;
            for (int i = 0; i < s.length(); i++) h += s.charAt(i);
            checksum = (checksum + h) % p;
        }
        System.out.println(checksum);
    }
}
