final class Tree {
    let l: Tree?
    let r: Tree?
    init(_ l: Tree?, _ r: Tree?) { self.l = l; self.r = r }
}
func make(_ d: Int) -> Tree {
    if d == 0 { return Tree(nil, nil) }
    return Tree(make(d - 1), make(d - 1))
}
func check(_ t: Tree) -> Int {
    guard let l = t.l else { return 1 }
    return 1 + check(l) + check(t.r!)
}
let mind = 4
let maxd = 18
let stretch = maxd + 1
print("stretch tree of depth \(stretch) check: \(check(make(stretch)))")
let longlived = make(maxd)
for d in stride(from: mind, through: maxd, by: 2) {
    let iters = 1 << (maxd - d + mind)
    var sum = 0
    for _ in 0..<iters { sum += check(make(d)) }
    print("\(iters) trees of depth \(d) check: \(sum)")
}
print("long-lived tree of depth \(maxd) check: \(check(longlived))")
