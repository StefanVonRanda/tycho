// GC-scan-cost, Go: the 2M live strings are pointers the GC re-scans every cycle.
package main
import ("fmt"; "os"; "runtime"; "strconv")
func main() {
    const m = 2000000
    live := make([]string, m)
    for i := 0; i < m; i++ { live[i] = "x" + strconv.Itoa(i%1000) }
    acc := 0
    for r := 0; r < 300; r++ {
        xs := make([]int, 0)
        for j := 0; j < 50000; j++ { xs = append(xs, (r+j)%997) }
        s := 0; for _, v := range xs { s += v }
        acc = (acc + s) % 1000000007
    }
    lacc := 0; for i := 0; i < m; i++ { lacc += len(live[i]) }
    var ms runtime.MemStats; runtime.ReadMemStats(&ms)
    fmt.Fprintf(os.Stderr, "gc=%d pause_us=%d\n", ms.NumGC, ms.PauseTotalNs/1000)
    fmt.Println(acc, lacc)
}
