# Value-lifetime regions — an arena whose lifetime follows a value

> Status: **research study, not a proposal to build.** Reaches a recommendation for
> the maintainer. Nothing here changes the compiler, the runtime, or codegen. Every
> claim about current behaviour cites `path:line` and was re-opened at the source
> before being written down. The companion negative result on *stored references* is
> [`limited-references-spike.md`](limited-references-spike.md); this is a different
> question and §1 says how.

## 1. The question, and what it is not

The reference spike closed one question: can a *stored reference* express a graph
edge? No — a remote part has no sound deep-copy at the thread boundary, so it fails
the invariant that makes threads race-free (`limited-references-spike.md:81-88`), and
the decision was recorded as final (`limited-references-spike.md:133-145`). That
question was about **aliasing**.

This study asks a question that shares none of that machinery: **can a region — a
set of allocations reclaimed as a unit — have its lifetime tied to a *value* rather
than to a *lexical scope*, with no value ever pointing into storage it does not
own?** Nothing here proposes aliasing, a reference type, or reference counting. The
whole proposal is a change of *bookkeeping*: today the thing that owns an arena is a
scope; the question is whether it could be a value.

The distinction matters because the two questions fail for different reasons, and a
reader who has internalised the reference spike's "no" will assume this one is
already answered. It is not. Aliasing fails at the thread boundary. Value-lifetime
regions, as §5 shows, survive the thread boundary intact and fail — where they fail —
on arithmetic and on the spec freeze instead.

## 2. The gap, from this repo

### 2.1 An arena's owner is a scope, and only a scope

`arena_new` and `arena_child` are the only two ways an arena comes into existence
(`runtime/tycho_rt.c:396-406`, `:437-441`), and `arena_free` is the only way one goes
away (`runtime/tycho_rt.c:512-521`). Every call site of those three in generated code
is a *scope* boundary. The runtime's own header states the mapping as the model:
every proc, every `if`/`else` block, every loop gets an arena, and a child is freed
when its scope ends (`runtime/tycho_rt.c:5-13`). The emitted shape is four rules —
`main` opens `_root`, every function opens `Arena _scope = arena_child(_parent)`,
every allocation takes an arena argument, and a return builds into `_parent`
(`docs/guides/memory-model.md:37-45`), realised in codegen by a single "current
arena" string that defaults to `&_scope` (`src/tychoc.c:7188`) and a per-proc stack
of enclosing block arenas freed innermost-first at any exit
(`src/tychoc.c:8693-8699`, `:8733-8737`).

The three exceptions in the tree are the ones that prove the rule, because each is a
*runtime* object the compiler finalises like a scope, not a user-visible lifetime:

- a task's `root` arena, created at `tycho_task_new` and freed at
  `tycho_task_free` (`runtime/tycho_rt.c:532`, `:537`, `:590`);
- a channel cell's payload arena, one per ring slot, created at channel construction
  and freed when a later send reclaims the slot (`runtime/tycho_rt.c:633`, `:673`,
  `:725`, `:831`);
- a typed FFI handle's destructor, which is emitted into exactly the slot the task
  finaliser uses (`docs/internals/typed-handles-design.md:56-60`, `:98-102`).

All three are still lexically scoped from the program's point of view: the compiler
emits their finaliser at every exit of the scope that declared them.

### 2.2 What "no pointer crosses sideways" means here

Data moves between arenas in exactly two directions — **down** as a call argument (a
pointer into a parent arena, valid for the call, no copy) and **up** as a return or
an outer assignment (`runtime/tycho_rt.c:15-24`, `docs/guides/memory-model.md:47-51`,
normatively `docs/spec/07-memory-model.md:101-118`). There is no third direction. A
value in one scope's arena is never reachable from a sibling scope's arena, because
neither arena outlives the other in a way the other can observe. That absence is what
makes the escape decision *local*: the destination of every write is known at the
write site from the enclosing scopes and the signatures alone
(`docs/guides/memory-model.md:57-66`), which is the whole claim of the project
(`docs/thesis.md:14-17`, `:39-53`).

