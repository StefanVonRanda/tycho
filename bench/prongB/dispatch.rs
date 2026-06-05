fn main(){
  let (n,k)=(2000i64,2000i64); let mut total=0i64;
  for p in 0..k {
    let mut fs: Vec<Box<dyn Fn(i64)->i64>> = Vec::with_capacity(n as usize);
    for i in 0..n { fs.push(Box::new(move |x| x*(i+1)+p)); }
    for f in &fs { total=(total+f(7))&1048575; }
  }
  println!("{}",total);
}
