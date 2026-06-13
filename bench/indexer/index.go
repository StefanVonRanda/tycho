// Parallel text indexer -- Go rival for the hier concurrency dogfood.
//
//	index <corpus-dir>
//
// Same algorithm as index.hi: fan file paths through a buffered channel to K=4
// worker goroutines, each tallying a LOCAL map[string]int, then merge. Go
// shares the string bodies under its GC; hier deep-copies every worker map back
// across the thread boundary (value semantics). Prints the identical checksum.
package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
)

const K = 4

// tally one document body: a term is a maximal run of letters, 'A'..'Z' fold to
// lowercase, every other byte is a separator. Identical to index.hi / index.c.
func tally(text []byte, acc map[string]int) {
	var cur []byte
	for _, c := range text {
		if c >= 'A' && c <= 'Z' {
			cur = append(cur, c+32)
		} else if c >= 'a' && c <= 'z' {
			cur = append(cur, c)
		} else if len(cur) > 0 {
			acc[string(cur)]++
			cur = cur[:0]
		}
	}
	if len(cur) > 0 {
		acc[string(cur)]++
	}
}

func worker(ch <-chan string, out chan<- map[string]int) {
	local := make(map[string]int)
	for path := range ch {
		if data, err := os.ReadFile(path); err == nil {
			tally(data, local)
		}
	}
	out <- local
}

func main() {
	if len(os.Args) < 2 {
		fmt.Println("usage: index <corpus-dir>")
		return
	}
	dir := os.Args[1]
	entries, _ := os.ReadDir(dir)
	var paths []string
	for _, e := range entries {
		if strings.HasSuffix(e.Name(), ".txt") {
			paths = append(paths, filepath.Join(dir, e.Name()))
		}
	}

	ch := make(chan string, 256)
	out := make(chan map[string]int, K)
	var wg sync.WaitGroup
	for i := 0; i < K; i++ {
		wg.Add(1)
		go func() { defer wg.Done(); worker(ch, out) }()
	}
	for _, p := range paths {
		ch <- p
	}
	close(ch)
	wg.Wait()
	close(out)

	m := make(map[string]int)
	for part := range out {
		for k, v := range part {
			m[k] += v
		}
	}

	tokens, csum := 0, 0
	for k, v := range m {
		tokens += v
		csum += len(k) * v
	}
	fmt.Printf("files=%d tokens=%d distinct=%d csum=%d\n", len(paths), tokens, len(m), csum)
}
