package main

import "fmt"

func main() {
	var n, m, k int64 = 100000, 200, 1000003
	xs := make([]int64, n)
	for i := int64(0); i < n; i++ {
		xs[i] = i
	}
	var checksum int64 = 0
	for j := int64(0); j < m; j++ {
		ys := make([]int64, n)
		for i := int64(0); i < n; i++ {
			t := xs[i]*1103515245 + 12345 + j
			ys[i] = t % k
		}
		var s int64 = 0
		for i := int64(0); i < n; i++ {
			s += ys[i]
		}
		checksum = (checksum + s) % k
	}
	fmt.Println(checksum)
}
