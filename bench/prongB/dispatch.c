/* dispatch, C — manual closure: a {captured env, fn pointer} struct, an array of
 * them rebuilt and applied each pass. The malloc'd array is freed per pass. */
#include <stdio.h>
#include <stdlib.h>
typedef struct { long i, p; } Env;
typedef struct { Env e; long (*call)(Env*, long); } Clo;
static long body(Env* e, long x){ return x * (e->i + 1) + e->p; }
int main(void){
  long n = 2000, k = 2000, total = 0;
  for (long p = 0; p < k; p++){
    Clo* fs = malloc((size_t)n * sizeof(Clo));
    for (long i = 0; i < n; i++){ fs[i].e.i = i; fs[i].e.p = p; fs[i].call = body; }
    for (long i = 0; i < n; i++) total = (total + fs[i].call(&fs[i].e, 7)) & 1048575;
    free(fs);
  }
  printf("%ld\n", total);
  return 0;
}
