# From `malloc` to implicit arenas

If you write C, you already know Tycho's memory model — you just have to remove one thing
from it. This is the idea in five steps, starting from code you've written a hundred times.
For the rigorous version with benchmarks, see [thesis.md](thesis.md); this is the on-ramp.

## Step 1 — the C you know

```c
char* greet(const char* name) {
    size_t n = strlen(name) + 7;
    char* out = malloc(n);
    snprintf(out, n, "hello %s", name);
    return out;          // caller must free() this. miss it -> leak. free twice -> crash.
}
```

Every allocation is a promise to free, and the compiler doesn't hold you to it. That's the
whole tax of manual memory management: the knowledge of *when* to free lives in your head,
not in the code.

## Step 2 — arenas, an old and fast fix

An **arena** (or region, or bump allocator) replaces `malloc`/`free` with something
simpler: hand out memory by moving a pointer forward, and free *everything at once* when
the arena's job is done.

```c
Arena a = arena_new();
char* x = arena_alloc(&a, 32);   // no individual free
char* y = arena_alloc(&a, 64);
// ... use x, y ...
arena_free(&a);                  // one free reclaims both
```

Allocation is a pointer bump (nearly free), there's no per-object bookkeeping, and you can't
leak an individual object. Game engines, compilers, and web servers have used arenas for
decades. So why isn't every program written this way?

## Step 3 — the one hard question

Because of *escape*. If a value allocated in an arena is still referenced after the arena is
freed, you have a dangling pointer:

```c
Arena a = arena_new();
char* p = arena_alloc(&a, 32);
stash_globally(p);      // p escapes the arena's lifetime...
arena_free(&a);         // ...and is now a use-after-free.
```

Knowing whether a value escapes, in a language with pointers, requires **whole-program alias
analysis** — following every pointer everywhere to prove nothing outlives its arena. That's
the hard part, and it's why arenas stay a manual, expert-only tool.

## Step 4 — Tycho's move: delete the question

Tycho removes escape analysis by removing the thing it analyzes: **there is no reference
type.** You cannot store or return a pointer into another value's memory, and assignment
copies:

```
b := a        # b is an independent copy, not an alias
```

Now a value can leave a scope in exactly two ways, and *both are written in the source*:

- **down** — passed as an argument to a function it calls, or
- **up** — returned to its caller.

There's no third way, because there are no references to smuggle one out. So the compiler
can see every value's lifetime from the syntax alone. Here's the C function from Step 1,
in Tycho:

```
fn greet(name: string) -> string:
    return "hello " + name
```

No `malloc`, no `free`, no arena handle. Each scope — every function call, every loop
iteration, every `if` block — owns an arena. The `"hello " + name` string is built in
`greet`'s arena; because it's *returned*, the compiler allocates it in the **caller's**
arena instead, so it's still alive after `greet` returns and is freed when the caller's
scope ends. The compiler placed every allocation and every free itself, from the shape of
the code. No garbage collector runs; no annotations were written.

And it's not magic underneath — Tycho transpiles to C, so you can read exactly the arena
calls it emits (`./tychoc greet.ty --emit-c`).

## Step 5 — the honest cost

Value semantics is not free, and pretending otherwise would be the dishonest part:

- **Copies cost.** `b := a` on a big array copies it. The compiler elides copies it can prove
  are unobserved (a returned-and-discarded value is a move, not a copy; `acc = acc + x` in a
  loop grows one buffer in place, turning the textbook O(n²) string build into O(n)) — but
  the ones it can't prove away are real.
- **Shared, pointer-shaped data costs more.** A tree or graph whose nodes point at each other
  is stored *by value* here, with no sharing, so a trie costs ~1.55× C's memory and there's no
  way to build a doubly-linked list or an observer the pointer way. The idiom is a **flat node
  pool** — hold all nodes in one array and link them by integer index — which is both the
  value-semantics-friendly shape *and* the cache-friendly one data-oriented engines choose on
  purpose. The full loss column is in
  [internals/value-semantics-limits.md](internals/value-semantics-limits.md).
- **Arenas reclaim at scope exit, not incrementally.** A long-lived scope holds its transients
  until it returns; the fix is to scope them in an inner function.

## What you get for it

No garbage collector and its pauses. No manual `free` and its leaks and use-after-frees. No
borrow checker and its annotations. Just lexical scope — the same braces you already write —
deciding when memory is freed, with the compiler doing the placement. Whether that trade is
worth it is exactly the question this experiment exists to answer; the
[benchmarks in the thesis](thesis.md) are the evidence so far.
