// Go peer for tools/tycho-hash: walk sorted files, hash with a fixed worker pool.
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"sync"
)

type result struct { idx int; hash string; size int64; rel string }

func main() {
	if len(os.Args) != 3 { os.Exit(2) }
	root := os.Args[1]
	workers, _ := strconv.Atoi(os.Args[2])
	var rels []string
	filepath.Walk(root, func(p string, info os.FileInfo, err error) error {
		if err == nil && !info.IsDir() {
			r, _ := filepath.Rel(root, p)
			rels = append(rels, r)
		}
		return nil
	})
	sort.Strings(rels)
	jobs := make(chan int, 32)
	out := make(chan result, 32)
	var wg sync.WaitGroup
	for w := 0; w < workers; w++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := range jobs {
				f, err := os.Open(filepath.Join(root, rels[i]))
				if err != nil { out <- result{i, "unreadable", -1, rels[i]}; continue }
				h := sha256.New()
				n, _ := io.Copy(h, f)
				f.Close()
				out <- result{i, hex.EncodeToString(h.Sum(nil)), n, rels[i]}
			}
		}()
	}
	go func() { for i := range rels { jobs <- i }; close(jobs) }()
	results := make([]result, len(rels))
	for i := 0; i < len(rels); i++ { r := <-out; results[r.idx] = r }
	wg.Wait()
	var total int64
	for _, r := range results {
		fmt.Printf("%s  %d  %s\n", r.hash, r.size, r.rel)
		if r.size > 0 { total += r.size }
	}
	fmt.Printf("-- %d files, %d bytes, %d hashed\n", len(rels), total, len(rels))
}
