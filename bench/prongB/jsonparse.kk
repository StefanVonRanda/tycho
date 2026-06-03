// json-parse prong B — Koka (Perceus RC). Recursive-descent into a `json` value,
// walk-checksum, dropped by refcount. Koka strings are UTF-8 (O(n) byte index),
// so the input is converted once to a vector<char> for O(1) indexing; `pos` is
// threaded (pure). Reads the doc from a file path argument. K parse passes.
import std/os/file
import std/os/path
import std/os/env

type json
  JNull
  JBool(b : bool)
  JNum(n : int)
  JStr(s : string)
  JArr(xs : list<json>)
  JObj(ks : list<string>, vs : list<json>)

fun skip-ws(v : vector<char>, n : int, p : int) : <exn,div> int
  if p < n then
    val c = v[p].int
    if c == 32 || c == 9 || c == 10 || c == 13 then skip-ws(v, n, p + 1) else p
  else p

fun parse-str-loop(v : vector<char>, n : int, p : int, acc : list<char>) : <exn,div> (string, int)
  if p < n && v[p].int != 34 then
    if v[p].int == 92 then
      val e = v[p + 1].int
      val c = if e == 110 then '\n' elif e == 116 then '\t' elif e == 34 then '"' elif e == 92 then '\\' else v[p + 1]
      parse-str-loop(v, n, p + 2, Cons(c, acc))
    else
      parse-str-loop(v, n, p + 1, Cons(v[p], acc))
  else
    (acc.reverse.string, p + 1)

fun parse-num-loop(v : vector<char>, n : int, p : int, acc : int) : <exn,div> (int, int)
  if p < n && v[p].int >= 48 && v[p].int <= 57 then
    parse-num-loop(v, n, p + 1, acc * 10 + (v[p].int - 48))
  else (acc, p)

fun parse-number(v : vector<char>, n : int, p : int) : <exn,div> (int, int)
  if v[p].int == 45 then
    val (x, p2) = parse-num-loop(v, n, p + 1, 0)
    (0 - x, p2)
  else parse-num-loop(v, n, p, 0)

fun parse-arr-loop(v : vector<char>, n : int, p : int, acc : list<json>) : <exn,div> (json, int)
  if p < n && v[p].int != 93 then
    val (item, p2) = parse-value(v, n, p)
    val p3 = skip-ws(v, n, p2)
    val p4 = if p3 < n && v[p3].int == 44 then skip-ws(v, n, p3 + 1) else p3
    parse-arr-loop(v, n, p4, Cons(item, acc))
  else (JArr(acc.reverse), p + 1)

fun parse-obj-loop(v : vector<char>, n : int, p : int, ks : list<string>, vs : list<json>) : <exn,div> (json, int)
  if p < n && v[p].int != 125 then
    val (k, p1) = parse-str-loop(v, n, p + 1, [])
    val p2 = skip-ws(v, n, p1)
    val (val0, p3) = parse-value(v, n, p2 + 1)
    val p4 = skip-ws(v, n, p3)
    val p5 = if p4 < n && v[p4].int == 44 then skip-ws(v, n, p4 + 1) else p4
    parse-obj-loop(v, n, p5, Cons(k, ks), Cons(val0, vs))
  else (JObj(ks.reverse, vs.reverse), p + 1)

fun parse-value(v : vector<char>, n : int, p0 : int) : <exn,div> (json, int)
  val p = skip-ws(v, n, p0)
  val c = v[p].int
  if c == 123 then parse-obj-loop(v, n, p + 1, [], [])
  elif c == 91 then parse-arr-loop(v, n, p + 1, [])
  elif c == 34 then
    val (s, p1) = parse-str-loop(v, n, p + 1, [])
    (JStr(s), p1)
  elif c == 116 then (JBool(True), p + 4)
  elif c == 102 then (JBool(False), p + 5)
  elif c == 110 then (JNull, p + 4)
  else
    val (x, p1) = parse-number(v, n, p)
    (JNum(x), p1)

fun walk(j : json) : <exn,div> int
  match j
    JNum(x)      -> x + 1
    JStr(s)      -> s.count + 1
    JBool(_)     -> 1
    JNull        -> 1
    JArr(xs)     -> 1 + xs.map(walk).sum
    JObj(ks, vs) -> 1 + ks.map(fn(k) k.count).sum + vs.map(walk).sum

fun loopk(v : vector<char>, n : int, k : int, acc : int) : <exn,div> int
  if k <= 0 then acc
  else
    val (j, _) = parse-value(v, n, 0)
    loopk(v, n, k - 1, acc + walk(j))

fun main()
  val args = get-args()
  val s = read-text-file(args.head("").path)
  val v = s.list.vector
  println("checksum=" ++ loopk(v, v.length, 30, 0).show)
