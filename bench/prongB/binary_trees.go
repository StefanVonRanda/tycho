// binary-trees, Go — pointer structs, garbage collected.
package main
import "fmt"
type Tree struct{ l, r *Tree }
func make_(d int) *Tree {
	t := &Tree{}
	if d > 0 {
		t.l = make_(d - 1)
		t.r = make_(d - 1)
	}
	return t
}
func check(t *Tree) int {
	if t.l == nil {
		return 1
	}
	return 1 + check(t.l) + check(t.r)
}
func main() {
	mind, maxd := 4, 18
	stretch := maxd + 1
	fmt.Printf("stretch tree of depth %d check: %d\n", stretch, check(make_(stretch)))
	longlived := make_(maxd)
	for d := mind; d <= maxd; d += 2 {
		iters := 1
		for k := 0; k < maxd-d+mind; k++ {
			iters *= 2
		}
		sum := 0
		for i := 0; i < iters; i++ {
			sum += check(make_(d))
		}
		fmt.Printf("%d trees of depth %d check: %d\n", iters, d, sum)
	}
	fmt.Printf("long-lived tree of depth %d check: %d\n", maxd, check(longlived))
}
