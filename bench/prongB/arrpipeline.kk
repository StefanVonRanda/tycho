// array-pipeline benchmark, Koka — Perceus RC. Each pass allocates a fresh
// mapped list of N ints, sums it, and drops it. (Koka uses a linked list where
// C/Rust/Go/Hier use a flat array; the comparison is allocate-and-discard
// throughput, not identical layout.)
fun pass(xs : list<int>, j : int, k : int) : list<int>
  xs.map(fn(x) (x * 1103515245 + 12345 + j) % k)

fun suml(xs : list<int>) : int
  xs.foldl(0, fn(a, x) a + x)

fun loopn(xs : list<int>, j : int, m : int, k : int, acc : int) : div int
  if j >= m then acc
  else loopn(xs, j + 1, m, k, (acc + suml(pass(xs, j, k))) % k)

fun main()
  val xs = list(0, 100000 - 1)
  println((loopn(xs, 0, 200, 1000003, 0)).show)
