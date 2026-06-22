// Inverted-index build, map-native form — Go, map[string][]int with append.
// Same LCG corpus and checksum as invindex_map.ty/.c (occurrence list, no dedup):
// the natural Go idiom, idx[term] = append(idx[term], d).
package main

import (
	"fmt"
	"strconv"
)

func main() {
	const n, w, v = 300000, 12, 8000
	idx := make(map[string][]int, v)
	seed := int64(1)
	for d := 0; d < n; d++ {
		for j := 0; j < w; j++ {
			seed = (seed*1103515245 + 12345) % 2147483647
			tid := seed % v
			term := "t" + strconv.FormatInt(tid, 10)
			idx[term] = append(idx[term], d)
		}
	}
	sum := 0
	for term, docs := range idx {
		sum += len(term) + len(docs)
		for _, dd := range docs {
			sum += dd
		}
	}
	fmt.Printf("vocab=%d checksum=%d\n", len(idx), sum)
}
