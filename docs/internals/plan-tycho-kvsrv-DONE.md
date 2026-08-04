# What the next program says the language needs

> This plan is a fresh clone, 2026-08-04: the completed tycho-rsa plan lives
> at `docs/internals/plan-tycho-rsa-DONE.md`. The rule from that plan holds
> here: *does anything that is not the program written to want it need this?*
> A finding becomes a phase only when a second, independent caller exists.

## The program -- the HTTP key-value server  (tools/tycho-kvsrv/)

A concurrent HTTP server exposing a key-value store: GET/PUT/DELETE
`/kv/<key>`, keep-alive connections, status codes, over core:net + core:httpd
(which already parse requests and render responses). The stress is
networking plus the shared-state concurrency question the store's pscan
probed only read-only.

The language answers the shared-state question before the first line: there
is no shared mutable storage and no lock machinery — race freedom is by
construction. The store is therefore an ACTOR: one task owns the map, and
connection handlers reach it through channels. The channel topology is
forced by the affine/non-storable rules (a `Channel` cannot be a struct
field, a container element, or a return): ONE shared command channel whose
payload carries a worker id, plus one NAMED reply channel per worker, passed
as spawn arguments — so every channel has exactly one receiver and nothing
races. The worker count is a named constant (4), the same constraint the
store's pscan hit (Task/Channel handles are affine — no arrays of them); the
worker pool uses tycho-httpd's proven recursive fan-out on the one shared
listening fd.

The differential is the daemon-testing pattern from server/run.sh: start
with --port 0, poll the stderr banner for the real port, drive it with a
raw-socket python client, assert the responses. The gate asserts the
round-trips (PUT/GET/DELETE, 404 on a missing key), keep-alive (two
requests, one connection), a method/path 405/404, and the concurrency probe:
4 parallel clients PUT distinct keys, all 4 come back intact (the actor
serializes the map; the assertion proves no command is dropped).

Scope, honestly sized: the store is IN-MEMORY (no persistence — the B+
tree lives in the tycho-kv tool and is not a corelib; persistence would be
an extraction refactor, not the point of this program).

## Phases

### Phase 1 -- the server: routes, the actor store, the worker pool  [DONE 2026-08-04]

The server is ~240 lines: core:httpd for request parsing and response
rendering, core:net for the fds, and the concurrency shape the language
FORCES and the program makes explicit: the store is an ACTOR (one task owns
the `[string: string]` map), reachable through ONE shared command channel
whose payload carries the worker id, plus four NAMED reply channels passed
as spawn arguments. The rules that forced this: a Channel cannot be a
struct field (compiler-rejected with a clean diagnostic), a container
element, or a return value; tasks cannot detach (implicit join at scope
exit). The worker pool is tycho-httpd's recursive fan-out over the one
shared listening fd, four workers, each serving its accepted connection
inline -- the same affine constant the kv store's pscan hit.

Every channel has exactly one receiver, so nothing can race -- by
construction, not by lock. The gate proves the actor round-trip under
interleaving: 4 parallel clients PUT distinct keys and all 4 come back
intact.

### Phase 2 -- the daemon gate  [DONE 2026-08-04]

`make kvsrv-check` (ci step [3l/18], ~2s) follows the server/run.sh daemon
pattern: start with --port 0, poll the stderr banner for the bound port
(the socket is already listening, so the runner never sleeps a fixed
interval), drive it with one raw-socket python client, assert: the
round-trips (PUT/GET/DELETE, 404 on a missing key), the protocol (POST
405, a non-kv path 404), keep-alive (two requests on one connection, both
answered), and the concurrency probe -- all golden-locked.

**No findings to file.** The networking and shared-state-concurrency axes
held: the actor shape fell out of the documented channel rules, race
freedom came free, and the only surprises were the author's (a python
client that forgot the blank line after the request head -- the server
correctly waited for it).

## Findings

(none yet -- findings appear here as the program is written)

## Phases

(none yet -- a finding becomes a phase only when a second, independent caller
needs it)
