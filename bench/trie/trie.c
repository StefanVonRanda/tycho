/* trie head-to-head, C port. Same words + checksum as the tycho/Go ports. Each node
 * owns a minimal open-addressing int->child map (mirrors tycho's per-node [int: Trie]),
 * one malloc per node, held live -- so peak RSS reflects the whole trie. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct Node {
    int *keys;
    struct Node **vals;
    unsigned char *occ;          /* 0 empty, 1 used */
    long cap, len;
    int word;
} Node;

static Node *node_new(void) { return (Node *)calloc(1, sizeof(Node)); }

static void rehash(Node *n, long nc) {
    int *nk = (int *)calloc((size_t)nc, sizeof(int));
    Node **nv = (Node **)calloc((size_t)nc, sizeof(Node *));
    unsigned char *no = (unsigned char *)calloc((size_t)nc, 1);
    unsigned long mask = (unsigned long)nc - 1;
    for (long i = 0; i < n->cap; i++) if (n->occ[i]) {
        unsigned long h = (unsigned long)n->keys[i] & mask;
        while (no[h]) h = (h + 1) & mask;
        nk[h] = n->keys[i]; nv[h] = n->vals[i]; no[h] = 1;
    }
    free(n->keys); free(n->vals); free(n->occ);
    n->keys = nk; n->vals = nv; n->occ = no; n->cap = nc;
}

static Node *child(Node *n, int k, int *created) {   /* find-or-create the child for key k */
    if (n->cap == 0) rehash(n, 8);
    else if ((n->len + 1) * 2 > n->cap) rehash(n, n->cap * 2);
    unsigned long mask = (unsigned long)n->cap - 1;
    unsigned long h = (unsigned long)k & mask;
    while (n->occ[h]) { if (n->keys[h] == k) return n->vals[h]; h = (h + 1) & mask; }
    Node *c = node_new();
    n->keys[h] = k; n->vals[h] = c; n->occ[h] = 1; n->len++;
    *created = 1;
    return c;
}

static long g_nodes = 1;             /* the root */

static int insert(Node *root, const char *s, int len) {
    Node *t = root;
    for (int i = 0; i < len; i++) {
        int created = 0;
        t = child(t, (unsigned char)s[i], &created);
        if (created) g_nodes++;
    }
    if (t->word) return 0;
    t->word = 1;
    return 1;
}

int main(void) {
    int n = 150000;
    uint64_t state = 88172645463325252ULL;
    Node *root = node_new();
    long nwords = 0;
    char buf[16];
    for (int w = 0; w < n; w++) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        int wlen = 3 + (int)((state & 1073741823ULL) % 5);
        for (int j = 0; j < wlen; j++) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            buf[j] = (char)(97 + (state & 1073741823ULL) % 26);
        }
        nwords += insert(root, buf, wlen);
    }
    printf("%ld %ld\n", g_nodes, nwords);
    return 0;
}
