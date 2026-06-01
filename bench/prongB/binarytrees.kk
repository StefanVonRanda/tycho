// binary-trees, Koka — Perceus reference counting + reuse.
type tree
  Leaf
  Node(left : tree, right : tree)

fun make(d : int) : div tree
  if d == 0 then Leaf else Node(make(d - 1), make(d - 1))

fun check(t : tree) : div int
  match t
    Leaf -> 1
    Node(l, r) -> 1 + check(l) + check(r)

fun pow2(n : int) : div int
  if n <= 0 then 1 else 2 * pow2(n - 1)

fun sumtrees(d : int, n : int, acc : int) : div int
  if n <= 0 then acc else sumtrees(d, n - 1, acc + check(make(d)))

fun depthloop(d : int, maxd : int, mind : int) : <div,console> ()
  if d > maxd then ()
  else
    val iters = pow2(maxd - d + mind)
    val sum = sumtrees(d, iters, 0)
    println(iters.show ++ " trees of depth " ++ d.show ++ " check: " ++ sum.show)
    depthloop(d + 2, maxd, mind)

fun main()
  val mind = 4
  val maxd = 18
  val stretch = maxd + 1
  println("stretch tree of depth " ++ stretch.show ++ " check: " ++ check(make(stretch)).show)
  val longlived = make(maxd)
  depthloop(mind, maxd, mind)
  println("long-lived tree of depth " ++ maxd.show ++ " check: " ++ check(longlived).show)
