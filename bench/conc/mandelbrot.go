// Compute-bound parallel reduction over FLOAT work -- Go, one goroutine per core,
// int64 partials merged after join. Mirrors mandelbrot.ty op-for-op. Go does not
// fuse float ops into FMA unless math.FMA is called explicitly, and the multiply
// is materialized into `m` anyway, so the chaotic escape counts agree bit-for-bit.
package main

import (
	"fmt"
	"runtime"
	"sync"
)

const (
	W     = 1200
	H     = 1200
	MAXIT = 2000
)

const (
	xmin = 0.0 - 2.5
	xmax = 1.0
	ymin = 0.0 - 1.25
	ymax = 1.25
)

func escape(cx, cy float64) int {
	zx, zy := 0.0, 0.0
	i := 0
	for ; i < MAXIT; i++ {
		x2 := zx * zx
		y2 := zy * zy
		if x2+y2 > 4.0 {
			return i
		}
		m := 2.0 * zx * zy
		zy = m + cy
		zx = x2 - y2 + cx
	}
	return MAXIT
}

func main() {
	k := runtime.NumCPU()
	if k < 1 {
		k = 1
	}
	totals := make([]int64, k)
	insets := make([]int64, k)
	var wg sync.WaitGroup
	for c := 0; c < k; c++ {
		lo := H * c / k
		hi := H * (c + 1) / k
		wg.Add(1)
		go func(idx, lo, hi int) {
			defer wg.Done()
			var total, inset int64
			for py := lo; py < hi; py++ {
				cy := ymin + (ymax-ymin)*float64(py)/float64(H)
				for px := 0; px < W; px++ {
					cx := xmin + (xmax-xmin)*float64(px)/float64(W)
					e := escape(cx, cy)
					total += int64(e)
					if e >= MAXIT {
						inset++
					}
				}
			}
			totals[idx] = total
			insets[idx] = inset
		}(c, lo, hi)
	}
	wg.Wait()
	var total, inset int64
	for c := 0; c < k; c++ {
		total += totals[c]
		inset += insets[c]
	}
	fmt.Printf("%d %d", total, inset)
}
