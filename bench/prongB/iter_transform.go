package main
import "fmt"
func step(a []int64, p int64) []int64 {
	b := make([]int64, len(a))
	for i, x := range a { t := x*1103515245 + 12345; b[i] = t - (t/p)*p }
	return b
}
func main() {
	var n, m, p int64 = 100000, 2000, 2147483647
	a := make([]int64, n)
	for i := int64(0); i < n; i++ { a[i] = i }
	for j := int64(0); j < m; j++ { a = step(a, p) }
	var s int64 = 0
	for _, x := range a { s += x }
	fmt.Println(s - (s/1000003)*1000003)
}
