fn main() {
    let n: i64 = 100000; let m: i64 = 200; let k: i64 = 1000003;
    let xs: Vec<i64> = (0..n).collect();
    let mut checksum: i64 = 0;
    for j in 0..m {
        let ys: Vec<i64> = (0..n).map(|i| (xs[i as usize] * 1103515245 + 12345 + j) % k).collect();
        let s: i64 = ys.iter().sum();
        checksum = (checksum + s) % k;
    }
    println!("{}", checksum);
}
