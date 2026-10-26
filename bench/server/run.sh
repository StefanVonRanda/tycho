#!/bin/sh
# Real-program server check: keep-alive HTTP service, varying client concurrency.
set -eu
cd "$(dirname "$0")/../.." || exit 2
TYCHOC="${TYCHOC:-./tychoc1}"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
"$TYCHOC" bench/server/server.ty -o "$T/tycho-server" >/dev/null
go build -o "$T/go-server" bench/server/server.go
go build -o "$T/load" bench/server/load.go
go build -o "$T/static" bench/server/static.go
"$TYCHOC" server/main.ty -o "$T/tycho-static" >/dev/null
run() {
    name=$1; bin=$2; port=$3; clients=$4; procs=$5
    shift 5
    if [ "$name" = tycho-static ]; then
        "$bin" --port "$port" "$@" >"$T/$name.out" 2>"$T/$name.err" &
    elif [ "$procs" = "-" ]; then
        PORT=$port "$bin" --port "$port" "$@" >"$T/$name.out" 2>"$T/$name.err" &
    else
        GOMAXPROCS=$procs PORT=$port "$bin" --port "$port" "$@" >"$T/$name.out" 2>"$T/$name.err" &
    fi
    pid=$!
    sleep 0.8
    out=$("$T/load" "127.0.0.1:$port" 1000 "$clients")
    rss=$(ps -o rss= -p "$pid" | tr -d ' ')
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    printf '%-10s %s rss=%sKB\n' "$name" "$out" "$rss"
}
printf '%-10s %s\n' server "load"
run tycho-c1 "$T/tycho-server" 18080 1 -
run tycho-c8 "$T/tycho-server" 18081 8 -
run go1-c1 "$T/go-server" 18082 1 1
run go1-c8 "$T/go-server" 18083 8 1
run goall-c8 "$T/go-server" 18084 8 -
run tycho-static "$T/tycho-static" 18085 8 - --root server/www --workers 8 --quiet
run go-static "$T/static" 18086 8 -
