// Sequential keep-alive load client: one connection, one request at a time.
// This measures per-core server cost without a benchmark framework dependency.
package main

import (
	"bufio"
	"fmt"
	"net"
	"net/http"
	"os"
	"strconv"
	"sync"
	"time"
)

func main() {
	if len(os.Args) < 3 || len(os.Args) > 4 {
		fmt.Fprintln(os.Stderr, "usage: load host:port requests [clients]")
		os.Exit(2)
	}
	n, _ := strconv.Atoi(os.Args[2])
	clients := 1
	if len(os.Args) == 4 {
		clients, _ = strconv.Atoi(os.Args[3])
	}
	start := time.Now()
	var wg sync.WaitGroup
	errs := make(chan error, clients)
	for c := 0; c < clients; c++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			var conn net.Conn
			var err error
			for i := 0; i < 50; i++ {
				conn, err = net.Dial("tcp", os.Args[1])
				if err == nil {
					break
				}
				time.Sleep(20 * time.Millisecond)
			}
			if err != nil {
				errs <- err
				return
			}
			defer conn.Close()
			br := bufio.NewReader(conn)
			for i := 0; i < n; i++ {
				fmt.Fprintf(conn, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
				resp, err := http.ReadResponse(br, nil)
				if err != nil {
					errs <- err
					return
				}
				resp.Body.Close()
			}
		}()
	}
	wg.Wait()
	select {
	case err := <-errs:
		panic(err)
	default:
	}
	elapsed := time.Since(start)
	total := n * clients
	fmt.Printf("clients=%d requests=%d seconds=%.3f rps=%.0f\n", clients, total, elapsed.Seconds(), float64(total)/elapsed.Seconds())
}
