// lru, Java: fixed-capacity LRU over the shared LCG operation stream.
import java.util.HashMap;

public class lru {
    static final int CAP = 200000, KEYSPACE = 600000;
    static int nops = 5000000;
    static int[] key = new int[CAP], val = new int[CAP], prev = new int[CAP], next = new int[CAP];
    static int head = -1, tail = -1, size = 0;
    static HashMap<Integer,Integer> idx = new HashMap<>(CAP * 2);

    static void unlink(int i) {
        int p = prev[i], nx = next[i];
        if (p == -1) head = nx; else next[p] = nx;
        if (nx == -1) tail = p; else prev[nx] = p;
    }
    static void front(int i) {
        prev[i] = -1; next[i] = head;
        if (head == -1) tail = i; else prev[head] = i;
        head = i;
    }
    static void touch(int i) { unlink(i); front(i); }
    static int get(int k, int[] out) {
        Integer i = idx.get(k);
        if (i == null) return 0;
        touch(i); out[0] = val[i]; return 1;
    }
    static void put(int k, int v) {
        Integer i = idx.get(k);
        if (i != null) { val[i] = v; touch(i); return; }
        if (size >= CAP) {
            int t = tail; unlink(t); idx.remove(key[t]);
            key[t] = k; val[t] = v; idx.put(k, t); front(t); return;
        }
        int j = size++;
        key[j] = k; val[j] = v; prev[j] = -1; next[j] = -1;
        idx.put(k, j); front(j);
    }
    public static void main(String[] args) {
        long state = 88172645463325252L;
        long hits = 0, sum = 0; int[] out = new int[1];
        for (int op = 0; op < nops; op++) {
            state = state * 6364136223846793005L + 1442695040888963407L;
            int k = (int)((state & 1073741823L) % KEYSPACE);
            state = state * 6364136223846793005L + 1442695040888963407L;
            int r = (int)(state & 1073741823L);
            if (r % 100 < 70) { int h = get(k, out); if (h != 0) { hits += h; sum += out[0]; } }
            else {
                state = state * 6364136223846793005L + 1442695040888963407L;
                put(k, (int)((state & 1073741823L) % 1000000));
            }
        }
        System.out.println(hits + " " + sum);
    }
}
