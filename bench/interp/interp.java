// interp, Java: recursive AST build, eval, fold and structural compare.
public class interp {
    static int depth = 20;
    static final class E {
        int tag, op; long lit; E l, r, c;
        E(int t) { tag=t; }
    }
    static E gen(int d, long[] seed) {
        seed[0] = seed[0] * 6364136223846793005L + 1442695040888963407L;
        if (d <= 0 || (seed[0] & 7) == 0) { E e=new E(0); e.lit=(seed[0]>>10)&63; return e; }
        int k=(int)((seed[0]>>18)&3); E l=gen(d-1,seed), r=gen(d-1,seed);
        if (k==3) { E e=new E(2); e.l=l; e.r=r; e.c=gen(d-1,seed); return e; }
        E e=new E(1); e.op=k; e.l=l; e.r=r; return e;
    }
    static long eval(E e) {
        if (e.tag==0) return e.lit;
        if (e.tag==1) { long a=eval(e.l), b=eval(e.r); return e.op==0?a+b:e.op==1?a-b:a*b; }
        return eval(e.l)!=0?eval(e.r):eval(e.c);
    }
    static long count(E e) { return e.tag==0?1:e.tag==1?1+count(e.l)+count(e.r):1+count(e.l)+count(e.r)+count(e.c); }
    static E fold(E e) {
        if (e.tag==0) { E z=new E(0); z.lit=e.lit; return z; }
        if (e.tag==1) { E l=fold(e.l), r=fold(e.r); if (l.tag==0&&r.tag==0){ long v=e.op==0?l.lit+r.lit:e.op==1?l.lit-r.lit:l.lit*r.lit; E z=new E(0); z.lit=v; return z;} E z=new E(1); z.op=e.op; z.l=l; z.r=r; return z; }
        E z=new E(2); z.l=fold(e.l); z.r=fold(e.r); z.c=fold(e.c); return z;
    }
    static boolean eq(E a,E b) { if(a.tag!=b.tag)return false; if(a.tag==0)return a.lit==b.lit; if(a.tag==1)return a.op==b.op&&eq(a.l,b.l)&&eq(a.r,b.r); return eq(a.l,b.l)&&eq(a.r,b.r)&&eq(a.c,b.c); }
    public static void main(String[] args) {
        E t=gen(depth,new long[]{88172645463325252L}); long n=count(t), ev=eval(t); E f=fold(t);
        System.out.println(n+" "+ev+" "+count(f)+" "+eval(f)+" "+eq(t,f));
    }
}
