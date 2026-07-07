/* Stage 3 fixture: a companion C shim compiled+linked via `--shim` (no prebuilt
 * library). This is the `*_shim.c` pattern — a thin C wrapper an extern binds to. */
long ffi_triple(long x) { return x * 3; }

/* Sized-int FFI boundary fixtures: the real C ABI uses fixed-width types. A u32
 * add wraps at 2^32; a u64 shift carries past bit 32; a signed-char return is
 * sign-extended. A bare `long` prototype (the pre-sized-types behaviour) would
 * misread the u32 return's high bits — so these prove the emitted prototype
 * matches the C symbol. */
unsigned int ffi_add32(unsigned int a, unsigned int b) { return a + b; }
unsigned long long ffi_shl64(unsigned int x, int n) { return (unsigned long long)x << n; }
signed char ffi_negbyte(unsigned char x) { return -(signed char)x; }