Throughout this document, **sideways** means a pointer from one value's storage into
another value's storage where neither strictly contains the other's lifetime. Every
design in §4 is judged on whether it introduces one.

### 2.3 One correction to the framing

The premise this study was commissioned under says the only way a value outlives its
scope is "to be copied up into the parent's arena". That overstates the cost.
Up-escape is **destination-passing first, copy second**: the value is *produced
directly in the destination's storage* (`docs/spec/07-memory-model.md:109-112`), and
the emitted return is `{ T _ret = <built in _parent>; arena_free(&_scope); return
_ret; }` (`docs/guides/memory-model.md:43-45`, `:31-34`). A copy is paid only when
the value already exists somewhere else and must be duplicated to be independent — and
even then the tree carries four static mechanisms that remove copies rather than add
them (`docs/guides/memory-model.md:78-113`). So "repeated copy-up" is not the
standing tax on long-lived structures. The standing tax is the one in §3: storage that
is dead but not reclaimable until the owning scope exits.

## 3. How the model handles long-lived dynamic structures today

### 3.1 The recommended representation, and what it costs

For pointer-shaped, structurally-shared data the documented idiom is a flat node pool
with integer-index children (`docs/internals/value-semantics-limits.md:52-85`). The
project is deliberate that this is a representation worth choosing and not merely a
constraint to satisfy, and equally deliberate that it is *not* presented as the model
(`docs/architecture.md:87-90`). Its two named costs are ergonomic: "you index a pool
instead of following references, and you cannot delete a single node without
compacting" (`docs/internals/value-semantics-limits.md:82-84`).

The measured cost of *not* using it — the by-value recursive form, which is what a
naive long-lived structure gets — is **~1.55× C peak RSS**, from a prefix tree where
each node owns an `int -> child` map: tycho 58.7 MB vs C 37.8 MB vs Go 33.8 MB
(`docs/internals/value-semantics-limits.md:41-45`). I re-opened every place that
figure appears rather than trusting the number I was handed: it is stated identically
at `docs/internals/value-semantics-limits.md:45`, `docs/guides/memory-model.md:118`,
`docs/architecture.md:87`, `README.md:139`, `bench/README.md:33` and
`bench/trie/RESULTS.md:28`, all of them attributing the halving from ~3.2× (119 MB) to
the compact indexed-dict map layout. The figure is current.

One stale copy exists and should be corrected separately: `examples/triepool.ty:3`
still says the by-value form "costs ~2.7x C on memory". That predates the compact-map
work. Fixing it is outside this study's scope (one document, no code) but it is a real
drift and is recorded here so it is not lost.

Two adjacent measurements bound the shape of the problem. Expressed the value-semantic
way — a graph as an adjacency list of indices — `bench/dijkstra` lands at ~1.3× C
memory and ~1.2× wall, competitive (`docs/internals/value-semantics-limits.md:86-95`).
Expressed as a delete-heavy fixed-capacity cache, `bench/lru` lands at ~2.8× C and
ahead of Go on both axes (`docs/internals/value-semantics-limits.md:96-106`), with the
index pool spelled out in its own header: nodes in one `[Node]` array, `prev`/`next` as
`int` indices, and the tail slot overwritten in place on eviction so a full cache holds
steady with zero per-op allocation (`bench/lru/lru.ty:4-13`).

### 3.2 What actually happens to a removed node

This is the crux, and it is worth spelling out at source level because it is where a
value-lifetime region would earn its keep or not.

`corelib/pool` packages the idiom: `add` returns a `Handle` (a distinct int newtype,
introducing no aliasing), and every access is checked against a per-slot generation
counter so a stale handle is caught at runtime (`corelib/pool/pool.ty:1-22`). It is
explicit that it improves ergonomics, not memory (`corelib/pool/pool.ty:15-18`).

