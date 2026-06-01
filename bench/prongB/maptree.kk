// tree-rewrite benchmark, Koka — Perceus reference counting + reuse.
type tree
  Leaf(n : int)
  Node(left : tree, right : tree)

fun build(d : int) : div tree
  if d == 0 then Leaf(1) else Node(build(d - 1), build(d - 1))

fun maptree(t : tree) : div tree
  match t
    Leaf(n) -> Leaf(n + 1)
    Node(l, r) -> Node(maptree(l), maptree(r))

fun checksum(t : tree) : div int
  match t
    Leaf(n) -> n
    Node(l, r) -> checksum(l) + checksum(r)

fun loopn(t : tree, n : int, acc : int) : div int
  if n <= 0 then acc else loopn(t, n - 1, acc + checksum(maptree(t)))

fun main()
  val t = build(16)
  println((loopn(t, 200, 0)).show)
