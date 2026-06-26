// Tree-walking interpreter — Go reference. Builds the SAME deterministic expression
// AST as interp.ty / interp.c (an LCG-threaded recursive generator), evals it,
// constant-folds it (a rewrite pass allocating a new AST), counts nodes, and deep-
// compares original vs folded, folding into a `n eval foldedN foldedEval same`
// checksum. The AST is the memory under test: tycho's value-semantic recursive enum
// vs Go's struct pointers (GC) vs C's tagged union. int64 wraps by definition, matching
// tycho's integer contract. Same generator + same eval => byte-identical checksum.
package main

import "fmt"

const depth = 20

// tag 0=Lit 1=Bin 2=If. For If: l=cond, r=then, c=els.
type Expr struct {
	tag     int
	lit     int64
	op      int
	l, r, c *Expr
}

func gen(d int, seed *uint64) *Expr {
	*seed = *seed*6364136223846793005 + 1442695040888963407
	if d <= 0 || (*seed&7) == 0 {
		return &Expr{tag: 0, lit: int64((*seed >> 10) & 63)}
	}
	k := int((*seed >> 18) & 3)
	l := gen(d-1, seed)
	r := gen(d-1, seed)
	if k == 3 {
		c := gen(d-1, seed)
		return &Expr{tag: 2, l: l, r: r, c: c}
	}
	return &Expr{tag: 1, op: k, l: l, r: r}
}

func eval(e *Expr) int64 {
	switch e.tag {
	case 0:
		return e.lit
	case 1:
		a, b := eval(e.l), eval(e.r)
		switch e.op {
		case 0:
			return a + b
		case 1:
			return a - b
		}
		return a * b
	}
	if eval(e.l) != 0 {
		return eval(e.r)
	}
	return eval(e.c)
}

func count(e *Expr) int64 {
	switch e.tag {
	case 0:
		return 1
	case 1:
		return 1 + count(e.l) + count(e.r)
	}
	return 1 + count(e.l) + count(e.r) + count(e.c)
}

func fold(e *Expr) *Expr {
	switch e.tag {
	case 0:
		return &Expr{tag: 0, lit: e.lit}
	case 1:
		fl, fr := fold(e.l), fold(e.r)
		if fl.tag == 0 && fr.tag == 0 {
			a, b := fl.lit, fr.lit
			var v int64
			switch e.op {
			case 0:
				v = a + b
			case 1:
				v = a - b
			default:
				v = a * b
			}
			return &Expr{tag: 0, lit: v}
		}
		return &Expr{tag: 1, op: e.op, l: fl, r: fr}
	}
	return &Expr{tag: 2, l: fold(e.l), r: fold(e.r), c: fold(e.c)}
}

func eq(a, b *Expr) bool {
	if a.tag != b.tag {
		return false
	}
	switch a.tag {
	case 0:
		return a.lit == b.lit
	case 1:
		return a.op == b.op && eq(a.l, b.l) && eq(a.r, b.r)
	}
	return eq(a.l, b.l) && eq(a.r, b.r) && eq(a.c, b.c)
}

func main() {
	seed := uint64(88172645463325252)
	t := gen(depth, &seed)
	n := count(t)
	ev := eval(t)
	ft := fold(t)
	fn2 := count(ft)
	fev := eval(ft)
	same := eq(t, ft)
	fmt.Printf("%d %d %d %d %v\n", n, ev, fn2, fev, same)
}