`remove` marks the slot unused, bumps its generation, and pushes the index onto a free
list (`corelib/pool/pool.ty:77-85`). It does **not** clear `val`. So the removed
value's heap bytes stay owned by the pool's arena until the slot is *reused*: `add`
reaches `p.slots[idx].val = v` (`corelib/pool/pool.ty:44-52`), which is an
element-overwrite, and element-overwrite recycling hands the evicted element's buffer
back to the array's arena free list (`docs/guides/memory-model.md:108-113`), backed by
`arena_recycle` and its segregated per-size-class lists
(`runtime/tycho_rt.c:415-435`, reused on the next allocation at `:456-474`).

The consequence, stated plainly: **a pool's memory tracks the high-water mark of live
nodes, and never returns below it.** Bytes are recovered on reuse, never on removal.
For a cache with a fixed capacity that is exactly right and is why `bench/lru` holds
steady. For a structure whose live set genuinely shrinks — a server that had 10 000
connections and now has 10 — nothing shrinks with it until the owning scope exits, and
for a server the owning scope is `main`.

### 3.3 The build-and-hold case, and the idiom that already answers it

The second documented weak spot is a long-lived scope holding a transient: an arena
reclaims at scope exit, not incrementally, so a function that builds a large parse
buffer and keeps working holds it until it returns
(`docs/internals/value-semantics-limits.md:118-124`). The documented answer is to
scope the transient in an inner function or block so its arena reclaims before the
long-lived work continues (`docs/internals/value-semantics-limits.md:126-135`). That
answer is O(1) — one `arena_free` — and needs no new construct. Any proposed region
feature must beat it, not merely match it.

### 3.4 The case the repo has never actually hit

The motivating example for value-lifetime regions is a server holding N live
connections. This repo contains a server, and it does not have that shape. The accept
loop reads one request, routes it, writes the response, and closes the fd, holding no
per-connection state across iterations (`examples/webserver/main.ty:205-215`); the
`httpd` package is explicit that the caller owns the accept loop and that the package
supplies only per-request plumbing (`corelib/httpd/httpd.ty:5-14`). A connection is an
`int` fd, not a Tycho value with storage. Under the current model each iteration's
arena is reset per iteration, so the loop runs in bounded memory
(`runtime/tycho_rt.c:10-13`, `docs/guides/memory-model.md:53-55`).

That is not proof that the gap is imaginary — it is proof that **no call site in this
repository currently pays for it.** A design study should say so.

## 4. Three designs

Each is judged on four axes: the source surface, the sideways-pointer invariant (§2.2),
the interaction with `spawn` and channels, and the C lowering.

### 4.1 Design A — region variables (`region` as a named lifetime in the type)

**Surface.** A region is created, allocations are directed into it, and it is dropped:

```
r := region()
n := Node("root") in r          # n's storage is allocated in r, not in _scope
m := Node("leaf") in r
link(r, n, m)
drop(r)                          # or at r's scope exit
```

For the compiler to reject `escape(n)` where `n` outlives `r`, the region must appear
in `n`'s type — `Node@r` — and therefore in the signature of every function that
touches such a value, and a function taking values from two regions needs region
polymorphism. This is Tofte–Talpin region inference / Cyclone's `rgn_t<`r>`, which the
thesis names explicitly as the well-trodden prior art that Tycho's contribution is to
make *implicit* (`docs/thesis.md:26-32`).

**Sideways.** Design A does not merely permit a sideways pointer; permitting one *is*
the feature. `n` in scope S1 and `m` in sibling scope S2 both point into `r`, which
outlives both. That is precisely the third direction §2.2 says does not exist, and it
is what destroys locality: the destination of a write is no longer decidable from
enclosing scopes and signatures (`docs/guides/memory-model.md:57-66`) — it needs the
region variable, which is an annotation on every signature in the transitive closure.

