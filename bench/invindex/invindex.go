// Inverted-index build, at scale — Go, GC + built-in map. Same LCG, same add
// logic, same checksum as invindex.ty (the cross-language oracle).
package main

import (
	"fmt"
	"strconv"
)

type Posting struct {
	term  string
	docs  []int64
	freqs []int64
}

func main() {
	var n, w, v int64 = 300000, 12, 8000
	post := []Posting{}
	tidx := map[string]int{} // term -> slot in post
	var seed int64 = 1
	for d := int64(0); d < n; d++ {
		for j := int64(0); j < w; j++ {
			seed = (seed*1103515245 + 12345) % 2147483647
			tid := seed % v
			term := "t" + strconv.FormatInt(tid, 10)
			if slot, ok := tidx[term]; !ok {
				post = append(post, Posting{term: term, docs: []int64{d}, freqs: []int64{1}})
				tidx[term] = len(post) - 1
			} else {
				p := &post[slot]
				nn := len(p.docs)
				if nn > 0 && p.docs[nn-1] == d {
					p.freqs[nn-1]++
				} else {
					p.docs = append(p.docs, d)
					p.freqs = append(p.freqs, 1)
				}
			}
		}
	}
	var sum int64 = 0
	for i := range post {
		sum += int64(len(post[i].term)) + int64(len(post[i].docs))
		for _, f := range post[i].freqs {
			sum += f
		}
	}
	var hits int64 = 0
	if s0, ok := tidx["t0"]; ok {
		p := &post[s0]
		s1, ok1 := tidx["t1"]
		for _, doc := range p.docs {
			okk := true
			if !ok1 {
				okk = false
			} else {
				q := &post[s1]
				found := false
				for _, dd := range q.docs {
					if dd == doc {
						found = true
						break
					}
				}
				if !found {
					okk = false
				}
			}
			if okk {
				hits++
			}
		}
	}
	fmt.Printf("vocab=%d checksum=%d and01=%d\n", len(post), sum, hits)
}
