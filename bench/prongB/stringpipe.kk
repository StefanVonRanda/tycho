// string-pipeline benchmark, Koka — build a digit string per pass, hash it.
fun build(j : int, kk : int) : list<char>
  list(0, kk - 1).map(fn(i) (('0'.int) + (i + j) % 10).char)

fun loopn(j : int, m : int, kk : int, p : int, acc : int) : div int
  if j >= m then acc
  else
    val s = build(j, kk).string
    val h = s.list.foldl(0, fn(a, c) a + c.int)
    loopn(j + 1, m, kk, p, (acc + h) % p)

fun main()
  println((loopn(0, 4000, 256, 1000003, 0)).show)
