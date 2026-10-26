// Minimal HTTP/1.1 keep-alive service used for the real-program server check.
package main

import (
	"fmt"
	"net/http"
	"os"
)

func main() {
	port := "18080"
	if p := os.Getenv("PORT"); p != "" {
		port = p
	}
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/plain")
		fmt.Fprint(w, "hello\n")
	})
	fmt.Fprintln(os.Stderr, "server: ready "+port)
	if err := http.ListenAndServe("127.0.0.1:"+port, nil); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
