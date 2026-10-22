// Recursive expression interpreter, Rust: enum + Box tree, eval/fold/count/eq.
enum Expr { Lit(i64), Bin(i64, Box<Expr>, Box<Expr>), If(Box<Expr>, Box<Expr>, Box<Expr>) }
fn gen(d: i64, seed: &mut u64) -> Expr {
    *seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
    if d <= 0 || (*seed & 7) == 0 {
        return Expr::Lit(((*seed >> 10) & 63) as i64);
    }
    let k = (*seed >> 18) & 3;
    let l = gen(d - 1, seed);
    let r = gen(d - 1, seed);
    if k == 3 {
        let c = gen(d - 1, seed);
        return Expr::If(Box::new(l), Box::new(r), Box::new(c));
    }
    Expr::Bin(k as i64, Box::new(l), Box::new(r))
}
fn eval(e: &Expr) -> i64 {
    match e {
        Expr::Lit(v) => *v,
        Expr::Bin(op, l, r) => {
            let a = eval(l); let b = eval(r);
            match op { 0 => a.wrapping_add(b), 1 => a.wrapping_sub(b), _ => a.wrapping_mul(b) }
        }
        Expr::If(c, t, f) => if eval(c) != 0 { eval(t) } else { eval(f) },
    }
}
fn count(e: &Expr) -> i64 {
    match e { Expr::Lit(_) => 1, Expr::Bin(_, l, r) => 1 + count(l) + count(r), Expr::If(c, t, f) => 1 + count(c) + count(t) + count(f) }
}
fn fold(e: &Expr) -> Expr {
    match e {
        Expr::Lit(v) => Expr::Lit(*v),
        Expr::Bin(op, l, r) => {
            let fl = fold(l); let fr = fold(r);
            if let (Expr::Lit(a), Expr::Lit(b)) = (&fl, &fr) {
                let v = match op { 0 => a.wrapping_add(*b), 1 => a.wrapping_sub(*b), _ => a.wrapping_mul(*b) };
                return Expr::Lit(v);
            }
            Expr::Bin(*op, Box::new(fl), Box::new(fr))
        }
        Expr::If(c, t, f) => Expr::If(Box::new(fold(c)), Box::new(fold(t)), Box::new(fold(f))),
    }
}
fn eq(a: &Expr, b: &Expr) -> bool {
    match (a, b) {
        (Expr::Lit(x), Expr::Lit(y)) => x == y,
        (Expr::Bin(o1, l1, r1), Expr::Bin(o2, l2, r2)) => o1 == o2 && eq(l1, l2) && eq(r1, r2),
        (Expr::If(c1, t1, f1), Expr::If(c2, t2, f2)) => eq(c1, c2) && eq(t1, t2) && eq(f1, f2),
        _ => false,
    }
}
fn main() {
    let depth: i64 = 20;
    let mut seed: u64 = 88172645463325252;
    let t = gen(depth, &mut seed);
    let n = count(&t); let ev = eval(&t);
    let ft = fold(&t); let fn2 = count(&ft); let fev = eval(&ft);
    println!("{} {} {} {} {}", n, ev, fn2, fev, eq(&t, &ft));
}
