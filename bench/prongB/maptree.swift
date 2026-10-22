final class Tree {
    let leaf: Bool
    let n: Int
    let l: Tree?
    let r: Tree?
    init(_ n: Int) { leaf = true; self.n = n; l = nil; r = nil }
    init(_ l: Tree, _ r: Tree) { leaf = false; n = 0; self.l = l; self.r = r }
}
func build(_ d: Int) -> Tree {
    d == 0 ? Tree(1) : Tree(build(d - 1), build(d - 1))
}
func mapTree(_ t: Tree) -> Tree {
    t.leaf ? Tree(t.n + 1) : Tree(mapTree(t.l!), mapTree(t.r!))
}
func checksum(_ t: Tree) -> Int {
    t.leaf ? t.n : checksum(t.l!) + checksum(t.r!)
}
let depth = 16
let t = build(depth)
var sum = 0
for _ in 0..<200 { sum += checksum(mapTree(t)) }
print(sum)
