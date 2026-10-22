// Fixed-size sliding window, Go: slots are reused; no per-element heap.
package main
import "fmt"
func main() {
    const n, w = 2000000, 50000
    ring := make([]int, w)
    acc := 0
    for i := 0; i < n; i++ {
        s := i % w
        ring[s] = i % 100000
        acc += ring[s]
    }
    fmt.Println(acc)
}
