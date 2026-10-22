// Windowed group-by, expert Go: count first, exact-capacity slices, replay.
package main
import "fmt"
func main() {
    const nw, w, g = 40, 250000, 8000
    seed := int64(1)
    checksum := 0
    for win := 0; win < nw; win++ {
        seed0 := seed
        counts := make(map[int64]int, g)
        for e := 0; e < w; e++ {
            seed = (seed*1103515245 + 12345) % 2147483647
            counts[seed%g]++
        }
        m := make(map[int64][]int64, g)
        for k, c := range counts { m[k] = make([]int64, 0, c) }
        seed = seed0
        for e := 0; e < w; e++ {
            seed = (seed*1103515245 + 12345) % 2147483647
            gid := seed % g
            v := (seed / g) % 100
            m[gid] = append(m[gid], v)
        }
        for _, lst := range m {
            checksum += len(lst)
            for _, v := range lst { checksum += int(v) }
        }
    }
    fmt.Printf("windows=%d checksum=%d\n", nw, checksum)
}
