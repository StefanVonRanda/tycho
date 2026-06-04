// Windowed group-by aggregation — Go, map[int][]int per window, discarded each cycle
// (the GC reclaims it). Same LCG and checksum as winagg.hi (oracle: windows=40
// checksum=505098931).
package main

import "fmt"

func main() {
	const nw, w, g = 40, 250000, 8000
	seed := int64(1)
	checksum := 0
	for win := 0; win < nw; win++ {
		m := make(map[int64][]int64, g) // fresh per-window map (becomes garbage at loop end)
		for e := 0; e < w; e++ {
			seed = (seed*1103515245 + 12345) % 2147483647
			gid := seed % g
			v := (seed / g) % 100
			m[gid] = append(m[gid], v)
		}
		for _, lst := range m {
			checksum += len(lst)
			for _, v := range lst {
				checksum += int(v)
			}
		}
		// m drops out of scope; the GC reclaims the window
	}
	fmt.Printf("windows=%d checksum=%d\n", nw, checksum)
}
