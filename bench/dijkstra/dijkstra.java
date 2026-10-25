// dijkstra, Java: deterministic graph + binary min-heap with lazy deletion.
public class dijkstra {
    static int n = 300000, deg = 4;
    static final long INF = 4000000000000000000L;
    static final class Edge { int to; long w; Edge(int t, long w) { to=t; this.w=w; } }
    static final class H { long d; int node; H(long d,int n){this.d=d;node=n;} }

    public static void main(String[] args) {
        long state = 88172645463325252L;
        Edge[][] adj = new Edge[n][deg];
        for (int u = 0; u < n; u++) for (int k = 0; k < deg; k++) {
            state = state * 6364136223846793005L + 1442695040888963407L;
            int to = (int)((state & 1073741823L) % n);
            state = state * 6364136223846793005L + 1442695040888963407L;
            long w = (state & 1073741823L) % 100 + 1;
            adj[u][k] = new Edge(to, w);
        }
        long[] dist = new long[n];
        java.util.Arrays.fill(dist, INF);
        dist[0] = 0;
        H[] heap = new H[1024];
        int hn = 0;
        heap[hn++] = new H(0, 0);
        while (hn > 0) {
            H c = heap[0];
            heap[0] = heap[--hn];
            int i = 0;
            while (true) {
                int l = 2*i+1, r = 2*i+2, s = i;
                if (l < hn && heap[l].d < heap[s].d) s = l;
                if (r < hn && heap[r].d < heap[s].d) s = r;
                if (s == i) break;
                H t = heap[i]; heap[i] = heap[s]; heap[s] = t; i = s;
            }
            if (c.d > dist[c.node]) continue;
            for (Edge e : adj[c.node]) {
                long nd = c.d + e.w;
                if (nd < dist[e.to]) {
                    dist[e.to] = nd;
                    if (hn == heap.length) heap = java.util.Arrays.copyOf(heap, hn * 2);
                    int j = hn++;
                    heap[j] = new H(nd, e.to);
                    while (j > 0) {
                        int p = (j-1)/2;
                        if (heap[p].d <= heap[j].d) break;
                        H t = heap[p]; heap[p] = heap[j]; heap[j] = t; j = p;
                    }
                }
            }
        }
        long sum = 0; int reach = 0;
        for (int i = 0; i < n; i++) if (dist[i] < INF) { sum += dist[i]; reach++; }
        System.out.println(reach + " " + sum);
    }
}
