/* Stage 3 fixture: a companion C shim compiled+linked via `--shim` (no prebuilt
 * library). This is the `*_shim.c` pattern — a thin C wrapper an extern binds to. */
long ffi_triple(long x) { return x * 3; }