**`spawn` / channels.** A spawn site allocates its argument struct in the task root
and deep-copies each heap argument into it, after which spawner and task share zero
bytes (`src/tychoc.c:8395-8410`, `runtime/tycho_rt.c:523-531`); `wait` copies the
result out and frees the root eagerly (`src/tychoc.c:8113-8116`). A channel does the
same per payload into a per-cell arena (`runtime/tycho_rt.c:604-611`). An `@r` value
offers neither option: copying it means copying the whole region (unbounded — the
region exists *because* it is the long-lived structure), and passing the region pointer
shares mutable storage across threads, which is the exact hypothesis the race-freedom
guarantee rests on (`docs/spec/13-concurrency.md:16-27`). So `@r` values must be banned
from spawn arguments and channel payloads — a `Sendable`-style bifurcation of the type
system that the spec's opening paragraph specifically claims the model does not need
(`docs/spec/13-concurrency.md:3-7`).

**C lowering.** The cheapest part. One extra `Arena *` parameter per region variable
per function — structurally identical to the `_parent` parameter every function already
takes (`docs/guides/memory-model.md:40-41`). The lowering is nearly free; the entire
cost is in the source language and the checker.

**Verdict.** This is a lifetime-annotation system with a different keyword. It
contradicts `docs/thesis.md:10` ("no lifetime annotations, no borrow checker") not
incidentally but at the load-bearing joint. Reject.

### 4.2 Design B — the value-homed arena (the region is a hidden field, never a type variable)

**Surface.** No lifetime variables anywhere. A region is an ordinary value whose
storage happens to be its own arena, and whose contents are reachable only by copy.
Two spellings, same semantics:

```
struct Server:
    conns: owned [Conn]         # this container allocates into its own arena
```

```
r := region(Conn)               # a generic Region(T)
h := insert(r, Conn(fd, buf))   # h: Handle -- a pooled index, pointer-free
c := get(r, h)                  # deep copy OUT, exactly as today
remove(r, h)                    # the element's bytes return to r's arena NOW
```

The second spelling is worth naming honestly: it is `corelib/pool` with an arena
underneath instead of a `[Slot($T)]` array. The API is already written
(`corelib/pool/pool.ty:44-90`), including the property that makes it sound — `get`
returns a copy, so the pool is never aliased (`corelib/pool/pool.ty:63-68`).

**Sideways.** None. The rule is that nothing outside the region ever holds a pointer
into it: reads copy out (already the language's defining invariant,
`docs/spec/07-memory-model.md:28-39`), writes copy in. The region *value* obeys value
semantics like any other value — it moves down as an argument and up as a return, and
its arena travels with it. The two-direction rule of §2.2 is untouched, because a
region introduces no new direction; it changes only *which* arena an allocation lands
in, and the compiler already picks that per write site (`src/tychoc.c:7188`,
`docs/guides/memory-model.md:57-66`).

**`spawn` / channels.** This design survives the boundary, which is the reason it is
worth writing down at all. Because a region is owned and pointer-free from outside,
`copy_into(param, "(&_tk->root)", arg)` (`src/tychoc.c:8407`) generalises without a new
rule: create a fresh arena inside the task root, deep-copy every live element into it.
Blocks crossing threads is already the status quo — the block pool is thread-local
(`runtime/tycho_rt.c:357`), blocks are released to whichever thread's pool frees them
(`runtime/tycho_rt.c:392-394`), and a spawned thread flushes its pool before exiting so
nothing leaks with the TLS (`runtime/tycho_rt.c:543-549`). Channels need nothing new
either: a region payload deep-copies into the cell arena like any other value
(`runtime/tycho_rt.c:604-611`).

The honest limit is that this makes regions **cheap to free and no cheaper to send**.
A 1 GB region crosses a channel as 1 GB of deep copy. Today you would send an index
instead, and you still would.

**C lowering.** A region-bearing value carries its `Arena` inline — 48 bytes on LP64,
six fields (`runtime/tycho_rt.c:91`):

```c
typedef struct { Arena rgn; Arr_Conn conns; } Server;
```

