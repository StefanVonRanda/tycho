package main
import "fmt"
type Tree struct{ leaf bool; n int; l,r *Tree }
func leaf(n int)*Tree{ return &Tree{leaf:true,n:n} }
func node(l,r *Tree)*Tree{ return &Tree{l:l,r:r} }
func build(d int)*Tree{ if d==0 {return leaf(1)}; return node(build(d-1),build(d-1)) }
func maptree(t *Tree)*Tree{ if t.leaf {return leaf(t.n+1)}; return node(maptree(t.l),maptree(t.r)) }
func checksum(t *Tree)int{ if t.leaf {return t.n}; return checksum(t.l)+checksum(t.r) }
func main(){ t:=build(16); sum:=0; for i:=0;i<200;i++{ sum+=checksum(maptree(t)) }; fmt.Println(sum) }
