final class Posting { let term: String; var docs:[Int]; var freqs:[Int]; init(_ t:String,_ cap:Int){term=t;docs=[];freqs=[];docs.reserveCapacity(cap);freqs.reserveCapacity(cap)} }
let n=300_000, w=12, v=8000
var idx=[String:Int](); var terms=[String](); var counts=[Int](); var seed=1
for _ in 0..<n { for _ in 0..<w { seed=(seed &* 1103515245 &+ 12345)%2147483647; let term="t\(seed % v)"; if let slot=idx[term] { counts[slot]+=1 } else { idx[term]=terms.count; terms.append(term); counts.append(1) } } }
var post=[Posting](); for i in 0..<terms.count { post.append(Posting(terms[i],counts[i])) }
seed=1
for d in 0..<n { for _ in 0..<w { seed=(seed &* 1103515245 &+ 12345)%2147483647; let p=post[idx["t\(seed % v)"]!]; if let last=p.docs.last, last==d { p.freqs[p.freqs.count-1]+=1 } else { p.docs.append(d); p.freqs.append(1) } } }
var sum=0; for p in post { sum += p.term.utf8.count+p.docs.count; for f in p.freqs { sum += f } }
var hits=0
if let p0=idx["t0"], let p1=idx["t1"] { for d in post[p0].docs where post[p1].docs.contains(d) { hits += 1 } }
print("vocab=\(post.count) checksum=\(sum) and01=\(hits)")