Allocations for elements of `s.conns` target `&s.rgn` instead of the current scope
string; the deep copy becomes `_c.rgn = arena_new(0);` followed by the existing
recursive element copy; and the free is one `arena_free(&s.rgn)` emitted into exactly
the finaliser slot tasks and handles already use (`src/tychoc.c:8716-8718`,
`docs/internals/typed-handles-design.md:98-102`), which fires at block end, early
`return`, `break`/`continue` and `or_return` (`src/tychoc.c:8733-8737`). There is a
genuine simplification available: `inout` on a heap value today threads the caller's
owning scope to the callee as a hidden parameter so an allocating mutation lands where
the borrowed value lives (`docs/spec/07-memory-model.md:186-192`). If the value owns
its arena, that arena is reachable *from the value*, and the hidden parameter is no
longer needed for region-bearing types.

**The arithmetic that decides it.** `arena_new(0)` sets `blocksz` to
`TYCHO_BLOCK_DEFAULT` = 65536 (`runtime/tycho_rt.c:51`, `:400`), and the first
allocation in a fresh arena takes a block of `max(n, blocksz)`
(`runtime/tycho_rt.c:475-480`) from the thread-local pool or, failing a fit, from
`malloc` (`runtime/tycho_rt.c:359-389`). So **every live region holds a 64 KiB block
out of the pool for as long as it lives**, whatever it contains. Resident cost is
bounded by pages actually touched, not by the full 64 KiB — the block is `malloc`'d, not
committed — but the pool's high-water mark rises by one block per concurrently-live
region regardless.

That sets the crossover precisely. A region pays off when a single value's payload is
comparable to or larger than a block *and* the live count fluctuates. One region per
server: fine. One region per connection holding a few hundred bytes: strictly worse
than the high-water-mark pool it replaces, since the pool amortises one block across
thousands of nodes. `arena_new` does take a block size, so `region(1024)` is
expressible — but at that point the programmer is sizing an allocator by hand, and
"manual memory-management escape hatches as the *idiomatic* path" is a decided
non-goal (`docs/architecture.md:81`).

**Verdict.** Sound, buildable, no new analysis, survives the thread boundary. Its win
is exactly one thing: **bulk reclamation at a point that is not a scope exit.** It does
not reduce the ~1.55× storage premium (that is value-vs-pointer layout, unchanged) and
it does not enable sharing (that was settled in `limited-references-spike.md:90-102`).

### 4.3 Design C — `drop(x)`: give the value early death, with no region object at all

**Surface.** One statement. `drop(x)` ends `x`'s lifetime; every later use of `x` is a
compile error. This is the minimal reading of the question: rather than an arena
outliving a value until scope exit, the value's storage dies with the value.

Every piece of machinery already exists. The runtime primitive is `arena_recycle`,
which hands a proven-dead, uniquely-owned buffer back to its arena and is reused by the
next allocation (`runtime/tycho_rt.c:408-435`, `:456-474`), guarded by `arena_owns` so
only buffers the arena actually owns are recycled (`runtime/tycho_rt.c:443-454`). The
static precondition is unique ownership, which value semantics already guarantees
(`docs/spec/07-memory-model.md:55-62`). The use-after-consume check already ships:
passing a variable to a `sink` parameter consumes it, and using it afterwards is a
compile error, not a silent copy (`docs/spec/11-functions.md:35-38`). `drop(x)` is
`sink` into a callee that does nothing.

**Sideways.** None. `drop` removes storage; it opens no new path for a pointer.

**`spawn` / channels.** None. `drop` is intra-scope, and a dropped value cannot be sent
because it is consumed.

**C lowering.** `drop(x)` emits a recursive walk over `x`'s heap-bearing fields calling
`arena_recycle` on each against `x`'s home arena. The one-level version of exactly this
walk already ships as element-overwrite recycling
(`docs/guides/memory-model.md:108-113`), and the recursive shape already ships as the
deep-copy walk emitted at every escape point (`docs/guides/memory-model.md:193-198`).

**The objection that sinks it.** Correctness in this model rests on a single asymmetry,
stated normatively: *under value semantics, a wrong escape decision can change only
**when** memory is reclaimed — never **whether** a pointer dangles*
(`docs/spec/07-memory-model.md:141-152`). Over-approximating "this might escape" costs
mild retention and can never be a bug, because there are no aliases to invalidate, and
there is no symmetric failure. That asymmetry is why the spec can guarantee "no
dangling ... *by construction*" (`docs/spec/07-memory-model.md:122-126`) and why the
§9.5 optimisations can be declared observationally transparent by fiat
(`docs/spec/07-memory-model.md:64-74`).

