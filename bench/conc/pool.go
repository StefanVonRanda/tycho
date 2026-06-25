// Worker pool: 1 producer -> bounded chan(256) -> K = NumCPU goroutines, each
// running the same MINSTD kernel and accumulating a per-worker local, summed
// once at the end. The idiomatic Go pool: `for j := range jobs` workers behind
// a WaitGroup. Same kernel, same channel cap, same K-picks-cores rule as the
// tycho/C/Rust contenders. Checksum: sum of the kernel over 1e6 jobs.
package main

import (
	"fmt"
	"runtime"
	"sync"
)

func work(seed int64) int64 {
	x := (seed + 1) % 2147483647
	for k := 0; k < 50; k++ {
		x = (x * 48271) % 2147483647
	}
	return x
}

func main() {
	const N = 1000000
	jobs := make(chan int64, 256)
	K := runtime.NumCPU()
	partials := make([]int64, K)
	var wg sync.WaitGroup
	for w := 0; w < K; w++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			var local int64
			for j := range jobs {
				local += work(j)
			}
			partials[id] = local
		}(w)
	}
	for i := int64(0); i < N; i++ {
		jobs <- i
	}
	close(jobs)
	wg.Wait()
	var sum int64
	for _, p := range partials {
		sum += p
	}
	fmt.Print(sum)
}
