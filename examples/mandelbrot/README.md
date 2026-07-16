# mandelbrot

A parallel Mandelbrot renderer — about 70 lines of pure Tycho, no FFI. It prints
a small ASCII preview and the total escape-iteration and in-set pixel counts.

It exists as a dogfood for a corner neither the concurrency benchmarks (int-only)
nor the ray tracer (float, sequential) reach: **float compute inside a
`parallel for` reduction**. `K = ncpu()` workers each own whole rows; because each
pixel's escape count is a pure function of its coordinates, the per-row sums
reduce **order-independently** — the result is identical no matter how the workers
interleave. That determinism is what makes the run a real check on the reduction
and the deep-copy thread boundary:

- **tychoc == tychoc0** — the C and self-hosted compilers emit the same stdout.
- **ThreadSanitizer** — no data race on the reduction accumulators.
- **AddressSanitizer/UBSan** — clean, leak-free.
- golden-locked stdout (the worker count is machine-dependent, so it goes to stderr).

```sh
make mandelbrot                  # build (both compilers + TSan + ASan), run, diff
sh examples/mandelbrot/run.sh    # same, standalone
./tychoc examples/mandelbrot/main.ty -o /tmp/mb && /tmp/mb   # just run it
```
