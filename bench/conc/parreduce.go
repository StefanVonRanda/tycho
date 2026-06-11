// Compute-bound parallel reduction -- Go, one goroutine per core, partials over a channel.
package main

import (
	"fmt"
	"runtime"
)

const n = 400000000

func main() {
	k := runtime.NumCPU()
	if k > 64 {
		k = 64
	}
	parts := make(chan int64, k)
	for c := 0; c < k; c++ {
		lo, hi := int64(n)*int64(c)/int64(k), int64(n)*int64(c+1)/int64(k)
		go func(lo, hi int64) {
			var acc int64
			for i := lo; i < hi; i++ {
				acc += (i*31 + 7) % 1000003
			}
			parts <- acc
		}(lo, hi)
	}
	var total int64
	for c := 0; c < k; c++ {
		total += <-parts
	}
	fmt.Print(total)
}
