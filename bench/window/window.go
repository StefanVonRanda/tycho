// Sliding-window, Go: a ring of strings; overwriting a slot makes the evicted
// string garbage (GC reclaims it), so peak RSS tracks the window + GC headroom.
package main
import ("fmt"; "strconv")
func main() {
    const n, w = 2000000, 50000
    ring := make([]string, w)
    acc := 0
    for i := 0; i < n; i++ {
        ring[i%w] = "rec" + strconv.Itoa(i%100000)
        acc += len(ring[i%w])
    }
    fmt.Println(acc)
}