`drop` introduces the symmetric failure. A wrong `drop` dangles. The guarantee
degrades from *by construction* to *by check*, and the check is a new liveness analysis
the compiler does not have today for arbitrary places — `sink` tracks consumption of a
whole variable, not of a place reached through a container or a field. Design C is the
cheapest to build and the only one that weakens the property the project exists to
demonstrate.

**Second objection.** Its motivating case — the build-and-hold transient — already has
an O(1) answer that needs no new construct (§3.3,
`docs/internals/value-semantics-limits.md:126-135`). `drop` wins only where the
transient's lifetime is genuinely not nestable, and no call site in this repo shows
that shape (§3.4).

**Verdict.** Reject on the asymmetry alone.

## 5. Compatibility with the 1.0 spec freeze

Tycho 1.0 is ratified and is *the freeze*: the spec states that it "defines **Tycho
1.0**, which is the first *frozen* version", that within a frozen version the document
is normative and stable, and that corrections changing observable behaviour are issued
as errata against a named version, "never silently"
(`docs/spec/00-conventions.md:106-114`); the spec
index records it as ratified on 2026-07-13 (`docs/spec/README.md:3`), and the plan's
locked decision is that the 1.0 spec *is* the language freeze with date-based versions
after it (`docs/internals/spec-plan.md:43`). The spec's scope is language **and**
corelib (`docs/internals/spec-plan.md:41`), so a corelib-only addition is a spec change
too.

| Design | Fits inside the 1.0 freeze? | Why |
|---|---|---|
| **A — region variables** | **No, and not post-1.0 either.** | Breaks §9.1 at the root: "Tycho has **no reference type**. A program cannot name, store, or return a pointer into another value's storage" (`docs/spec/07-memory-model.md:16-21`). An `@r` value *is* such a pointer. Also breaks §10.1's "Lifetime is lexical … never from a whole-program pointer analysis" (`docs/spec/07-memory-model.md:85-91`) and §20's no-`Sendable` claim (`docs/spec/13-concurrency.md:3-7`). This is not a breaking change to Tycho; it is a different language. |
| **B — value-homed arena** | **Compatible in substance; needs a new dated version in procedure.** | It violates no normative clause. A region value copies deeply (§9.2, `docs/spec/07-memory-model.md:28-39`), holds no externally-visible pointer (§9.1), keeps unique ownership (§9.4, `:55-62`), and its own lifetime is still lexical (§10.1), so §10.4's asymmetry survives intact. But it adds a type constructor and a builtin family — new grammar in Ch 4 and new clauses in §16–19 and Part XII — and additions to a frozen version are a new dated release (`Tycho 2027`), not errata. **Post-1.0, additive.** |
| **C — `drop`** | **Needs a breaking change, not merely an addition.** | Same procedural answer as B for the grammar, but worse in substance: §10.3.1 guarantees no dangling *by construction* (`docs/spec/07-memory-model.md:122-126`) and §10.4 states the asymmetry as the reason (`:141-152`). `drop` makes both hold by check instead. Those clauses would have to be reworded, which is a change to observable behaviour of the conformance model — errata against 1.0 in the strict sense the spec forbids doing silently. |

One useful negative check: none of the three is reachable as an *implementation*
detail. §10.1 explicitly says the arena mechanism "is an implementation realization and
is not observable beyond the guarantees in §10.3"
(`docs/spec/07-memory-model.md:93-99`), and §9.5 lets an implementation reuse buffers
freely so long as nothing observable changes (`:64-74`). A compiler could therefore
give a container a private sub-arena *today*, with no spec change at all, as long as it
is invisible. What it could not do is let the programmer name it. Every design in §4
fails to fit inside the freeze for the same reason: the surface, not the mechanism.

## 6. Recommendation

