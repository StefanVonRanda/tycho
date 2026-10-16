/* Experimental bounded two-ended storage, not part of the runtime ABI.
 * One owner, two disjoint allocation groups. High-end scopes are strictly LIFO.
 * No value references, recycling, thread sharing, growth or hidden fallback. */
typedef struct { unsigned char *data; size_t cap, low, high, top, serial; } Dual;
typedef struct { size_t high, token, parent; const Dual *owner; } DualMark;
static int dual_init(Dual *d, size_t cap) {
    cap &= ~(size_t)7;
    d->data = malloc(cap ? cap : 8);
    d->cap = cap; d->low = 0; d->high = cap; d->top = 0; d->serial = 0;
    return d->data != NULL;
}
static void *dual_alloc(Dual *d, size_t n, int high) {
    if (n > SIZE_MAX - 7) return NULL;
    n = (n + 7) & ~(size_t)7;
    if (n > d->high - d->low) return NULL;
    size_t off;
    if (high) { d->high -= n; off = d->high; }
    else { off = d->low; d->low += n; }
    return d->data + off;
}
static DualMark dual_mark(Dual *d) {
    if (d->serial == SIZE_MAX) return (DualMark){0, 0, 0, NULL};
    DualMark m = {d->high, ++d->serial, d->top, d};
    d->top = m.token;
    return m;
}
static int dual_rewind(Dual *d, DualMark m) {
    if (m.owner != d || !m.token || m.token != d->top || m.high < d->high || m.high > d->cap) return 0;
    d->high = m.high; d->top = m.parent;
    return 1;
}
static int dual_owns(Dual *d, const void *p, int high) {
    uintptr_t pos = (uintptr_t)p, base = (uintptr_t)d->data;
    return high ? pos >= base + d->high && pos < base + d->cap
                : pos >= base && pos < base + d->low;
}
static void dual_clear_low(Dual *d) { d->low = 0; }
static void dual_clear(Dual *d) { d->low = 0; d->high = d->cap; d->top = 0; }
static void dual_free(Dual *d) { free(d->data); d->data = NULL; }
