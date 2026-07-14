/* compact-dict proof: build the same 229k-node trie with two per-node map
 * layouts; compare held bytes + wall on the identical workload. Value stored in
 * the map = the child Node (a map header, like tycho's `[int: Trie]` storing
 * Trie structs inline). Both layouts malloc-and-HOLD everything (incl. abandoned
 * rehash tables) to model tycho's arena (frees nothing until scope exit).
 * Keys are 97..122, so key==0 is a valid empty-slot marker. Argv[1]=A|B. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static long g_bytes = 0;
static void *xalloc(size_t n){ g_bytes += (long)n; void*p=calloc(1,n); if(!p){exit(1);} return p; }

/* ---- Layout A: open-addressing, value-inline slots + intrusive order list ---- */
typedef struct MapA { int len, cap, head, tail; long *keys; struct NodeA *vals; int *nxt, *prv; } MapA;
typedef struct NodeA { MapA m; int word; } NodeA;
static void a_grow(MapA*m){
    int nc = m->cap ? m->cap*2 : 4;
    long *k = xalloc(nc*sizeof(long)); NodeA *v = xalloc(nc*sizeof(NodeA));
    int *nx = xalloc(nc*sizeof(int)), *pv = xalloc(nc*sizeof(int));
    int nhead=-1, ntail=-1;
    for(int s=(m->len? m->head : -1); s!=-1; s=m->nxt[s]){   /* re-insert in insertion order (empty map: calloc'd head=0) */
        long key=m->keys[s]; int slot=(int)((uint64_t)key%nc);
        while(k[slot]) slot=(slot+1)%nc;
        k[slot]=key; v[slot]=m->vals[s]; nx[slot]=-1; pv[slot]=ntail;
        if(ntail!=-1) nx[ntail]=slot; else nhead=slot; ntail=slot;
    }
    m->keys=k; m->vals=v; m->nxt=nx; m->prv=pv; m->cap=nc; m->head=nhead; m->tail=ntail;
}
static NodeA* a_get(MapA*m, long key){
    if(m->cap){ int slot=(int)((uint64_t)key%m->cap);
        while(m->keys[slot]){ if(m->keys[slot]==key) return &m->vals[slot]; slot=(slot+1)%m->cap; } }
    if(m->len+1 > m->cap*3/4) a_grow(m);
    int slot=(int)((uint64_t)key%m->cap); while(m->keys[slot]) slot=(slot+1)%m->cap;
    m->keys[slot]=key; m->nxt[slot]=-1; m->prv[slot]=m->tail;
    if(m->tail!=-1) m->nxt[m->tail]=slot; else m->head=slot; m->tail=slot; m->len++;
    return &m->vals[slot];
}

/* ---- Layout B: small index table -> dense entries; order = insertion ---- */
struct EntryB; typedef struct MapB { int cap_idx; int32_t *idx; int len, cap_ent; struct EntryB *entries; } MapB;
typedef struct EntryB { long key; struct NodeB { MapB m; int word; } val; } EntryB;
typedef struct NodeB NodeB;
static void b_grow_idx(MapB*m){
    int nc = m->cap_idx ? m->cap_idx*2 : 4;
    int32_t *ni = xalloc(nc*sizeof(int32_t));
    for(int i=0;i<m->len;i++){ long key=m->entries[i].key; int slot=(int)((uint64_t)key%nc);
        while(ni[slot]) slot=(slot+1)%nc; ni[slot]=i+1; }
    m->idx=ni; m->cap_idx=nc;
}
static NodeB* b_get(MapB*m, long key){
    if(m->cap_idx){ int slot=(int)((uint64_t)key%m->cap_idx);
        while(m->idx[slot]){ if(m->entries[m->idx[slot]-1].key==key) return &m->entries[m->idx[slot]-1].val; slot=(slot+1)%m->cap_idx; } }
    if(m->len+1 > m->cap_idx*3/4) b_grow_idx(m);
    if(m->len+1 > m->cap_ent){ int nc=m->cap_ent?m->cap_ent*2:2;
        EntryB *ne=xalloc(nc*sizeof(EntryB)); memcpy(ne,m->entries,(size_t)m->len*sizeof(EntryB)); m->entries=ne; m->cap_ent=nc; }
    int i=m->len++; m->entries[i].key=key;
    int slot=(int)((uint64_t)key%m->cap_idx); while(m->idx[slot]) slot=(slot+1)%m->cap_idx; m->idx[slot]=i+1;
    return &m->entries[i].val;
}

int main(int argc,char**argv){
    int B = argc>1 && argv[1][0]=='B';
    unsigned long long state=88172645463325252ULL;
    long nodes=1, words=0;
    NodeA rootA; memset(&rootA,0,sizeof rootA);
    NodeB rootB; memset(&rootB,0,sizeof rootB);
    for(int w=0; w<150000; w++){
        state = state*6364136223846793005ULL + 1442695040888963407ULL;
        int wlen = 3 + (int)((state & 1073741823ULL) % 5);
        MapA *ma=&rootA.m; NodeA *curA=&rootA; MapB *mb=&rootB.m; NodeB *curB=&rootB;
        for(int j=0;j<wlen;j++){
            state = state*6364136223846793005ULL + 1442695040888963407ULL;
            long c = 97 + (long)((state & 1073741823ULL) % 26);
            if(B){ int before=mb->len; NodeB *ch=b_get(mb,c); if(mb->len>before) nodes++; curB=ch; mb=&ch->m; }
            else { int before=ma->len; NodeA *ch=a_get(ma,c); if(ma->len>before) nodes++; curA=ch; ma=&ch->m; }
        }
        if(B){ if(!curB->word){curB->word=1;words++;} } else { if(!curA->word){curA->word=1;words++;} }
    }
    printf("%-8s nodes=%ld words=%ld  heldMB=%.1f  sizeof(node)=%zu/%zu\n",
           B?"compact":"current", nodes, words, g_bytes/1048576.0, sizeof(NodeA), sizeof(NodeB));
    return 0;
}
