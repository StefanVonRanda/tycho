// Inverted index, count-then-fill, Go: postings are sized exactly once.
package main
import "fmt"
type Posting struct { term string; docs []int; freqs []int }
func main() {
    const n, w, v = 300000, 12, 8000
    idx := map[string]int{}
    terms := []string{}
    counts := []int{}
    seed := 1
    for d := 0; d < n; d++ {
        for j := 0; j < w; j++ {
            seed = (seed*1103515245 + 12345) % 2147483647
            term := "t" + fmt.Sprint(seed%v)
            if slot, ok := idx[term]; ok { counts[slot]++ } else {
                idx[term] = len(terms); terms = append(terms, term); counts = append(counts, 1)
            }
        }
    }
    post := make([]Posting, len(terms))
    for i := range terms { post[i] = Posting{terms[i], make([]int, 0, counts[i]), make([]int, 0, counts[i])} }
    seed = 1
    for d := 0; d < n; d++ {
        for j := 0; j < w; j++ {
            seed = (seed*1103515245 + 12345) % 2147483647
            pi := idx["t"+fmt.Sprint(seed%v)]
            p := &post[pi]
            if k := len(p.docs); k > 0 && p.docs[k-1] == d { p.freqs[k-1]++ } else { p.docs = append(p.docs, d); p.freqs = append(p.freqs, 1) }
        }
    }
    sum := 0
    for i := range post { sum += len(post[i].term) + len(post[i].docs); for _, f := range post[i].freqs { sum += f } }
    hits := 0
    p0, ok0 := idx["t0"]; p1, ok1 := idx["t1"]
    if ok0 && ok1 { for _, d := range post[p0].docs { for _, e := range post[p1].docs { if d == e { hits++; break } } } }
    fmt.Printf("vocab=%d checksum=%d and01=%d\n", len(post), sum, hits)
}
