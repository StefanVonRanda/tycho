// Latency angle, Go: the working set is garbage each round; the GC must collect it.
// Reports its own GC count + total pause time (stderr) — the pause cost hier/C don't pay.
package main
import ("fmt"; "os"; "runtime")
func main() {
    const iters, k = 2000, 100000
    acc := 0
    for it := 0; it < iters; it++ {
        xs := make([]int, 0)
        for j := 0; j < k; j++ { xs = append(xs, (it+j)%997) }
        s := 0
        for _, v := range xs { s += v }
        acc = (acc + s) % 1000000007
    }
    var ms runtime.MemStats
    runtime.ReadMemStats(&ms)
    fmt.Fprintf(os.Stderr, "gc=%d pause_us=%d\n", ms.NumGC, ms.PauseTotalNs/1000)
    fmt.Println(acc)
}
