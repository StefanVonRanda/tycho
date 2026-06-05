// dispatch, Koka — Perceus RC. K passes, each builds a list of N first-class
// closures (capturing the indices) and applies every one. Masked to 20 bits
// (m - (m/2^20)*2^20 == & 1048575 for non-negative m) so all languages agree.
fun main()
  var total := 0
  list(0, 2000 - 1).foreach fn(p)
    val fs = list(0, 2000 - 1).map fn(i)
      fn(x : int) x * (i + 1) + p
    fs.foreach fn(f)
      val m = total + f(7)
      total := m - (m / 1048576) * 1048576
  println(total.show)
