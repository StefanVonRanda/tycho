enum Tree { Leaf(i64), Node(Box<Tree>, Box<Tree>) }
fn build(d:i64)->Tree{ if d==0 {Tree::Leaf(1)} else {Tree::Node(Box::new(build(d-1)),Box::new(build(d-1)))} }
fn maptree(t:&Tree)->Tree{ match t { Tree::Leaf(n)=>Tree::Leaf(n+1), Tree::Node(l,r)=>Tree::Node(Box::new(maptree(l)),Box::new(maptree(r))) } }
fn checksum(t:&Tree)->i64{ match t { Tree::Leaf(n)=>*n, Tree::Node(l,r)=>checksum(l)+checksum(r) } }
fn main(){ let t=build(16); let mut sum=0i64; for _ in 0..200 { sum+=checksum(&maptree(&t)); } println!("{}",sum); }
