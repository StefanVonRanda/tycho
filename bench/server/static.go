// Go static-file peer for the production Tycho HTTP server check.
package main

import (
	"fmt"
	"net/http"
	"os"
)

func main() {
	port := "18086"
	if p := os.Getenv("PORT"); p != "" { port = p }
	fmt.Fprintln(os.Stderr, "static: ready "+port)
	if err := http.ListenAndServe("127.0.0.1:"+port, http.FileServer(http.Dir("server/www"))); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
