/* json-parse prong B — C, manual malloc/free. Same recursive-descent parser,
 * Json tree, and walk-checksum as json_parse.hi; reads the doc from stdin and
 * does K parse-and-FREE passes (the tree is freed by hand each pass). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { JNULL, JBOOL, JNUM, JSTR, JARR, JOBJ };
typedef struct Json Json;
struct Json {
    int tag;
    long num;            /* JNUM / JBOOL */
    char *str;           /* JSTR (owned) */
    long n;              /* JARR / JOBJ count */
    Json **vals;         /* JARR items / JOBJ values */
    char **keys;         /* JOBJ keys (owned) */
};

static const char *S; static long N, P;

static void skip_ws(void){ while (P<N && (S[P]==32||S[P]==9||S[P]==10||S[P]==13)) P++; }

static char *parse_string(void){
    P++; long start=P; long cap=16,len=0; char *r=malloc(cap);
    while (P<N && S[P]!='"'){
        char c;
        if (S[P]=='\\'){ P++; char e=S[P];
            if(e=='n')c='\n'; else if(e=='t')c='\t'; else if(e=='"')c='"'; else if(e=='\\')c='\\'; else c=e;
        } else c=S[P];
        if(len+1>=cap){cap*=2;r=realloc(r,cap);} r[len++]=c; P++;
    }
    P++; r[len]=0; (void)start; return r;
}
static long parse_number(void){
    int neg=0; if(S[P]=='-'){neg=1;P++;} long v=0;
    while(P<N && S[P]>='0'&&S[P]<='9'){ v=v*10+(S[P]-'0'); P++; }
    return neg?-v:v;
}
static Json *parse_value(void);
static Json *parse_array(void){
    P++; Json *j=calloc(1,sizeof*j); j->tag=JARR; long cap=4; j->vals=malloc(cap*sizeof(Json*)); skip_ws();
    while(P<N && S[P]!=']'){
        if(j->n==cap){cap*=2;j->vals=realloc(j->vals,cap*sizeof(Json*));}
        j->vals[j->n++]=parse_value(); skip_ws();
        if(P<N&&S[P]==','){P++;skip_ws();}
    }
    P++; return j;
}
static Json *parse_object(void){
    P++; Json *j=calloc(1,sizeof*j); j->tag=JOBJ; long cap=4; j->keys=malloc(cap*sizeof(char*)); j->vals=malloc(cap*sizeof(Json*)); skip_ws();
    while(P<N && S[P]!='}'){
        char *k=parse_string(); skip_ws(); P++; /* : */
        if(j->n==cap){cap*=2;j->keys=realloc(j->keys,cap*sizeof(char*));j->vals=realloc(j->vals,cap*sizeof(Json*));}
        j->keys[j->n]=k; j->vals[j->n]=parse_value(); j->n++; skip_ws();
        if(P<N&&S[P]==','){P++;skip_ws();}
    }
    P++; return j;
}
static Json *parse_value(void){
    skip_ws(); char c=S[P];
    if(c=='{')return parse_object();
    if(c=='[')return parse_array();
    if(c=='"'){ Json*j=calloc(1,sizeof*j); j->tag=JSTR; j->str=parse_string(); return j; }
    if(c=='t'){P+=4; Json*j=calloc(1,sizeof*j); j->tag=JBOOL; j->num=1; return j;}
    if(c=='f'){P+=5; Json*j=calloc(1,sizeof*j); j->tag=JBOOL; j->num=0; return j;}
    if(c=='n'){P+=4; Json*j=calloc(1,sizeof*j); j->tag=JNULL; return j;}
    Json*j=calloc(1,sizeof*j); j->tag=JNUM; j->num=parse_number(); return j;
}
static long walk(Json *j){
    long t;
    switch(j->tag){
        case JNUM: return j->num+1;
        case JSTR: return (long)strlen(j->str)+1;
        case JBOOL: return 1;
        case JNULL: return 1;
        case JARR: t=1; for(long i=0;i<j->n;i++)t+=walk(j->vals[i]); return t;
        case JOBJ: t=1; for(long i=0;i<j->n;i++)t+=(long)strlen(j->keys[i]); for(long i=0;i<j->n;i++)t+=walk(j->vals[i]); return t;
    }
    return 0;
}
static void freej(Json *j){
    if(j->tag==JSTR) free(j->str);
    if(j->tag==JARR){ for(long i=0;i<j->n;i++)freej(j->vals[i]); free(j->vals); }
    if(j->tag==JOBJ){ for(long i=0;i<j->n;i++){free(j->keys[i]);freej(j->vals[i]);} free(j->keys); free(j->vals); }
    free(j);
}
int main(void){
    long cap=1<<20,len=0; char*buf=malloc(cap); int c;
    while((c=getchar())!=EOF){ if(len+1>=cap){cap*=2;buf=realloc(buf,cap);} buf[len++]=(char)c; }
    buf[len]=0; S=buf; N=len;
    long acc=0;
    for(int k=0;k<30;k++){ P=0; Json*j=parse_value(); acc+=walk(j); freej(j); }
    printf("checksum=%ld\n", acc);
    return 0;
}
