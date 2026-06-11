// Channel pipeline -- Go, buffered chan string (cap 256), 1 producer -> 4 consumers.
// String bodies are SHARED through the channel (header copy only); the GC makes
// that safe.
package main

import (
	"fmt"
	"strconv"
)

const (
	n = 1000000
	w = 4
)

func main() {
	ch := make(chan string, 256)
	done := make(chan int, w)
	for i := 0; i < w; i++ {
		go func() {
			c := 0
			for s := range ch {
				c += len(s)
			}
			done <- c
		}()
	}
	go func() {
		for i := 0; i < n; i++ {
			ch <- "item-" + strconv.Itoa(i)
		}
		close(ch)
	}()
	total := 0
	for i := 0; i < w; i++ {
		total += <-done
	}
	fmt.Print(total)
}
