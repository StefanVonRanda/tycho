// trie head-to-head, Go port. Same words + checksum as the tycho/C ports. Each node
// owns a map[byte]*Node (the idiomatic Go trie), all GC-tracked and held live, so peak
// RSS reflects the whole trie.
package main

import "fmt"

type Node struct {
	kids map[byte]*Node
	word bool
}

var nodes int64 = 1 // the root

func insert(root *Node, s []byte) int {
	t := root
	for _, c := range s {
		nx := t.kids[c]
		if nx == nil {
			nx = &Node{}
			if t.kids == nil {
				t.kids = make(map[byte]*Node)
			}
			t.kids[c] = nx
			nodes++
		}
		t = nx
	}
	if t.word {
		return 0
	}
	t.word = true
	return 1
}

func main() {
	n := 150000
	var state uint64 = 88172645463325252
	root := &Node{}
	var nwords int64
	buf := make([]byte, 16)
	for w := 0; w < n; w++ {
		state = state*6364136223846793005 + 1442695040888963407
		wlen := 3 + int(state&1073741823)%5
		for j := 0; j < wlen; j++ {
			state = state*6364136223846793005 + 1442695040888963407
			buf[j] = byte(97 + (state&1073741823)%26)
		}
		nwords += int64(insert(root, buf[:wlen]))
	}
	fmt.Println(nodes, nwords)
}