**Do not build.** Not Design A, not Design C, and not Design B now.

Design A is a borrow checker with a different keyword and contradicts the thesis at the
joint (`docs/thesis.md:10`, `:14-17`). Design C buys the least and is the only proposal
that weakens the asymmetry the whole project exists to demonstrate
(`docs/spec/07-memory-model.md:141-152`) — a feature that trades the model's central
guarantee for a case already answered by an idiom the docs recommend
(`docs/internals/value-semantics-limits.md:126-135`) is a bad trade at any price.

Design B is the only one that is genuinely sound, and it should still not be built yet,
because its payoff is a single narrow thing — bulk reclamation at a non-scope-exit point
— and nothing in the repo currently pays the cost that payoff addresses. §3.2 shows the
existing idiom recovers a removed node's bytes on reuse and holds at the live-set
high-water mark; §3.4 shows the motivating long-lived-server shape does not exist in the
tree; §4.2 shows a per-node region is *worse* than the pool it would replace below a
64 KiB payload.

**The condition under which to build Design B, and only Design B:**

> A benchmark exists in `bench/` whose peak RSS is dominated by storage that is dead
> but unreclaimable before scope exit, and which none of the three shipped mechanisms
> recovers: the inner-function transient scope
> (`docs/internals/value-semantics-limits.md:126-135`), element-overwrite recycling
> (`docs/guides/memory-model.md:108-113`), or slot reuse in an index pool
> (`corelib/pool/pool.ty:44-52`). The workload's live values must have payloads
> comparable to or larger than one 64 KiB block (`runtime/tycho_rt.c:51`), and its live
> count must genuinely shrink, not merely churn at a fixed capacity.

No such benchmark exists today. I checked the full workload table in
`bench/README.md:19-45`, which is every measured workload in the tree, and opened the
three closest candidates: `bench/gcscan` retains a large **live** set to make a tracing
GC re-scan it (`bench/gcscan/gcscan.ty:1-11`), `bench/latency` churns a working set
that the per-iteration arena reset reclaims (`bench/latency/latency.ty:1-5`), and
`bench/invindex` is build-and-hold growth of live data, closing to ~1.07× C with
`reserve` (`bench/README.md:27`). All three are *live* retention, which no region
feature helps. Writing that benchmark is the cheap next step, and it is also the honest
one: if it cannot be written, the gap is theoretical and the recommendation is
unconditional.

## 7. Two findings that change the surrounding picture

**The reference spike's optional half has shipped.** `limited-references-spike.md:104-131`
proposed user-defined yielding subscripts as the one arena- and thread-compatible
reference-shaped feature, ranked as an optional convenience. They now exist:
`docs/reference/subscripts.md:1-8` documents them as a compile-time place-macro with no
runtime object, the spec covers them normatively at §15.6 and §18.7 with fixtures
(`docs/spec/appendix-e-conformance.md:132`, `:146`), and the self-hosted compiler
enforces the yield-a-place rule (`compiler/tychoc0.ty:2050`). Any future work in this
space should start from the fact that the ergonomic half of the reference question is
already answered, so a region proposal must justify itself on *reclamation timing*
alone — which is what §4 does.

**`handle` is the lifecycle object this question was asking for, minus the storage.**
A typed FFI handle is already an affine value with a compiler-known destructor that runs
at every scope exit, built by reusing the task finaliser machinery
(`docs/internals/typed-handles-design.md:1-12`, `:56-70`), and the design note's own
soundness argument is that affine ownership plus a scope-exit finaliser plus
container/escape bans equals no use-after-free, no double-free, no leak
(`docs/internals/typed-handles-design.md:118-123`). Design B is that pattern with
`arena_free` as the finaliser instead of a C symbol — which is the strongest available
evidence that it would work, and also the clearest statement of what it would cost: the
same v1 bans (no storing in containers, no returning) that handles still carry
(`docs/internals/typed-handles-design.md:65-70`, `:80-82`) would apply to regions on
day one, and lifting them is precisely the ownership-transfer work that note defers.
