// Dijkstra head-to-head, Go port. Same LCG graph + checksum as the tycho/C ports.
// Adjacency as [][]Edge; a hand-built binary min-heap with lazy deletion (the same
// shape as the other ports, not container/heap, to keep the comparison about the data).
package main

import "fmt"

type Edge struct {
	to int
	w  int64
}
type HItem struct {
	d    int64
	node int
}

func main() {
	n, deg := 300000, 4
	var state uint64 = 88172645463325252
	adj := make([][]Edge, n)
	for u := 0; u < n; u++ {
		adj[u] = make([]Edge, deg)
		for k := 0; k < deg; k++ {
			state = state*6364136223846793005 + 1442695040888963407
			to := int((state & 1073741823) % uint64(n))
			state = state*6364136223846793005 + 1442695040888963407
			w := int64((state&1073741823)%100) + 1
			adj[u][k] = Edge{to, w}
		}
	}
	const INF int64 = 4000000000000000000
	dist := make([]int64, n)
	for i := range dist {
		dist[i] = INF
	}
	dist[0] = 0
	heap := make([]HItem, 0, 1024)
	heap = append(heap, HItem{0, 0})
	hpush := func(d int64, node int) {
		heap = append(heap, HItem{d, node})
		i := len(heap) - 1
		for i > 0 {
			p := (i - 1) / 2
			if heap[p].d <= heap[i].d {
				break
			}
			heap[p], heap[i] = heap[i], heap[p]
			i = p
		}
	}
	hpop := func() HItem {
		top := heap[0]
		heap[0] = heap[len(heap)-1]
		heap = heap[:len(heap)-1]
		i := 0
		for {
			l, r, s := 2*i+1, 2*i+2, i
			if l < len(heap) && heap[l].d < heap[s].d {
				s = l
			}
			if r < len(heap) && heap[r].d < heap[s].d {
				s = r
			}
			if s == i {
				break
			}
			heap[i], heap[s] = heap[s], heap[i]
			i = s
		}
		return top
	}
	for len(heap) > 0 {
		c := hpop()
		d, u := c.d, c.node
		if d > dist[u] {
			continue
		}
		for _, e := range adj[u] {
			nd := d + e.w
			if nd < dist[e.to] {
				dist[e.to] = nd
				hpush(nd, e.to)
			}
		}
	}
	var sum, reach int64
	for i := 0; i < n; i++ {
		if dist[i] < INF {
			sum += dist[i]
			reach++
		}
	}
	fmt.Println(reach, sum)
}
