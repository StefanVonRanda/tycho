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
	"time"
)

func main() {
	if len(os.Args) != 3 {
		fmt.Fprintln(os.Stderr, "usage: load host:port requests")
		os.Exit(2)
	}
	n, _ := strconv.Atoi(os.Args[2])
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
		panic(err)
	}
	defer conn.Close()
	br := bufio.NewReader(conn)
	start := time.Now()
	for i := 0; i < n; i++ {
		fmt.Fprintf(conn, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
		resp, err := http.ReadResponse(br, nil)
		if err != nil {
			panic(err)
		}
		resp.Body.Close()
	}
	elapsed := time.Since(start)
	fmt.Printf("requests=%d seconds=%.3f rps=%.0f\n", n, elapsed.Seconds(), float64(n)/elapsed.Seconds())
}
