// interp, Swift: recursive AST build, eval, fold and structural compare.
import Foundation

let depth = 20
final class E {
    var tag = 0, op = 0
    var lit: Int64 = 0
    var l: E?, r: E?, c: E?
    init(_ t: Int) { tag = t }
}
var state: UInt64 = 88172645463325252

func gen(_ d: Int) -> E {
    state = state &* 6364136223846793005 &+ 1442695040888963407
    if d <= 0 || (state & 7) == 0 { let e=E(0); e.lit=Int64((state >> 10) & 63); return e }
    let k = Int((state >> 18) & 3)
    let l=gen(d-1), r=gen(d-1)
    if k == 3 { let e=E(2); e.l=l; e.r=r; e.c=gen(d-1); return e }
    let e=E(1); e.op=k; e.l=l; e.r=r; return e
}
func eval(_ e: E) -> Int64 {
    if e.tag==0 { return e.lit }
    if e.tag==1 { let a=eval(e.l!), b=eval(e.r!); return e.op==0 ? a &+ b : e.op==1 ? a &- b : a &* b }
    return eval(e.l!) != 0 ? eval(e.r!) : eval(e.c!)
}
func count(_ e: E) -> Int64 {
    if e.tag==0 { return 1 }
    if e.tag==1 { return 1+count(e.l!)+count(e.r!) }
    return 1+count(e.l!)+count(e.r!)+count(e.c!)
}
func fold(_ e: E) -> E {
    if e.tag==0 { let z=E(0); z.lit=e.lit; return z }
    if e.tag==1 {
        let l=fold(e.l!), r=fold(e.r!)
        if l.tag==0 && r.tag==0 { let z=E(0); z.lit = e.op==0 ? l.lit &+ r.lit : e.op==1 ? l.lit &- r.lit : l.lit &* r.lit; return z }
        let z=E(1); z.op=e.op; z.l=l; z.r=r; return z
    }
    let z=E(2); z.l=fold(e.l!); z.r=fold(e.r!); z.c=fold(e.c!); return z
}
func eq(_ a: E,_ b: E) -> Bool {
    if a.tag != b.tag { return false }
    if a.tag==0 { return a.lit==b.lit }
    if a.tag==1 { return a.op==b.op && eq(a.l!,b.l!) && eq(a.r!,b.r!) }
    return eq(a.l!,b.l!) && eq(a.r!,b.r!) && eq(a.c!,b.c!)
}
let t=gen(depth), n=count(t), ev=eval(t), f=fold(t)
print("\(n) \(ev) \(count(f)) \(eval(f)) \(eq(t,f))")
