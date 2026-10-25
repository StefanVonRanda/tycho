// trie, Java: the same deterministic words into a map-backed prefix tree.
import java.util.HashMap;
import java.util.Map;

public class trie {
    static int n = 150000;
    static final class Node {
        Map<Byte, Node> kids = new HashMap<>();
        boolean word;
    }
    static long nodes = 1;

    static int insert(Node root, byte[] s, int len) {
        Node t = root;
        for (int i = 0; i < len; i++) {
            byte c = s[i];
            Node nx = t.kids.get(c);
            if (nx == null) {
                nx = new Node();
                t.kids.put(c, nx);
                nodes++;
            }
            t = nx;
        }
        if (t.word) return 0;
        t.word = true;
        return 1;
    }

    public static void main(String[] args) {
        long state = 88172645463325252L;
        Node root = new Node();
        long nwords = 0;
        byte[] buf = new byte[16];
        for (int w = 0; w < n; w++) {
            state = state * 6364136223846793005L + 1442695040888963407L;
            int wlen = 3 + (int)((state & 1073741823L) % 5);
            for (int j = 0; j < wlen; j++) {
                state = state * 6364136223846793005L + 1442695040888963407L;
                buf[j] = (byte)(97 + (int)((state & 1073741823L) % 26));
            }
            nwords += insert(root, buf, wlen);
        }
        System.out.println(nodes + " " + nwords);
    }
}
