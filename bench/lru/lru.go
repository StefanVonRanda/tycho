// LRU cache head-to-head — Go reference. A fixed-capacity LRU driven by the same
// shared LCG op-stream as lru.ty / lru.c, folding hits + returned values into a
// checksum. Idiomatic Go: the builtin map[int]int (key -> node index) for lookup +
// a node slice with prev/next indices for the recency list, recycling the LRU tail
// slot in place on eviction. Same op stream + same LRU policy => byte-identical
// `hits sum` checksum across all three ports.
package main

import "fmt"

const (
	cap      = 200000
	keyspace = 600000
	nops     = 5000000
)

type node struct{ key, val, prev, next int }

type lru struct {
	pool             []node
	idx              map[int]int
	head, tail, size int
}

func newLRU() *lru {
	return &lru{pool: make([]node, 0, cap), idx: make(map[int]int, cap), head: -1, tail: -1}
}

func (c *lru) unlink(i int) {
	p, nx := c.pool[i].prev, c.pool[i].next
	if p == -1 {
		c.head = nx
	} else {
		c.pool[p].next = nx
	}
	if nx == -1 {
		c.tail = p
	} else {
		c.pool[nx].prev = p
	}
}

func (c *lru) pushFront(i int) {
	c.pool[i].prev = -1
	c.pool[i].next = c.head
	if c.head == -1 {
		c.tail = i
	} else {
		c.pool[c.head].prev = i
	}
	c.head = i
}

func (c *lru) touch(i int) { c.unlink(i); c.pushFront(i) }

func (c *lru) get(key int) (int, int) {
	if i, ok := c.idx[key]; ok {
		c.touch(i)
		return 1, c.pool[i].val
	}
	return 0, 0
}

func (c *lru) put(key, val int) {
	if i, ok := c.idx[key]; ok {
		c.pool[i].val = val
		c.touch(i)
		return
	}
	if c.size >= cap { // full: recycle the LRU tail slot in place
		t := c.tail
		c.unlink(t)
		delete(c.idx, c.pool[t].key)
		c.pool[t].key = key
		c.pool[t].val = val
		c.idx[key] = t
		c.pushFront(t)
		return
	}
	i := len(c.pool)
	c.pool = append(c.pool, node{key: key, val: val, prev: -1, next: -1})
	c.size++
	c.idx[key] = i
	c.pushFront(i)
}

func main() {
	c := newLRU()
	state := uint64(88172645463325252)
	hits, sum := 0, 0
	for op := 0; op < nops; op++ {
		state = state*6364136223846793005 + 1442695040888963407
		key := int(state&1073741823) % keyspace
		state = state*6364136223846793005 + 1442695040888963407
		r := int(state & 1073741823)
		if r%100 < 70 {
			h, v := c.get(key)
			hits += h
			sum += v
		} else {
			state = state*6364136223846793005 + 1442695040888963407
			val := int(state&1073741823) % 1000000
			c.put(key, val)
		}
	}
	fmt.Printf("%d %d\n", hits, sum)
}
