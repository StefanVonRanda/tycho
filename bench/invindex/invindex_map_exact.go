// Inverted-index build, map-native COUNT-FILL — Go. Same as invindex_map.go but each
// posting slice is make()'d at its exact capacity from a first counting pass, no append
// growth. The analogue of invindex_map_exact.hi's reserve(idx[term], count). Same LCG
// corpus and checksum (540001838890).
package main

import (
	"fmt"
	"strconv"
)

func main() {
	const n, w, v = 300000, 12, 8000
	cnt := make(map[string]int, v)
	seed := int64(1)
	for d := 0; d < n; d++ {
		for j := 0; j < w; j++ {
			seed = (seed*1103515245 + 12345) % 2147483647
			cnt["t"+strconv.FormatInt(seed%v, 10)]++
		}
	}
	idx := make(map[string][]int, len(cnt))
	for term, c := range cnt {
		idx[term] = make([]int, 0, c) // exact capacity, no growth
	}
	seed = 1
	for d := 0; d < n; d++ {
		for j := 0; j < w; j++ {
			seed = (seed*1103515245 + 12345) % 2147483647
			term := "t" + strconv.FormatInt(seed%v, 10)
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
