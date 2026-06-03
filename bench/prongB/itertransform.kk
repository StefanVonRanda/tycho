// iterated-transform, Koka — Perceus RC. `xs` is uniquely owned and rebuilt each
// step (loopn never reuses the old list), so map reuses the cons cells in place
// (FBIP) — flat memory, the case an arena that can't free mid-scope loses on.
fun step(xs : list<int>, p : int) : list<int>
  xs.map(fn(x) { val t = x * 1103515245 + 12345; t - (t / p) * p })

fun loopn(xs : list<int>, j : int, m : int, p : int) : div list<int>
  if j >= m then xs else loopn(step(xs, p), j + 1, m, p)

fun suml(xs : list<int>) : int
  xs.foldl(0, fn(a, x) a + x)

fun main()
  val a = loopn(list(0, 100000 - 1), 0, 2000, 2147483647)
  val s = suml(a)
  println((s - (s / 1000003) * 1000003).show)
