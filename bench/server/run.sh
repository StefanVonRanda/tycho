#!/bin/sh
# Real-program server check: one keep-alive connection, single event loop.
set -eu
cd "$(dirname "$0")/../.." || exit 2
TYCHOC="${TYCHOC:-./tychoc1}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
"$TYCHOC" bench/server/server.ty -o "$T/tycho-server" >/dev/null
go build -o "$T/go-server" bench/server/server.go
go build -o "$T/load" bench/server/load.go
run() {
    name=$1; bin=$2; port=$3
    PORT=$port "$bin" >"$T/$name.out" 2>"$T/$name.err" &
    pid=$!
    sleep 0.2
    out=$("$T/load" "127.0.0.1:$port" 20000)
    rss=$(ps -o rss= -p "$pid" | tr -d ' ')
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    printf '%-6s %s rss=%sKB\n' "$name" "$out" "$rss"
}
run tycho "$T/tycho-server" 18080
GOMAXPROCS=1 run go "$T/go-server" 18081
